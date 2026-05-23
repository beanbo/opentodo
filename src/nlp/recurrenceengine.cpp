#include "recurrenceengine.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Parse two-letter BYDAY codes into Qt day-of-week numbers (1=Mon … 7=Sun).
static const QMap<QString, int> &byDayMap()
{
    static const QMap<QString, int> m = {
        {QStringLiteral("MO"), 1}, {QStringLiteral("TU"), 2},
        {QStringLiteral("WE"), 3}, {QStringLiteral("TH"), 4},
        {QStringLiteral("FR"), 5}, {QStringLiteral("SA"), 6},
        {QStringLiteral("SU"), 7},
    };
    return m;
}

// Parse an RFC 5545 UNTIL value. Accepts:
//   * YYYYMMDD                 (date-only form)
//   * YYYYMMDDTHHMMSSZ         (UTC date-time form, the only legal time form)
//   * YYYYMMDDTHHMMSS          (floating local time — tolerated for robustness)
// Returns an invalid QDate if the value cannot be parsed.
static QDate parseUntil(const QString &value)
{
    const QString v = value.trimmed();
    if (v.isEmpty()) return QDate();

    // UTC date-time form. Strip trailing 'Z' and parse as UTC, then convert
    // to a date in the local zone for comparison against the in-memory QDate
    // anchor (which is itself a local-zone date).
    if (v.endsWith(QLatin1Char('Z')) || v.endsWith(QLatin1Char('z'))) {
        const QString core = v.left(v.size() - 1);
        QDateTime dt = QDateTime::fromString(core, QStringLiteral("yyyyMMdd'T'HHmmss"));
        if (dt.isValid()) {
            dt.setTimeZone(QTimeZone::UTC);
            return dt.toLocalTime().date();
        }
    }
    // Floating local date-time
    QDateTime dtLocal = QDateTime::fromString(v, QStringLiteral("yyyyMMdd'T'HHmmss"));
    if (dtLocal.isValid())
        return dtLocal.date();

    // Date-only form
    return QDate::fromString(v, QStringLiteral("yyyyMMdd"));
}

// Parse an RRULE string into a key→value map.  Keys are upper-case.
// e.g. "FREQ=WEEKLY;BYDAY=MO,WE,FR;INTERVAL=2" →
//        { "FREQ":"WEEKLY", "BYDAY":"MO,WE,FR", "INTERVAL":"2" }
static QMap<QString, QString> parseRRule(const QString &rule)
{
    QMap<QString, QString> kv;
    const QStringList parts = rule.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq > 0)
            kv.insert(part.left(eq).trimmed().toUpper(),
                      part.mid(eq + 1).trimmed().toUpper());
    }
    return kv;
}

// Given a BYDAY string ("MO,WE,FR"), return sorted day-of-week list.
static QList<int> parseByday(const QString &byday)
{
    QList<int> days;
    const QStringList codes = byday.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const auto &m = byDayMap();
    for (const QString &code : codes) {
        if (m.contains(code))
            days.append(m.value(code));
    }
    std::sort(days.begin(), days.end());
    return days;
}

// Find the next day-of-week occurrence from `from` that is strictly after it.
// `days` must be sorted ascending.
//
// `wrapped` (optional out-param) tells the caller whether the result landed
// in the next ISO-week (true) or stayed in the same week as `from` (false).
// The WEEKLY;BYDAY;INTERVAL>1 branch in advance() needs this to decide
// whether to add the extra (interval-1) weeks — see the comment there.
static QDate nextByday(const QDate &from, const QList<int> &days,
                       bool *wrapped = nullptr)
{
    if (wrapped) *wrapped = false;
    if (days.isEmpty()) return QDate();
    const int curDow = from.dayOfWeek(); // 1=Mon … 7=Sun

    // Look for a day *later in the same week*
    for (int d : days) {
        if (d > curDow) {
            return from.addDays(d - curDow);
        }
    }
    // Wrap around to the next week's first matching day
    if (wrapped) *wrapped = true;
    const int firstDay = days.first();
    return from.addDays(7 - curDow + firstDay);
}

} // namespace

// ---------------------------------------------------------------------------
// RecurrenceEngine::advance  (B-4.60 primary RRULE path)
// ---------------------------------------------------------------------------

