#include <QtTest>
#include "nlp/dateparser.h"

// Reference date: 2026-05-15 (Friday, dayOfWeek=5)
static const QDate REF(2026, 5, 15);

class TestDateParserEn : public QObject
{
    Q_OBJECT

private slots:
    // ── Simple day references ──────────────────────────────────────────────

    void simple_today()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("today"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
    }

    void simple_tomorrow()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("tomorrow"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void simple_yesterday()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("yesterday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
    }

    void simple_yesterday_humanString()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("yesterday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QStringLiteral("Yesterday"));
        QVERIFY(!r.isRecurring);
    }

    void simple_dayBeforeYesterday()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("day before yesterday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-13"));
        QCOMPARE(r.humanString, QStringLiteral("Day before yesterday"));
        QVERIFY(!r.isRecurring);
    }

    void datetime_yesterday_at_6pm()
    {
        // "yesterday at 6pm" -> 2026-05-14T18:00 (REF - 1 day at 18:00)
        const auto r = DateParser::parseEnglish(QStringLiteral("yesterday at 6pm"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-14T18:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void datetime_dayBeforeYesterday_at_9am()
    {
        // "day before yesterday at 9am" -> 2026-05-13T09:00 (REF - 2 days at 09:00)
        const auto r = DateParser::parseEnglish(
            QStringLiteral("day before yesterday at 9am"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-13"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-13T09:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void invalid_just_yesterday_news()
    {
        // The simple-day regex is anchored ^...$ so any extra leading/trailing
        // text other than the optional "at <time>" tail must NOT match.
        // "just yesterday news" should remain valid=false.
        const auto r = DateParser::parseEnglish(
            QStringLiteral("just yesterday news"), REF);
        QVERIFY(!r.valid);
    }

    void invalid_yesterday_extra_trailing()
    {
        // Trailing junk after "yesterday" without an "at <time>" clause should
        // not match either. The regex requires " at <time>" or nothing.
        const auto r = DateParser::parseEnglish(
            QStringLiteral("yesterday foo"), REF);
        QVERIFY(!r.valid);
    }

    // ── "in N days/weeks" ──────────────────────────────────────────────────

    void in_n_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("in 3 days")
            << QStringLiteral("in 3 days")
            << REF.addDays(3).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("in 1 day")
            << QStringLiteral("in 1 day")
            << REF.addDays(1).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("in 2 weeks")
            << QStringLiteral("in 2 weeks")
            << REF.addDays(14).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("in 1 week")
            << QStringLiteral("in 1 week")
            << REF.addDays(7).toString(QStringLiteral("yyyy-MM-dd"));

        QTest::newRow("in 1 month")
            << QStringLiteral("in 1 month")
            << REF.addMonths(1).toString(QStringLiteral("yyyy-MM-dd"));
    }

    void in_n()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto r = DateParser::parseEnglish(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, expected);
    }

    // ── "next <weekday>" ──────────────────────────────────────────────────
    // REF = 2026-05-15 (Friday, dow=5)

    void next_weekday_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        // Monday: dow=1, daysAhead=1-5=-4 -> +7=3 -> 2026-05-18
        QTest::newRow("next monday") << QStringLiteral("next monday") << QStringLiteral("2026-05-18");
        // Tuesday: dow=2, daysAhead=2-5=-3 -> +7=4 -> 2026-05-19
        QTest::newRow("next tuesday") << QStringLiteral("next tuesday") << QStringLiteral("2026-05-19");
        // Wednesday: dow=3, daysAhead=3-5=-2 -> +7=5 -> 2026-05-20
        QTest::newRow("next wednesday") << QStringLiteral("next wednesday") << QStringLiteral("2026-05-20");
        // Thursday: dow=4, daysAhead=4-5=-1 -> +7=6 -> 2026-05-21
        QTest::newRow("next thursday") << QStringLiteral("next thursday") << QStringLiteral("2026-05-21");
        // Saturday: dow=6, daysAhead=6-5=1 -> 2026-05-16
        QTest::newRow("next saturday") << QStringLiteral("next saturday") << QStringLiteral("2026-05-16");
        // Sunday: dow=7, daysAhead=7-5=2 -> 2026-05-17
        QTest::newRow("next sunday") << QStringLiteral("next sunday") << QStringLiteral("2026-05-17");
    }

    void next_weekday()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto r = DateParser::parseEnglish(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, expected);
    }

    // Friday: same day as REF -> daysAhead = 5-5=0 -> +7 -> 2026-05-22
    void next_friday_same_as_ref()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("next friday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-22"));
    }

    // ── ISO date ──────────────────────────────────────────────────────────

    void iso_date()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("2026-12-31"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-12-31"));
    }

    void iso_date_far_future()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("2027-01-01"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2027-01-01"));
    }

    // ── US-style slash date (M/D/YYYY) ────────────────────────────────────

    void us_date_with_year()
    {
        // 12/31/2026 -> 2026-12-31
        const auto r = DateParser::parseEnglish(QStringLiteral("12/31/2026"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-12-31"));
    }

    void us_date_without_year()
    {
        // 6/20 -> June 20, 2026 (in the future relative to May 15 2026)
        const auto r = DateParser::parseEnglish(QStringLiteral("6/20"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-06-20"));
    }

    // ── Named month dates ──────────────────────────────────────────────────

    void named_month_date_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        // Dec 31 2026 is in the future -> 2026-12-31
        QTest::newRow("dec 31") << QStringLiteral("dec 31") << QStringLiteral("2026-12-31");
        // Jan 1 without year -> Jan 1 2026 is past -> bumped to 2027
        QTest::newRow("jan 1") << QStringLiteral("jan 1") << QStringLiteral("2027-01-01");
        // With explicit year
        QTest::newRow("january 1 2027") << QStringLiteral("january 1, 2027") << QStringLiteral("2027-01-01");
        // June 15 -> future
        QTest::newRow("june 15") << QStringLiteral("june 15") << QStringLiteral("2026-06-15");
    }

    void named_month_date()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto r = DateParser::parseEnglish(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, expected);
    }

    // ── Recurring rules ────────────────────────────────────────────────────

    void recurring_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("isRecurring");
        QTest::addColumn<QString>("recurrenceRule");

        QTest::newRow("every day")
            << QStringLiteral("every day")
            << true
            << QStringLiteral("FREQ=DAILY");

        QTest::newRow("every week")
            << QStringLiteral("every week")
            << true
            << QStringLiteral("FREQ=WEEKLY");

        QTest::newRow("every month")
            << QStringLiteral("every month")
            << true
            << QStringLiteral("FREQ=MONTHLY");

        QTest::newRow("every year")
            << QStringLiteral("every year")
            << true
            << QStringLiteral("FREQ=YEARLY");

        QTest::newRow("every weekday")
            << QStringLiteral("every weekday")
            << true
            // B-4.x: "every weekday" now produces FREQ=WEEKLY not FREQ=DAILY
            // (RFC 5545 canonical form for a weekly weekday schedule uses BYDAY
            //  with FREQ=WEEKLY rather than FREQ=DAILY with BYDAY filter)
            << QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");

        QTest::newRow("every monday")
            << QStringLiteral("every monday")
            << true
            << QStringLiteral("FREQ=WEEKLY;BYDAY=MO");

        QTest::newRow("every 2 weeks")
            << QStringLiteral("every 2 weeks")
            << true
            << QStringLiteral("FREQ=WEEKLY;INTERVAL=2");

        QTest::newRow("every 3 months")
            << QStringLiteral("every 3 months")
            << true
            << QStringLiteral("FREQ=MONTHLY;INTERVAL=3");
    }

    void recurring()
    {
        QFETCH(QString, input);
        QFETCH(bool, isRecurring);
        QFETCH(QString, recurrenceRule);

        const auto r = DateParser::parseEnglish(input, REF);
        QVERIFY(r.valid);
        QCOMPARE(r.isRecurring, isRecurring);
        QCOMPARE(r.recurrenceRule, recurrenceRule);
    }

    // ── Datetime: "today at 5pm" ───────────────────────────────────────────

    void datetime_today_at_5pm()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("today at 5pm"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-15T17:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void datetime_tomorrow_at_9am()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("tomorrow at 9am"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-16T09:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    void datetime_today_at_hhmm()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("today at 14:30"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-15T14:30")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    // ── Invalid input ──────────────────────────────────────────────────────

    void invalid_string()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("abracadabra"), REF);
        QVERIFY(!r.valid);
    }

    void invalid_empty()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral(""), REF);
        QVERIFY(!r.valid);
    }

    // ── humanString sanity checks ──────────────────────────────────────────

    void humanstring_today()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("today"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QStringLiteral("Today"));
    }

    void humanstring_tomorrow()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("tomorrow"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QStringLiteral("Tomorrow"));
    }

    // ── Special: parse("today", lang="ru") should translate humanString ───
    // The DateParser::parse() with lang="ru" first tries parseRussian("today").
    // "today" is not Russian, so it falls back to parseEnglish and then
    // translateHumanStringToRussian("Today") -> "Сегодня".

    void translate_humanstring_today_via_parse_ru()
    {
        // Use DateParser::parse() (the QML-invokable instance method) with lang="ru"
        // We need a DateParser instance for this.
        DateParser dp;
        // parse() uses QDate::currentDate() as reference; we can't inject REF here.
        // But we can verify the translation logic directly by checking parseEnglish
        // and that "Today" -> "Сегодня" translation is working via parse() call.
        // We verify: when called with an English word and lang="ru", humanString is Russian.
        // Note: parse() uses currentDate, so we only check humanString, not the specific date.
        const auto r = dp.parse(QStringLiteral("today"), QStringLiteral("ru"));
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Сегодня"));
    }

    void translate_humanstring_tomorrow_via_parse_ru()
    {
        DateParser dp;
        const auto r = dp.parse(QStringLiteral("tomorrow"), QStringLiteral("ru"));
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Завтра"));
    }

    void translate_humanstring_every_day_via_parse_ru()
    {
        DateParser dp;
        const auto r = dp.parse(QStringLiteral("every day"), QStringLiteral("ru"));
        QVERIFY(r.valid);
        QCOMPARE(r.humanString, QString::fromUtf8("Каждый день"));
    }

    void translate_humanstring_next_monday_via_parse_ru()
    {
        DateParser dp;
        const auto r = dp.parse(QStringLiteral("next monday"), QStringLiteral("ru"));
        QVERIFY(r.valid);
        // "Next Monday" -> "След. понедельник"
        QCOMPARE(r.humanString, QString::fromUtf8("След. понедельник"));
    }

    void translate_humanstring_in_3_days_via_parse_ru()
    {
        DateParser dp;
        const auto r = dp.parse(QStringLiteral("in 3 days"), QStringLiteral("ru"));
        QVERIFY(r.valid);
        // "In 3 days" -> "Через 3 дн"
        QCOMPARE(r.humanString, QString::fromUtf8("Через 3 дн"));
    }
};

QTEST_GUILESS_MAIN(TestDateParserEn)
#include "test_dateparser_en.moc"
