// Date-phrase boundaries in QuickAdd / DateParser (global bug-fix pass, F4).
//
// Pins:
//   P1  — a date keyword followed by arbitrary words is NOT a date phrase, so
//         QuickAdd keeps those words as the task title ("Завтра позвонить
//         маме" → content "позвонить маме", not "").
//   P6  — a trailing time clause no longer breaks the anchored phrases
//         ("every day at 9am", "next monday at 3pm", "каждый день в 9:00").
//   P7  — weekly BYDAY rules start on the first matching weekday, not today.
//   P8  — a bare 1–2 digit number is not a time ("Read chapter 5").
//   P9  — "last day of every month" → BYMONTHDAY=-1, honoured by the engine.
//   P12 — DateParser::parse(…, "en") falls back to the Russian rules.
//
// Reference date: Wednesday 2026-05-13.

#include <QtTest>

#include "nlp/dateparser.h"
#include "nlp/quickaddparser.h"
#include "nlp/recurrenceengine.h"

static const QDate REF(2026, 5, 13); // Wednesday

static QTime timeOf(const DateParseResult &r)
{
    return QDateTime::fromString(r.datetime, Qt::ISODate).time();
}

class TestNlpTitlePreservation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCOMPARE(REF.dayOfWeek(), 3);
    }

    // ── P1: trailing words are title text ─────────────────────────────────
    void p1_rules_rejectTrailingWords()
    {
        QVERIFY(!DateParser::parseRussian(QString::fromUtf8("завтра позвонить маме"), REF).valid);
        QVERIFY(!DateParser::parseRussian(QString::fromUtf8("в понедельник позвонить врачу"), REF).valid);
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("tomorrow at the office"), REF).valid);
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("tomorrow at 5pm call mom"), REF).valid);
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("every monday buy milk"), REF).valid);
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("every! nonsense"), REF).valid);

        // Still-valid tails.
        const auto evening = DateParser::parseRussian(QString::fromUtf8("завтра вечером"), REF);
        QVERIFY(evening.valid);
        QCOMPARE(evening.date, QStringLiteral("2026-05-14"));
        QVERIFY(DateParser::parseRussian(QString::fromUtf8("в пятницу утром"), REF).valid);
        const auto monWed = DateParser::parseEnglish(QStringLiteral("every mon and wed"), REF);
        QVERIFY(monWed.valid);
        QCOMPARE(monWed.recurrenceRule, QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE"));
    }

    void p1_quickAdd_keepsTitle_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("due");
        QTest::newRow("ru prefix day")
            << QString::fromUtf8("Завтра позвонить маме")
            << QString::fromUtf8("позвонить маме") << QString::fromUtf8("Завтра");
        QTest::newRow("en prefix day + time")
            << QStringLiteral("Tomorrow at 5pm call mom")
            << QStringLiteral("call mom") << QStringLiteral("Tomorrow at 5pm");
        QTest::newRow("en every weekday prefix")
            << QStringLiteral("every monday buy milk")
            << QStringLiteral("buy milk") << QStringLiteral("every monday");
        QTest::newRow("ru date in the middle stays in title")
            << QString::fromUtf8("Встреча завтра обсудить бюджет")
            << QString::fromUtf8("Встреча завтра обсудить бюджет") << QString();
        QTest::newRow("en suffix still works")
            << QStringLiteral("Buy milk tomorrow")
            << QStringLiteral("Buy milk") << QStringLiteral("tomorrow");
    }
    void p1_quickAdd_keepsTitle()
    {
        QFETCH(QString, input);
        QFETCH(QString, content);
        QFETCH(QString, due);
        const QuickAddResult r = QuickAddParser::parse(input);
        QCOMPARE(r.content, content);
        QCOMPARE(r.dueString, due);
    }

    // ── P6: trailing time clause ─────────────────────────────────────────
    void p6_timeTail_anchoredPhrases()
    {
        auto daily = DateParser::parseEnglish(QStringLiteral("every day at 9am"), REF);
        QVERIFY(daily.valid);
        QVERIFY(daily.isRecurring);
        QCOMPARE(daily.recurrenceRule, QStringLiteral("FREQ=DAILY"));
        QCOMPARE(timeOf(daily), QTime(9, 0));

        auto wk = DateParser::parseEnglish(QStringLiteral("every weekday at 9:30am"), REF);
        QVERIFY(wk.valid);
        QCOMPARE(wk.recurrenceRule, QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"));
        QCOMPARE(timeOf(wk), QTime(9, 30));

        auto nextMon = DateParser::parseEnglish(QStringLiteral("next monday at 3pm"), REF);
        QVERIFY(nextMon.valid);
        QCOMPARE(nextMon.date, QStringLiteral("2026-05-18"));
        QCOMPARE(timeOf(nextMon), QTime(15, 0));

        auto in3 = DateParser::parseEnglish(QStringLiteral("in 3 days at 5pm"), REF);
        QVERIFY(in3.valid);
        QCOMPARE(in3.date, QStringLiteral("2026-05-16"));
        QCOMPARE(timeOf(in3), QTime(17, 0));

        auto nextWeek = DateParser::parseEnglish(QStringLiteral("next week at 9am"), REF);
        QVERIFY(nextWeek.valid);
        QCOMPARE(timeOf(nextWeek), QTime(9, 0));

        auto ruDaily = DateParser::parseRussian(QString::fromUtf8("каждый день в 9:00"), REF);
        QVERIFY(ruDaily.valid);
        QCOMPARE(ruDaily.recurrenceRule, QStringLiteral("FREQ=DAILY"));
        QCOMPARE(timeOf(ruDaily), QTime(9, 0));

        auto ruIn3 = DateParser::parseRussian(QString::fromUtf8("через 3 дня в 10:00"), REF);
        QVERIFY(ruIn3.valid);
        QCOMPARE(ruIn3.date, QStringLiteral("2026-05-16"));
        QCOMPARE(timeOf(ruIn3), QTime(10, 0));

        auto ruDot = DateParser::parseRussian(QString::fromUtf8("завтра в 18.30"), REF);
        QVERIFY(ruDot.valid);
        QCOMPARE(timeOf(ruDot), QTime(18, 30));

        auto ruNoon = DateParser::parseRussian(QString::fromUtf8("завтра в полдень"), REF);
        QVERIFY(ruNoon.valid);
        QCOMPARE(timeOf(ruNoon), QTime(12, 0));

        // Russian day-part markers act like am/pm.
        QCOMPARE(timeOf(DateParser::parseRussian(QString::fromUtf8("завтра в 9 вечера"), REF)),
                 QTime(21, 0));
        QCOMPARE(timeOf(DateParser::parseRussian(QString::fromUtf8("каждый день в 3 дня"), REF)),
                 QTime(15, 0));
        QCOMPARE(timeOf(DateParser::parseRussian(QString::fromUtf8("в 7 утра"), REF)),
                 QTime(7, 0));
        // "через 3 дня" stays "in 3 days", not "at 3 pm".
        const auto in3days = DateParser::parseRussian(QString::fromUtf8("через 3 дня"), REF);
        QCOMPARE(in3days.date, QStringLiteral("2026-05-16"));
        QVERIFY(in3days.datetime.isEmpty());

        // Out-of-range explicit time still rejects the whole phrase.
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("every day at 25:00"), REF).valid);
    }

    void p6_quickAdd_recurringWithTime()
    {
        const QuickAddResult r = QuickAddParser::parse(QStringLiteral("Standup every day at 9am"));
        QCOMPARE(r.content, QStringLiteral("Standup"));
        QCOMPARE(r.dueString, QStringLiteral("every day at 9am"));
        QVERIFY(DateParser::parseEnglish(r.dueString, REF).isRecurring);
    }

    // ── P7: weekly BYDAY first occurrence ────────────────────────────────
    void p7_bydayStartsOnMatchingWeekday()
    {
        QCOMPARE(DateParser::parseEnglish(QStringLiteral("every monday"), REF).date,
                 QStringLiteral("2026-05-18"));
        QCOMPARE(DateParser::parseEnglish(QStringLiteral("every wednesday"), REF).date,
                 QStringLiteral("2026-05-13")); // today matches → today
        QCOMPARE(DateParser::parseEnglish(QStringLiteral("every other tuesday"), REF).date,
                 QStringLiteral("2026-05-19"));
        const QDate saturday(2026, 5, 16);
        QCOMPARE(DateParser::parseEnglish(QStringLiteral("every weekday"), saturday).date,
                 QStringLiteral("2026-05-18"));
        QCOMPARE(DateParser::parseRussian(QString::fromUtf8("каждый понедельник"), REF).date,
                 QStringLiteral("2026-05-18"));
        QCOMPARE(DateParser::parseRussian(QString::fromUtf8("по пятницам"), REF).date,
                 QStringLiteral("2026-05-15"));
        QCOMPARE(DateParser::parseRussian(QString::fromUtf8("каждый будний день"), saturday).date,
                 QStringLiteral("2026-05-18"));
        // Non-BYDAY rules still start today.
        QCOMPARE(DateParser::parseEnglish(QStringLiteral("every day"), REF).date,
                 QStringLiteral("2026-05-13"));
    }

    // ── P8: bare numbers are not times ───────────────────────────────────
    void p8_bareNumberIsNotTime()
    {
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("5"), REF).valid);
        QVERIFY(!DateParser::parseEnglish(QStringLiteral("2.10"), REF).valid);
        QVERIFY(DateParser::parseEnglish(QStringLiteral("at 5"), REF).valid);
        QVERIFY(DateParser::parseEnglish(QStringLiteral("5pm"), REF).valid);
        QVERIFY(DateParser::parseEnglish(QStringLiteral("5:30"), REF).valid);
        QVERIFY(!DateParser::parseRussian(QStringLiteral("5"), REF).valid);
        QVERIFY(!DateParser::parseRussian(QStringLiteral("15.03"), REF).valid);
        QVERIFY(DateParser::parseRussian(QString::fromUtf8("в 5"), REF).valid);
        QVERIFY(DateParser::parseRussian(QStringLiteral("15:30"), REF).valid);

        const QuickAddResult en = QuickAddParser::parse(QStringLiteral("Read chapter 5"));
        QCOMPARE(en.content, QStringLiteral("Read chapter 5"));
        QVERIFY(en.dueString.isEmpty());
        const QuickAddResult ru = QuickAddParser::parse(QString::fromUtf8("Прочитать главу 5"));
        QCOMPARE(ru.content, QString::fromUtf8("Прочитать главу 5"));
        QVERIFY(ru.dueString.isEmpty());
    }

    // ── P9: last day of month ────────────────────────────────────────────
    void p9_lastDayOfMonth_parser()
    {
        const auto en = DateParser::parseEnglish(QStringLiteral("last day of every month"), REF);
        QVERIFY(en.valid);
        QCOMPARE(en.recurrenceRule, QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-1"));
        QCOMPARE(en.date, QStringLiteral("2026-05-31"));
        // humanString round-trips.
        const auto again = DateParser::parseEnglish(en.humanString, REF);
        QCOMPARE(again.recurrenceRule, en.recurrenceRule);
        QCOMPARE(again.date, en.date);

        const auto ru = DateParser::parseRussian(
            QString::fromUtf8("последнего числа каждого месяца"), REF);
        QVERIFY(ru.valid);
        QCOMPARE(ru.recurrenceRule, QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-1"));
        QCOMPARE(ru.date, QStringLiteral("2026-05-31"));
        QVERIFY(DateParser::parseRussian(ru.humanString, REF).valid);
    }

    void p9_lastDayOfMonth_engine()
    {
        const QString rule = QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-1");
        QCOMPARE(RecurrenceEngine::advance(rule, QDate(2026, 1, 31)), QDate(2026, 2, 28));
        QCOMPARE(RecurrenceEngine::advance(rule, QDate(2026, 2, 28)), QDate(2026, 3, 31));
        QCOMPARE(RecurrenceEngine::advance(rule, QDate(2026, 3, 31)), QDate(2026, 4, 30));
        QCOMPARE(RecurrenceEngine::advance(rule, QDate(2028, 1, 31)), QDate(2028, 2, 29));
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY;INTERVAL=2;BYMONTHDAY=-1"),
                                           QDate(2026, 4, 30)), QDate(2026, 6, 30));
        // -2 = second-to-last day.
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-2"),
                                           QDate(2026, 1, 30)), QDate(2026, 2, 27));
        // -31 does not exist in February → skipped to March 1st.
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=-31"),
                                           QDate(2026, 1, 1)), QDate(2026, 3, 1));
    }

    // ── P12: en → ru fallback ────────────────────────────────────────────
    void p12_englishFallsBackToRussian()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("завтра"), QStringLiteral("en"));
        QVERIFY(r.valid);
        QCOMPARE(r.date, QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd")));
        QVERIFY(!parser.parse(QStringLiteral("gibberish"), QStringLiteral("en")).valid);
    }
};

QTEST_GUILESS_MAIN(TestNlpTitlePreservation)
#include "test_nlp_title_preservation.moc"
