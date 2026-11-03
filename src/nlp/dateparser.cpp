#include "dateparser.h"
#include <QRegularExpression>
#include <QMap>

DateParser::DateParser(QObject *parent)
    : QObject(parent)
{
}

// Translate an English humanString produced by parseEnglish into its Russian
// equivalent. Returns the input unchanged if no mapping is found.
static QString translateHumanStringToRussian(const QString &en)
{
    if (en.isEmpty()) return en;

    // Direct phrase mappings
    static const QMap<QString, QString> direct = {
        {QStringLiteral("Today"), QStringLiteral("Сегодня")},
        {QStringLiteral("Tomorrow"), QStringLiteral("Завтра")},
        {QStringLiteral("Yesterday"), QStringLiteral("Вчера")},
        {QStringLiteral("Next week"), QStringLiteral("След. неделя")},
        {QStringLiteral("Every day"), QStringLiteral("Каждый день")},
        {QStringLiteral("Every week"), QStringLiteral("Каждую неделю")},
        {QStringLiteral("Every month"), QStringLiteral("Каждый месяц")},
        {QStringLiteral("Every year"), QStringLiteral("Каждый год")},
        {QStringLiteral("Every weekday"), QStringLiteral("Каждый будний день")},
    };
    if (direct.contains(en))
        return direct.value(en);

    // Weekday name mapping (English long form -> Russian)
    static const QMap<QString, QString> weekdayRu = {
        {QStringLiteral("Monday"),    QStringLiteral("понедельник")},
        {QStringLiteral("Tuesday"),   QStringLiteral("вторник")},
        {QStringLiteral("Wednesday"), QStringLiteral("среда")},
        {QStringLiteral("Thursday"),  QStringLiteral("четверг")},
        {QStringLiteral("Friday"),    QStringLiteral("пятница")},
        {QStringLiteral("Saturday"),  QStringLiteral("суббота")},
        {QStringLiteral("Sunday"),    QStringLiteral("воскресенье")},
    };

    // "Next <Weekday>" -> "След. <weekday>"
    {
        static const QRegularExpression nextRe(
            QStringLiteral("^Next\\s+(Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday)$"));
        const auto m = nextRe.match(en);
        if (m.hasMatch()) {
            const QString day = weekdayRu.value(m.captured(1), m.captured(1));
            return QStringLiteral("След. %1").arg(day);
        }
    }

    // "In N days/day/weeks/week/months/month" -> "Через N дн/нед/мес"
    {
        static const QRegularExpression inNRe(
            QStringLiteral("^In\\s+(\\d+)\\s+(day|days|week|weeks|month|months)$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = inNRe.match(en);
        if (m.hasMatch()) {
            const QString n = m.captured(1);
            const QString unit = m.captured(2).toLower();
            QString ru;
            if (unit.startsWith(QStringLiteral("day")))   ru = QStringLiteral("дн");
            else if (unit.startsWith(QStringLiteral("week"))) ru = QStringLiteral("нед");
            else if (unit.startsWith(QStringLiteral("month"))) ru = QStringLiteral("мес");
            if (!ru.isEmpty())
                return QStringLiteral("Через %1 %2").arg(n, ru);
        }
    }

    // "%1 days from now" -> "Через %1 дн"
    {
        static const QRegularExpression fromNowRe(
            QStringLiteral("^(\\d+)\\s+(day|days|week|weeks|month|months)\\s+from\\s+now$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = fromNowRe.match(en);
        if (m.hasMatch()) {
            const QString n = m.captured(1);
            const QString unit = m.captured(2).toLower();
            QString ru;
            if (unit.startsWith(QStringLiteral("day")))   ru = QStringLiteral("дн");
            else if (unit.startsWith(QStringLiteral("week"))) ru = QStringLiteral("нед");
            else if (unit.startsWith(QStringLiteral("month"))) ru = QStringLiteral("мес");
            if (!ru.isEmpty())
                return QStringLiteral("Через %1 %2").arg(n, ru);
        }
    }

    // "Every N <unit>" -> "Каждые N <unit_ru>"
    {
        static const QRegularExpression everyNRe(
            QStringLiteral("^Every\\s+(\\d+)\\s+(day|days|week|weeks|month|months|year|years)$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = everyNRe.match(en);
        if (m.hasMatch()) {
            const QString n = m.captured(1);
            const QString unit = m.captured(2).toLower();
            QString ru;
            if (unit.startsWith(QStringLiteral("day")))   ru = QStringLiteral("дн");
            else if (unit.startsWith(QStringLiteral("week"))) ru = QStringLiteral("нед");
            else if (unit.startsWith(QStringLiteral("month"))) ru = QStringLiteral("мес");
            else if (unit.startsWith(QStringLiteral("year"))) ru = QStringLiteral("лет");
            if (!ru.isEmpty())
                return QStringLiteral("Каждые %1 %2").arg(n, ru);
        }
    }

    // "Every <weekday>" -> "Каждый <weekday>"
    {
        static const QRegularExpression everyDayRe(
            QStringLiteral("^Every\\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday)$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = everyDayRe.match(en);
        if (m.hasMatch()) {
            QString cap = m.captured(1);
            cap[0] = cap[0].toUpper();
            const QString day = weekdayRu.value(cap, cap.toLower());
            return QStringLiteral("Каждый %1").arg(day);
        }
    }

    // "Today at HH:MM" -> "Сегодня в HH:MM"
    {
        static const QRegularExpression todayAtRe(
            QStringLiteral("^Today\\s+at\\s+(.+)$"));
        const auto m = todayAtRe.match(en);
        if (m.hasMatch())
            return QStringLiteral("Сегодня в %1").arg(m.captured(1));
    }

    // " (from completion)" suffix on recurrence rules
    if (en.endsWith(QStringLiteral(" (from completion)"))) {
        const QString base = en.left(en.size() - QStringLiteral(" (from completion)").size());
        const QString translatedBase = translateHumanStringToRussian(base);
        return translatedBase + QStringLiteral(" (от выполнения)");
    }

    // No mapping found — leave original.
    return en;
}

DateParseResult DateParser::parse(const QString &input, const QString &lang, int nextWeekDay)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return {};

    const QDate today = QDate::currentDate();

    if (lang == QStringLiteral("ru")) {
        DateParseResult ru = parseRussian(trimmed, today, nextWeekDay);
        if (ru.valid)
            return ru;
        // Russian parser failed — fall back to English but rewrite humanString.
        DateParseResult en = parseEnglish(trimmed, today, nextWeekDay);
        if (en.valid)
            en.humanString = translateHumanStringToRussian(en.humanString);
        return en;
    }

    DateParseResult en = parseEnglish(trimmed, today, nextWeekDay);
    if (en.valid)
        return en;
    // Mirror of the ru→en fallback: QuickAddParser accepts a due phrase when
    // EITHER language matches, so "Buy milk завтра" typed with the UI in
    // English hands us "завтра" — without this the date was silently dropped.
    DateParseResult ru = parseRussian(trimmed, today, nextWeekDay);
    return ru.valid ? ru : en;
}

QDate DateParser::firstBydayOnOrAfter(const QString &recurrenceRule, const QDate &reference)
{
    static const QRegularExpression bydayRe(QStringLiteral("(?:^|;)BYDAY=([A-Z,]+)"));
    const auto m = bydayRe.match(recurrenceRule);
    if (!m.hasMatch() || !reference.isValid())
        return reference;
    static const QMap<QString, int> codes = {
        {QStringLiteral("MO"), 1}, {QStringLiteral("TU"), 2}, {QStringLiteral("WE"), 3},
        {QStringLiteral("TH"), 4}, {QStringLiteral("FR"), 5}, {QStringLiteral("SA"), 6},
        {QStringLiteral("SU"), 7},
    };
    QList<int> dows;
    for (const QString &c : m.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (codes.contains(c))
            dows.append(codes.value(c));
    }
    if (dows.isEmpty())
        return reference;
    for (int i = 0; i < 7; ++i) {
        const QDate d = reference.addDays(i);
        if (dows.contains(d.dayOfWeek()))
            return d;
    }
    return reference;
}

DateParseResult DateParser::tryParseTime(const QString &input, DateParseResult result)
{
    // Q3.02-E Bug 4 fix: word-form times ("noon", "midnight", "полдень",
    // "полночь") preceded by "at"/"в". Map to the canonical HH:MM and then
    // fall through to the numeric path so all existing validation / human
    // string handling continues to work. Tried BEFORE the numeric regex
    // so "yesterday at noon" doesn't fall through to the digit matcher.
    {
        // NB: a trailing \b would never match after "полдень"/"полночь":
        // without UCP, Cyrillic letters are non-word characters, so there is
        // no word boundary between "ь" and the end of the string.
        static const QRegularExpression wordTimeRe(
            QStringLiteral("(?:at|в)\\s+(noon|midnight|midday|полдень|полночь)(?=\\s|$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto wm = wordTimeRe.match(input);
        if (wm.hasMatch()) {
            const QString word = wm.captured(1).toLower();
            int hour = 0;
            if (word == QStringLiteral("noon")
                || word == QStringLiteral("midday")
                || word == QString::fromUtf8("полдень")) {
                hour = 12;
            } // midnight / полночь → hour stays 0
            QDate date = QDate::fromString(result.date, QStringLiteral("yyyy-MM-dd"));
            if (date.isValid()) {
                QDateTime dt(date, QTime(hour, 0), QTimeZone::LocalTime);
                result.datetime = formatDateTime(dt);
            }
            return result;
        }
    }

    // Look for time patterns explicitly introduced by "at" or "в". A bare
    // number tail (e.g. "tomorrow 5") is NOT considered a time here — the
    // simple-day handler wouldn't have left it dangling unless the user
    // meant something the parser doesn't understand. Requiring the "at"/"в"
    // marker also lets us detect when the user clearly *intended* a time
    // and that time happened to be out-of-range: in that case the whole
    // expression is rejected (Q3.02-D B7/E7 fix).
    // "." is accepted as the minutes separator too ("в 18.30" is common in
    // Russian); the rules' time-tail strippers accept the same shapes.
    // Russian day-part markers work like am/pm: "в 9 утра" = 09:00,
    // "в 9 вечера" = 21:00, "в 3 дня" = 15:00, "в 2 ночи" = 02:00.
    static const QRegularExpression timeRe(
        QStringLiteral("(?:at|в)\\s+(\\d{1,2})(?:[:.](\\d{2}))?\\s*(am|pm|утра|дня|вечера|ночи)?"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = timeRe.match(input);
    if (match.hasMatch()) {
        int hour = match.captured(1).toInt();
        int minute = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
        const QString marker = match.captured(3).toLower();
        const bool pm = marker == QStringLiteral("pm")
            || marker == QString::fromUtf8("дня") || marker == QString::fromUtf8("вечера");
        const bool am = marker == QStringLiteral("am")
            || marker == QString::fromUtf8("утра") || marker == QString::fromUtf8("ночи");

        // Validate raw HH:MM BEFORE AM/PM adjustment — "25:00" should be
        // rejected regardless of any am/pm suffix.
        const bool rawHourValid   = (hour >= 0 && hour <= 23);
        const bool rawMinuteValid = (minute >= 0 && minute <= 59);

        if (pm && hour < 12) hour += 12;
        else if (am && hour == 12) hour = 0;

        if (rawHourValid && rawMinuteValid
            && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            QDate date = QDate::fromString(result.date, QStringLiteral("yyyy-MM-dd"));
            QTime time(hour, minute);
            QDateTime dt(date, time, QTimeZone::LocalTime);
            result.datetime = formatDateTime(dt);
        } else {
            // The user wrote an explicit "at HH:MM" / "в HH:MM" clause but
            // the time is out of range. Reject the whole expression rather
            // than silently dropping the time portion.
            result.valid = false;
            result.date.clear();
            result.datetime.clear();
            result.humanString.clear();
            result.isRecurring = false;
            result.recurrenceRule.clear();
        }
    }

    return result;
}

QString DateParser::formatDate(const QDate &date)
{
    return date.toString(QStringLiteral("yyyy-MM-dd"));
}

QString DateParser::formatDateTime(const QDateTime &dt)
{
    return dt.toString(Qt::ISODate);
}
