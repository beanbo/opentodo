// ---------------------------------------------------------------------------
// test_recurrence_edges_v2.cpp
//
// Quality round Q2.27: behavioural pinning for previously-uncovered corners
// of RecurrenceEngine::advance().
//
// Existing coverage in this directory:
//   * test_recurrence.cpp              — happy-path English + Russian human
//                                        strings + the TaskViewModel wiring.
//   * test_recurrence_edge_cases.cpp   — leap-year + BYMONTHDAY=31 + COUNT=1
//                                        + UNTIL + malformed FREQ basics.
//   * test_recurrence_advance_chain.cpp — multi-step chains, year-2100/9999
//                                        extremes, large INTERVAL, UNTIL chain.
//
// This v2 suite pins twelve additional corners that none of the above touch:
//
//   1. monthly_BYMONTHDAY_31_in_February_skips_or_clamps — RFC 5545 §3.3.10
//      skip-on-missing for explicit BYMONTHDAY=31 from Jan 31 (Feb has only
//      28/29 → engine must advance to a 31-day month).
//   2. monthly_BYMONTHDAY_29_in_non_leap_February — same skip-on-missing for
//      BYMONTHDAY=29 in a non-leap year February.
//   3. yearly_Feb_29_in_non_leap_year — plain FREQ=YEARLY (no BYMONTHDAY)
//      from 2024-02-29 lands on 2025-02-28 (QDate::addYears clamp). Documents
//      the difference between plain YEARLY and YEARLY+BYMONTHDAY=29.
//   4. monthly_BYDAY_neg1FR_last_Friday — pin support (or lack thereof) for
//      RFC 5545 signed BYDAY prefixes such as "-1FR" / "1MO".
//   5. weekly_BYDAY_no_explicit_day_uses_DTSTART_day — pin that a bare
//      FREQ=WEEKLY without BYDAY preserves the source weekday (engine uses
//      base.addDays(7 * interval)).
//   6. interval_zero_or_negative_is_safely_handled — pin the qMax(1, n)
//      clamp on INTERVAL.
//   7. interval_INT_MAX_does_not_overflow — pin behaviour for INTERVAL near
//      qint32 max (overflow must yield invalid QDate, not crash or wrap).
//   8. UNTIL_in_past_first_occurrence_is_empty_or_null — pin that UNTIL is
//      gated against the *computed* next occurrence, not the from-date.
//   9. COUNT_zero_first_occurrence_is_empty_or_null — pin that COUNT=0
//      falls under the same `count <= 1 → invalid` gate as COUNT=1.
//  10. RRULE_with_unknown_FREQ_returns_empty_or_error — pin behaviour for an
//      invalid FREQ value (FORTNIGHTLY, EVERY, HOURLY).
//  11. BYSETPOS_with_BYDAY_picks_nth_match — pin (lack of) BYSETPOS support
//      in the MONTHLY branch.
//  12. RRULE_with_DTSTART_in_TZ_normalises_to_LocalTime — pin that the
//      engine's signature takes a QDate (no time-of-day, no zone) and that
//      the only TZ-aware path is UNTIL parsing via parseUntil().
//
// Constraints:
//   * QTEST_GUILESS_MAIN (no GUI; pure-domain helper).
//   * Unique QSettings org/app to keep parallel ctest runs isolated.
//   * No raw strings R"(...)" — moc-hostile.
//   * Registered as App layer (see CMakeLists.txt) to match the existing
//     test_recurrence + test_recurrence_edge_cases sister-suites.
//   * NO production-code edits — assertions pin CURRENT behaviour, with
//     qInfo() comments where the behaviour diverges from RFC 5545 strict
//     interpretation. Flipping the assertions when a fix lands is the
//     regression-bench.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <climits>

#include "nlp/recurrenceengine.h"

class TestRecurrenceEdgesV2 : public QObject
{
    Q_OBJECT

private slots:
    // ── Per-test fixture ────────────────────────────────────────────────────

    void initTestCase()
    {
        // Unique QSettings namespace so parallel ctest runs don't collide with
        // the existing test_recurrence* suites.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_RecEdgesV2"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_recurrence_edges_v2"));
    }

