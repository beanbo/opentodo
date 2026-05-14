// Round-trip invariants for DateParser.
//
// Property under test: parse(humanString) yields the SAME date / datetime /
// recurrence as the original parse. The canonical humanString is meant to be
// re-parseable.
//
// Notes on capitalization:
//   Both parseRussian() and parseEnglish() lowercase the input before
//   matching, but their humanString outputs are typically Title-Cased
//   ("Today", "Завтра", "Каждый день"). When we feed the humanString back
//   through the parser, lower-casing happens internally, so re-parse works
//   without explicit normalization on our side. We therefore feed the raw
//   humanString back in.
//
// Reference date: 2026-05-15 (Friday, dayOfWeek=5) for determinism.

#include <QtTest>
#include "nlp/dateparser.h"

static const QDate REF(2026, 5, 15);

class TestDateParserInvariants : public QObject
{
    Q_OBJECT

private:
    // Helper: parse(input), assert valid + non-empty humanString, then re-parse
    // the humanString and assert the second pass equals the first on the key
    // observable fields. `lang` is "ru" or "en" — chooses which static parser
    // to call.
    void checkRoundTrip(const QString &input, const QString &lang) const
    {
        const auto first = (lang == QStringLiteral("ru"))
                               ? DateParser::parseRussian(input, REF)
                               : DateParser::parseEnglish(input, REF);

        QVERIFY2(first.valid,
                 qPrintable(QStringLiteral("first parse failed for: %1").arg(input)));
        QVERIFY2(!first.humanString.isEmpty(),
                 qPrintable(QStringLiteral("humanString empty for: %1").arg(input)));

        const auto second = (lang == QStringLiteral("ru"))
                                ? DateParser::parseRussian(first.humanString, REF)
                                : DateParser::parseEnglish(first.humanString, REF);

        QVERIFY2(second.valid,
                 qPrintable(QStringLiteral("re-parse of humanString '%1' (from input '%2') failed")
                                .arg(first.humanString, input)));

        // Date must be preserved.
        QVERIFY2(second.date == first.date,
                 qPrintable(QStringLiteral("date mismatch for '%1' -> '%2': first=%3 second=%4")
                                .arg(input, first.humanString, first.date, second.date)));

        // Recurrence must be preserved (both flag and rule).
        QVERIFY2(second.isRecurring == first.isRecurring,
                 qPrintable(QStringLiteral("isRecurring mismatch for '%1' -> '%2'")
                                .arg(input, first.humanString)));
        QVERIFY2(second.recurrenceRule == first.recurrenceRule,
                 qPrintable(QStringLiteral("recurrenceRule mismatch for '%1' -> '%2': first='%3' second='%4'")
                                .arg(input, first.humanString, first.recurrenceRule, second.recurrenceRule)));
    }

private slots:
    // ── Russian: simple day references ────────────────────────────────────

    void rt_ru_segodnya()        { checkRoundTrip(QString::fromUtf8("сегодня"),     QStringLiteral("ru")); }
    void rt_ru_zavtra()          { checkRoundTrip(QString::fromUtf8("завтра"),      QStringLiteral("ru")); }
    void rt_ru_vchera()          { checkRoundTrip(QString::fromUtf8("вчера"),       QStringLiteral("ru")); }
    void rt_ru_pozavchera()      { checkRoundTrip(QString::fromUtf8("позавчера"),   QStringLiteral("ru")); }
    void rt_ru_poslezavtra()     { checkRoundTrip(QString::fromUtf8("послезавтра"), QStringLiteral("ru")); }

    // ── Russian: relative ("через N ...") ────────────────────────────────

    void rt_ru_cherez_3_dnya()   { checkRoundTrip(QString::fromUtf8("через 3 дня"), QStringLiteral("ru")); }

    // ── Russian: explicit dates ──────────────────────────────────────────
    // Note: the Russian parser does NOT accept bare numeric dates like
    // "15.03" — only the named-month form. The closest analogue to the
    // task's "15.03" / "15.03.YYYY" slots is "15 марта" / "15 марта 2027".
    // The plain "DD.MM" form only round-trips when wrapped in
    // "каждый год DD.MM" (yearly recurrence), which is covered below.

