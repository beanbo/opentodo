#include <QtTest>
#include "nlp/dateparser.h"

// Reference date: 2026-05-15 (Friday, dayOfWeek=5)
// Used in all tests for determinism.
static const QDate REF(2026, 5, 15);

class TestDateParserRu : public QObject
{
    Q_OBJECT

private slots:
    // ── Simple day references ──────────────────────────────────────────────

    void simple_today()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("сегодня"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
    }

    void simple_tomorrow()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("завтра"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void simple_yesterday()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("вчера"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
    }

    void simple_dayBeforeYesterday()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("позавчера"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-13"));
        QCOMPARE(r.humanString, QString::fromUtf8("Позавчера"));
        QVERIFY(!r.isRecurring);
    }

    void simple_yesterday_humanString()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("вчера"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Вчера"));
        QVERIFY(!r.isRecurring);
    }

    void datetime_yesterday_at_18()
    {
        // "вчера в 18:00" -> datetime is 2026-05-14T18:00 (REF - 1 day at 18:00)
        const auto r = DateParser::parseRussian(QString::fromUtf8("вчера в 18:00"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-14T18:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void datetime_dayBeforeYesterday_at_9()
    {
        // "позавчера в 9:00" -> datetime is 2026-05-13T09:00 (REF - 2 days at 09:00)
        const auto r = DateParser::parseRussian(QString::fromUtf8("позавчера в 9:00"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-13"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-13T09:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void invalid_just_yesterday_substring()
    {
        // "просто день" must NOT accidentally parse as a "вчера" form. It contains
        // neither "вчера" nor "позавчера" and should remain invalid.
        const auto r = DateParser::parseRussian(QString::fromUtf8("просто день"), REF);
        QVERIFY(!r.valid);
    }

    void invalid_yesterday_inside_word()
    {
        // "невчерашний" contains "вчера" as a substring but the matchDay rule
        // requires either an exact match or "вчера " followed by a space, so a
        // word starting with "не" must not trigger the вчера branch.
        const auto r = DateParser::parseRussian(QString::fromUtf8("невчерашний"), REF);
        QVERIFY(!r.valid);
    }

    void simple_dayAfterTomorrow()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("послезавтра"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-17"));
    }

    // ── "через N ..." ──────────────────────────────────────────────────────

    void cherez_3_dnya()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("через 3 дня"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, REF.addDays(3).toString(QStringLiteral("yyyy-MM-dd")));
    }

    void cherez_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("cherez 1 nedelu")
            << QString::fromUtf8("через 1 неделю")
            << REF.addDays(7).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("cherez 2 nedeli")
            << QString::fromUtf8("через 2 недели")
            << REF.addDays(14).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("cherez 3 nedeli")
            << QString::fromUtf8("через 3 недели")
            << REF.addDays(21).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("cherez mesyac")
            << QString::fromUtf8("через 1 месяц")
            << REF.addMonths(1).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("cherez 5 dney")
            << QString::fromUtf8("через 5 дней")
            << REF.addDays(5).toString(QStringLiteral("yyyy-MM-dd"));
    }

    void cherez()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto r = DateParser::parseRussian(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, expected);
    }

    // ── Weekday navigation ─────────────────────────────────────────────────
    // REF = 2026-05-15 (Friday, dow=5)
    // "в понедельник" -> next Monday = 2026-05-18 (dow=1, daysAhead = 1-5 = -4 -> +7-4=3 days? )
    // daysAhead = targetDow - currentDow; if <=0 add 7
    // targetDow=1, currentDow=5 -> daysAhead = -4 -> -4+7=3 -> 2026-05-18

    void weekday_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        // Monday: dow=1, ref=Friday(5), daysAhead = 1-5=-4 -> +7=3 -> 2026-05-18
        QTest::newRow("ponedelnik")
            << QString::fromUtf8("в понедельник")
            << QStringLiteral("2026-05-18");

        // Tuesday: dow=2, daysAhead=2-5=-3 -> +7=4 -> 2026-05-19
        QTest::newRow("vtornik")
            << QString::fromUtf8("во вторник")
            << QStringLiteral("2026-05-19");

        // Wednesday: dow=3, daysAhead=3-5=-2 -> +7=5 -> 2026-05-20
        QTest::newRow("sreda")
            << QString::fromUtf8("в среду")
            << QStringLiteral("2026-05-20");

        // Thursday: dow=4, daysAhead=4-5=-1 -> +7=6 -> 2026-05-21
        QTest::newRow("chetverg")
            << QString::fromUtf8("в четверг")
            << QStringLiteral("2026-05-21");

        // Saturday: dow=6, daysAhead=6-5=1 -> 2026-05-16
        QTest::newRow("subbota")
            << QString::fromUtf8("в субботу")
            << QStringLiteral("2026-05-16");

        // Sunday: dow=7, daysAhead=7-5=2 -> 2026-05-17
        QTest::newRow("voskresenye")
            << QString::fromUtf8("в воскресенье")
            << QStringLiteral("2026-05-17");
    }

    void weekday()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto r = DateParser::parseRussian(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, expected);
    }

    // Friday itself: daysAhead = 5-5=0 -> 0 <= 0 -> +7 = 7 -> 2026-05-22
    void weekday_same_day_friday()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("в пятницу"), REF);
        QVERIFY(r.valid);
        // Same weekday -> jump to next week's Friday
        QCOMPARE(r.date, QStringLiteral("2026-05-22"));
    }