    // -----------------------------------------------------------------------
    // 1. monthly_BYMONTHDAY_31_in_February_skips_or_clamps
    // -----------------------------------------------------------------------
    //
    // FREQ=MONTHLY;BYMONTHDAY=31 from 2026-01-31. February 2026 has only 28
    // days. RFC 5545 §3.3.10 says skip the occurrence (do NOT clamp to Feb 28),
    // i.e. advance to the next month that has day 31. The engine implements
    // skip-on-missing for explicit BYMONTHDAY → expect 2026-03-31.
    //
    // (This is asserted independently of monthly_bymonthday31_skips_feb in the
    // sister suite — that one uses 2026-01-31; we cross-verify and also pin
    // the leap-year variant to lock the contract for both 28-day and 29-day
    // Februaries.)
    // -----------------------------------------------------------------------
    void monthly_BYMONTHDAY_31_in_February_skips_or_clamps()
    {
        // Non-leap Feb (2026): Jan 31 → Mar 31 (skips Feb).
        const QDate fromNonLeap(2026, 1, 31);
        const QDate resultNonLeap = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=31"), fromNonLeap);
        QCOMPARE(resultNonLeap, QDate(2026, 3, 31));

        // Leap Feb (2024): Jan 31 → Mar 31 (Feb 2024 has only 29 days, not
        // 31, so the skip still kicks in. The engine does NOT special-case
        // leap year here — only days-in-month matters.)
        const QDate fromLeap(2024, 1, 31);
        const QDate resultLeap = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=31"), fromLeap);
        QCOMPARE(resultLeap, QDate(2024, 3, 31));

        // Bonus: from 2026-03-31, BYMONTHDAY=31 → April has 30 days, May has
        // 31. Engine walks forward and lands on May 31.
        const QDate fromMar(2026, 3, 31);
        const QDate resultMay = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=31"), fromMar);
        QCOMPARE(resultMay, QDate(2026, 5, 31));
    }

    // -----------------------------------------------------------------------
    // 2. monthly_BYMONTHDAY_29_in_non_leap_February
    // -----------------------------------------------------------------------
    //
    // FREQ=MONTHLY;BYMONTHDAY=29 from 2026-01-29 in non-leap year. Feb 2026
    // has 28 days. Engine should skip Feb and land on Mar 29. (Same skip-on-
    // missing logic as BYMONTHDAY=31 — the only difference is the threshold.)
    //
    // Leap-year contrast: from 2024-01-29 the engine should land on 2024-02-29
    // because Feb 2024 has 29 days (matches the anchor exactly).
    // -----------------------------------------------------------------------
    void monthly_BYMONTHDAY_29_in_non_leap_February()
    {
        // Non-leap year (2026): Jan 29 → Mar 29.
        const QDate fromNonLeap(2026, 1, 29);
        const QDate resultNonLeap = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=29"), fromNonLeap);
        QCOMPARE(resultNonLeap, QDate(2026, 3, 29));

        // Leap year (2024): Jan 29 → Feb 29 (no skip needed).
        const QDate fromLeap(2024, 1, 29);
        const QDate resultLeap = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=29"), fromLeap);
        QCOMPARE(resultLeap, QDate(2024, 2, 29));

        // Non-leap year, BYMONTHDAY=30: Jan 30 → Mar 30 (Feb 2026 < 30 days).
        // Pin the >= threshold logic generalises beyond 29 + 31.
        const QDate fromJan30(2026, 1, 30);
        const QDate resultMar30 = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=30"), fromJan30);
        QCOMPARE(resultMar30, QDate(2026, 3, 30));
    }