QDate RecurrenceEngine::advance(const QString &recurrenceRule,
                                const QDate &fromDate,
                                const QDate &completionDate)
{
    if (!fromDate.isValid())
        return QDate();

    // Empty rule → fall back to human-string path (rule was stored pre-migration)
    if (recurrenceRule.trimmed().isEmpty())
        return QDate();

    // If the string looks like it has no '=' it is a legacy humanString, not an RRULE.
    if (!recurrenceRule.contains(QLatin1Char('=')))
        return advanceFromHumanString(recurrenceRule, fromDate);

    const QMap<QString, QString> kv = parseRRule(recurrenceRule);

    const QString freq = kv.value(QStringLiteral("FREQ"));
    if (freq.isEmpty())
        return QDate(); // unparseable

    const int interval = qMax(1, kv.value(QStringLiteral("INTERVAL"), QStringLiteral("1")).toInt());

    // ── RFC 5545 termination: COUNT ─────────────────────────────────────────
    //
    // The engine is stateless per call — it does not remember how many times
    // it has been invoked for a given rule. The only COUNT value we can honour
    // unambiguously without state is COUNT=1: the original due-date counts as
    // occurrence #1, so any "next" call exceeds the limit and must return
    // invalid. For COUNT>1 the caller is responsible for tracking the running
    // count externally (see header doc); the engine advances normally.
    if (kv.contains(QStringLiteral("COUNT"))) {
        bool ok = false;
        const int count = kv.value(QStringLiteral("COUNT")).toInt(&ok);
        if (ok && count <= 1)
            return QDate();
    }

    // Determine effective base date.
    // B-4.60: FROM=COMPLETION means advance from the actual completion date.
    const bool fromCompletion = (kv.value(QStringLiteral("FROM")) == QStringLiteral("COMPLETION"));
    const QDate base = (fromCompletion && completionDate.isValid()) ? completionDate : fromDate;

    // Compute the candidate per FREQ branch, then apply UNTIL gating at the end.
    QDate candidate;

    if (freq == QStringLiteral("DAILY")) {
        // B-4.59 FIX: FREQ=DAILY with BYDAY is non-RFC; treat plain DAILY as every N days.
        // If BYDAY is present on a DAILY rule we ignore it (malformed RRULE produced by
        // old rules_en.cpp line 109 — now fixed to emit FREQ=WEEKLY for "every weekday").
        candidate = base.addDays(interval);
    }
    else if (freq == QStringLiteral("WEEKLY")) {
        const QString byday = kv.value(QStringLiteral("BYDAY"));
        if (byday.isEmpty()) {
            candidate = base.addDays(7 * interval);
        } else {
            // Find next matching weekday strictly after `base`. Whether to
            // apply the extra (interval-1) weeks depends on whether the next
            // BYDAY match wraps to a new ISO-week or stays in the current
            // on-week.
            //
            // Iter-night-10: the previous code added (interval-1) weeks
            // unconditionally. That broke multi-day BYDAY rules whose
            // INTERVAL>1 whenever the previous occurrence wasn't the LAST
            // BYDAY of its on-week:
            //
            //   FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR, base = Mon May 4
            //     nextByday → Wed May 6 (same-week match, no wrap)
            //     +1 extra week → Wed May 13 (in the OFF-week — invalid)
            //     expected:    Wed May 6  (still in the same on-week)
            //
            // RFC 5545: a BYDAY=MO,WE,FR;INTERVAL=2 rule expands to every
            // {Mon,Wed,Fri} of every OTHER week. Within an on-week the
            // adjacent BYDAYs are themselves valid occurrences; only when
            // we wrap into a new week does the (interval-1) skip kick in
            // to land us in the next on-week.
            const QList<int> days = parseByday(byday);
            if (days.isEmpty()) {
                candidate = base.addDays(7 * interval);
            } else {
                bool wrapped = false;
                candidate = nextByday(base, days, &wrapped);
                // Only add the extra (interval-1) weeks when the match
                // crossed a week boundary — otherwise we're still inside
                // the current on-week and the same-week BYDAY IS the
                // correct next occurrence.
                if (wrapped && interval > 1)
                    candidate = candidate.addDays(7 * (interval - 1));
            }
        }
    }
    else if (freq == QStringLiteral("MONTHLY")) {
        // B-4.34 + edge-case fix:
        //   * BYMONTHDAY wins if present, otherwise anchor on base.day().
        //   * If the anchor day does not exist in the target month (e.g.
        //     BYMONTHDAY=31 in February), RFC 5545 §3.3.10 says the occurrence
        //     is skipped — i.e. advance further until a month that has it.
        //     We only apply skip-on-missing when BYMONTHDAY is *explicit and
        //     in-range*. Plain MONTHLY (anchor derived from base.day()) keeps
        //     the historical clamp-to-month-end behaviour so existing tests
        //     like monthly_jan31_to_feb28 (FREQ=MONTHLY without BYMONTHDAY)
        //     continue to pass; an out-of-range BYMONTHDAY (e.g. 99) is
        //     silently ignored and behaves like the no-BYMONTHDAY case.
        int anchorDay = base.day();
        bool explicitAnchor = false;  // gates skip-on-missing
        if (kv.contains(QStringLiteral("BYMONTHDAY"))) {
            bool ok = false;
            const int d = kv.value(QStringLiteral("BYMONTHDAY")).toInt(&ok);
            if (ok && d >= 1 && d <= 31) {
                anchorDay = d;
                explicitAnchor = true;
            }
            // Otherwise: fall back to base.day() with legacy clamping.
        }

        QDate advanced = base.addMonths(interval);
        if (explicitAnchor) {
            // Walk forward one month at a time until a month that contains the
            // anchor day is reached. Bound the loop generously to guarantee
            // termination in pathological inputs (anchorDay ≤ 31, so at most
            // a handful of month-skips ever needed in practice).
            int safety = 0;
            while (advanced.daysInMonth() < anchorDay && safety < 24) {
                advanced = advanced.addMonths(1);
                ++safety;
            }
            candidate = QDate(advanced.year(), advanced.month(), anchorDay);
        } else {
            const int daysInMonth = advanced.daysInMonth();
            const int targetDay   = qMin(anchorDay, daysInMonth);
            candidate = QDate(advanced.year(), advanced.month(), targetDay);
        }
    }
    else if (freq == QStringLiteral("YEARLY")) {
        // Edge-case fix: if BYMONTHDAY is set (typically 29 paired with
        // BYMONTH=2) and the naive year-advance lands on an invalid date,
        // walk year-by-year until the anchor day exists. Pure FREQ=YEARLY
        // (no BYMONTHDAY) keeps the historical addYears() clamp behaviour.
        if (kv.contains(QStringLiteral("BYMONTHDAY"))) {
            bool ok = false;
            const int anchorDay = kv.value(QStringLiteral("BYMONTHDAY")).toInt(&ok);
            if (ok && anchorDay >= 1 && anchorDay <= 31) {
                // Use the base month (RFC also accepts BYMONTH but we don't
                // implement it; the natural pairing is base.month() which for
                // Feb-29 sources is February anyway).
                const int month = base.month();
                int year = base.year() + interval;
                int safety = 0;
                while (true) {
                    const QDate cand(year, month, anchorDay);
                    if (cand.isValid()) {
                        candidate = cand;
                        break;
                    }
                    ++year;
                    if (++safety > 8) // 8 years is enough for Feb-29 quadrennial
                        return QDate();
                }
            } else {
                candidate = base.addYears(interval);
            }
        } else {
            candidate = base.addYears(interval);
        }
    }
    else {
        return QDate(); // unrecognised FREQ
    }

    // ── RFC 5545 termination: UNTIL ─────────────────────────────────────────
    if (kv.contains(QStringLiteral("UNTIL"))) {
        const QDate until = parseUntil(kv.value(QStringLiteral("UNTIL")));
        if (until.isValid() && candidate.isValid() && candidate > until)
            return QDate();
    }

    return candidate;
}