    // ── Specific date patterns ─────────────────────────────────────────────

    void date_1_yanvarya()
    {
        // "1 января" — reference is May 15, 2026, so Jan 1 is in the past
        // -> date = QDate(2026,1,1) < REF -> bumped to 2027-01-01
        const auto r = DateParser::parseRussian(QString::fromUtf8("1 января"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2027-01-01"));
    }

    void date_31_dekabrya()
    {
        // "31 декабря" — Dec 31 2026 is in the future relative to May 15 2026
        const auto r = DateParser::parseRussian(QString::fromUtf8("31 декабря"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-12-31"));
    }

    void date_15_marta()
    {
        // "15 марта" — March 15, 2026 is before May 15, 2026 -> bumped to 2027
        const auto r = DateParser::parseRussian(QString::fromUtf8("15 марта"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2027-03-15"));
    }

    void date_16_maya()
    {
        // "16 мая" — May 16, 2026 is one day ahead of ref -> stays in 2026
        const auto r = DateParser::parseRussian(QString::fromUtf8("16 мая"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void date_with_explicit_year()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("1 января 2027"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2027-01-01"));
    }

    // ── Recurring rules ────────────────────────────────────────────────────

    void recurring_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("isRecurring");
        QTest::addColumn<QString>("recurrenceRule");

        QTest::newRow("kazhdyy den")
            << QString::fromUtf8("каждый день")
            << true
            << QStringLiteral("FREQ=DAILY");

        QTest::newRow("kazhduyu nedelu")
            << QString::fromUtf8("каждую неделю")
            << true
            << QStringLiteral("FREQ=WEEKLY");

        QTest::newRow("kazhdyy mesyac")
            << QString::fromUtf8("каждый месяц")
            << true
            << QStringLiteral("FREQ=MONTHLY");

        QTest::newRow("kazhdyy god")
            << QString::fromUtf8("каждый год")
            << true
            << QStringLiteral("FREQ=YEARLY");

        QTest::newRow("kazhdyy ponedelnik")
            << QString::fromUtf8("каждый понедельник")
            << true
            << QStringLiteral("FREQ=WEEKLY;BYDAY=MO");

        QTest::newRow("kazhdye 2 nedeli")
            << QString::fromUtf8("каждые 2 недели")
            << true
            << QStringLiteral("FREQ=WEEKLY;INTERVAL=2");

        QTest::newRow("kazhdye 3 mesyaca")
            << QString::fromUtf8("каждые 3 месяца")
            << true
            << QStringLiteral("FREQ=MONTHLY;INTERVAL=3");
    }

    void recurring()
    {
        QFETCH(QString, input);
        QFETCH(bool, isRecurring);
        QFETCH(QString, recurrenceRule);

        const auto r = DateParser::parseRussian(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.isRecurring, isRecurring);
        QCOMPARE(r.recurrenceRule, recurrenceRule);
    }

    // ── Annual recurrence with explicit date ──────────────────────────────
    // REF = 2026-05-15. 15.03 has already passed this year -> next = 2027-03-15.
    // 9 мая has already passed this year -> next = 2027-05-09.
    // 31.12 is later this year -> next = 2026-12-31.

    void annual_kazhdyy_god_numeric()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("каждый год 15.03"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTH=3")),
                 qPrintable(r.recurrenceRule));
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTHDAY=15")),
                 qPrintable(r.recurrenceRule));
        QVERIFY(r.recurrenceRule.startsWith(QStringLiteral("FREQ=YEARLY")));
        // 15.03.2026 is before REF (2026-05-15) -> bumped to 2027.
        QCOMPARE(r.date, QStringLiteral("2027-03-15"));
    }

    void annual_kazhdyy_god_with_year()
    {
        // Year suffix is ignored — recurrence is annual regardless.
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("каждый год 15.03.2026"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTH=3")),
                 qPrintable(r.recurrenceRule));
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTHDAY=15")),
                 qPrintable(r.recurrenceRule));
    }

    void annual_ezhegodno_word_month()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("ежегодно 9 мая"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTH=5")),
                 qPrintable(r.recurrenceRule));
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTHDAY=9")),
                 qPrintable(r.recurrenceRule));
        // 09.05.2026 is before REF (2026-05-15) -> bumped to 2027.
        QCOMPARE(r.date, QStringLiteral("2027-05-09"));
    }

    void annual_kazhdoe_numeric()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("каждое 31.12"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTH=12")),
                 qPrintable(r.recurrenceRule));
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTHDAY=31")),
                 qPrintable(r.recurrenceRule));
        // 31.12.2026 is after REF -> stays in 2026.
        QCOMPARE(r.date, QStringLiteral("2026-12-31"));
    }

    void annual_kazhdyy_god_word_month()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("каждый год 9 мая"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTH=5")),
                 qPrintable(r.recurrenceRule));
        QVERIFY2(r.recurrenceRule.contains(QStringLiteral("BYMONTHDAY=9")),
                 qPrintable(r.recurrenceRule));
    }