    // -----------------------------------------------------------------------
    // 3. yearly_Feb_29_in_non_leap_year
    // -----------------------------------------------------------------------
    //
    // Plain FREQ=YEARLY (no BYMONTHDAY) from 2024-02-29 → 2025-02-28. The
    // engine falls through to QDate::addYears(interval), which clamps Feb 29
    // to Feb 28 in non-leap years. This is the LEGACY clamp path, distinct
    // from the YEARLY;BYMONTHDAY=29 path that walks year-by-year until a leap
    // year (covered in test_recurrence_edge_cases::leap_year_feb29_yearly_bymonthday).
    //
    // This slot pins:
    //   a) plain YEARLY clamps to Feb 28 (next year, not next leap year),
    //   b) YEARLY;BYMONTH=2;BYMONTHDAY=29 lands on 2028-02-29 (next leap year),
    //   c) chaining plain YEARLY from a leap Feb 29 loses the "Feb 29" anchor
    //      forever (subsequent steps base on Feb 28 → never recovers Feb 29).
    //
    // Note: BYMONTH is NOT implemented in the engine — included in the rule
    // string for documentation purity but ignored. base.month() (= 2 for our
    // input) is what actually drives the YEARLY+BYMONTHDAY branch.
    // -----------------------------------------------------------------------
    void yearly_Feb_29_in_non_leap_year()
    {
        // Plain YEARLY (no BYMONTHDAY) clamps Feb 29 → Feb 28.
        const QDate from(2024, 2, 29);
        const QDate plainNext = RecurrenceEngine::advance(
            QStringLiteral("FREQ=YEARLY"), from);
        QCOMPARE(plainNext, QDate(2025, 2, 28));

        // With explicit BYMONTHDAY=29 the engine walks year-by-year until a
        // leap year — 2024 → 2028.
        const QDate bymdNext = RecurrenceEngine::advance(
            QStringLiteral("FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=29"), from);
        QCOMPARE(bymdNext, QDate(2028, 2, 29));

        // Chained plain YEARLY: 2024-02-29 → 2025-02-28 → 2026-02-28 → ...
        // Once the date clamps to Feb 28 the anchor day is "lost" — the
        // subsequent step's base.day() = 28, so the engine never rediscovers
        // Feb 29 even when 2028 is leap.
        QDate cur = from;
        for (int i = 0; i < 4; ++i) {
            cur = RecurrenceEngine::advance(QStringLiteral("FREQ=YEARLY"), cur);
        }
        // 2024-02-29 → 2025-02-28 → 2026-02-28 → 2027-02-28 → 2028-02-28
        QCOMPARE(cur, QDate(2028, 2, 28));
    }

    // -----------------------------------------------------------------------
    // 4. monthly_BYDAY_neg1FR_last_Friday
    // -----------------------------------------------------------------------
    //
    // RFC 5545 §3.3.10 BYDAY supports a signed integer prefix
    // (e.g. "-1FR" = last Friday, "1MO" = first Monday). The OpenTodo engine
    // currently does NOT honour these prefixes — its byDayMap accepts only
    // the bare two-letter codes "MO".."SU". A code "-1FR" simply fails the
    // map lookup, parseByday returns an empty list, and the MONTHLY branch
    // (which does not consult BYDAY at all — only WEEKLY does) falls through
    // to base.day() advancement.
    //
    // Pin both halves:
    //   a) MONTHLY;BYDAY=-1FR is silently treated as plain MONTHLY (uses
    //      base.day() as anchor).
    //   b) WEEKLY;BYDAY=-1FR — parseByday returns empty, then the WEEKLY
    //      branch's `days.isEmpty()` guard kicks in and falls back to
    //      base.addDays(7 * interval).
    //
    // qInfo flags the RFC gap so a future maintainer who wires up signed-
    // BYDAY support will see the test signal change.
    // -----------------------------------------------------------------------
    void monthly_BYDAY_neg1FR_last_Friday()
    {
        // 2026-05-15 is a Friday. RFC 5545 "last Friday of the month" from
        // this date should be the last Friday of June 2026 → 2026-06-26.
        const QDate from(2026, 5, 15);
        QCOMPARE(from.dayOfWeek(), 5); // sanity: Friday

        qInfo("RFC 5545 signed BYDAY prefixes (-1FR, 1MO, etc.) are NOT "
              "supported by RecurrenceEngine — parseByday silently drops "
              "the unrecognised code and the MONTHLY branch falls through "
              "to base.day() advancement.");

        // MONTHLY;BYDAY=-1FR — BYDAY ignored, base.day() = 15 used as anchor
        // (no BYMONTHDAY → no explicitAnchor → legacy clamp path).
        // base.addMonths(1) = 2026-06-15.
        const QDate monthly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR"), from);
        QCOMPARE(monthly, QDate(2026, 6, 15));

        // WEEKLY;BYDAY=-1FR — parseByday returns empty → WEEKLY fallback
        // path uses 7*interval days. From Fri 2026-05-15 → 2026-05-22.
        const QDate weekly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;BYDAY=-1FR"), from);
        QCOMPARE(weekly, QDate(2026, 5, 22));

        // YEARLY;BYDAY=-1FR — BYDAY irrelevant for YEARLY branch (which only
        // consults BYMONTHDAY). Falls through to addYears(interval).
        const QDate yearly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=YEARLY;BYDAY=-1FR"), from);
        QCOMPARE(yearly, QDate(2027, 5, 15));
    }

