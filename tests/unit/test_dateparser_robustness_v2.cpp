// DateParser robustness v2 — behavioural pinning for tricky Russian/English
// natural-language phrases that the existing test suite (test_dateparser_ru,
// test_dateparser_en, test_dateparser_invariants, test_nlp_robustness)
// hasn't covered.
//
// Goal: NOT to add new parser features. The Russian / English rules in
// src/nlp/rules_ru.cpp + rules_en.cpp are intentionally minimal — many of
// the phrases below are NOT currently understood. For each phrase, the
// slot:
//   * Asserts the CURRENT observed behaviour (the parser's actual output
//     given REF = 2026-05-15, Friday).
//   * If the parser produces an unhelpful answer (silently treats the
//     expression as a different shorter phrase, returns invalid, etc.),
//     emits a qInfo() line documenting what was parsed vs what would be
//     ideal. The next person tasked with fixing the rule flips the
//     assertion when the fix lands.
//
// Reference date: 2026-05-15 (Friday, dayOfWeek=5). Same convention as
// every other DateParser test in this repo.
//
// Surface-bug summary (each line corresponds to one slot below). "FIXED"
// entries assert the post-fix correct behaviour; the rest still pin the
// parser's current (buggy) output for future fix sessions to flip.
//   B1  FIXED ru "через 2 недели в понедельник" — relative + weekday now
//       snaps to the next named weekday on/after the offset date.
//   B2  FIXED ru "каждый второй вторник" → INTERVAL=2;BYDAY=TU.
//   B3  FIXED ru "по будням" → FREQ=WEEKLY;BYDAY=MO..FR.
//   B4  FIXED ru "в выходные" → FREQ=WEEKLY;BYDAY=SA,SU.
//   B5  FIXED ru "первого числа каждого месяца" → FREQ=MONTHLY;BYMONTHDAY=1.
//   B6  ru "31 февраля" — current parser ACCEPTS this and silently bumps
//       day to the last valid day of Feb (it doesn't via the date branch;
//       actually returns invalid). Pinned.
//   B7  FIXED ru "завтра в 25:00" — explicit out-of-range time now rejects
//       the whole expression (valid=false).
//   B9  FIXED ru "послезавтра в 9 утра" — 09:00. The day-part markers are
//       understood now ("в 9 вечера" = 21:00, "в 3 дня" = 15:00; see
//       test_nlp_title_preservation).
//   B10 FIXED ru "в субботу через 3 недели" — weekday-first + "через N <unit>"
//       tail is parsed; date snaps to the next Saturday then advances by the
//       offset.
//   COMMA FIXED ru "завтра, в 14:00" — comma-separator form now parses
//       via [,;!?]→whitespace normalisation at the head of parseRussian.
//   E1  FIXED en "in 2 weeks on monday" — compound relative + weekday now
//       parses; date is the relative offset snapped forward to the named dow.
//   E2  FIXED en "every other tuesday" → INTERVAL=2;BYDAY=TU. The "other"
//       prefix is now recognised by the every-handler.
//   E3  FIXED en "weekdays" → FREQ=WEEKLY;BYDAY=MO..FR (bare-noun rule).
//   E4  FIXED en "weekends" → FREQ=WEEKLY;BYDAY=SA,SU (bare-noun rule).
//   E5  FIXED en "first day of each month" → FREQ=MONTHLY;BYMONTHDAY=1.
//   E6  en "feb 31" — clamped to Feb 28 (parser's tryDate() clamps invalid
//       day to last day of month) — actually a feature, but worth pinning.
//   E7  FIXED en "tomorrow at 25:00" — same fix as B7 (out-of-range time
//       rejects the whole expression).
//   E8  FIXED en "yesterday at noon" — "noon" now maps to 12:00 (midnight
//       and "midday" mapped similarly) via word-time handler in tryParseTime.
//   E9  FIXED en "in 3 weeks on saturday" — same fix as E1.
//
// Constraints honoured:
//   * No production code changes (src/nlp/ untouched).
//   * QTEST_GUILESS_MAIN — no GUI required.
//   * Plain QString::fromUtf8 / QStringLiteral fixtures.
//   * QCOMPARE / QVERIFY assertions.
//   * Unique QSettings org/app to keep this test isolated under parallel
//     ctest -j8.

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>

