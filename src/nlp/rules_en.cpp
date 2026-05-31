#include "dateparser.h"

#include <QRegularExpression>

DateParseResult DateParser::parseEnglish(const QString &input, const QDate &reference, int nextWeekDay)
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

    // --- "in N days/weeks/months" with optional trailing " on <weekday>" ---
    // Q3.02-E Bug 1 + Bug 5 fix: compound expressions "in N weeks on monday" /
    // "in N weeks on saturday" — first compute the relative offset, then snap
    // forward to the next occurrence of the named weekday (inclusive of the
    // offset day itself if it happens to fall on the target dow).
    {
        static const QRegularExpression inNRe(
            QStringLiteral("^in\\s+(\\d+)\\s+(day|days|week|weeks|month|months)"
                           "(?:\\s+on\\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday"
                           "|mon|tue|wed|thu|fri|sat|sun))?$"));
        const auto m = inNRe.match(lower);
        if (m.hasMatch()) {
            int n = m.captured(1).toInt();
            const QString unit = m.captured(2);
            const QString dayName = m.captured(3);
            QDate date = reference;
            if (unit.startsWith(QStringLiteral("day"))) date = reference.addDays(n);
            else if (unit.startsWith(QStringLiteral("week"))) date = reference.addDays(n * 7);
            else if (unit.startsWith(QStringLiteral("month"))) date = reference.addMonths(n);

            if (!dayName.isEmpty()) {
                static const QMap<QString, int> dayMap = {
                    {QStringLiteral("monday"), 1}, {QStringLiteral("mon"), 1},
                    {QStringLiteral("tuesday"), 2}, {QStringLiteral("tue"), 2},
                    {QStringLiteral("wednesday"), 3}, {QStringLiteral("wed"), 3},
                    {QStringLiteral("thursday"), 4}, {QStringLiteral("thu"), 4},
                    {QStringLiteral("friday"), 5}, {QStringLiteral("fri"), 5},
                    {QStringLiteral("saturday"), 6}, {QStringLiteral("sat"), 6},
                    {QStringLiteral("sunday"), 7}, {QStringLiteral("sun"), 7},
                };
                const int target = dayMap.value(dayName, 0);
                if (target > 0) {
                    int curDow = date.dayOfWeek();
                    int daysAhead = target - curDow;
                    if (daysAhead < 0) daysAhead += 7;
                    date = date.addDays(daysAhead);
                }
                result.humanString = QStringLiteral("In %1 %2 on %3").arg(n).arg(unit).arg(dayName);
            } else {
                result.humanString = QStringLiteral("In %1 %2").arg(n).arg(unit);
            }

            result.date = formatDate(date);
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
    // night iter-147: nextWeekDay (the user's `next_week_day` setting, 1=Mon..7=Sun)
    // now selects WHICH weekday "next week" resolves to. The result is ALWAYS in the
    // NEXT calendar week — never the current one. Weeks are anchored to Monday for
    // this computation, so the target = (next Monday) + (nextWeekDay - 1) days:
    //   daysToTarget = (8 - currentDow) + (nextWeekDay - 1)
    // The old (weekStart - currentDow + 7) % 7 formula was correct only for Monday;
    // for a mid-week target on an earlier weekday it could land in the *current*
    // week. nextWeekDay defaults to 1 (Monday) ⇒ identical to the historic behaviour.
    if (lower == QStringLiteral("next week")) {
        const int currentDow = reference.dayOfWeek();                  // 1=Mon..7=Sun
        const int clamped = (nextWeekDay >= 1 && nextWeekDay <= 7) ? nextWeekDay : 1;
        const int daysToTarget = (8 - currentDow) + (clamped - 1);
        result.date = formatDate(reference.addDays(daysToTarget));
        result.humanString = QStringLiteral("Next week");
        result.valid = true;
        return result;
    }

    // --- Q3.02-D Bug 3 fix: bare nouns "weekdays" / "weekends" ---
    // No "every" prefix needed — natural way for users to express Mon-Fri or
    // Sat-Sun recurrence.
    {
        auto firstDayOnOrAfter = [&](int targetDow) {
            int currentDow = reference.dayOfWeek();
            int daysAhead = targetDow - currentDow;
            if (daysAhead < 0) daysAhead += 7;
            return reference.addDays(daysAhead);
        };

        if (lower == QStringLiteral("weekdays")) {
            const int dow = reference.dayOfWeek();
            QDate firstDate = (dow >= 1 && dow <= 5) ? reference : firstDayOnOrAfter(1);
            result.isRecurring = true;
            result.date = formatDate(firstDate);
            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
            result.humanString = QStringLiteral("Weekdays");
            result.valid = true;
            return tryParseTime(input, result);
        }
        if (lower == QStringLiteral("weekends")) {
            const int dow = reference.dayOfWeek();
            QDate firstDate = (dow == 6 || dow == 7) ? reference : firstDayOnOrAfter(6);
            result.isRecurring = true;
            result.date = formatDate(firstDate);
            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU");
            result.humanString = QStringLiteral("Weekends");
            result.valid = true;
            return tryParseTime(input, result);
        }
    }

    // --- Q3.02-E Bug 2 fix: "first day of each month", "1st of each month",
    // "Nth of every month", "15th day of each month" → monthly anchor on
    // a specific day-of-month. Produces FREQ=MONTHLY;BYMONTHDAY=N.
    //
    // The RecurrenceEngine MONTHLY branch already handles the BYMONTHDAY-31-
    // in-February skip-on-missing correctly (rules_en gives the anchor, the
    // engine walks forward); see RecurrenceEngine::advance().
    {
        // Map ordinal words / suffixed-digit forms to a day-of-month integer.
        // We only cover the most natural English forms here; "1st" through
        // "31st" via suffix, plus the named ordinals "first" / "last".
        auto wordOrdinalToDay = [](const QString &w) -> int {
            static const QMap<QString, int> ordinals = {
                {QStringLiteral("first"),     1},
                {QStringLiteral("second"),    2},
                {QStringLiteral("third"),     3},
                {QStringLiteral("fourth"),    4},
                {QStringLiteral("fifth"),     5},
                {QStringLiteral("sixth"),     6},
                {QStringLiteral("seventh"),   7},
                {QStringLiteral("eighth"),    8},
                {QStringLiteral("ninth"),     9},
                {QStringLiteral("tenth"),    10},
                {QStringLiteral("eleventh"), 11},
                {QStringLiteral("twelfth"),  12},
                {QStringLiteral("fifteenth"), 15},
                {QStringLiteral("twentieth"), 20},
                {QStringLiteral("thirtieth"), 30},
                {QStringLiteral("last"),     31}, // last day → BYMONTHDAY=31, engine clamps via skip-on-missing
            };
            return ordinals.value(w.toLower(), 0);
        };

        // Pattern: "(<ordinal>|<N><suffix>) [day] of (each|every) month"
        // Examples: "first day of each month", "1st of every month",
        //           "15th of each month", "last day of every month".
        static const QRegularExpression monthlyAnchorRe(
            QStringLiteral("^(\\d{1,2}(?:st|nd|rd|th)?|first|second|third|fourth|fifth|sixth|seventh"
                           "|eighth|ninth|tenth|eleventh|twelfth|fifteenth|twentieth|thirtieth|last)"
                           "(?:\\s+day)?\\s+of\\s+(?:each|every)\\s+month$"));
        const auto m = monthlyAnchorRe.match(lower);
        if (m.hasMatch()) {
            QString token = m.captured(1);
            int day = 0;
            // Numeric form: "1st", "15th", "31" → strip suffix and parse.
            static const QRegularExpression numericRe(QStringLiteral("^(\\d{1,2})(?:st|nd|rd|th)?$"));
            const auto nm = numericRe.match(token);
            if (nm.hasMatch())
                day = nm.captured(1).toInt();
            else
                day = wordOrdinalToDay(token);

            if (day >= 1 && day <= 31) {
                // First-occurrence date: this month's <day> if still upcoming,
                // else next month's <day>. The RecurrenceEngine handles the
                // skip-on-missing for BYMONTHDAY=29/30/31 in shorter months.
                int year = reference.year(), month = reference.month();
                QDate first(year, month, qMin(day, QDate(year, month, 1).daysInMonth()));
                if (day <= QDate(year, month, 1).daysInMonth())
                    first = QDate(year, month, day);
                if (first < reference || !first.isValid()) {
                    // Move to next month and clamp again.
                    QDate probe = QDate(year, month, 1).addMonths(1);
                    int target = qMin(day, probe.daysInMonth());
                    first = QDate(probe.year(), probe.month(), target);
                }
                result.isRecurring = true;
                result.date = formatDate(first);
                result.recurrenceRule = QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=%1").arg(day);
                result.humanString = QStringLiteral("Day %1 of every month").arg(day);
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
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
                    // "every monday", "every mon/wed/fri", "every other tuesday"
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

                    // Q3.02-D Bug 4 fix: detect "other" prefix → INTERVAL=2.
                    // "every other tuesday" / "every other day" — fortnightly.
                    QString remainder = what;
                    bool everyOther = false;
                    if (remainder.startsWith(QStringLiteral("other "))) {
                        everyOther = true;
                        remainder = remainder.mid(6).trimmed();
                    }

                    QStringList parts = remainder.split(QRegularExpression(QStringLiteral("[/,\\s]+")));
                    QStringList days;
                    for (const auto &p : parts) {
                        if (dayAbbrev.contains(p))
                            days.append(dayAbbrev.value(p));
                    }
                    if (!days.isEmpty()) {
                        if (everyOther) {
                            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=%1")
                                .arg(days.join(QLatin1Char(',')));
                            result.humanString = QStringLiteral("Every other %1").arg(remainder);
                        } else {
                            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=%1").arg(days.join(QLatin1Char(',')));
                            result.humanString = QStringLiteral("Every %1").arg(what);
                        }
                    } else if (everyOther && remainder == QStringLiteral("day")) {
                        // "every other day" — biweekly... no, fortnightly daily. Actually
                        // "every other day" means every 2 days.
                        result.recurrenceRule = QStringLiteral("FREQ=DAILY;INTERVAL=2");
                        result.humanString = QStringLiteral("Every other day");
                    } else if (everyOther && remainder == QStringLiteral("week")) {
                        result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2");
                        result.humanString = QStringLiteral("Every other week");
                    } else if (everyOther && remainder == QStringLiteral("month")) {
                        result.recurrenceRule = QStringLiteral("FREQ=MONTHLY;INTERVAL=2");
                        result.humanString = QStringLiteral("Every other month");
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
            if (!yearProvided && date < reference) {
                // Bump to next year. Defend against Feb 29 → non-leap year
                // producing an invalid QDate (formatDate would return "",
                // yet result.valid was being set to true and the caller
                // returned tryParseTime — net: result.valid=true with
                // result.date="").
                const QDate bumped(year + 1, month, day);
                if (!bumped.isValid()) return false;
                date = bumped;
            }
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