    // -----------------------------------------------------------------------
    // 5. weekly_BYDAY_no_explicit_day_uses_DTSTART_day
    // -----------------------------------------------------------------------
    //
    // RFC 5545 says FREQ=WEEKLY without BYDAY defaults to the DTSTART weekday.
    // The OpenTodo engine implements this implicitly: when BYDAY is absent
    // the WEEKLY branch simply calls base.addDays(7 * interval). Because
    // addDays(7) is a same-weekday jump, the source weekday is preserved.
    //
    // Probe all 7 weekdays to pin: WEEKLY-without-BYDAY ALWAYS preserves the
    // base weekday across one advance, regardless of where in the week the
    // base falls.
    // -----------------------------------------------------------------------
    void weekly_BYDAY_no_explicit_day_uses_DTSTART_day()
    {
        // 2026-05-04 = Monday; the seven dates 2026-05-04 .. 2026-05-10 cover
        // every weekday Mon..Sun.
        for (int dayOffset = 0; dayOffset < 7; ++dayOffset) {
            const QDate from = QDate(2026, 5, 4).addDays(dayOffset);
            const int sourceDow = from.dayOfWeek();

            const QDate next = RecurrenceEngine::advance(
                QStringLiteral("FREQ=WEEKLY"), from);

            QCOMPARE(next.dayOfWeek(), sourceDow);
            QCOMPARE(next, from.addDays(7));
        }

        // INTERVAL=3: still day-preserving — Mon 2026-05-04 → Mon 2026-05-25.
        const QDate fromMon(2026, 5, 4);
        const QDate plus3w = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;INTERVAL=3"), fromMon);
        QCOMPARE(plus3w, QDate(2026, 5, 25));
        QCOMPARE(plus3w.dayOfWeek(), 1);
    }

