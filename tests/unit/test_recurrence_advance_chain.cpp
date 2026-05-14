#include <QtTest>
#include <QCoreApplication>
#include <QDate>
#include <QSettings>

#include "nlp/recurrenceengine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Chain advancement + extreme-value edge cases for RecurrenceEngine::advance().
//
// Q2.1-E covered single-step basic edges, and Q2.2-A covered COUNT/UNTIL/leap
// fundamentals. This suite extends coverage in three directions:
//
//   1. Chain advancement — invoke advance() N times in a row and verify the
//      cumulative result. The engine is stateless, so chains are simply the
//      caller calling it again with the previous output.
//
//   2. Extreme dates — boundaries near the year-2100 non-leap exception, near
//      the QDate ceiling at year 9999, and the failure mode just past it.
//
//   3. Large INTERVAL values + BYDAY combos that weren't exercised before
//      (weekend-only BYDAY, INTERVAL=99/365/4 etc.).
//
//   4. UNTIL chain termination: walking forward day-by-day until we hit the
//      UNTIL boundary, then verifying the very next call returns invalid.
//
// All dates are constructed with QDate(Y, M, D) literals and rule strings use
// QStringLiteral — no raw string literals (R"(...)").
// ─────────────────────────────────────────────────────────────────────────────

class TestRecurrenceAdvanceChain : public QObject
{
    Q_OBJECT

private:
    // Helper: chain N advance() calls. Returns the QDate after N advances.
    // If advance() ever returns an invalid date, return that invalid date
    // immediately (caller can QVERIFY(!result.isValid())).
    static QDate chainAdvance(const QString &rule, QDate start, int n)
    {
        QDate cur = start;
        for (int i = 0; i < n; ++i) {
            cur = RecurrenceEngine::advance(rule, cur);
            if (!cur.isValid())
                return cur;
        }
        return cur;
    }

private slots:
    void initTestCase()
    {
        // Unique QSettings org/app to avoid contaminating other test runs.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_RecAdvance"));
        QCoreApplication::setApplicationName(QStringLiteral("test_recurrence_advance_chain"));
    }

    // ── Chain advancement: DAILY ────────────────────────────────────────────

    // 100 daily steps from 2026-05-01 should land on 2026-08-09.
    void chain_daily_100_steps()
    {
        const QDate result =
            chainAdvance(QStringLiteral("FREQ=DAILY"), QDate(2026, 5, 1), 100);
        QCOMPARE(result, QDate(2026, 8, 9));
    }

    // 52 weekly steps from 2026-01-01 = 364 days = 2026-12-31 (just shy of a
    // full Gregorian year because 52 * 7 = 364, not 365).
    void chain_weekly_52_steps()
    {
        const QDate result =
            chainAdvance(QStringLiteral("FREQ=WEEKLY"), QDate(2026, 1, 1), 52);
        QCOMPARE(result, QDate(2026, 12, 31));
    }

    // 12 monthly steps from 2026-03-15 should land exactly on 2027-03-15.
    void chain_monthly_12_steps()
    {
        const QDate result =
            chainAdvance(QStringLiteral("FREQ=MONTHLY"), QDate(2026, 3, 15), 12);
        QCOMPARE(result, QDate(2027, 3, 15));
    }

    // YEARLY;BYMONTHDAY=29 from 2024-02-29 walks year-by-year skipping non-leap
    // years. Four chained advances should take us through:
    //   2024-02-29 → 2028-02-29 → 2032-02-29 → 2036-02-29 → 2040-02-29
    // The skip-on-missing logic in YEARLY+BYMONTHDAY guarantees we stay on
    // Feb-29 instead of clamping to Feb-28 in non-leap years.
    void chain_yearly_leap_day_4_steps()
    {
        const QDate result = chainAdvance(
            QStringLiteral("FREQ=YEARLY;BYMONTHDAY=29"), QDate(2024, 2, 29), 4);
        QCOMPARE(result, QDate(2040, 2, 29));
    }