    void rt_ru_15_marta()        { checkRoundTrip(QString::fromUtf8("15 марта"),       QStringLiteral("ru")); }
    void rt_ru_15_marta_2027()   { checkRoundTrip(QString::fromUtf8("15 марта 2027"),  QStringLiteral("ru")); }

    // ── Russian: recurrence rules ────────────────────────────────────────

    void rt_ru_kazhdyy_den()     { checkRoundTrip(QString::fromUtf8("каждый день"),       QStringLiteral("ru")); }
    void rt_ru_kazhdyy_pn()      { checkRoundTrip(QString::fromUtf8("каждый понедельник"), QStringLiteral("ru")); }
    void rt_ru_kazhdyy_god_9may(){ checkRoundTrip(QString::fromUtf8("каждый год 9 мая"),  QStringLiteral("ru")); }

    // Also: the numeric annual form ("каждый год 15.03") — humanString here
    // is "каждый год 15.03" which round-trips through the same handler.
    void rt_ru_kazhdyy_god_1503(){ checkRoundTrip(QString::fromUtf8("каждый год 15.03"),  QStringLiteral("ru")); }

    // ── English: simple day references ───────────────────────────────────

    void rt_en_today()           { checkRoundTrip(QStringLiteral("today"),     QStringLiteral("en")); }
    void rt_en_tomorrow()        { checkRoundTrip(QStringLiteral("tomorrow"),  QStringLiteral("en")); }
    void rt_en_yesterday()       { checkRoundTrip(QStringLiteral("yesterday"), QStringLiteral("en")); }

    // ── English: relative ("in N ...") ───────────────────────────────────

    void rt_en_in_3_days()       { checkRoundTrip(QStringLiteral("in 3 days"), QStringLiteral("en")); }

    // ── English: month/day ───────────────────────────────────────────────
    // humanString format for month/day is "MMM d" (e.g. "Mar 15"). After
    // lowercase the parser still accepts "mar 15" via the abbreviated form.
    void rt_en_march_15()        { checkRoundTrip(QStringLiteral("march 15"), QStringLiteral("en")); }

    // ── English: recurrence rules ────────────────────────────────────────

    void rt_en_every_monday()    { checkRoundTrip(QStringLiteral("every monday"), QStringLiteral("en")); }

    // ── Cross-language properties ────────────────────────────────────────
    //
    // These run a property check across all the test phrases at once:
    //   - if valid==true, humanString is non-empty
    //   - re-parse of humanString -> valid==true
    //   - if isRecurring==true, recurrenceRule is non-empty in BOTH passes
    //   - if isRecurring==false, recurrenceRule is empty in BOTH passes

    void prop_humanString_invariants_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("lang");

        // Russian
        QTest::newRow("ru:сегодня")            << QString::fromUtf8("сегодня")             << QStringLiteral("ru");
        QTest::newRow("ru:завтра")             << QString::fromUtf8("завтра")              << QStringLiteral("ru");
        QTest::newRow("ru:вчера")              << QString::fromUtf8("вчера")               << QStringLiteral("ru");
        QTest::newRow("ru:позавчера")          << QString::fromUtf8("позавчера")           << QStringLiteral("ru");
        QTest::newRow("ru:послезавтра")        << QString::fromUtf8("послезавтра")         << QStringLiteral("ru");
        QTest::newRow("ru:через 3 дня")        << QString::fromUtf8("через 3 дня")         << QStringLiteral("ru");
        QTest::newRow("ru:15 марта")           << QString::fromUtf8("15 марта")            << QStringLiteral("ru");
        QTest::newRow("ru:15 марта 2027")      << QString::fromUtf8("15 марта 2027")       << QStringLiteral("ru");
        QTest::newRow("ru:каждый день")        << QString::fromUtf8("каждый день")         << QStringLiteral("ru");
        QTest::newRow("ru:каждый понедельник") << QString::fromUtf8("каждый понедельник")  << QStringLiteral("ru");
        QTest::newRow("ru:каждый год 9 мая")   << QString::fromUtf8("каждый год 9 мая")    << QStringLiteral("ru");
        QTest::newRow("ru:каждый год 15.03")   << QString::fromUtf8("каждый год 15.03")    << QStringLiteral("ru");