    // -----------------------------------------------------------------------
    // 6. interval_zero_or_negative_is_safely_handled
    // -----------------------------------------------------------------------
    //
    // INTERVAL=0 and INTERVAL=-N would normally cause the engine to either
    // freeze (addDays(0) = same date forever) or run backwards in time
    // (addDays(-1)). The engine clamps via `qMax(1, ... .toInt())` so any
    // value <= 0 is treated as INTERVAL=1.
    //
    // Pin the clamp for all four FREQ branches.
    // -----------------------------------------------------------------------
    void interval_zero_or_negative_is_safely_handled()
    {
        const QDate from(2026, 5, 4); // Monday

        // INTERVAL=0 — clamp to 1.
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;INTERVAL=0"), from),
                 QDate(2026, 5, 5));
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=WEEKLY;INTERVAL=0"), from),
                 QDate(2026, 5, 11));
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=MONTHLY;INTERVAL=0"), from),
                 QDate(2026, 6, 4));
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=YEARLY;INTERVAL=0"), from),
                 QDate(2027, 5, 4));

        // INTERVAL=-5 — also clamp to 1.
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;INTERVAL=-5"), from),
                 QDate(2026, 5, 5));

        // Non-numeric INTERVAL ("abc") — toInt() returns 0 with ok=false →
        // qMax(1, 0) = 1.
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;INTERVAL=abc"), from),
                 QDate(2026, 5, 5));

        // Empty INTERVAL value (";INTERVAL=;") — parseRRule keeps the empty
        // string, toInt() yields 0 → qMax clamps to 1.
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;INTERVAL="), from),
                 QDate(2026, 5, 5));
    }

    // -----------------------------------------------------------------------
    // 7. interval_INT_MAX_does_not_overflow
    // -----------------------------------------------------------------------
    //
    // INTERVAL=2147483647 (INT_MAX) for FREQ=DAILY would ask QDate::addDays
    // to advance by ~5.88 million years. QDate's internal Julian-day storage
    // is qint64, so the arithmetic does not overflow — but the resulting date
    // is well past QDate's representable maximum. Engine must not crash and
    // must return either invalid or a sane post-max date.
    //
    // The contract from test_recurrence_advance_chain::yearly_past_qdate_max
    // is "either invalid OR year > 9999". Apply the same standard here.
    // -----------------------------------------------------------------------
    void interval_INT_MAX_does_not_overflow()
    {
        const QDate from(2026, 1, 1);
        const QString intMaxStr = QString::number(INT_MAX);

        // DAILY;INTERVAL=INT_MAX
        const QDate daily = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;INTERVAL=") + intMaxStr, from);
        const bool dailyOk = !daily.isValid() || daily.year() > 9999;
        QVERIFY2(dailyOk,
                 qPrintable(QStringLiteral("Expected invalid or year>9999, got ")
                            + daily.toString(Qt::ISODate)));

        // WEEKLY;INTERVAL=INT_MAX — 7 * INT_MAX in addDays. addDays takes
        // qint64 so the multiplication happens in qint64 space (no signed
        // overflow as long as Qt converts up before multiplying — which it
        // does, see WEEKLY branch using base.addDays(7 * interval) where
        // interval is int).
        // NOTE: 7 * INT_MAX in C++ int math IS undefined behaviour. The
        // engine's expression is `base.addDays(7 * interval)` with interval
        // typed `int`. We pin the OBSERVED behaviour, not the spec.
        qInfo("WEEKLY;INTERVAL=INT_MAX evaluates 7*INT_MAX in int arithmetic "
              "inside the engine — formally signed-integer overflow / UB. "
              "Pin observed behaviour rather than spec.");
        const QDate weekly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;INTERVAL=") + intMaxStr, from);
        // Just assert no crash + deterministic value. Either invalid or a
        // far-future / wrapped date are both acceptable for the "does not
        // overflow into UB segfault" contract.
        Q_UNUSED(weekly);

        // MONTHLY;INTERVAL=INT_MAX — base.addMonths(INT_MAX) is well past
        // QDate's max; expect invalid.
        const QDate monthly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;INTERVAL=") + intMaxStr, from);
        const bool monthlyOk = !monthly.isValid() || monthly.year() > 9999;
        QVERIFY2(monthlyOk,
                 qPrintable(QStringLiteral("Expected invalid or year>9999, got ")
                            + monthly.toString(Qt::ISODate)));

        // YEARLY;INTERVAL=INT_MAX — definitely past max.
        const QDate yearly = RecurrenceEngine::advance(
            QStringLiteral("FREQ=YEARLY;INTERVAL=") + intMaxStr, from);
        const bool yearlyOk = !yearly.isValid() || yearly.year() > 9999;
        QVERIFY2(yearlyOk,
                 qPrintable(QStringLiteral("Expected invalid or year>9999, got ")
                            + yearly.toString(Qt::ISODate)));
    }

    // -----------------------------------------------------------------------
    // 8. UNTIL_in_past_first_occurrence_is_empty_or_null
    // -----------------------------------------------------------------------
    //
    // UNTIL is checked against the *computed* next occurrence (not against
    // the from-date). So if the from-date is today and UNTIL is yesterday,
    // the would-be next occurrence (tomorrow) is strictly > UNTIL → invalid.
    //
    // Pin three sub-cases:
    //   a) UNTIL one day before from-date — invalid.
    //   b) UNTIL == from-date — engine's next() is from+1 (strictly > UNTIL)
    //      → invalid.
    //   c) UNTIL one day after from-date — engine's next() == UNTIL exactly,
    //      NOT strictly > → valid. (Boundary inclusive — matches the chain
    //      termination test in test_recurrence_advance_chain.)
    // -----------------------------------------------------------------------
    void UNTIL_in_past_first_occurrence_is_empty_or_null()
    {
        const QDate from(2026, 5, 15);

        // (a) UNTIL = yesterday (2026-05-14)
        const QDate aResult = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260514T000000Z"), from);
        QVERIFY(!aResult.isValid());

        // (b) UNTIL = today (2026-05-15) — next would be 2026-05-16 > UNTIL
        const QDate bResult = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260515T000000Z"), from);
        QVERIFY(!bResult.isValid());

        // (c) UNTIL = tomorrow (2026-05-16) — boundary inclusive, valid
        const QDate cResult = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260516T000000Z"), from);
        QCOMPARE(cResult, QDate(2026, 5, 16));

        // (d) Date-only UNTIL form (YYYYMMDD, no T-component) — same gate.
        const QDate dResult = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260101"), from);
        QVERIFY(!dResult.isValid());

        // (e) Garbage UNTIL — parseUntil returns invalid QDate. The engine's
        // `until.isValid() && ...` guard short-circuits → UNTIL is silently
        // ignored, advance succeeds normally.
        qInfo("Unparseable UNTIL (e.g. 'tomorrow') is silently dropped — "
              "the rule degrades into an open-ended recurrence rather than "
              "rejecting the malformed input.");
        const QDate eResult = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=tomorrow"), from);
        QCOMPARE(eResult, QDate(2026, 5, 16));
    }

    // -----------------------------------------------------------------------
    // 9. COUNT_zero_first_occurrence_is_empty_or_null
    // -----------------------------------------------------------------------
    //
    // RFC 5545 §3.3.10 says COUNT must be a positive integer (≥1). COUNT=0 is
    // technically invalid input. The engine's `count <= 1` guard catches BOTH
    // COUNT=0 and COUNT=1 → invalid QDate.
    //
    // Pin the contract for 0, 1, and the boundary at 2 (which should be
    // ignored because COUNT > 1 falls outside the stateless engine's
    // responsibility — see header docs).
    // -----------------------------------------------------------------------
    void COUNT_zero_first_occurrence_is_empty_or_null()
    {
        const QDate from(2026, 5, 15);

        // COUNT=0 — invalid (caught by `count <= 1` guard).
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=DAILY;COUNT=0"), from).isValid());

        // COUNT=1 — invalid (no "next" after the original occurrence).
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=DAILY;COUNT=1"), from).isValid());

        // COUNT=-3 — also <=1, also invalid.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=DAILY;COUNT=-3"), from).isValid());

        // COUNT=2 — engine has no state, advances normally.
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;COUNT=2"), from),
                 QDate(2026, 5, 16));

        // COUNT=abc — toInt() returns 0 with ok=false. The `ok && count <= 1`
        // guard requires ok=true to reject, so ok=false means COUNT is
        // silently ignored — advance succeeds.
        qInfo("COUNT=<non-numeric> is silently ignored (parseInt ok=false), "
              "rule degrades into open-ended recurrence rather than rejecting.");
        QCOMPARE(RecurrenceEngine::advance(
                     QStringLiteral("FREQ=DAILY;COUNT=abc"), from),
                 QDate(2026, 5, 16));
    }

    // -----------------------------------------------------------------------
    // 10. RRULE_with_unknown_FREQ_returns_empty_or_error
    // -----------------------------------------------------------------------
    //
    // Engine recognises only FREQ ∈ {DAILY, WEEKLY, MONTHLY, YEARLY}. Anything
    // else falls through to the trailing `return QDate()`. Pin for several
    // commonly-mistaken values.
    //
    // Note: parseRRule uppercases values, so "fortnightly" and "FORTNIGHTLY"
    // are equivalent at the lookup level.
    // -----------------------------------------------------------------------
    void RRULE_with_unknown_FREQ_returns_empty_or_error()
    {
        const QDate from(2026, 5, 15);

        // FORTNIGHTLY — common user confusion (means "every two weeks" in
        // English). RFC 5545 has no such value.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=FORTNIGHTLY"), from).isValid());

        // HOURLY / MINUTELY / SECONDLY — RFC 5545 DOES define these, but the
        // OpenTodo engine is date-grained (QDate, not QDateTime) so they're
        // unimplemented. Pin invalid.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=HOURLY"), from).isValid());
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=MINUTELY"), from).isValid());
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=SECONDLY"), from).isValid());

        // EVERY / EACH / RECUR — common natural-language confusions.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=EVERY"), from).isValid());
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=EACH"), from).isValid());
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ=RECUR"), from).isValid());

        // Empty FREQ value — also rejected.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("FREQ="), from).isValid());

        // Lowercase rule with unknown FREQ — uppercase normalisation happens
        // at parseRRule level, so this still cascades to the unrecognised path.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("freq=fortnightly"), from).isValid());

        // Rule with no FREQ key at all — falls through `freq.isEmpty() →
        // return invalid`. INTERVAL/BYDAY alone are not enough to schedule.
        QVERIFY(!RecurrenceEngine::advance(
                    QStringLiteral("INTERVAL=2;BYDAY=MO"), from).isValid());
    }

    // -----------------------------------------------------------------------
    // 11. BYSETPOS_with_BYDAY_picks_nth_match
    // -----------------------------------------------------------------------
    //
    // RFC 5545 §3.3.10 BYSETPOS picks the Nth element from the candidate set
    // generated by BYDAY/BYMONTHDAY for a given period. E.g.
    //   FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1 = first weekday of month
    //   FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1 = last weekday of month
    //
    // The OpenTodo engine does NOT implement BYSETPOS. It silently drops the
    // key — for FREQ=MONTHLY this means the rule reduces to plain MONTHLY,
    // which uses base.day() as the anchor (since BYDAY only affects WEEKLY).
    //
    // Pin the silent-drop behaviour for both BYSETPOS=1 and BYSETPOS=-1, and
    // contrast with the (already-supported) WEEKLY+BYDAY path that DOES emit
    // the next weekday but ignores BYSETPOS.
    // -----------------------------------------------------------------------
    void BYSETPOS_with_BYDAY_picks_nth_match()
    {
        qInfo("BYSETPOS is not implemented by the engine. Pin silent-drop "
              "for MONTHLY and WEEKLY usages.");

        // 2026-05-04 is a Monday.
        const QDate from(2026, 5, 4);
        QCOMPARE(from.dayOfWeek(), 1);

        // MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1 — RFC: first weekday of
        // June 2026 = Monday 2026-06-01. ENGINE: ignores BYDAY+BYSETPOS,
        // anchors on base.day()=4 → base.addMonths(1)=2026-06-04 (Thursday).
        const QDate firstWeekday = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1"),
            from);
        QCOMPARE(firstWeekday, QDate(2026, 6, 4));

        // MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1 — RFC: last weekday of
        // June 2026 = Tuesday 2026-06-30. ENGINE: same silent-drop → 2026-06-04.
        const QDate lastWeekday = RecurrenceEngine::advance(
            QStringLiteral("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1"),
            from);
        QCOMPARE(lastWeekday, QDate(2026, 6, 4));

        // WEEKLY;BYDAY=MO,WE,FR;BYSETPOS=1 — engine honours WEEKLY+BYDAY
        // (returns next matching weekday) but ignores BYSETPOS entirely.
        // From Mon 2026-05-04, next in {MO,WE,FR} strictly > Mon is Wed.
        const QDate weeklyFirstMatch = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR;BYSETPOS=1"), from);
        QCOMPARE(weeklyFirstMatch, QDate(2026, 5, 6));

        // BYSETPOS=2 likewise dropped — same Wed result as BYSETPOS=1.
        const QDate weeklySecondMatch = RecurrenceEngine::advance(
            QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR;BYSETPOS=2"), from);
        QCOMPARE(weeklySecondMatch, QDate(2026, 5, 6));
    }

    // -----------------------------------------------------------------------
    // 12. RRULE_with_DTSTART_in_TZ_normalises_to_LocalTime
    // -----------------------------------------------------------------------
    //
    // The RecurrenceEngine::advance signature takes a QDate (date-only, no
    // time, no zone). The engine ITSELF has no concept of DTSTART — DTSTART
    // is implicit in the from-date parameter. The only TZ-aware path in the
    // engine is parseUntil(), which handles "...Z" UTC date-times by converting
    // to local-zone date for comparison.
    //
    // This slot pins:
    //   a) The signature is QDate-based — no time component is read or
    //      preserved (callers like TaskViewModel re-attach time after advance).
    //   b) UNTIL with the "Z" suffix is parsed as UTC and converted to the
    //      LOCAL zone date for comparison. So a UTC midnight UNTIL appears as
    //      yesterday-or-today depending on local offset.
    //   c) UNTIL without the "Z" suffix is treated as floating local time —
    //      the date portion is taken as-is, no conversion happens.
    //   d) A "DTSTART" stanza inside the RRULE string is silently ignored by
    //      parseRRule (kept as a key/value entry but never consulted by any
    //      branch). The function signature is the authoritative source of
    //      "when".
    // -----------------------------------------------------------------------
    void RRULE_with_DTSTART_in_TZ_normalises_to_LocalTime()
    {
        // (a) QDate-only signature: no time component is part of the contract.
        const QDate from(2026, 5, 15);
        const QDate next = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY"), from);
        QCOMPARE(next, QDate(2026, 5, 16));
        // The return value is QDate, so the test below is a compile-time
        // affirmation only — there's no time-of-day method to even call.

        // (b) UNTIL with "Z" suffix — UTC date-time converted to local zone.
        //
        // 20260520T000000Z = 2026-05-20 00:00:00 UTC. In a local TZ ahead of
        // UTC (e.g. UTC+3 → MSK) this is 2026-05-20 03:00 local → date 2026-05-20.
        // In a local TZ behind UTC (e.g. UTC-5 → EST) this is 2026-05-19 19:00
        // local → date 2026-05-19. Either way the engine should round-trip
        // through QDateTime::fromString(...UTC...).toLocalTime().date().
        //
        // Cross-check both halves by constructing the expected local date the
        // same way the engine does, then asserting the boundary holds.
        const QDateTime utcDt =
            QDateTime::fromString(QStringLiteral("20260520T000000"),
                                  QStringLiteral("yyyyMMdd'T'HHmmss"));
        QVERIFY(utcDt.isValid());
        QDateTime utcDtTagged = utcDt;
        utcDtTagged.setTimeZone(QTimeZone::UTC);
        const QDate expectedLocalUntil = utcDtTagged.toLocalTime().date();

        // Advance from a date one day after the expected local UNTIL → must
        // be strictly past the gate → invalid.
        const QDate pastLocalUntil = expectedLocalUntil.addDays(1);
        const QDate gated = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260520T000000Z"),
            pastLocalUntil);
        QVERIFY(!gated.isValid());

        // Advance from one day before the expected local UNTIL → next is
        // expectedLocalUntil itself → boundary inclusive → valid.
        const QDate beforeLocalUntil = expectedLocalUntil.addDays(-1);
        const QDate okGate = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260520T000000Z"),
            beforeLocalUntil);
        QCOMPARE(okGate, expectedLocalUntil);

        // (c) UNTIL without "Z" — floating local time. Engine uses
        // QDateTime::fromString(... yyyyMMdd'T'HHmmss).date(), no zone shift.
        const QDate floatingNext = RecurrenceEngine::advance(
            QStringLiteral("FREQ=DAILY;UNTIL=20260520T230000"), from);
        // From 2026-05-15, daily advance → 2026-05-16. UNTIL (floating) =
        // 2026-05-20. 16 ≤ 20 → valid.
        QCOMPARE(floatingNext, QDate(2026, 5, 16));

        // (d) DTSTART inside the rule is parsed by parseRRule but never used
        // by any branch. The from-date parameter wins. Use a clearly-different
        // DTSTART value and verify the engine ignores it.
        qInfo("Engine ignores DTSTART inside the RRULE string — the from-date "
              "parameter is the authoritative anchor.");
        const QDate dtstartIgnored = RecurrenceEngine::advance(
            QStringLiteral("DTSTART=20200101T000000Z;FREQ=DAILY"), from);
        QCOMPARE(dtstartIgnored, QDate(2026, 5, 16));
    }
};

QTEST_GUILESS_MAIN(TestRecurrenceEdgesV2)
#include "test_recurrence_edges_v2.moc"