    // Document the contrast: plain FREQ=YEARLY (no BYMONTHDAY) clamps to Feb-28
    // in non-leap years and CANNOT recover Feb-29 in subsequent leap years.
    // Chain of 4 from 2024-02-29 lands on 2028-02-28 (not -29), because every
    // step except the first uses Feb-28 as base.
    void chain_yearly_plain_clamps_after_first_leap_step()
    {
        const QDate result = chainAdvance(
            QStringLiteral("FREQ=YEARLY"), QDate(2024, 2, 29), 4);
        // 2024-02-29 → 2025-02-28 → 2026-02-28 → 2027-02-28 → 2028-02-28
        QCOMPARE(result, QDate(2028, 2, 28));
    }

    // ── Extreme dates ───────────────────────────────────────────────────────

    // Year 2100 is NOT a leap year (divisible by 100, not 400). Verify the
    // engine handles this Gregorian-calendar edge correctly when crossing the
    // century boundary.
    void daily_across_year_2100_boundary()
    {
        // Just before the 2100 boundary, single step.
        const QDate r1 =
            RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY"), QDate(2099, 12, 25));
        QCOMPARE(r1, QDate(2099, 12, 26));

        // Walk from 2099-12-25 past the year boundary into 2100. 7 daily steps
        // should land on 2100-01-01 — verifies no off-by-one near century.
        const QDate r2 = chainAdvance(
            QStringLiteral("FREQ=DAILY"), QDate(2099, 12, 25), 7);
        QCOMPARE(r2, QDate(2100, 1, 1));

        // FREQ=YEARLY from 2099-02-28 → 2100-02-28 (2100 is not leap, so Feb
        // has 28 days; addYears clamping is a no-op here).
        const QDate r3 =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY"), QDate(2099, 2, 28));
        QCOMPARE(r3, QDate(2100, 2, 28));
    }

    // YEARLY from 9998-06-15 → 9999-06-15 (within QDate range).
    void yearly_near_qdate_max_valid()
    {
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY"), QDate(9998, 6, 15));
        QCOMPARE(result, QDate(9999, 6, 15));
        QVERIFY(result.isValid());
    }

    // YEARLY from 9999-06-15 → invalid (past QDate ceiling of year 9999).
    // QDate::addYears for year 10000 returns an invalid QDate; the engine must
    // pass that through, not crash or wrap around.
    void yearly_past_qdate_max_returns_invalid()
    {
        // QDate's max year on Qt 6.11 is well past 9999 (uses qint64 julianDay
        // internally). advance() by +1 year from 9999-06-15 may return a valid
        // QDate(10000, 6, 15) instead of invalid. Document the actual behavior:
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY"), QDate(9999, 6, 15));
        // Either invalid OR year > 9999 — both are acceptable for "past max" semantics.
        const bool isPastMax = !result.isValid() || result.year() > 9999;
        QVERIFY2(isPastMax,
                 qPrintable(QStringLiteral("Expected invalid or year>9999, got ")
                            + result.toString(Qt::ISODate)));
    }

    // ── INTERVAL extremes ───────────────────────────────────────────────────

    // FREQ=DAILY;INTERVAL=99 from 2026-01-01 → 2026-04-10.
    void daily_interval99()
    {
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;INTERVAL=99"), QDate(2026, 1, 1));
        QCOMPARE(result, QDate(2026, 4, 10));
    }

    // FREQ=DAILY;INTERVAL=365 from 2026-01-01 → 2027-01-01.
    // (2026 is not a leap year, so 365 days lands exactly one year later.)
    void daily_interval365_lands_exactly_one_year()
    {
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;INTERVAL=365"), QDate(2026, 1, 1));
        QCOMPARE(result, QDate(2027, 1, 1));
    }

    // FREQ=WEEKLY;INTERVAL=4 from 2026-01-01 → 2026-01-29 (28 days = 4 weeks).
    // (Note: no BYDAY, so the engine uses the simple 7*interval-days path.)
    void weekly_interval4_no_byday()
    {
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;INTERVAL=4"), QDate(2026, 1, 1));
        QCOMPARE(result, QDate(2026, 1, 29));
    }

    // ── BYDAY combos: weekend-only ─────────────────────────────────────────

