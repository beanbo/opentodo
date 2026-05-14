#include <QtTest>
#include <QDate>
#include <QDateTime>

#include "nlp/recurrenceengine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Edge-case tests for RecurrenceEngine.
//
// The engine recognises an RFC-like RRULE subset:
//   FREQ        = DAILY | WEEKLY | MONTHLY | YEARLY
//   INTERVAL    = <positive integer>
//   BYDAY       = comma-separated MO/TU/WE/TH/FR/SA/SU
//   BYMONTHDAY  = 1..31
//   FROM        = COMPLETION
//
// Notably NOT implemented: BYMONTH, BYYEARDAY, BYWEEKNO, and COUNT>1 (the
// engine is stateless per call so it cannot count occurrences across calls;
// COUNT=1 IS honoured because that case is unambiguous — the original due
// date is occurrence #1, so no "next" exists). UNTIL is fully honoured.
// ─────────────────────────────────────────────────────────────────────────────

class TestRecurrenceEdgeCases : public QObject
{
    Q_OBJECT

private slots:
    // ── Leap year ────────────────────────────────────────────────────────────

    // FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=29 from 2024-02-29.
    // RFC 5545 says skip non-leap years until a year that has Feb 29 — i.e.
    // the next quadrennial leap year, 2028. (Engine honours BYMONTHDAY by
    // walking year-by-year; BYMONTH is not implemented but the natural pairing
    // is base.month() = February anyway.)
    void leap_year_feb29_yearly_bymonthday()
    {
        const QDate from(2024, 2, 29);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=29"), from);
        QCOMPARE(result, QDate(2028, 2, 29));
    }

    // FREQ=YEARLY (no BYMONTHDAY) from 2024-02-29.
    // QDate::addYears clamps Feb 29 → Feb 28 in non-leap year.
    void leap_year_feb29_yearly_plain()
    {
        const QDate from(2024, 2, 29);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY"), from);
        // Qt's QDate::addYears clamps the day-of-month.
        QCOMPARE(result, QDate(2025, 2, 28));
    }

    // ── Month-end rollover ───────────────────────────────────────────────────

    // FREQ=MONTHLY from 2026-01-31 → 2026-02-28 (Feb has 28 days in 2026).
    void monthly_jan31_to_feb28()
    {
        const QDate from(2026, 1, 31);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY"), from);
        QCOMPARE(result, QDate(2026, 2, 28));
    }

    // FREQ=MONTHLY;BYMONTHDAY=31 from 2026-01-31 — RFC says skip Feb → 2026-03-31.
    // Engine now honours skip-on-missing for explicit BYMONTHDAY values.
    void monthly_bymonthday31_skips_feb()
    {
        const QDate from(2026, 1, 31);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=31"), from);
        QCOMPARE(result, QDate(2026, 3, 31));
    }

    // FREQ=MONTHLY from 2024-01-31 → 2024-02-29 (leap year).
    void monthly_jan31_to_feb29_leap()
    {
        const QDate from(2024, 1, 31);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY"), from);
        QCOMPARE(result, QDate(2024, 2, 29));
    }

    // FREQ=MONTHLY from 2026-03-31 → 2026-04-30 (April has 30 days).
    void monthly_mar31_to_apr30()
    {
        const QDate from(2026, 3, 31);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY"), from);
        QCOMPARE(result, QDate(2026, 4, 30));
    }

    // ── Weekly with multi-BYDAY (MO,WE,FR) ───────────────────────────────────

    void weekly_multibyday_mon_to_wed()
    {
        // 2026-05-04 is Monday. Next BYDAY occurrence after Mon = Wed 2026-05-06.
        const QDate from(2026, 5, 4);
        QCOMPARE(from.dayOfWeek(), 1); // sanity: Monday
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR"), from);
        QCOMPARE(result, QDate(2026, 5, 6));
    }

    void weekly_multibyday_wed_to_fri()
    {
        const QDate from(2026, 5, 6);  // Wednesday
        QCOMPARE(from.dayOfWeek(), 3);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR"), from);
        QCOMPARE(result, QDate(2026, 5, 8));
    }

    void weekly_multibyday_fri_to_next_mon()
    {
        const QDate from(2026, 5, 8);  // Friday
        QCOMPARE(from.dayOfWeek(), 5);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR"), from);
        QCOMPARE(result, QDate(2026, 5, 11));
    }

    // ── Daily with INTERVAL ──────────────────────────────────────────────────

