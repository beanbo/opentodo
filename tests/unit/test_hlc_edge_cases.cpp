// Edge-case test suite for HybridLogicalClock.
//
// Complements tests/unit/test_crdt.cpp (basic compare/parse/now coverage).
// Focuses on boundaries of the HLC implementation:
//
//   - Strict tight-loop monotonicity (≥1000 calls)
//   - Counter advances when many now() calls collide in the same ms
//   - compare() is a strict total order across (physMs, counter, deviceId)
//   - update() obeys the receive rule under every (l vs r vs phys) ordering
//   - Counter increment when same physMs+counter received from a different device
//   - parse() / toString() round-trip integrity
//   - parse() of malformed / empty / very-large strings doesn't crash
//   - Simulated remote-clock skew (forwards/backwards) via update()

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "sync/hlc.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static const QString kDevA = QStringLiteral("device-aaa");
static const QString kDevB = QStringLiteral("device-bbb");
static const QString kDevC = QStringLiteral("device-ccc");

// Build an HLC string from raw components matching the documented format
// "<phys_ms_16hex>:<ctr_8hex>:<deviceId>". Bypasses QSettings.
static QString makeHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

// ── Test class ───────────────────────────────────────────────────────────────

class TestHlcEdgeCases : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Use an isolated QSettings namespace so we don't trample real settings
        // and so our cleanup is local.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_HlcEdge"));
        QCoreApplication::setApplicationName(QStringLiteral("test_hlc_edge_cases"));

        // Clear any previously-persisted last_hlc so process-wide HLC state
        // starts from a known-blank slate. We can only influence the
        // *persisted* value here — the in-process s_lastPhysMs / s_lastCounter
        // statics live for the test run and naturally accumulate.
        QSettings settings;
        settings.remove(QStringLiteral("sync/last_hlc"));
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.remove(QStringLiteral("sync/last_hlc"));
    }

    // =========================================================================
    // Monotonicity & uniqueness
    // =========================================================================

    // ── Tight-loop: 1000 successive now() calls strictly increasing ─────────
    void now_tightLoop_strictlyIncreasing()
    {
        const int N = 1000;
        QStringList timestamps;
        timestamps.reserve(N);

        for (int i = 0; i < N; ++i) {
            timestamps.append(HybridLogicalClock::now(kDevA));
        }

        // Each successive call must be STRICTLY greater than the previous —
        // even within a single millisecond, the counter must advance.
        for (int i = 1; i < N; ++i) {
            const int cmp = HybridLogicalClock::compare(timestamps[i - 1],
                                                        timestamps[i]);
            QVERIFY2(cmp < 0,
                     qPrintable(QStringLiteral("HLC #%1 (%2) was not strictly "
                                               "less than HLC #%3 (%4)")
                                .arg(i - 1).arg(timestamps[i - 1])
                                .arg(i).arg(timestamps[i])));
        }

        // All values must be unique.
        const int uniqueCount = QSet<QString>(timestamps.begin(),
                                              timestamps.end()).size();
        QCOMPARE(uniqueCount, N);
    }

    // ── now() comparable & monotonic across two devices using same instance ─
    void now_twoDevices_perInstanceMonotone()
    {
        // The HybridLogicalClock implementation uses static process-wide state,
        // so "two HLC instances with the same deviceId" really means two
        // sequences of now() calls. Each call must remain ≥ all prior calls.
        const QString a1 = HybridLogicalClock::now(kDevA);
        const QString a2 = HybridLogicalClock::now(kDevA);
        const QString b1 = HybridLogicalClock::now(kDevA); // same device
        const QString b2 = HybridLogicalClock::now(kDevA);

        QVERIFY(HybridLogicalClock::compare(a1, a2) < 0);
        QVERIFY(HybridLogicalClock::compare(a2, b1) < 0);
        QVERIFY(HybridLogicalClock::compare(b1, b2) < 0);
        // Transitive: a1 < b2.
        QVERIFY(HybridLogicalClock::compare(a1, b2) < 0);
    }

    // ── Counter advances when multiple now() calls land in the same ms ──────
    void now_sameMillisecond_counterAdvances()
    {
        // Take a burst of timestamps fast enough that at least two will share
        // a physical-ms value. We assert that any two adjacent timestamps with
        // identical physMs have strictly-increasing counter.
        const int burst = 200;
        QStringList ts;
        ts.reserve(burst);
        for (int i = 0; i < burst; ++i)
            ts.append(HybridLogicalClock::now(kDevA));

        bool foundCollision = false;
        for (int i = 1; i < burst; ++i) {
            const auto a = HybridLogicalClock::parse(ts[i - 1]);
            const auto b = HybridLogicalClock::parse(ts[i]);
            QVERIFY(a.valid && b.valid);
            if (a.physMs == b.physMs) {
                foundCollision = true;
                QVERIFY2(b.counter > a.counter,
                         "Counter must strictly advance within same ms");
            } else {
                QVERIFY(b.physMs > a.physMs);
            }
        }
        // It would be a freak machine if 200 now() calls all landed in
        // distinct milliseconds. We don't fail if foundCollision is false —
        // just record it for debugging.
        Q_UNUSED(foundCollision);
    }

    // =========================================================================
    // compare() ordering
    // =========================================================================

    // ── Earlier physMs → smaller ────────────────────────────────────────────
    void compare_physMs_dominates()
    {
        const QString a = makeHlc(1000, 9999, kDevC); // larger ctr, larger dev
        const QString b = makeHlc(1001, 0,    kDevA); // smaller ctr, smaller dev
        QCOMPARE(HybridLogicalClock::compare(a, b), -1);
        QCOMPARE(HybridLogicalClock::compare(b, a), +1);
    }

    // ── Same physMs, smaller counter → smaller ──────────────────────────────
    void compare_counter_dominates_devId()
    {
        // counter dominates over deviceId tiebreak.
        const QString a = makeHlc(1000, 0, QStringLiteral("zzz")); // big devId
        const QString b = makeHlc(1000, 1, QStringLiteral("aaa")); // small devId
        QCOMPARE(HybridLogicalClock::compare(a, b), -1);
        QCOMPARE(HybridLogicalClock::compare(b, a), +1);
    }

    // ── Same physMs+counter, smaller deviceId (lexicographic) → smaller ─────
    void compare_deviceId_lexicographicTiebreak()
    {
        const QString a = makeHlc(1000, 5, QStringLiteral("aaa"));
        const QString b = makeHlc(1000, 5, QStringLiteral("aab"));
        const QString c = makeHlc(1000, 5, QStringLiteral("zzz"));

        QVERIFY(HybridLogicalClock::compare(a, b) < 0);
        QVERIFY(HybridLogicalClock::compare(b, c) < 0);
        QVERIFY(HybridLogicalClock::compare(a, c) < 0);
        // Reverse direction.
        QVERIFY(HybridLogicalClock::compare(c, a) > 0);
    }

    // ── Equal HLCs compare to 0 ─────────────────────────────────────────────
    void compare_equal_returnsZero()
    {
        const QString a = makeHlc(12345, 67, kDevA);
        const QString b = makeHlc(12345, 67, kDevA);
        QCOMPARE(HybridLogicalClock::compare(a, b), 0);
        QCOMPARE(HybridLogicalClock::compare(a, a), 0);
    }

    // ── Total order: every pair of distinct HLCs has a deterministic winner ─
    void compare_isTotalOrder()
    {
        // A varied set of HLCs touching every coordinate.
        QStringList hlcs;
        hlcs << makeHlc(100, 0, QStringLiteral("aaa"))
             << makeHlc(100, 0, QStringLiteral("bbb"))
             << makeHlc(100, 1, QStringLiteral("aaa"))
             << makeHlc(100, 1, QStringLiteral("bbb"))
             << makeHlc(200, 0, QStringLiteral("aaa"))
             << makeHlc(200, 5, QStringLiteral("zzz"));

        for (const auto &x : hlcs) {
            // Reflexivity.
            QCOMPARE(HybridLogicalClock::compare(x, x), 0);
            for (const auto &y : hlcs) {
                if (x == y) continue;
                const int cxy = HybridLogicalClock::compare(x, y);
                const int cyx = HybridLogicalClock::compare(y, x);
                // Antisymmetry: cxy and cyx have opposite signs and neither 0.
                QVERIFY(cxy != 0);
                QVERIFY((cxy < 0 && cyx > 0) || (cxy > 0 && cyx < 0));

                // Transitivity: x < y < z → x < z.
                for (const auto &z : hlcs) {
                    if (z == x || z == y) continue;
                    const int cyz = HybridLogicalClock::compare(y, z);
                    const int cxz = HybridLogicalClock::compare(x, z);
                    if (cxy < 0 && cyz < 0) QVERIFY(cxz < 0);
                    if (cxy > 0 && cyz > 0) QVERIFY(cxz > 0);
                }
            }
        }
    }

    // =========================================================================
    // update() (incoming HLC)
    // =========================================================================

    // ── update with larger remote physMs → local catches up ─────────────────
    void update_remoteFuture_catchesUp()
    {
        // Remote is 60s in the future relative to wall clock.
        const quint64 nowMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        const quint64 futureMs = nowMs + 60'000;
        const QString remote = makeHlc(futureMs, 0, kDevB);

        const QString local  = HybridLogicalClock::now(kDevA);
        const QString result = HybridLogicalClock::update(local, remote, kDevA);

        const auto rc = HybridLogicalClock::parse(result);
        QVERIFY(rc.valid);
        QCOMPARE(rc.physMs, futureMs);
        // Counter for path "maxPhys == rPhys (only)" → c_remote + 1 = 1.
        QCOMPARE(rc.counter, quint32(1));
        QCOMPARE(rc.deviceId, kDevA); // result is stamped with our devId

        // result strictly greater than the remote we received.
        QVERIFY(HybridLogicalClock::compare(result, remote) > 0);
    }

    // ── update with smaller remote physMs → local takes max(local, phys_now)
    void update_remotePast_localUnchangedOrAdvancesBy_physOrLocal()
    {
        // Build a local with phys==now+10s, so phys_now is below local AND
        // remote is far below local. The receive rule then picks lPhys.
        const quint64 nowMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());

        const QString local  = makeHlc(nowMs + 10'000, 7, kDevA);
        const QString remote = makeHlc(nowMs - 60'000, 99, kDevB);

        const QString result = HybridLogicalClock::update(local, remote, kDevA);
        const auto rc = HybridLogicalClock::parse(result);
        QVERIFY(rc.valid);

        // maxPhys == lPhys (local future) → counter = lCtr + 1 = 8.
        QCOMPARE(rc.physMs, nowMs + 10'000);
        QCOMPARE(rc.counter, quint32(8));
        QCOMPARE(rc.deviceId, kDevA);

        // Result strictly greater than local (counter advanced).
        QVERIFY(HybridLogicalClock::compare(result, local) > 0);
        // Result strictly greater than remote.
        QVERIFY(HybridLogicalClock::compare(result, remote) > 0);
    }

    // ── update with equal physMs but larger remote counter → local catches up
    void update_samePhys_largerRemoteCounter_catchesUp()
    {
        // Use a phys far in the future so phys_now can't dominate.
        const quint64 farFutureMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) + 3'600'000;
        const QString local  = makeHlc(farFutureMs, 3, kDevA);
        const QString remote = makeHlc(farFutureMs, 7, kDevB);

        const QString result = HybridLogicalClock::update(local, remote, kDevA);
        const auto rc = HybridLogicalClock::parse(result);
        QVERIFY(rc.valid);

        // maxPhys == lPhys == rPhys → counter = max(lCtr, rCtr) + 1 = 8.
        QCOMPARE(rc.physMs, farFutureMs);
        QCOMPARE(rc.counter, quint32(8));
        QCOMPARE(rc.deviceId, kDevA);

        QVERIFY(HybridLogicalClock::compare(result, remote) > 0);
        QVERIFY(HybridLogicalClock::compare(result, local)  > 0);
    }

    // ── update with same physMs+counter on a different device → ctr += 1 ────
    void update_samePhysSameCounter_differentDevice_counterIncrements()
    {
        const quint64 farFutureMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) + 3'600'000;

        const QString local  = makeHlc(farFutureMs, 5, kDevA);
        const QString remote = makeHlc(farFutureMs, 5, kDevB); // same phys+ctr

        const QString result = HybridLogicalClock::update(local, remote, kDevA);
        const auto rc = HybridLogicalClock::parse(result);
        QVERIFY(rc.valid);

        // maxPhys == lPhys == rPhys, max(lCtr, rCtr) = 5 → newCtr = 6.
        QCOMPARE(rc.physMs, farFutureMs);
        QCOMPARE(rc.counter, quint32(6));
        QCOMPARE(rc.deviceId, kDevA);
    }

    // =========================================================================
    // parse() / toString() round-trip
    // =========================================================================

    // ── Round-trip: format produced by makeHlc parses back to identical comps
    void parse_roundtrip_basic()
    {
        const quint64 phys = 0x000001ABCDEF0123ull;
        const quint32 ctr  = 0xDEADBEEFu;
        const QString dev  = QStringLiteral("device-roundtrip-1");
        const QString s    = makeHlc(phys, ctr, dev);

        const auto c = HybridLogicalClock::parse(s);
        QVERIFY(c.valid);
        QCOMPARE(c.physMs,   phys);
        QCOMPARE(c.counter,  ctr);
        QCOMPARE(c.deviceId, dev);

        // Re-formatting via makeHlc must reproduce the exact original string.
        QCOMPARE(makeHlc(c.physMs, c.counter, c.deviceId), s);
    }

    // ── Round-trip: now()'s output round-trips through parse() ──────────────
    void parse_roundtrip_fromNow()
    {
        const QString live = HybridLogicalClock::now(kDevA);
        const auto c = HybridLogicalClock::parse(live);
        QVERIFY(c.valid);
        QCOMPARE(c.deviceId, kDevA);

        const QString rebuilt = makeHlc(c.physMs, c.counter, c.deviceId);
        QCOMPARE(rebuilt, live);
        QCOMPARE(HybridLogicalClock::compare(rebuilt, live), 0);
    }

    // ── parse("invalid") returns invalid Components and doesn't crash ───────
    void parse_invalidString_returnsInvalid()
    {
        const auto c = HybridLogicalClock::parse(QStringLiteral("invalid"));
        QVERIFY(!c.valid);
    }

    // ── parse("") returns invalid ───────────────────────────────────────────
    void parse_emptyString_returnsInvalid()
    {
        const auto c = HybridLogicalClock::parse(QString());
        QVERIFY(!c.valid);
        const auto c2 = HybridLogicalClock::parse(QStringLiteral(""));
        QVERIFY(!c2.valid);
    }

    // ── parse() of malformed counter / phys parts → invalid, no crash ───────
    void parse_malformedCounter_gracefulFailure()
    {
        // Bad counter (non-hex 'g').
        const auto a = HybridLogicalClock::parse(
            QStringLiteral("00000000000003e8:000000gg:dev"));
        QVERIFY(!a.valid);

        // Bad phys (non-hex 'z').
        const auto b = HybridLogicalClock::parse(
            QStringLiteral("z000000000000000:00000001:dev"));
        QVERIFY(!b.valid);

        // Only one colon → no second separator → invalid.
        const auto c = HybridLogicalClock::parse(QStringLiteral("000:001"));
        QVERIFY(!c.valid);

        // No colons at all → invalid.
        const auto d = HybridLogicalClock::parse(QStringLiteral("abcdef"));
        QVERIFY(!d.valid);
    }

    // ── parse() of very large physMs (far-future, year-9999-ish) handled ────
    void parse_veryLargePhysMs_handled()
    {
        // Year 9999 ≈ 253402300799000 ms ≈ 0xE677D21FDC58 (well under 2^64).
        const quint64 year9999Ms = 253402300799000ull;
        const QString s = makeHlc(year9999Ms, 42, kDevA);

        const auto c = HybridLogicalClock::parse(s);
        QVERIFY(c.valid);
        QCOMPARE(c.physMs,   year9999Ms);
        QCOMPARE(c.counter,  quint32(42));
        QCOMPARE(c.deviceId, kDevA);

        // It must still order *after* "now".
        const QString nowStr = HybridLogicalClock::now(kDevB);
        QVERIFY(HybridLogicalClock::compare(s, nowStr) > 0);
    }

    // ── parse() handles a phys close to UINT64_MAX without overflow ─────────
    void parse_extremePhysMs_handled()
    {
        // Just below max — should still parse cleanly.
        const quint64 huge = 0xFFFFFFFFFFFFFFF0ull;
        const QString s = makeHlc(huge, 0, kDevA);
        const auto c = HybridLogicalClock::parse(s);
        QVERIFY(c.valid);
        QCOMPARE(c.physMs, huge);
    }

    // =========================================================================
    // Clock skew (simulated via update())
    // =========================================================================

    // ── Backwards skew: simulated remote in the past must NOT roll us back ──
    void clockSkew_backwards_doesNotRegress()
    {
        // Start from a confirmed "now" snapshot.
        const QString t1 = HybridLogicalClock::now(kDevA);

        // Receive a remote whose physMs is far in the past — this simulates a
        // remote whose system clock has jumped backwards (or one that just
        // hadn't ticked recently).
        const QString remotePast =
            makeHlc(/*1970-ish*/ 0x0000000000000064ull /*=100ms*/, 0, kDevB);

        const QString updated = HybridLogicalClock::update(t1, remotePast, kDevA);

        // The updated HLC must NOT be smaller than t1 — even though remote is
        // ancient, the receive rule preserves the local high-water mark.
        QVERIFY(HybridLogicalClock::compare(updated, t1) >= 0);

        // A subsequent now() must STILL be ≥ updated.
        const QString t2 = HybridLogicalClock::now(kDevA);
        QVERIFY(HybridLogicalClock::compare(t2, updated) >= 0);
    }

    // ── Forward skew within budget — local catches up to remote.
    //
    // Q2.18-E changed the contract: a remote HLC more than 1 hour ahead of
    // the local WALL CLOCK is REJECTED (clock-skew defence). Within-budget
    // remotes still merge normally.
    //
    // Robustness note: the budget check inside `update()` compares
    // `cr.physMs` against the WALL CLOCK at update() call time, NOT against
    // any prior `now()` result. So we must construct `remote` relative to
    // wallNow, not relative to local0 — otherwise accumulated process-state
    // can push remote past the budget and trigger an unintended reject.
    void clockSkew_forward_localCatchesUp()
    {
        const quint64 wallNow = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        // 30 seconds ahead of the WALL CLOCK — guaranteed within the 60-min
        // skew budget. Tiny offset so we don't accidentally bake in a
        // larger value that could conflict with accumulated state.
        const quint64 farMs = wallNow + 30ull * 1000ull;
        const QString remote = makeHlc(farMs, 12, kDevB);

        const QString local0 = HybridLogicalClock::now(kDevA);

        const QString afterUpdate =
            HybridLogicalClock::update(local0, remote, kDevA);
        const auto comps = HybridLogicalClock::parse(afterUpdate);
        QVERIFY(comps.valid);

        // After the update, the HLC must be ≥ local0 (no regression) AND
        // ≥ remote (caught up, if remote was ahead of local0). The clamp
        // is NOT triggered because farMs - wallNow == 30 s « 60 min.
        QVERIFY(HybridLogicalClock::compare(afterUpdate, local0) >= 0);
        if (HybridLogicalClock::compare(remote, local0) > 0) {
            QVERIFY2(HybridLogicalClock::compare(afterUpdate, remote) > 0,
                     "When remote > local0 AND within budget, local must catch up");
        }

        // Subsequent now() must still be ≥ afterUpdate (process-wide max).
        const QString local1 = HybridLogicalClock::now(kDevA);
        QVERIFY(HybridLogicalClock::compare(local1, afterUpdate) >= 0);
    }

    // ── NEW: forward skew BEYOND budget — remote is rejected (Q2.18-E).
    //
    // A remote with physMs > wallNow + 1 hour is treated as a clock-skew
    // attack / accident and rejected. The defender returns the local HLC
    // string unchanged.
    void clockSkew_forward_beyondBudget_rejected()
    {
        const quint64 wallNow = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        // 10 hours ahead of wall clock — far past the 1-hour budget.
        const quint64 farMs = wallNow + 10ull * 3600ull * 1000ull;
        const QString remote = makeHlc(farMs, 12, kDevB);

        const QString local0 = HybridLogicalClock::now(kDevA);
        const auto localComps = HybridLogicalClock::parse(local0);
        QVERIFY(localComps.valid);

        const QString afterUpdate =
            HybridLogicalClock::update(local0, remote, kDevA);

        // The update must return localHlc UNCHANGED — local did not absorb
        // the poisoned remote phys.
        QCOMPARE(afterUpdate, local0);
    }

    // =========================================================================
    // isWithinAcceptableSkew() — defence-in-depth helper used by Merger
    // =========================================================================

    // ── Within budget → true ─────────────────────────────────────────────────
    void isWithinAcceptableSkew_inBudget_true()
    {
        const quint64 wallNow = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        // 30 minutes ahead — well under 1 hour budget.
        const QString s = makeHlc(wallNow + 30ull * 60ull * 1000ull, 0, kDevA);
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(s));
    }

    // ── Past timestamps are always acceptable (we only clamp forward skew) ──
    void isWithinAcceptableSkew_past_alwaysTrue()
    {
        // Year-1970-ish.
        const QString s = makeHlc(100ull, 0, kDevA);
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(s));
    }

    // ── Just over 1 hour ahead → false (boundary check) ──────────────────────
    void isWithinAcceptableSkew_overBudget_false()
    {
        const quint64 wallNow = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        // 65 minutes ahead — just over the 60-min budget.
        const quint64 farMs = wallNow + 65ull * 60ull * 1000ull;
        const QString s = makeHlc(farMs, 0, kDevA);
        QVERIFY(!HybridLogicalClock::isWithinAcceptableSkew(s));
    }

    // ── Year-2099 → false (extreme forward skew) ─────────────────────────────
    void isWithinAcceptableSkew_year2099_false()
    {
        const QString s = makeHlc(4070000000000ull, 0, kDevA);
        QVERIFY(!HybridLogicalClock::isWithinAcceptableSkew(s));
    }

    // ── Custom budget honoured ───────────────────────────────────────────────
    void isWithinAcceptableSkew_customBudget()
    {
        const quint64 wallNow = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        // 5 minutes ahead. Default budget (1 hour) → accept. 1-min budget → reject.
        const QString s = makeHlc(wallNow + 5ull * 60ull * 1000ull, 0, kDevA);
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(s));
        QVERIFY(!HybridLogicalClock::isWithinAcceptableSkew(s, /*maxSkewMs=*/60'000));
    }

    // ── Malformed / empty → true (treated as not-out-of-range; other
    //    layers handle malformed strings) ──────────────────────────────────────
    void isWithinAcceptableSkew_malformed_returnsTrue()
    {
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(QString()));
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(QStringLiteral("garbage")));
        QVERIFY(HybridLogicalClock::isWithinAcceptableSkew(QStringLiteral("not-a-hlc")));
    }

    // ── Forward skew followed by a "regular" remote → still monotone ─────────
    void clockSkew_forwardThenNormal_stillMonotone()
    {
        // Push the local clock far forward via a forward-skewed remote.
        const quint64 nowMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        const quint64 farMs = nowMs + 86'400'000ull; // +1 day
        const QString skewed = makeHlc(farMs, 0, kDevB);

        const QString a = HybridLogicalClock::now(kDevA);
        const QString b = HybridLogicalClock::update(a, skewed, kDevA);

        // Now feed a "normal" remote at present time. Result must still be ≥ b.
        const QString normal = makeHlc(nowMs, 0, kDevC);
        const QString c = HybridLogicalClock::update(b, normal, kDevA);
        QVERIFY(HybridLogicalClock::compare(c, b) >= 0);
        QVERIFY(HybridLogicalClock::compare(c, normal) > 0);

        // And finally, a fresh now() must remain monotone.
        const QString d = HybridLogicalClock::now(kDevA);
        QVERIFY(HybridLogicalClock::compare(d, c) >= 0);
    }
};

QTEST_GUILESS_MAIN(TestHlcEdgeCases)
#include "test_hlc_edge_cases.moc"