        // English
        QTest::newRow("en:today")              << QStringLiteral("today")        << QStringLiteral("en");
        QTest::newRow("en:tomorrow")           << QStringLiteral("tomorrow")     << QStringLiteral("en");
        QTest::newRow("en:yesterday")          << QStringLiteral("yesterday")    << QStringLiteral("en");
        QTest::newRow("en:in 3 days")          << QStringLiteral("in 3 days")    << QStringLiteral("en");
        QTest::newRow("en:march 15")           << QStringLiteral("march 15")     << QStringLiteral("en");
        QTest::newRow("en:every monday")       << QStringLiteral("every monday") << QStringLiteral("en");
    }

    void prop_humanString_invariants()
    {
        QFETCH(QString, input);
        QFETCH(QString, lang);

        const auto first = (lang == QStringLiteral("ru"))
                               ? DateParser::parseRussian(input, REF)
                               : DateParser::parseEnglish(input, REF);

        QVERIFY(first.valid);
        // Property 1: valid implies non-empty humanString.
        QVERIFY2(!first.humanString.isEmpty(),
                 qPrintable(QStringLiteral("humanString empty for valid input '%1'").arg(input)));

        const auto second = (lang == QStringLiteral("ru"))
                                ? DateParser::parseRussian(first.humanString, REF)
                                : DateParser::parseEnglish(first.humanString, REF);

        // Property 2: humanString re-parses successfully.
        QVERIFY2(second.valid,
                 qPrintable(QStringLiteral("humanString '%1' (from '%2') failed to re-parse")
                                .arg(first.humanString, input)));

        // Property 3 / 4: recurrenceRule presence agrees with isRecurring,
        // in both passes, and the rules themselves are equal.
        if (first.isRecurring) {
            QVERIFY2(!first.recurrenceRule.isEmpty(),
                     qPrintable(QStringLiteral("isRecurring=true but recurrenceRule empty for '%1'").arg(input)));
            QVERIFY2(!second.recurrenceRule.isEmpty(),
                     qPrintable(QStringLiteral("re-parse: isRecurring=true but recurrenceRule empty for humanString '%1'")
                                    .arg(first.humanString)));
            QVERIFY2(second.isRecurring,
                     qPrintable(QStringLiteral("re-parse lost isRecurring flag for '%1' -> '%2'")
                                    .arg(input, first.humanString)));
            QVERIFY2(second.recurrenceRule == first.recurrenceRule,
                     qPrintable(QStringLiteral("recurrenceRule changed: '%1' -> '%2'")
                                    .arg(first.recurrenceRule, second.recurrenceRule)));
        } else {
            QVERIFY2(first.recurrenceRule.isEmpty(),
                     qPrintable(QStringLiteral("isRecurring=false but recurrenceRule non-empty for '%1': '%2'")
                                    .arg(input, first.recurrenceRule)));
            QVERIFY2(second.recurrenceRule.isEmpty(),
                     qPrintable(QStringLiteral("re-parse: isRecurring=false but recurrenceRule non-empty for humanString '%1': '%2'")
                                    .arg(first.humanString, second.recurrenceRule)));
            QVERIFY2(!second.isRecurring,
                     qPrintable(QStringLiteral("re-parse spuriously set isRecurring for '%1' -> '%2'")
                                    .arg(input, first.humanString)));
        }
    }

    // ── Anti-tests: invalid input ────────────────────────────────────────
    // For gibberish, valid==false and humanString is empty — and we don't
    // try to round-trip an invalid result.

    void anti_gibberish_en()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral("xyz"), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.humanString.isEmpty());
    }

    void anti_gibberish_ru()
    {
        const auto r = DateParser::parseRussian(QString::fromUtf8("чрнвзкп"), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.humanString.isEmpty());
    }

    void anti_empty_en()
    {
        const auto r = DateParser::parseEnglish(QStringLiteral(""), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.humanString.isEmpty());
    }

    void anti_empty_ru()
    {
        const auto r = DateParser::parseRussian(QStringLiteral(""), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.humanString.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDateParserInvariants)
#include "test_dateparser_invariants.moc"