#include "nlp/dateparser.h"

// Reference date: 2026-05-15 (Friday, dayOfWeek=5).
static const QDate REF(2026, 5, 15);

class TestDateParserRobustnessV2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique QSettings namespace — keeps this test isolated from the
        // user's real OpenTodo install AND from any other tests running
        // in parallel under shared QStandardPaths.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_DPRobustV2"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_dateparser_robustness_v2"));
    }

    // ───────────────────────────────────────────────────────────────────────
    // Russian
    // ───────────────────────────────────────────────────────────────────────

    // B1 FIXED (Q3.02-E): "через 2 недели в понедельник" — compound relative
    // + weekday now parses. Computed as REF + 14 days, then snapped forward
    // to the next Monday (inclusive of the offset day if it falls on Mon).
    // REF=2026-05-15 + 14 days = 2026-05-29 (Friday) → next Mon = 2026-06-01.
    void ru_cherez_2_nedeli_v_ponedelnik()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("через 2 недели в понедельник"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-06-01"));
    }

    // B2 FIXED (Q3.02-E): "каждый второй вторник" — "every other Tuesday".
    // The "кажд(ый|ую|ое|ые) ..." handler now recognises "втор(ой|ую|ое) <day>"
    // as a fortnightly recurrence (FREQ=WEEKLY;INTERVAL=2;BYDAY=<DAY>).
    void ru_kazhdyy_vtoroy_vtornik()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("каждый второй вторник"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=TU"));
    }

    // B3 FIXED (Q3.02-E): "по будням" → Mon-Fri recurrence.
    // A dedicated handler recognises "по будням" / "в будни" / "будни" as
    // a weekday recurrence — first occurrence is today if today is Mon-Fri,
    // else the upcoming Monday. REF=Fri 2026-05-15 → first date = REF.
    void ru_po_budnyam()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("по будням"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"));
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
    }

    // B4 FIXED (Q3.02-E): "в выходные" → Sat-Sun recurrence.
    // First occurrence is today if today is Sat/Sun, else the upcoming
    // Saturday. REF=Fri 2026-05-15 → first date = Sat 2026-05-16.
    void ru_v_vyhodnye()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("в выходные"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU"));
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    // B5 FIXED (Q3.02-E): "первого числа каждого месяца" → monthly anchored
    // on day 1. A new handler matches ordinal-or-digit + "числа каждого
    // месяца" and emits FREQ=MONTHLY;BYMONTHDAY=1.
    void ru_pervogo_chisla_kazhdogo_mesyaca()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("первого числа каждого месяца"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=1"));
    }

    // B6: "31 февраля" — invalid Gregorian date. The ru-date branch
    // regex matches "31 февраля", computes QDate(2026, 2, 31), which is
    // invalid. The branch sees date.isValid()==false and falls through
    // → final result.valid stays false.
    void ru_31_fevralya_invalid()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("31 февраля"), REF);
        qInfo() << "ru '31 февраля' (invalid Feb 31)"
                << "valid=" << r.valid
                << "date=" << r.date;
        QVERIFY(!r.valid);
        QVERIFY(r.date.isEmpty());
    }

    // B7 FIXED (Q3.02-E): "завтра в 25:00" — explicit time clause with
    // out-of-range hour now rejects the whole expression. tryParseTime
    // requires "at"/"в" before the digits, so when those markers are
    // present and the time fails the 0-23/0-59 range check, valid=false
    // and the result is fully cleared rather than silently dropping just
    // the time portion.
    void ru_zavtra_v_25_invalid_time()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("завтра в 25:00"), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.date.isEmpty());
        QVERIFY(r.datetime.isEmpty());
    }

    // B8: "вчера" — yesterday, past relative. Parser is required to NOT
    // filter past dates (the docstring on rules_ru is explicit about this).
    // Confirm date = REF - 1 day.
    void ru_vchera_past_date_still_valid()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("вчера"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
        QCOMPARE(r.humanString, QString::fromUtf8("Вчера"));
    }

    // B9: "послезавтра в 9 утра" — should be REF + 2 days at 09:00. The
    // " в 9 утра" time tail is split off, the simple-day handler matches
    // "послезавтра", and tryParseTime maps the "утра" day-part to am.
    void ru_poslezavtra_v_9_utra()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("послезавтра в 9 утра"), REF);
        qInfo() << "ru 'послезавтра в 9 утра'"
                << "valid=" << r.valid
                << "date=" << r.date
                << "datetime=" << r.datetime;
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-17"));
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-17T09:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    // B10 FIXED (Q3.02-E): "в субботу через 3 недели" — weekday-first +
    // "через N <unit>" tail is parsed; date snaps to the nearest upcoming
    // Saturday (2026-05-16), then advances by 3 weeks → 2026-06-06.
    void ru_v_subbotu_cherez_3_nedeli()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("в субботу через 3 недели"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-06-06"));
    }

    // ── Capitalization variants for "завтра" ──────────────────────────────
    // The Russian parser .toLower()'s the input on entry, so all three
    // forms must produce the same result. Pin all three.

    void ru_zavtra_lowercase()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("завтра"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void ru_Zavtra_titlecase()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("Завтра"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void ru_ZAVTRA_allcaps()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("ЗАВТРА"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    // ── Whitespace tolerance ──────────────────────────────────────────────
    // The parser .trimmed()'s the input on entry (in DateParser::parse,
    // also at top of parseRussian). Leading/trailing whitespace must not
    // affect the result.

    void ru_zavtra_with_leading_trailing_whitespace()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("  завтра   "), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    // FIXED (Q3.02-E): "завтра, в 14:00" — comma between the day word and
    // the time now parses correctly. The Russian rule head normalises
    // [,;!?] → whitespace before matching, so "завтра, в 14:00" reduces
    // to "завтра в 14:00" and runs through the simple-day + tryParseTime
    // path as expected.
    void ru_zavtra_comma_v_14()
    {
        const auto r = DateParser::parseRussian(
            QString::fromUtf8("завтра, в 14:00"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-16T14:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    // ───────────────────────────────────────────────────────────────────────
    // English
    // ───────────────────────────────────────────────────────────────────────

    // E1 FIXED (Q3.02-E): "in 2 weeks on monday" — compound relative +
    // weekday now parses. Computed as REF + 14 days = 2026-05-29 (Fri),
    // then snapped forward to next Monday = 2026-06-01.
    void en_in_2_weeks_on_monday()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("in 2 weeks on monday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-06-01"));
    }

    // E2 FIXED (Q3.02-E): "every other tuesday" → INTERVAL=2 BYDAY=TU.
    // The "every <stuff>" handler now detects the "other " prefix on the
    // tail and emits FREQ=WEEKLY;INTERVAL=2;BYDAY=<DAY>. Also covers
    // "every other day/week/month".
    void en_every_other_tuesday()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("every other tuesday"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=TU"));
    }

    // E3 FIXED (Q3.02-E): "weekdays" (bare noun) → Mon-Fri recurrence.
    // First occurrence is today if today is Mon-Fri, else upcoming Monday.
    // REF=Fri 2026-05-15 → first date = REF.
    void en_weekdays_bare()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("weekdays"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"));
        QCOMPARE(r.date, QStringLiteral("2026-05-15"));
    }

    // E4 FIXED (Q3.02-E): "weekends" (bare noun) → Sat-Sun recurrence.
    // First occurrence is today if today is Sat/Sun, else upcoming Saturday.
    // REF=Fri 2026-05-15 → first date = Sat 2026-05-16.
    void en_weekends_bare()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("weekends"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU"));
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    // E5 FIXED (Q3.02-E): "first day of each month" → monthly anchor on
    // day 1. The new "<ordinal> [day] of (each|every) month" handler
    // emits FREQ=MONTHLY;BYMONTHDAY=1.
    void en_first_day_of_each_month()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("first day of each month"), REF);
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=1"));
    }

    // E6: "feb 31" — invalid Gregorian date. The English parser's
    // tryDate() lambda CLAMPS the day to the last valid day of the
    // month (see rules_en.cpp comments on the "feb 31" clamp). This
    // is a documented choice — not a bug. Pin the contract.
    void en_feb_31_clamps_to_feb_28()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("feb 31"), REF);
        qInfo() << "en 'feb 31' (invalid; documented clamp behaviour)"
                << "valid=" << r.valid
                << "date=" << r.date
                << "(documented: parser clamps to last day of Feb)";
        // 2026 is not a leap year, so last day of Feb is 28. Feb 28 2026
        // is in the past relative to REF=May 15 2026 → bumped to 2027.
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2027-02-28"));
    }

    // E7 FIXED (Q3.02-E): "tomorrow at 25:00" — same fix as B7. The
    // explicit "at" clause with an out-of-range hour now rejects the
    // whole expression instead of silently dropping just the time.
    void en_tomorrow_at_25_invalid_time()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("tomorrow at 25:00"), REF);
        QVERIFY(!r.valid);
        QVERIFY(r.date.isEmpty());
        QVERIFY(r.datetime.isEmpty());
    }

    // E8: "yesterday at noon" — "noon" is not a recognised time. The
    // simple-day branch matches "yesterday", populates result.date=
    // 2026-05-14. tryParseTime tries to match "at <digits>" but "noon"
    // is text, not digits, so datetime stays empty.
    // E8 FIXED (Q3.02-E): "yesterday at noon" — word-time mapping added.
    // tryParseTime now recognises "noon" / "midday" / "midnight" (and the
    // Russian "полдень" / "полночь") preceded by "at"/"в", mapping them
    // to 12:00 or 00:00 respectively.
    void en_yesterday_at_noon()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("yesterday at noon"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-14"));
        QVERIFY(!r.datetime.isEmpty());
        QVERIFY2(r.datetime.startsWith(QStringLiteral("2026-05-14T12:00")),
                 qPrintable(QStringLiteral("datetime = %1").arg(r.datetime)));
    }

    // E9 FIXED (Q3.02-E): "in 3 weeks on saturday" — same fix as E1.
    // REF + 21 days = 2026-06-05 (Fri), then snapped forward to next Sat
    // = 2026-06-06. (NOTE: the original "ideal" comment mentioned
    // 2026-06-13 i.e. a strict next-Saturday-AFTER-the-offset; the new
    // handler snaps forward 0..6 days to the named dow, so when the offset
    // already lands on the target we keep it. 2026-06-05 is Fri, so the
    // snap target is the immediately following Sat: 2026-06-06.)
    void en_in_3_weeks_on_saturday()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("in 3 weeks on saturday"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-06-06"));
    }

    // ── English capitalization sanity (mirrors the ru capitalization slots) ──
    // English parser .toLower()'s the input. "Tomorrow", "TOMORROW", and
    // "tomorrow" must all behave identically.

    void en_tomorrow_titlecase()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("Tomorrow"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    void en_tomorrow_allcaps()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("TOMORROW"), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }

    // ── English whitespace tolerance ──────────────────────────────────────

    void en_tomorrow_with_whitespace()
    {
        const auto r = DateParser::parseEnglish(
            QStringLiteral("   tomorrow   "), REF);
        QVERIFY(r.valid);
        QCOMPARE(r.date, QStringLiteral("2026-05-16"));
    }
};

QTEST_GUILESS_MAIN(TestDateParserRobustnessV2)
#include "test_dateparser_robustness_v2.moc"