// ---------------------------------------------------------------------------
// RecurrenceEngine::advanceFromHumanString  (B-4.60 legacy fallback)
// ---------------------------------------------------------------------------

QDate RecurrenceEngine::advanceFromHumanString(const QString &humanString,
                                               const QDate &fromDate)
{
    if (!fromDate.isValid()) return QDate();
    const QString r = humanString.toLower().trimmed();

    // every day / каждый день
    if (r == QStringLiteral("every day") || r == QStringLiteral("каждый день"))
        return fromDate.addDays(1);
    if (r == QStringLiteral("every week") || r == QStringLiteral("каждую неделю"))
        return fromDate.addDays(7);
    if (r == QStringLiteral("every month") || r == QStringLiteral("каждый месяц"))
        return fromDate.addMonths(1);
    if (r == QStringLiteral("every year") || r == QStringLiteral("каждый год"))
        return fromDate.addYears(1);
    if (r == QStringLiteral("every weekday") || r == QStringLiteral("каждый будний день")) {
        QDate d = fromDate.addDays(1);
        while (d.dayOfWeek() == 6 || d.dayOfWeek() == 7)
            d = d.addDays(1);
        return d;
    }

    // every N day(s)/week(s)/month(s)/year(s)
    {
        static const QRegularExpression enN(
            QStringLiteral("^every\\s+(\\d+)\\s+(day|days|week|weeks|month|months|year|years)$"));
        const auto m = enN.match(r);
        if (m.hasMatch()) {
            int n = m.captured(1).toInt();
            const QString unit = m.captured(2);
            if (unit.startsWith(QStringLiteral("day")))   return fromDate.addDays(n);
            if (unit.startsWith(QStringLiteral("week")))  return fromDate.addDays(n * 7);
            if (unit.startsWith(QStringLiteral("month"))) return fromDate.addMonths(n);
            if (unit.startsWith(QStringLiteral("year")))  return fromDate.addYears(n);
        }
    }

    // Russian "Каждые 3 нед/дн/мес/лет"
    {
        static const QRegularExpression ruN(
            QStringLiteral("^каждые\\s+(\\d+)\\s+(дн\\S*|нед\\S*|мес\\S*|лет|год\\S*)$"));
        const auto m = ruN.match(r);
        if (m.hasMatch()) {
            int n = m.captured(1).toInt();
            const QString unit = m.captured(2);
            if (unit.startsWith(QStringLiteral("дн")))  return fromDate.addDays(n);
            if (unit.startsWith(QStringLiteral("нед"))) return fromDate.addDays(n * 7);
            if (unit.startsWith(QStringLiteral("мес"))) return fromDate.addMonths(n);
            if (unit.startsWith(QStringLiteral("лет")) || unit.startsWith(QStringLiteral("год")))
                return fromDate.addYears(n);
        }
    }

    // every <weekday>
    {
        static const QMap<QString, int> dayMap = {
            {QStringLiteral("monday"), 1},    {QStringLiteral("mon"), 1},
            {QStringLiteral("tuesday"), 2},   {QStringLiteral("tue"), 2},
            {QStringLiteral("wednesday"), 3}, {QStringLiteral("wed"), 3},
            {QStringLiteral("thursday"), 4},  {QStringLiteral("thu"), 4},
            {QStringLiteral("friday"), 5},    {QStringLiteral("fri"), 5},
            {QStringLiteral("saturday"), 6},  {QStringLiteral("sat"), 6},
            {QStringLiteral("sunday"), 7},    {QStringLiteral("sun"), 7},
            {QStringLiteral("понедельник"), 1},
            {QStringLiteral("вторник"), 2},
            {QStringLiteral("среда"), 3}, {QStringLiteral("среду"), 3},
            {QStringLiteral("четверг"), 4},
            {QStringLiteral("пятница"), 5}, {QStringLiteral("пятницу"), 5},
            {QStringLiteral("суббота"), 6}, {QStringLiteral("субботу"), 6},
            {QStringLiteral("воскресенье"), 7},
        };
        static const QRegularExpression weekRe(
            QStringLiteral("^(?:every|каждый|каждую)\\s+(\\S+)$"));
        const auto m = weekRe.match(r);
        if (m.hasMatch()) {
            const QString day = m.captured(1);
            if (dayMap.contains(day)) {
                int target = dayMap.value(day);
                int cur = fromDate.dayOfWeek();
                int ahead = target - cur;
                if (ahead <= 0) ahead += 7;
                return fromDate.addDays(ahead);
            }
        }
    }

    return QDate();
}
