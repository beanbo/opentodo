#include "dateparser.h"

#include <QRegularExpression>

DateParseResult DateParser::parseEnglish(const QString &input, const QDate &reference)
{
    DateParseResult result;
    const QString lower = input.toLower().trimmed();

    // --- Simple day references (exact match or "today/tomorrow/yesterday at HH:MM") ---
    // "day before yesterday" is listed before "yesterday" in the alternation so that
    // the longer phrase is tried first — defensive ordering even though the anchors
    // ^...$ already prevent "yesterday" from spuriously matching "day before yesterday".
    {
        static const QRegularExpression todayAtRe(
            QStringLiteral("^(day before yesterday|today|tomorrow|tom|yesterday)(?:\\s+at\\s+.+)?$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = todayAtRe.match(lower);
        if (m.hasMatch()) {
            const QString keyword = m.captured(1).toLower();
            if (keyword == QStringLiteral("today")) {
                result.date = formatDate(reference);
                result.humanString = QStringLiteral("Today");
            } else if (keyword == QStringLiteral("tomorrow") || keyword == QStringLiteral("tom")) {
                result.date = formatDate(reference.addDays(1));
                result.humanString = QStringLiteral("Tomorrow");
            } else if (keyword == QStringLiteral("yesterday")) {
                result.date = formatDate(reference.addDays(-1));
                result.humanString = QStringLiteral("Yesterday");
            } else if (keyword == QStringLiteral("day before yesterday")) {
                result.date = formatDate(reference.addDays(-2));
                result.humanString = QStringLiteral("Day before yesterday");
            }
            result.valid = true;
            return tryParseTime(input, result);
        }
    }

    // --- "in N days/weeks/months" ---
    {
        static const QRegularExpression inNRe(
            QStringLiteral("^in\\s+(\\d+)\\s+(day|days|week|weeks|month|months)$"));
        const auto m = inNRe.match(lower);
        if (m.hasMatch()) {
            int n = m.captured(1).toInt();
            const QString unit = m.captured(2);
            QDate date = reference;
            if (unit.startsWith(QStringLiteral("day"))) date = reference.addDays(n);
            else if (unit.startsWith(QStringLiteral("week"))) date = reference.addDays(n * 7);
            else if (unit.startsWith(QStringLiteral("month"))) date = reference.addMonths(n);
            result.date = formatDate(date);
            result.humanString = QStringLiteral("In %1 %2").arg(n).arg(unit);
            result.valid = true;
            return tryParseTime(input, result);
        }
    }

    // --- "next <weekday>" ---
    {
        static const QRegularExpression nextDayRe(
            QStringLiteral("^next\\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday"
                           "|mon|tue|wed|thu|fri|sat|sun)$"));
        const auto m = nextDayRe.match(lower);
        if (m.hasMatch()) {
            QString dayName = m.captured(1);
            static const QMap<QString, int> dayMap = {
                {QStringLiteral("monday"), 1}, {QStringLiteral("mon"), 1},
                {QStringLiteral("tuesday"), 2}, {QStringLiteral("tue"), 2},
                {QStringLiteral("wednesday"), 3}, {QStringLiteral("wed"), 3},
                {QStringLiteral("thursday"), 4}, {QStringLiteral("thu"), 4},
                {QStringLiteral("friday"), 5}, {QStringLiteral("fri"), 5},
                {QStringLiteral("saturday"), 6}, {QStringLiteral("sat"), 6},
                {QStringLiteral("sunday"), 7}, {QStringLiteral("sun"), 7},
            };
            int targetDow = dayMap.value(dayName, 0);
            if (targetDow > 0) {
                int currentDow = reference.dayOfWeek();
                int daysAhead = targetDow - currentDow;
                if (daysAhead <= 0) daysAhead += 7;
                QDate date = reference.addDays(daysAhead);
                result.date = formatDate(date);
                result.humanString = QStringLiteral("Next %1").arg(
                    QLocale::c().dayName(targetDow, QLocale::LongFormat));
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
    }

    // --- "next week"
    // B-4.11 FIX: accept optional weekStart parameter via the signature so a future
    // caller can pass through the user setting.  For now defaults to Monday (1).
    if (lower == QStringLiteral("next week")) {
        // weekStart defaults to Monday (dayOfWeek == 1).
        // To extend later: add `int weekStart = 1` to parseEnglish signature and
        // pass it through from DateParser::parse().
        const int weekStart = 1; // Monday
        const int currentDow = reference.dayOfWeek();
        int daysToFirst = (weekStart - currentDow + 7) % 7;
        if (daysToFirst == 0) daysToFirst = 7; // always go to NEXT week's first day
        result.date = formatDate(reference.addDays(daysToFirst));
        result.humanString = QStringLiteral("Next week");
        result.valid = true;
        return result;
    }

    // --- Recurring: "every day", "every weekday", "every <day>", "every N <unit>" ---
    {
        static const QRegularExpression everyRe(
            QStringLiteral("^every!?\\s+(.+)$"));
        const auto m = everyRe.match(lower);
        if (m.hasMatch()) {
            bool fromCompletion = lower.contains(QLatin1Char('!'));
            QString what = m.captured(1).trimmed();
            result.isRecurring = true;
            result.date = formatDate(reference);

            if (what == QStringLiteral("day")) {
                result.recurrenceRule = QStringLiteral("FREQ=DAILY");
                result.humanString = QStringLiteral("Every day");
            } else if (what == QStringLiteral("weekday")) {
                // B-4.59 FIX: was "FREQ=DAILY;BYDAY=..." (non-RFC).
                // FREQ=WEEKLY with BYDAY covering Mon-Fri is correct.
                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
                result.humanString = QStringLiteral("Every weekday");
            } else if (what == QStringLiteral("week")) {
                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY");
                result.humanString = QStringLiteral("Every week");
            } else if (what == QStringLiteral("month")) {
                result.recurrenceRule = QStringLiteral("FREQ=MONTHLY");
                result.humanString = QStringLiteral("Every month");
            } else if (what == QStringLiteral("year")) {
                result.recurrenceRule = QStringLiteral("FREQ=YEARLY");
                result.humanString = QStringLiteral("Every year");
            } else {
                // "every monday", "every 3 months", etc.
                static const QRegularExpression everyNRe(
                    QStringLiteral("^(\\d+)\\s+(day|days|week|weeks|month|months|year|years)$"));
                const auto nm = everyNRe.match(what);
                if (nm.hasMatch()) {
                    int interval = nm.captured(1).toInt();
                    QString unit = nm.captured(2);
                    if (unit.startsWith(QStringLiteral("day")))
                        result.recurrenceRule = QStringLiteral("FREQ=DAILY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QStringLiteral("week")))
                        result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QStringLiteral("month")))
                        result.recurrenceRule = QStringLiteral("FREQ=MONTHLY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QStringLiteral("year")))
                        result.recurrenceRule = QStringLiteral("FREQ=YEARLY;INTERVAL=%1").arg(interval);
                    result.humanString = QStringLiteral("Every %1 %2").arg(interval).arg(unit);
                } else {
                    // "every monday", "every mon/wed/fri"
                    static const QMap<QString, QString> dayAbbrev = {
                        {QStringLiteral("monday"), QStringLiteral("MO")},
                        {QStringLiteral("mon"), QStringLiteral("MO")},
                        {QStringLiteral("tuesday"), QStringLiteral("TU")},
                        {QStringLiteral("tue"), QStringLiteral("TU")},
                        {QStringLiteral("wednesday"), QStringLiteral("WE")},
                        {QStringLiteral("wed"), QStringLiteral("WE")},
                        {QStringLiteral("thursday"), QStringLiteral("TH")},
                        {QStringLiteral("thu"), QStringLiteral("TH")},
                        {QStringLiteral("friday"), QStringLiteral("FR")},
                        {QStringLiteral("fri"), QStringLiteral("FR")},
                        {QStringLiteral("saturday"), QStringLiteral("SA")},
                        {QStringLiteral("sat"), QStringLiteral("SA")},
                        {QStringLiteral("sunday"), QStringLiteral("SU")},
                        {QStringLiteral("sun"), QStringLiteral("SU")},
                    };

                    QStringList parts = what.split(QRegularExpression(QStringLiteral("[/,\\s]+")));
                    QStringList days;
                    for (const auto &p : parts) {
                        if (dayAbbrev.contains(p))
                            days.append(dayAbbrev.value(p));
                    }
                    if (!days.isEmpty()) {
                        result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=%1").arg(days.join(QLatin1Char(',')));
                        result.humanString = QStringLiteral("Every %1").arg(what);
                    }
                }
            }

            if (fromCompletion) {
                result.recurrenceRule += QStringLiteral(";FROM=COMPLETION");
                result.humanString += QStringLiteral(" (from completion)");
            }

            result.valid = !result.recurrenceRule.isEmpty();
            return tryParseTime(input, result);
        }
    }

    // --- Month day patterns: "jan 15", "march 3 2026", "3/24", "2026-03-24" ---
    {
        // ISO date
        static const QRegularExpression isoRe(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})$"));
        auto m = isoRe.match(lower);
        if (m.hasMatch()) {
            QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
            if (date.isValid()) {
                result.date = formatDate(date);
                result.humanString = date.toString(QStringLiteral("MMM d, yyyy"));
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
    }
    {
        // US-style: M/D or M/D/YYYY
        static const QRegularExpression slashRe(QStringLiteral("^(\\d{1,2})/(\\d{1,2})(?:/(\\d{2,4}))?$"));
        auto m = slashRe.match(lower);
        if (m.hasMatch()) {
            int month = m.captured(1).toInt();
            int day = m.captured(2).toInt();
            int year = m.captured(3).isEmpty() ? reference.year() : m.captured(3).toInt();
            // B-4.5 FIX: treat 00-69 as 2000s, 70-99 as 1900s (standard two-digit cutoff).
            if (year < 70)        year += 2000;
            else if (year < 100)  year += 1900;
            QDate date(year, month, day);
            if (date.isValid()) {
                result.date = formatDate(date);
                result.humanString = date.toString(QStringLiteral("MMM d"));
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
    }
    {
        // "Jan 15", "March 3 2026", "march 3rd", "3rd of march"
        // F-4.10 FIX: accept ordinal suffixes (st|nd|rd|th) on day numbers,
        // and also the "Nth of Month" form.

        static const QMap<QString, int> monthMap = {
            {QStringLiteral("jan"), 1}, {QStringLiteral("january"), 1},
            {QStringLiteral("feb"), 2}, {QStringLiteral("february"), 2},
            {QStringLiteral("mar"), 3}, {QStringLiteral("march"), 3},
            {QStringLiteral("apr"), 4}, {QStringLiteral("april"), 4},
            {QStringLiteral("may"), 5},
            {QStringLiteral("jun"), 6}, {QStringLiteral("june"), 6},
            {QStringLiteral("jul"), 7}, {QStringLiteral("july"), 7},
            {QStringLiteral("aug"), 8}, {QStringLiteral("august"), 8},
            {QStringLiteral("sep"), 9}, {QStringLiteral("september"), 9},
            {QStringLiteral("oct"), 10}, {QStringLiteral("october"), 10},
            {QStringLiteral("nov"), 11}, {QStringLiteral("november"), 11},
            {QStringLiteral("dec"), 12}, {QStringLiteral("december"), 12},
        };

        // Helper: try to resolve and clamp a (year, month, day) triple.
        auto tryDate = [&](int year, int month, int day, bool yearProvided) -> bool {
            if (month < 1) return false;
            // B-4.7 FIX: clamp invalid days (e.g. "feb 31") to last day of month.
            QDate probe(year, month, 1);
            if (!probe.isValid()) return false;
            const int daysInMonth = probe.daysInMonth();
            if (day > daysInMonth) {
                // Clamp for this occurrence; the caller requested an impossible date
                // like "feb 31" so we silently map it to the last valid day.
                day = daysInMonth;
            }
            QDate date(year, month, day);
            if (!date.isValid()) return false;
            if (!yearProvided && date < reference)
                date = QDate(year + 1, month, day);
            result.date = formatDate(date);
            result.humanString = date.toString(QStringLiteral("MMM d"));
            result.valid = true;
            return true;
        };

        // Form 1: "Month Day[ordinal] [Year]"  e.g. "jan 15", "march 3rd 2026"
        {
            static const QRegularExpression monthDayRe(
                QStringLiteral("^(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec"
                               "|january|february|march|april|may|june|july|august"
                               "|september|october|november|december)"
                               "\\s+(\\d{1,2})(?:st|nd|rd|th)?(?:,?\\s+(\\d{4}))?$"));
            const auto m = monthDayRe.match(lower);
            if (m.hasMatch()) {
                int month = monthMap.value(m.captured(1));
                int day   = m.captured(2).toInt();
                int year  = m.captured(3).isEmpty() ? reference.year() : m.captured(3).toInt();
                if (tryDate(year, month, day, !m.captured(3).isEmpty()))
                    return tryParseTime(input, result);
            }
        }

        // Form 2: "Nth of Month [Year]"  e.g. "3rd of march", "1st of january 2027"
        {
            static const QRegularExpression ordinalOfMonthRe(
                QStringLiteral("^(\\d{1,2})(?:st|nd|rd|th)\\s+of\\s+"
                               "(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec"
                               "|january|february|march|april|may|june|july|august"
                               "|september|october|november|december)"
                               "(?:,?\\s+(\\d{4}))?$"));
            const auto m = ordinalOfMonthRe.match(lower);
            if (m.hasMatch()) {
                int day   = m.captured(1).toInt();
                int month = monthMap.value(m.captured(2));
                int year  = m.captured(3).isEmpty() ? reference.year() : m.captured(3).toInt();
                if (tryDate(year, month, day, !m.captured(3).isEmpty()))
                    return tryParseTime(input, result);
            }
        }
    }

    // --- Standalone time (implies today) ---
    {
        static const QRegularExpression timeOnlyRe(
            QStringLiteral("^(?:at\\s+)?(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm|AM|PM)?$"));
        const auto m = timeOnlyRe.match(lower);
        if (m.hasMatch()) {
            int hour = m.captured(1).toInt();
            int minute = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
            const QString ampm = m.captured(3).toLower();

            if (ampm == QStringLiteral("pm") && hour < 12) hour += 12;
            else if (ampm == QStringLiteral("am") && hour == 12) hour = 0;

            if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
                result.date = formatDate(reference);
                QDateTime dt(reference, QTime(hour, minute), QTimeZone::LocalTime);
                result.datetime = formatDateTime(dt);
                result.humanString = QStringLiteral("Today at %1:%2")
                    .arg(hour, 2, 10, QLatin1Char('0'))
                    .arg(minute, 2, 10, QLatin1Char('0'));
                result.valid = true;
                return result;
            }
        }
    }

    return result; // result.valid = false
}