    void daily_interval2()
    {
        const QDate from(2026, 5, 4);
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY;INTERVAL=2"), from),
                 QDate(2026, 5, 6));
    }

    void daily_interval7()
    {
        const QDate from(2026, 5, 4);
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY;INTERVAL=7"), from),
                 QDate(2026, 5, 11));
    }

    // ── Time preserved? ──────────────────────────────────────────────────────

    // The engine returns QDate (date-only) — it deliberately drops time-of-day.
    // This test documents that contract: callers wishing to preserve a due-time
    // must re-attach it at the call site (TaskViewModel::completeTask does so).
    void time_not_part_of_engine_contract()
    {
        const QDate from(2026, 5, 4);
        const QDate next =
            RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY"), from);
        QCOMPARE(next, QDate(2026, 5, 5));
        // No time component — engine works in QDate only.
        // Verify by re-attaching a time and confirming round-trip works.
        const QDateTime dueWithTime(next, QTime(14, 30));
        QCOMPARE(dueWithTime.date(), QDate(2026, 5, 5));
        QCOMPARE(dueWithTime.time(), QTime(14, 30));
    }

    // ── Past date → engine just adds INTERVAL once (not "next future") ───────

    void past_date_advances_one_step_only()
    {
        // Engine is deterministic: it advances exactly one step from the
        // supplied fromDate. It does NOT loop until the result is ≥ today.
        // Callers that need "next future occurrence" must loop themselves.
        const QDate from(2020, 1, 1);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY"), from);
        QCOMPARE(result, QDate(2020, 1, 2));
    }

    // ── Termination — COUNT and UNTIL ────────────────────────────────────────

    // RFC 5545 §3.3.10: COUNT=1 means the rule produces exactly one occurrence
    // (the original due-date) and then terminates. Asking for the *next*
    // occurrence after that must yield no result → invalid QDate.
    void daily_count1_terminates()
    {
        const QDate from(2026, 5, 4);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY;COUNT=1"), from);
        QVERIFY(!result.isValid());
    }

    // UNTIL=20260510T000000Z — once the next-occurrence would land strictly
    // after 2026-05-10, the engine returns invalid.
    void daily_until_past_returns_invalid()
    {
        const QDate from(2026, 5, 11); // already past the UNTIL
        const QDate result = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260510T000000Z"), from);
        QVERIFY(!result.isValid());
    }

    // ── Invalid rules — must not crash, return invalid QDate ─────────────────

    void empty_rule_returns_invalid()
    {
        const QDate result = RecurrenceEngine::advance(QString(), QDate(2026, 5, 4));
        QVERIFY(!result.isValid());
    }

    void whitespace_rule_returns_invalid()
    {
        const QDate result = RecurrenceEngine::advance(QStringLiteral("   "), QDate(2026, 5, 4));
        QVERIFY(!result.isValid());
    }

    void malformed_freq_returns_invalid()
    {
        // FREQ=BADFREQ — not in {DAILY,WEEKLY,MONTHLY,YEARLY}, should fall through
        // to the trailing `return QDate()` in advance().
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=BADFREQ"), QDate(2026, 5, 4));
        QVERIFY(!result.isValid());
    }

    void invalid_bymonth_yearly_ignored()
    {
        // Engine does not honour BYMONTH at all → BYMONTH=13 is silently ignored,
        // YEARLY simply addYears(1). Verify no crash and a sensible date.
        const QDate from(2026, 5, 4);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY;BYMONTH=13"), from);
        QCOMPARE(result, QDate(2027, 5, 4));
    }

    // Bonus invariant: invalid fromDate always yields invalid output regardless of rule.
    void invalid_fromDate_returns_invalid()
    {
        QVERIFY(!RecurrenceEngine::advance(QStringLiteral("FREQ=DAILY"), QDate()).isValid());
        QVERIFY(!RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY;BYMONTHDAY=29"),
                                           QDate()).isValid());
    }

    // Bonus: BYMONTHDAY out of range is silently ignored, falls back to base.day().
    void monthly_bymonthday_out_of_range_uses_base_day()
    {
        // BYMONTHDAY=99 fails the (1..31) guard → engine falls back to base.day().
        const QDate from(2026, 5, 15);
        const QDate result =
            RecurrenceEngine::advance(QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=99"), from);
        QCOMPARE(result, QDate(2026, 6, 15));
    }
};

QTEST_GUILESS_MAIN(TestRecurrenceEdgeCases)
#include "test_recurrence_edge_cases.moc"
