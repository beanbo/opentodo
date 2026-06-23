#include "dateparser.h"

#include <QLocale>
#include <QRegularExpression>

DateParseResult DateParser::parseRussian(const QString &input, const QDate &reference, int nextWeekDay)
{
    DateParseResult result;
    // Q3.02-D Bug 2 fix: normalise comma/semicolon/exclam/question separators
    // to whitespace so that natural forms like "завтра, в 14:00" parse the
    // same as "завтра в 14:00". The simple-day matcher and the time-tail
    // strippers all key off whitespace between tokens — comma after the
    // day word makes the token "завтра," and falls through.
    //
    // Perf guard (Q3.03-A follow-up): the QuickAddParser invokes us in an
    // O(N²) prefix-and-suffix sweep over the input words, so this function
    // runs ~2N times against ever-growing candidate strings. The regex was
    // turning a 10 KB pathological input from 242 ms → 900 ms because the
    // PCRE2 engine was being constructed-and-applied even when the candidate
    // contained zero punctuation. Cheap pre-check first; the regex still runs
    // unconditionally if any of `,;!?` appears.
    QString normalised = input;
    const bool hasPunct = std::any_of(
        input.cbegin(), input.cend(),
        [](QChar c) {
            return c == QLatin1Char(',') || c == QLatin1Char(';')
                || c == QLatin1Char('!') || c == QLatin1Char('?');
        });
    if (hasPunct) {
        // Punctuation-bearing inputs go through the full regex normalisation.
        static const QRegularExpression punctRe(QStringLiteral("[,;!?]+"));
        static const QRegularExpression wsRe(QStringLiteral("\\s+"));
        normalised.replace(punctRe, QStringLiteral(" "));
        normalised = normalised.trimmed().replace(wsRe, QStringLiteral(" "));
    } else {
        // No punctuation → skip the (perf-hostile) regex but ALWAYS trim
        // leading/trailing whitespace; the matchDay() helper compares the
        // first token with a literal word, so untrimmed input wouldn't match.
        normalised = normalised.trimmed();
    }
    const QString lower = normalised.toLower();

    // --- Simple day references ---
    // Allow trailing time clauses like "завтра в 18:00" — tryParseTime() will
    // pick the "в HH:MM" tail out of the original input.
    auto matchDay = [&](const QString &word) {
        return lower == word || lower.startsWith(word + QStringLiteral(" "));
    };

    if (matchDay(QString::fromUtf8("сегодня"))) {
        result.date = formatDate(reference);
        result.humanString = QString::fromUtf8("Сегодня");
        result.valid = true;
        return tryParseTime(input, result);
    }

    if (matchDay(QString::fromUtf8("завтра"))) {
        result.date = formatDate(reference.addDays(1));
        result.humanString = QString::fromUtf8("Завтра");
        result.valid = true;
        return tryParseTime(input, result);
    }

    // NB: "позавчера" must be checked before "вчера" — although the strings don't
    // share a prefix at the start (позавчера starts with "по"), keeping the longer
    // form first is the conventional ordering and makes the dispatch order obvious.
    if (matchDay(QString::fromUtf8("позавчера"))) {
        result.date = formatDate(reference.addDays(-2));
        result.humanString = QString::fromUtf8("Позавчера");
        result.valid = true;
        return tryParseTime(input, result);
    }

    if (matchDay(QString::fromUtf8("вчера"))) {
        result.date = formatDate(reference.addDays(-1));
        result.humanString = QString::fromUtf8("Вчера");
        result.valid = true;
        return tryParseTime(input, result);
    }

    if (matchDay(QString::fromUtf8("послезавтра"))) {
        result.date = formatDate(reference.addDays(2));
        result.humanString = QString::fromUtf8("Послезавтра");
        result.valid = true;
        return tryParseTime(input, result);
    }

    // --- "через N дней/недель/месяцев" with optional trailing "в <weekday>" ---
    // Q3.02-E Bug 1 fix: compound "через 2 недели в понедельник" — first
    // compute the relative offset, then snap forward to the next occurrence
    // of the named weekday (inclusive of the offset day itself if it falls
    // on the target dow).
    {
        static const QRegularExpression cherezRe(
            QString::fromUtf8("^через\\s+(\\d+)\\s+(день|дня|дней|неделю|недели|недель|месяц|месяца|месяцев)"
                              "(?:\\s+в(?:о)?\\s+(понедельник|вторник|среду|четверг|пятницу|субботу|воскресенье))?$"));
        const auto m = cherezRe.match(lower);
        if (m.hasMatch()) {
            int n = m.captured(1).toInt();
            const QString unit = m.captured(2);
            const QString dayName = m.captured(3);
            QDate date = reference;
            if (unit.startsWith(QString::fromUtf8("д"))) date = reference.addDays(n);
            else if (unit.startsWith(QString::fromUtf8("н"))) date = reference.addDays(n * 7);
            else if (unit.startsWith(QString::fromUtf8("м"))) date = reference.addMonths(n);

            if (!dayName.isEmpty()) {
                static const QMap<QString, int> ruDayMap = {
                    {QString::fromUtf8("понедельник"), 1},
                    {QString::fromUtf8("вторник"), 2},
                    {QString::fromUtf8("среду"), 3},
                    {QString::fromUtf8("четверг"), 4},
                    {QString::fromUtf8("пятницу"), 5},
                    {QString::fromUtf8("субботу"), 6},
                    {QString::fromUtf8("воскресенье"), 7},
                };
                const int target = ruDayMap.value(dayName, 0);
                if (target > 0) {
                    int curDow = date.dayOfWeek();
                    int daysAhead = target - curDow;
                    if (daysAhead < 0) daysAhead += 7;
                    date = date.addDays(daysAhead);
                }
                result.humanString = QString::fromUtf8("Через %1 %2 в %3")
                    .arg(n).arg(unit).arg(dayName);
            } else {
                result.humanString = QString::fromUtf8("Через %1 %2").arg(n).arg(unit);
            }

            result.date = formatDate(date);
            result.valid = true;
            return tryParseTime(input, result);
        }
    }

    // --- "на следующей неделе" ---
    // night iter-147: nextWeekDay (the user's `next_week_day` setting, 1=Mon..7=Sun)
    // selects which weekday "следующая неделя" resolves to. Always lands in the NEXT
    // calendar week: target = (next Monday) + (nextWeekDay - 1). Default 1 (Monday)
    // ⇒ identical to the historic next-Monday behaviour. Mirrors rules_en.cpp.
    if (lower == QString::fromUtf8("на следующей неделе") || lower == QString::fromUtf8("следующая неделя")) {
        const int currentDow = reference.dayOfWeek();                  // 1=Mon..7=Sun
        const int clamped = (nextWeekDay >= 1 && nextWeekDay <= 7) ? nextWeekDay : 1;
        const int daysToTarget = (8 - currentDow) + (clamped - 1);
        result.date = formatDate(reference.addDays(daysToTarget));
        result.humanString = QString::fromUtf8("Следующая неделя");
        result.valid = true;
        return result;
    }

    // --- "в понедельник", "в среду", etc. ---
    // B-4.14 FIX: also match when lower *starts with* the key followed by a space,
    // so that "в понедельник в 10:00" is recognised and time is parsed from the tail.
    {
        static const QMap<QString, int> ruDays = {
            {QString::fromUtf8("понедельник"), 1}, {QString::fromUtf8("в понедельник"), 1},
            {QString::fromUtf8("вторник"), 2}, {QString::fromUtf8("во вторник"), 2},
            {QString::fromUtf8("среду"), 3}, {QString::fromUtf8("в среду"), 3},
            {QString::fromUtf8("четверг"), 4}, {QString::fromUtf8("в четверг"), 4},
            {QString::fromUtf8("пятницу"), 5}, {QString::fromUtf8("в пятницу"), 5},
            {QString::fromUtf8("субботу"), 6}, {QString::fromUtf8("в субботу"), 6},
            {QString::fromUtf8("воскресенье"), 7}, {QString::fromUtf8("в воскресенье"), 7},
        };

        for (auto it = ruDays.begin(); it != ruDays.end(); ++it) {
            // B-4.14: accept exact match OR startsWith(key + " ") to allow time tails.
            if (lower == it.key() || lower.startsWith(it.key() + QStringLiteral(" "))) {
                int targetDow = it.value();
                int currentDow = reference.dayOfWeek();
                int daysAhead = targetDow - currentDow;
                if (daysAhead <= 0) daysAhead += 7;
                QDate date = reference.addDays(daysAhead);

                // Q3.02-E Bug 1 fix: compound "<weekday> через N <unit>" —
                // e.g. "в субботу через 3 недели" means the Saturday three
                // weeks from the upcoming Saturday. Detect the "через N
                // дней/недель/месяцев" tail and add the corresponding offset.
                // We do this BEFORE writing date/humanString so the offset
                // applies to the snapped date.
                const QString tail = (lower == it.key())
                    ? QString()
                    : lower.mid(it.key().size()).trimmed();
                if (!tail.isEmpty()) {
                    static const QRegularExpression cherezTailRe(
                        QString::fromUtf8("^через\\s+(\\d+)\\s+"
                                          "(день|дня|дней|неделю|недели|недель|"
                                          "месяц|месяца|месяцев)$"));
                    const auto cm = cherezTailRe.match(tail);
                    if (cm.hasMatch()) {
                        int n = cm.captured(1).toInt();
                        const QString unit = cm.captured(2);
                        if (unit.startsWith(QString::fromUtf8("д")))
                            date = date.addDays(n);
                        else if (unit.startsWith(QString::fromUtf8("н")))
                            date = date.addDays(n * 7);
                        else if (unit.startsWith(QString::fromUtf8("м")))
                            date = date.addMonths(n);
                    }
                }

                result.date = formatDate(date);
                static const QStringList ruDayNames = {
                    QString(), QString::fromUtf8("Понедельник"), QString::fromUtf8("Вторник"),
                    QString::fromUtf8("Среда"), QString::fromUtf8("Четверг"),
                    QString::fromUtf8("Пятница"), QString::fromUtf8("Суббота"),
                    QString::fromUtf8("Воскресенье")
                };
                result.humanString = ruDayNames.value(targetDow);
                result.valid = true;
                // Pass original input so tryParseTime can find the time clause.
                return tryParseTime(input, result);
            }
        }
    }

    // --- Q3.02-D Bug 3 fix: "по будням" / "в выходные" / "по выходным" ---
    // These are bare-noun forms that don't map to a single weekday.
    // "по будням"  / "в будни"     → Mon-Fri recurrence
    // "в выходные" / "по выходным" → Sat-Sun recurrence
    // Helper for picking the first matching weekday from the reference:
    auto firstWeekdayOnOrAfter = [&](int targetDow) {
        int currentDow = reference.dayOfWeek();
        int daysAhead = targetDow - currentDow;
        if (daysAhead < 0) daysAhead += 7;
        return reference.addDays(daysAhead);
    };
    {
        const bool weekdaysForm =
            lower == QString::fromUtf8("по будням")
         || lower == QString::fromUtf8("в будни")
         || lower == QString::fromUtf8("будни");
        const bool weekendsForm =
            lower == QString::fromUtf8("в выходные")
         || lower == QString::fromUtf8("по выходным")
         || lower == QString::fromUtf8("выходные");

        if (weekdaysForm) {
            // First date: today if today is Mon-Fri, else next Monday.
            const int dow = reference.dayOfWeek();
            QDate firstDate = (dow >= 1 && dow <= 5)
                ? reference
                : firstWeekdayOnOrAfter(1); // Monday
            result.isRecurring = true;
            result.date = formatDate(firstDate);
            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
            result.humanString = QString::fromUtf8("По будням");
            result.valid = true;
            return tryParseTime(input, result);
        }
        if (weekendsForm) {
            // First date: today if today is Sat-Sun, else upcoming Saturday.
            const int dow = reference.dayOfWeek();
            QDate firstDate = (dow == 6 || dow == 7)
                ? reference
                : firstWeekdayOnOrAfter(6); // Saturday
            result.isRecurring = true;
            result.date = formatDate(firstDate);
            result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU");
            result.humanString = QString::fromUtf8("По выходным");
            result.valid = true;
            return tryParseTime(input, result);
        }
    }

    // --- Q3.02-E Bug 2 fix: "первого числа каждого месяца" / "15 числа каждого
    // месяца" / "первое число каждого месяца" — monthly anchored to a specific
    // day-of-month. Emits FREQ=MONTHLY;BYMONTHDAY=N; the RecurrenceEngine
    // handles the skip-on-missing for shorter months (e.g. BYMONTHDAY=31 in
    // Feb is advanced to the next 31-day month).
    {
        // Russian ordinals for day-of-month. The genitive form ("первого",
        // "пятнадцатого") is the natural one for the "X числа" idiom; we also
        // accept the nominative "первое" via the rule's second branch.
        auto ruOrdinalToDay = [](const QString &w) -> int {
            static const QMap<QString, int> ordinals = {
                // genitive (X числа каждого месяца — "of the Xth day of every month")
                {QString::fromUtf8("первого"),      1},
                {QString::fromUtf8("второго"),      2},
                {QString::fromUtf8("третьего"),     3},
                {QString::fromUtf8("четвёртого"),   4},
                {QString::fromUtf8("четвертого"),   4},
                {QString::fromUtf8("пятого"),       5},
                {QString::fromUtf8("шестого"),      6},
                {QString::fromUtf8("седьмого"),     7},
                {QString::fromUtf8("восьмого"),     8},
                {QString::fromUtf8("девятого"),     9},
                {QString::fromUtf8("десятого"),    10},
                {QString::fromUtf8("пятнадцатого"),15},
                {QString::fromUtf8("двадцатого"),  20},
                {QString::fromUtf8("тридцатого"),  30},
                // nominative
                {QString::fromUtf8("первое"),       1},
                {QString::fromUtf8("второе"),       2},
                {QString::fromUtf8("третье"),       3},
                {QString::fromUtf8("четвёртое"),    4},
                {QString::fromUtf8("четвертое"),    4},
                {QString::fromUtf8("пятое"),        5},
                {QString::fromUtf8("пятнадцатое"), 15},
                {QString::fromUtf8("двадцатое"),   20},
                {QString::fromUtf8("тридцатое"),   30},
            };
            return ordinals.value(w, 0);
        };

        // Match either ordinal-word OR digit form, followed by "числа каждого
        // месяца" (genitive) OR "число каждого месяца" (nominative).
        static const QRegularExpression monthlyAnchorRe(
            QString::fromUtf8("^(\\d{1,2}|первого|второго|третьего|четвёртого|четвертого"
                              "|пятого|шестого|седьмого|восьмого|девятого|десятого"
                              "|пятнадцатого|двадцатого|тридцатого"
                              "|первое|второе|третье|четвёртое|четвертое"
                              "|пятое|пятнадцатое|двадцатое|тридцатое)"
                              "\\s+числ[ао]\\s+кажд(?:ого|ое)\\s+месяца$"));
        const auto m = monthlyAnchorRe.match(lower);
        if (m.hasMatch()) {
            const QString token = m.captured(1);
            int day = 0;
            bool numericOk = false;
            const int asDigit = token.toInt(&numericOk);
            if (numericOk)
                day = asDigit;
            else
                day = ruOrdinalToDay(token);

            if (day >= 1 && day <= 31) {
                int year = reference.year(), month = reference.month();
                QDate first;
                if (day <= QDate(year, month, 1).daysInMonth())
                    first = QDate(year, month, day);
                if (!first.isValid() || first < reference) {
                    QDate probe = QDate(year, month, 1).addMonths(1);
                    int target = qMin(day, probe.daysInMonth());
                    first = QDate(probe.year(), probe.month(), target);
                }
                result.isRecurring = true;
                result.date = formatDate(first);
                result.recurrenceRule = QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=%1").arg(day);
                result.humanString = QString::fromUtf8("%1 числа каждого месяца").arg(day);
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
    }

    // --- F-4.16 FIX: "по понедельникам", "по вторникам" etc. (dative plural) ---
    // "по <weekdayPlural>" → FREQ=WEEKLY;BYDAY=<CODE>
    {
        static const QRegularExpression poDativRe(
            QString::fromUtf8("^по\\s+(\\S+)$"));
        const auto m = poDativRe.match(lower);
        if (m.hasMatch()) {
            const QString form = m.captured(1);
            static const QMap<QString, QString> dativeToByday = {
                {QString::fromUtf8("понедельникам"), QStringLiteral("MO")},
                {QString::fromUtf8("вторникам"),     QStringLiteral("TU")},
                {QString::fromUtf8("средам"),        QStringLiteral("WE")},
                {QString::fromUtf8("четвергам"),     QStringLiteral("TH")},
                {QString::fromUtf8("пятницам"),      QStringLiteral("FR")},
                {QString::fromUtf8("субботам"),      QStringLiteral("SA")},
                {QString::fromUtf8("воскресеньям"),  QStringLiteral("SU")},
            };
            if (dativeToByday.contains(form)) {
                result.isRecurring = true;
                result.date = formatDate(reference);
                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=%1").arg(dativeToByday.value(form));
                result.humanString = QString::fromUtf8("Каждый %1").arg(form);
                result.valid = true;
                return tryParseTime(input, result);
            }
        }
    }

    // --- Annual recurrence with explicit date: "каждый год DD.MM[.YYYY]",
    //     "каждый год N месяц", "ежегодно ...", "каждое DD.MM", "каждое N месяц" ---
    // Matches BEFORE the generic "кажд(ый|ую|ое|ые)" block so that "каждое 9 мая"
    // and "каждый год 15.03" are recognised as YEARLY recurrence anchored on a date,
    // rather than falling through to other handlers.
    {
        static const QMap<QString, int> ruMonthsAnnual = {
            {QString::fromUtf8("январ"),  1}, {QString::fromUtf8("феврал"), 2},
            {QString::fromUtf8("март"),   3}, {QString::fromUtf8("апрел"),  4},
            {QString::fromUtf8("ма"),     5}, {QString::fromUtf8("июн"),    6},
            {QString::fromUtf8("июл"),    7}, {QString::fromUtf8("август"), 8},
            {QString::fromUtf8("сентябр"),9}, {QString::fromUtf8("октябр"), 10},
            {QString::fromUtf8("ноябр"), 11}, {QString::fromUtf8("декабр"), 12},
        };

        // Strip optional time tail " в HH[:MM]" so that "ежегодно 9 мая в 10:00"
        // still matches; tryParseTime() will read the time off the original input.
        QString annualPart = lower;
        {
            static const QRegularExpression timeTailRe(
                QString::fromUtf8("\\s+в\\s+\\d{1,2}(?::\\d{2})?\\s*$"));
            annualPart.remove(timeTailRe);
        }

        // Prefix forms accepted (case-insensitive thanks to toLower above):
        //   "каждый год ..." | "ежегодно ..." | "каждое ..."
        static const QRegularExpression annualPrefixRe(
            QString::fromUtf8("^(?:кажд(?:ый|ое)\\s+год|ежегодно|кажд(?:ое|ый))\\s+(.+)$"));
        // Note: this prefix RE intentionally allows "каждый <date>" / "каждое <date>"
        // to fall through unmatched when the tail isn't a date — we only emit a result
        // below if the date payload parses, so non-date tails (e.g. "каждый день")
        // are left for the generic кажд(ый|ую|ое|ые) handler that runs after this block.
        const auto pm = annualPrefixRe.match(annualPart);
        if (pm.hasMatch()) {
            const QString tail = pm.captured(1).trimmed();

            int day = 0, month = 0;
            QString humanMonth; // human-readable month name for the humanString

            // Form A: "DD.MM" or "DD.MM.YYYY" — year is ignored (annual recurrence).
            static const QRegularExpression numericDateRe(
                QStringLiteral(R"(^(\d{1,2})\.(\d{1,2})(?:\.\d{2,4})?$)"));
            const auto nm = numericDateRe.match(tail);
            if (nm.hasMatch()) {
                day   = nm.captured(1).toInt();
                month = nm.captured(2).toInt();
            } else {
                // Form B: "N <month-name>" — e.g. "9 мая", "15 марта".
                static const QRegularExpression wordDateRe(
                    QString::fromUtf8("^(\\d{1,2})\\s+(январ[яь]|феврал[яь]|март[а]?|апрел[яь]"
                                      "|ма[яй]|июн[яь]|июл[яь]|август[а]?|сентябр[яь]"
                                      "|октябр[яь]|ноябр[яь]|декабр[яь])$"));
                const auto wm = wordDateRe.match(tail);
                if (wm.hasMatch()) {
                    day = wm.captured(1).toInt();
                    const QString monthStr = wm.captured(2);
                    // Match by longest prefix first to avoid "ма" eating "март".
                    QStringList keys = ruMonthsAnnual.keys();
                    std::sort(keys.begin(), keys.end(),
                              [](const QString &a, const QString &b) { return a.size() > b.size(); });
                    for (const QString &key : keys) {
                        if (monthStr.startsWith(key)) {
                            month = ruMonthsAnnual.value(key);
                            humanMonth = monthStr;
                            break;
                        }
                    }
                }
            }

            if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
                // Pick next occurrence: this year if upcoming, else next year.
                QDate next(reference.year(), month, day);
                if (!next.isValid()) {
                    // Invalid Gregorian date (e.g. 31.02). Bail out — fall through
                    // to other handlers; caller gets valid=false.
                } else {
                    if (next < reference)
                        next = next.addYears(1);
                    result.date = formatDate(next);
                    result.isRecurring = true;
                    result.recurrenceRule =
                        QStringLiteral("FREQ=YEARLY;BYMONTH=%1;BYMONTHDAY=%2").arg(month).arg(day);
                    if (!humanMonth.isEmpty()) {
                        result.humanString =
                            QString::fromUtf8("ежегодно %1 %2").arg(day).arg(humanMonth);
                    } else {
                        result.humanString =
                            QString::fromUtf8("каждый год %1.%2")
                                .arg(day, 2, 10, QLatin1Char('0'))
                                .arg(month, 2, 10, QLatin1Char('0'));
                    }
                    result.valid = true;
                    return tryParseTime(input, result);
                }
            }
            // If we reach here, the prefix matched but the date payload didn't —
            // continue to other handlers (e.g. "каждое утро" should not be eaten here).
        }
    }

    // --- Recurring: "каждый день", "каждую неделю", "каждый понедельник", "каждые N месяцев" ---
    {
        static const QRegularExpression kazhdyRe(
            QString::fromUtf8("^кажд(ый|ую|ое|ые)!?\\s+(.+)$"));
        const auto m = kazhdyRe.match(lower);
        if (m.hasMatch()) {
            // "каждые! 3 дня" / "каждый! понедельник" — a trailing "!" on кажд…
            // anchors the next occurrence to the COMPLETION date instead of the
            // schedule (mirrors English "every!"). The RecurrenceEngine honours
            // the ;FROM=COMPLETION rule suffix.
            const bool fromCompletion = lower.contains(QLatin1Char('!'));
            QString what = m.captured(2).trimmed();
            result.isRecurring = true;
            result.date = formatDate(reference);

            if (what == QString::fromUtf8("день")) {
                result.recurrenceRule = QStringLiteral("FREQ=DAILY");
                result.humanString = QString::fromUtf8("Каждый день");
            } else if (what == QString::fromUtf8("неделю")) {
                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY");
                result.humanString = QString::fromUtf8("Каждую неделю");
            } else if (what == QString::fromUtf8("месяц")) {
                result.recurrenceRule = QStringLiteral("FREQ=MONTHLY");
                result.humanString = QString::fromUtf8("Каждый месяц");
            } else if ([&]() -> bool {
                // "каждый год DD.MM" — annual recurrence anchored on a specific date.
                // `what` at this point is the text after "кажд(ый/ую/ое/ые) ", e.g. "год 15.05".
                static const QRegularExpression annualDateRe(
                    QStringLiteral(R"(^год\s+(\d{1,2})\.(\d{1,2})$)"));
                const QRegularExpressionMatch annualMatch = annualDateRe.match(what);
                if (!annualMatch.hasMatch())
                    return false;
                int day   = annualMatch.captured(1).toInt();
                int month = annualMatch.captured(2).toInt();
                if (day < 1 || day > 31 || month < 1 || month > 12)
                    return false;
                QDate today = QDate::currentDate();
                QDate next(today.year(), month, day);
                if (!next.isValid())
                    return false; // e.g. Feb 30 — fall through; caller gets invalid result
                if (next < today)
                    next = next.addYears(1);
                result.date = next.toString(Qt::ISODate);
                result.isRecurring = true;
                result.recurrenceRule =
                    QStringLiteral("FREQ=YEARLY;BYMONTH=%1;BYMONTHDAY=%2").arg(month).arg(day);
                QLocale ru(QLocale::Russian);
                result.humanString =
                    QStringLiteral("ежегодно %1 %2")
                        .arg(day)
                        .arg(ru.monthName(month, QLocale::LongFormat).toLower());
                result.valid = true;
                return true;
            }()) {
                // All population was done inside the lambda; just fall through to the
                // result.valid assignment and early-return below.
                return tryParseTime(input, result);
            } else if (what == QString::fromUtf8("год")) {
                result.recurrenceRule = QStringLiteral("FREQ=YEARLY");
                result.humanString = QString::fromUtf8("Каждый год");
            } else if (what == QString::fromUtf8("будний день") || what == QString::fromUtf8("будний")) {
                // F-4.17 FIX: "каждый будний день" → every weekday.
                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
                result.humanString = QString::fromUtf8("Каждый будний день");
            } else {
                // "каждый понедельник", "каждые 3 месяца"
                static const QRegularExpression kazhdyNRe(
                    QString::fromUtf8("^(\\d+)\\s+(день|дня|дней|неделю|недели|недель|месяц|месяца|месяцев|год|года|лет)$"));
                const auto nm = kazhdyNRe.match(what);
                if (nm.hasMatch()) {
                    int interval = nm.captured(1).toInt();
                    QString unit = nm.captured(2);
                    if (unit.startsWith(QString::fromUtf8("д")))
                        result.recurrenceRule = QStringLiteral("FREQ=DAILY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QString::fromUtf8("н")))
                        result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QString::fromUtf8("м")))
                        result.recurrenceRule = QStringLiteral("FREQ=MONTHLY;INTERVAL=%1").arg(interval);
                    else if (unit.startsWith(QString::fromUtf8("г")) || unit.startsWith(QString::fromUtf8("л")))
                        result.recurrenceRule = QStringLiteral("FREQ=YEARLY;INTERVAL=%1").arg(interval);
                    result.humanString = QString::fromUtf8("Каждые %1 %2").arg(interval).arg(unit);
                } else {
                    // Day name: "каждый понедельник"
                    static const QMap<QString, QString> ruDayAbbrev = {
                        {QString::fromUtf8("понедельник"), QStringLiteral("MO")},
                        {QString::fromUtf8("вторник"), QStringLiteral("TU")},
                        {QString::fromUtf8("среду"), QStringLiteral("WE")},
                        {QString::fromUtf8("четверг"), QStringLiteral("TH")},
                        {QString::fromUtf8("пятницу"), QStringLiteral("FR")},
                        {QString::fromUtf8("субботу"), QStringLiteral("SA")},
                        {QString::fromUtf8("воскресенье"), QStringLiteral("SU")},
                    };

                    if (ruDayAbbrev.contains(what)) {
                        result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=%1").arg(ruDayAbbrev.value(what));
                        result.humanString = QString::fromUtf8("Каждый %1").arg(what);
                    } else {
                        // Q3.02-D Bug 4 (Russian): "каждый второй <weekday>" / "каждую вторую <weekday>"
                        // → FREQ=WEEKLY;INTERVAL=2;BYDAY=<DAY>
                        // The genitive/accusative forms of "второй" follow the gender of
                        // the weekday: "второй понедельник/вторник/четверг", "вторую среду/
                        // пятницу/субботу", "второе воскресенье".
                        static const QRegularExpression vtoroyRe(
                            QString::fromUtf8("^втор(?:ой|ую|ое)\\s+(\\S+)$"));
                        const auto vm = vtoroyRe.match(what);
                        if (vm.hasMatch()) {
                            const QString dayWord = vm.captured(1);
                            if (ruDayAbbrev.contains(dayWord)) {
                                result.recurrenceRule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=%1")
                                    .arg(ruDayAbbrev.value(dayWord));
                                result.humanString = QString::fromUtf8("Каждый второй %1").arg(dayWord);
                            }
                        }
                    }
                }
            }

            if (fromCompletion && !result.recurrenceRule.isEmpty()) {
                result.recurrenceRule += QStringLiteral(";FROM=COMPLETION");
                result.humanString += QString::fromUtf8(" (от выполнения)");
            }

            result.valid = !result.recurrenceRule.isEmpty();
            return tryParseTime(input, result);
        }
    }

    // --- Date patterns: "24 марта", "15 января 2026", "24 марта в 14:00" ---
    // B-4.15 FIX: strip optional time tail " в HH:MM" before the regex, then
    // pass the original input to tryParseTime for time extraction.
    {
        // Try to strip a trailing time clause of the form " в H:MM" or " в HH"
        QString datePart = lower;
        {
            static const QRegularExpression timeTailRe(
                QString::fromUtf8("\\s+в\\s+\\d{1,2}(?::\\d{2})?\\s*$"));
            datePart.remove(timeTailRe); // mutates only if matched
        }

        static const QRegularExpression ruDateRe(
            QString::fromUtf8("^(\\d{1,2})\\s+(январ[яь]|феврал[яь]|март[а]?|апрел[яь]|ма[яй]|июн[яь]"
                              "|июл[яь]|август[а]?|сентябр[яь]|октябр[яь]|ноябр[яь]|декабр[яь])"
                              "(?:\\s+(\\d{4}))?$"));
        const auto m = ruDateRe.match(datePart);
        if (m.hasMatch()) {
            static const QMap<QString, int> ruMonths = {
                {QString::fromUtf8("январ"), 1}, {QString::fromUtf8("феврал"), 2},
                {QString::fromUtf8("март"), 3}, {QString::fromUtf8("апрел"), 4},
                {QString::fromUtf8("ма"), 5}, {QString::fromUtf8("июн"), 6},
                {QString::fromUtf8("июл"), 7}, {QString::fromUtf8("август"), 8},
                {QString::fromUtf8("сентябр"), 9}, {QString::fromUtf8("октябр"), 10},
                {QString::fromUtf8("ноябр"), 11}, {QString::fromUtf8("декабр"), 12},
            };

            int day = m.captured(1).toInt();
            const QString monthStr = m.captured(2);
            int year = m.captured(3).isEmpty() ? reference.year() : m.captured(3).toInt();

            // Find month by prefix matching from the longest prefix first.
            int month = 0;
            QStringList keys = ruMonths.keys();
            std::sort(keys.begin(), keys.end(),
                      [](const QString &a, const QString &b) { return a.size() > b.size(); });
            for (const QString &key : keys) {
                if (monthStr.startsWith(key)) {
                    month = ruMonths.value(key);
                    break;
                }
            }

            if (month > 0) {
                QDate date(year, month, day);
                if (date.isValid()) {
                    bool ok = true;
                    if (m.captured(3).isEmpty() && date < reference) {
                        // Bump to next year. Defend against Feb 29 → non-leap
                        // year producing an invalid QDate (formatDate would
                        // return "", yet result.valid was being set to true).
                        const QDate bumped(year + 1, month, day);
                        if (bumped.isValid())
                            date = bumped;
                        else
                            ok = false; // fall through to next parser
                    }
                    if (ok) {
                        result.date = formatDate(date);
                        result.humanString = QStringLiteral("%1 %2").arg(day).arg(monthStr);
                        result.valid = true;
                        // Pass original input (not stripped datePart) so time is extracted.
                        return tryParseTime(input, result);
                    }
                }
            }
        }
    }

    // --- Time only: "в 9:00", "в 14:30" ---
    {
        static const QRegularExpression ruTimeRe(
            QString::fromUtf8("^(?:в\\s+)?(\\d{1,2})(?::(\\d{2}))?$"));
        const auto m = ruTimeRe.match(lower);
        if (m.hasMatch()) {
            int hour = m.captured(1).toInt();
            int minute = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
            if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
                result.date = formatDate(reference);
                QDateTime dt(reference, QTime(hour, minute), QTimeZone::LocalTime);
                result.datetime = formatDateTime(dt);
                result.humanString = QString::fromUtf8("Сегодня в %1:%2")
                    .arg(hour, 2, 10, QLatin1Char('0'))
                    .arg(minute, 2, 10, QLatin1Char('0'));
                result.valid = true;
                return result;
            }
        }
    }

    return result; // result.valid = false
}