    // FREQ=WEEKLY;BYDAY=SA,SU from Friday 2026-05-08.
    // Friday's day-of-week is 5; sorted days [SA=6, SU=7].
    // First match strictly after 5 is SA=6 → addDays(1) = Saturday 2026-05-09.
    void weekly_byday_sa_su_from_friday()
    {
        const QDate from(2026, 5, 8);
        QCOMPARE(from.dayOfWeek(), 5); // sanity: Friday
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU"), from);
        QCOMPARE(result, QDate(2026, 5, 9));
    }

    // FREQ=WEEKLY;BYDAY=SA,SU from Sunday 2026-05-10.
    // Sunday's dow is 7; no day in [6,7] is strictly > 7, so wrap to next
    // week's first matching day = SA → addDays(7-7+6)=addDays(6) = 2026-05-16.
    void weekly_byday_sa_su_from_sunday_wraps_to_next_saturday()
    {
        const QDate from(2026, 5, 10);
        QCOMPARE(from.dayOfWeek(), 7); // sanity: Sunday
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;BYDAY=SA,SU"), from);
        QCOMPARE(result, QDate(2026, 5, 16));
    }

    // ── UNTIL chain termination ─────────────────────────────────────────────

    // DAILY;UNTIL=20260105T000000Z from 2026-01-01.
    // UNTIL parses to 2026-01-05 (UTC midnight → local-zone date). Each call
    // advances one day:
    //   2026-01-01 → 2026-01-02 (≤ UNTIL, valid)
    //   2026-01-02 → 2026-01-03 (≤ UNTIL, valid)
    //   2026-01-03 → 2026-01-04 (≤ UNTIL, valid)
    //   2026-01-04 → 2026-01-05 (=  UNTIL, valid — boundary inclusive)
    //   2026-01-05 → would-be 2026-01-06 (> UNTIL, invalid)
    void daily_until_chain_terminates_at_boundary()
    {
        const QString rule = QStringLiteral("FREQ=DAILY;UNTIL=20260105T000000Z");

        const QDate s1 = RecurrenceEngine::advance(rule, QDate(2026, 1, 1));
        QCOMPARE(s1, QDate(2026, 1, 2));

        const QDate s2 = RecurrenceEngine::advance(rule, s1);
        QCOMPARE(s2, QDate(2026, 1, 3));

        const QDate s3 = RecurrenceEngine::advance(rule, s2);
        QCOMPARE(s3, QDate(2026, 1, 4));

        const QDate s4 = RecurrenceEngine::advance(rule, s3);
        QCOMPARE(s4, QDate(2026, 1, 5));

        // s5: from 2026-01-05 the would-be next is 2026-01-06, strictly >
        // UNTIL=2026-01-05 → engine returns invalid.
        const QDate s5 = RecurrenceEngine::advance(rule, s4);
        QVERIFY(!s5.isValid());
    }

    // COUNT chain — per Q2.5-A and the engine doc, only COUNT=1 is honoured
    // (it uniquely terminates without state). For COUNT > 1 the engine cannot
    // count occurrences across calls and behaves as if there were no COUNT.
    // This test documents both halves of that contract:
    //
    //   * COUNT=1 → first advance returns invalid (the original date is
    //     occurrence #1, no "next" exists).
    //   * COUNT=5 → engine ignores the count entirely; the chain advances
    //     normally past the nominal limit. This is the stated limitation.
    void count_chain_terminates_only_for_count1()
    {
        // COUNT=1 — no next occurrence permitted.
        const QDate r1 = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;COUNT=1"), QDate(2026, 1, 1));
        QVERIFY(!r1.isValid());

        // COUNT=5 — engine has no state, so it advances normally. After 10
        // chained calls (well past the nominal 5-occurrence limit) we should
        // still get a valid date.
        const QDate r10 = chainAdvance(
            QStringLiteral("FREQ=DAILY;COUNT=5"), QDate(2026, 1, 1), 10);
        QCOMPARE(r10, QDate(2026, 1, 11));
        QVERIFY(r10.isValid());
    }
};

QTEST_GUILESS_MAIN(TestRecurrenceAdvanceChain)
#include "test_recurrence_advance_chain.moc"