    void annual_negative_plain_date()
    {
        // "15.03" without "каждый год" / "ежегодно" / "каждое" prefix must NOT
        // be parsed as recurring. Plain numeric date pattern isn't supported by
        // the Russian parser at all, so we just assert isRecurring is false.
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("просто 15.03"), REF);
        QVERIFY(!r.isRecurring);
    }

    // ── Datetime: "завтра в 18:00" ────────────────────────────────────────

    void datetime_tomorrow_18()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("завтра в 18:00"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
        QVERIFY(!r.datetime.isEmpty());
        // datetime should start with 2026-05-16T18:00
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-16T18:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void datetime_today_time()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("сегодня в 9:30"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-15T09:30")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    // ── Invalid input ──────────────────────────────────────────────────────

    void invalid_string()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("абракадабра"), REF);
        QVERIFY(!r.valid);
    }

    void invalid_empty()
    {
        // empty string — parse() returns default-constructed result
        const auto r = DateParser::parseRussian(QStringLiteral(""), REF);
        QVERIFY(!r.valid);
    }

    // ── humanString sanity checks ──────────────────────────────────────────

    void humanstring_today()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("сегодня"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Сегодня"));
    }

    void humanstring_tomorrow()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("завтра"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Завтра"));
    }

    void humanstring_recurring_daily()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("каждый день"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Каждый день"));
    }

    // ── "через неделю" (no number — not supported by regex, skip) ─────────

    void cherez_nedelu_no_number()
    {
        // The cherez regex requires explicit \d+; "через неделю" (without "1")
        // does not match and returns valid=false. This is expected parser behaviour.
        const auto r = DateParser::parseRussian(QString::fromUtf8("через неделю"), REF);
        QVERIFY(!r.valid);
    }

    // ── night iter-147: "следующая неделя" honours the nextWeekDay setting ──
    // Mirrors the English path (same formula). REF=2026-05-15 (Fri, dow=5);
    // next Monday = REF+3 = 2026-05-18.
    void sleduyushaya_nedelya_honours_nextWeekDay()
    {
        // Default (no arg) → next Monday, preserving the historic behaviour.
        const auto def = DateParser::parseRussian(QString::fromUtf8("следующая неделя"), REF);
        QVERIFY(def.valid);
        QCOMPARE(def.date, QStringLiteral("2026-05-18"));
        QCOMPARE(def.humanString, QString::fromUtf8("Следующая неделя"));

        // nextWeekDay=3 (Wednesday) → next week's Wednesday = 2026-05-20.
        const auto wed = DateParser::parseRussian(QString::fromUtf8("на следующей неделе"), REF, 3);
        QVERIFY(wed.valid);
        QCOMPARE(wed.date, QStringLiteral("2026-05-20"));

        // nextWeekDay=7 (Sunday) → next week's Sunday = 2026-05-24.
        const auto sun = DateParser::parseRussian(QString::fromUtf8("следующая неделя"), REF, 7);
        QVERIFY(sun.valid);
        QCOMPARE(sun.date, QStringLiteral("2026-05-24"));
    }
};

QTEST_GUILESS_MAIN(TestDateParserRu)
#include "test_dateparser_ru.moc"
