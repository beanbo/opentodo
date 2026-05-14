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

DateParseResult DateParser::parse(const QString &input, const QString &lang)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return {};

    const QDate today = QDate::currentDate();

    if (lang == QStringLiteral("ru")) {
        DateParseResult ru = parseRussian(trimmed, today);
        if (ru.valid)
            return ru;
        // Russian parser failed — fall back to English but rewrite humanString.
        DateParseResult en = parseEnglish(trimmed, today);
        if (en.valid)
            en.humanString = translateHumanStringToRussian(en.humanString);
        return en;
    }

    return parseEnglish(trimmed, today);
}

DateParseResult DateParser::tryParseTime(const QString &input, DateParseResult result)
{
    // Look for time patterns: "at HH:MM", "at Ham", "at Hpm"
    static const QRegularExpression timeRe(
        QStringLiteral("(?:at\\s+|в\\s+)?(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm|AM|PM)?"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = timeRe.match(input);
    if (match.hasMatch()) {
        int hour = match.captured(1).toInt();
        int minute = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
        const QString ampm = match.captured(3).toLower();

        if (ampm == QStringLiteral("pm") && hour < 12) hour += 12;
        else if (ampm == QStringLiteral("am") && hour == 12) hour = 0;

        if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            QDate date = QDate::fromString(result.date, QStringLiteral("yyyy-MM-dd"));
            QTime time(hour, minute);
            QDateTime dt(date, time, QTimeZone::LocalTime);
            result.datetime = formatDateTime(dt);
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
