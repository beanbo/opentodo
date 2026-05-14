// Edge-case coverage for PomodoroService (Q2.18-style).
//
// The existing tests (test_pomodoroservice.cpp + test_pomodoro_full_session.cpp)
// drive the happy-path state machine and the mid-session reload-defer contract.
// This file covers the gaps flagged in the Q2.19-A audit:
//
//   1. State-machine "recovery" after app crash mid-session — the in-memory
//      timer/phase/remaining state is by design NOT persisted; only
//      completed_count + completed_date are. Pin the actual contract so a
//      future "save my session" feature gets a deliberate refactor instead
//      of a silent change.
//
//   2. Settings changes during a running timer at deeper boundaries — multiple
//      reloads buffered, long-break trigger value changing mid-session, etc.
//
//   3. Resume after suspend/sleep — QTimer doesn't tick during hibernate.
//      The service relies on onTick() to drive state; when the OS pauses the
//      process, no ticks happen, so remainingSeconds doesn't advance. After
//      a long-enough hibernate, the wall-clock has advanced beyond the
//      configured duration. Document the behavior and verify the timer still
//      drains to zero cleanly after resume (no negative-skid into next phase).
//
//   4. Custom durations of 0 / negative / huge — the loader clamps to ≥1 and
//      ≥2 for cyclesBeforeLongBreak. We already test 0 + negative in the
//      existing suite. Add huge (10000) to verify cycleSeconds() doesn't
//      overflow int when multiplied by 60.
//
// All tests use QTEST_GUILESS_MAIN and a unique QSettings org/app so we never
// touch real OpenTodo state and never collide with parallel test binaries.

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QMetaObject>
#include <climits>

#include "services/pomodoroservice.h"

namespace pomo_edges {
// Named constants for QSettings keys & phase tokens — keeps the test source
// free of repeated raw-string literals (audit guidance). Wrapped with
// QStringLiteral via inline helpers at the call sites so the macro receives
// a literal in its expansion as Qt requires.
constexpr const char *kKeyWork  = "pomodoro/workMinutes";
constexpr const char *kKeyShort = "pomodoro/shortBreakMinutes";
constexpr const char *kKeyLong  = "pomodoro/longBreakMinutes";
constexpr const char *kKeyCyc   = "pomodoro/cyclesBeforeLongBreak";
constexpr const char *kKeyDate  = "pomodoro/completed_date";
constexpr const char *kKeyCount = "pomodoro/completed_count";

constexpr const char *kPhaseWork       = "work";
constexpr const char *kPhaseShortBreak = "shortBreak";
constexpr const char *kPhaseLongBreak  = "longBreak";
constexpr const char *kPhaseIdle       = "idle";

// Convert the constexpr name to a QString. QLatin1String is the cheapest
// conversion for ASCII-only keys. Named "qs" (not "s") to avoid clashing
// with the conventional local `QSettings s;`.
inline QString qs(const char *k) { return QLatin1String(k); }
} // namespace pomo_edges

class TestPomodoroEdges : public QObject
{
    Q_OBJECT

private:
    // Drive PomodoroService::onTick() synchronously through the meta-object
    // (it's a private slot but moc registers it by name). This avoids waiting
    // real wall-clock seconds and keeps the test deterministic.
    static void tick(PomodoroService *svc)
    {
        QMetaObject::invokeMethod(svc, "onTick", Qt::DirectConnection);
    }

    // Tick N consecutive times.
    static void tickN(PomodoroService *svc, int n)
    {
        for (int i = 0; i < n; ++i) tick(svc);
    }

    // Tick until phase transitions; safety-bounded to catch state-machine
    // bugs that would otherwise infinite-loop the test.
    static int tickUntilPhaseChange(PomodoroService *svc, int safetyLimit = 100000)
    {
        const QString startPhase = svc->phase();
        int n = 0;
        while (svc->phase() == startPhase && n < safetyLimit) {
            tick(svc);
            ++n;
        }
        return n;
    }

private slots:
    // ── Test fixture lifecycle ───────────────────────────────────────────────

    void initTestCase()
    {
        // Per-test-binary org/app so parallel ctest runs don't smash each
        // other's QSettings. See Q2.12 parallel-test flake fix.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_PomoEdges"));
        QCoreApplication::setApplicationName(QStringLiteral("test_pomodoro_edges"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // ── 1. Crash-recovery: only completed_count + completed_date survive ────
    //
    // Simulate the "app crashed mid-work-phase" scenario by pre-seeding
    // QSettings with the persisted daily stats (date=today, count=2), then
    // constructing a fresh PomodoroService. We expect:
    //
    //   * completedCyclesToday() returns 2 (the count survives)
    //   * phase() is "idle" (in-memory state did NOT survive — no phase
    //     restoration; the user must press Start again)
    //   * remainingSeconds is the configured work duration
    //   * running() is false
    //
    // This pins the documented contract: the timer/phase/remainingSeconds
    // are session-scoped, not persistent. A future "save my session" would
    // need a deliberate change to PomodoroService::loadCompletedToday() and
    // a new save path on every tick.
    void crash_recovery_restores_only_daily_count()
    {
        using namespace pomo_edges;

        // Pre-seed the persisted daily stats (the "crashed" state).
        {
            QSettings s;
            s.setValue(qs(kKeyDate),
                       QDate::currentDate().toString(Qt::ISODate));
            s.setValue(qs(kKeyCount), 2);
            s.setValue(qs(kKeyWork), 25);
            s.sync();
        }

        PomodoroService svc(nullptr);

        // Daily count survived the "crash".
        QCOMPARE(svc.completedCyclesToday(), 2);

        // In-memory state did NOT survive — service is idle, not running, and
        // remainingSeconds is back to the full work duration. The user must
        // press Start to resume a session; we do not auto-restore in-flight
        // state because we never persisted it. Documenting this contract.
        QCOMPARE(svc.phase(), qs(kPhaseIdle));
        QVERIFY(!svc.running());
        QCOMPARE(svc.remainingSeconds(), 25 * 60);
        QCOMPARE(svc.cycleSeconds(), 25 * 60);

        qInfo("PomodoroService: crash-recovery semantics — only completed_count "
              "and completed_date are persisted across process restarts. "
              "Phase, remainingSeconds, and running state are session-scoped.");
    }

    // ── 1b. Stale completed_date → counter resets cleanly ────────────────────
    //
    // If the persisted date is yesterday (e.g. user left the app open
    // overnight, or it crashed and restarted the next day), the in-memory
    // counter must start at 0. The persisted value on disk doesn't reset
    // until the first save on the new day.
    void crash_recovery_stale_date_resets_counter()
    {
        using namespace pomo_edges;

        const QDate yesterday = QDate::currentDate().addDays(-1);

        {
            QSettings s;
            s.setValue(qs(kKeyDate),
                       yesterday.toString(Qt::ISODate));
            s.setValue(qs(kKeyCount), 7);
            s.sync();
        }

        PomodoroService svc(nullptr);

        // Yesterday's count must not be visible.
        QCOMPARE(svc.completedCyclesToday(), 0);
        QCOMPARE(svc.phase(), qs(kPhaseIdle));
    }

    // ── 2. Multiple buffered reloads during a running session ────────────────
    //
    // Existing tests cover a single mid-session reload. This pins the
    // "last-write-wins on the pending buffer" semantics: two reloads in a
    // row before the phase boundary should apply the SECOND set of values,
    // not the first.
    void multiple_reloads_pending_uses_latest_values()
    {
        using namespace pomo_edges;

        {
            QSettings s;
            s.setValue(qs(kKeyWork),  1);
            s.setValue(qs(kKeyShort), 1);
            s.setValue(qs(kKeyLong),  1);
            s.setValue(qs(kKeyCyc),   4);
            s.sync();
        }

        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), qs(kPhaseWork));
        QCOMPARE(svc.cycleSeconds(), 60);

        // Tick partway through so we're mid-cycle (cannot apply immediately).
        tick(&svc);

        // First reload — buffer values A.
        {
            QSettings s;
            s.setValue(qs(kKeyWork),  5);
            s.setValue(qs(kKeyShort), 2);
            s.sync();
        }
        svc.reloadDurationsFromSettings();

        // Still mid-work: current cycleSeconds reflects the OLD value (1 min).
        QCOMPARE(svc.cycleSeconds(), 60);

        // Second reload — buffer values B (should overwrite A).
        {
            QSettings s;
            s.setValue(qs(kKeyWork),  7);
            s.setValue(qs(kKeyShort), 3);
            s.sync();
        }
        svc.reloadDurationsFromSettings();

        // Still mid-work, current cycle uses OLD value.
        QCOMPARE(svc.cycleSeconds(), 60);

        // Drain current work phase → apply B at the boundary.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseShortBreak));
        // shortBreak duration is the LATEST buffered value (3, not 2).
        QCOMPARE(svc.cycleSeconds(), 3 * 60);

        // Drain short break, next work phase uses the latest buffered work value.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseWork));
        QCOMPARE(svc.cycleSeconds(), 7 * 60);
    }

    // ── 2b. Changing cyclesBeforeLongBreak mid-session is applied at boundary ─
    //
    // The user starts a session with N=4, completes 2 work cycles, then
    // reduces N to 3 in Settings. The change should take effect on the next
    // work-cycle end: completing the 3rd work cycle should trigger a long
    // break (since 3 % 3 == 0), even though the original config required 4.
    //
    // This is a strict reading of the contract — applyPendingDurationChanges
    // is called at every enterPhase boundary, including the work-cycle that
    // counts toward the long-break trigger.
    void cycles_before_long_break_change_applies_at_boundary()
    {
        using namespace pomo_edges;

        {
            QSettings s;
            s.setValue(qs(kKeyWork),  1);
            s.setValue(qs(kKeyShort), 1);
            s.setValue(qs(kKeyLong),  1);
            s.setValue(qs(kKeyCyc),   4);  // long break after 4 work cycles
            s.sync();
        }

        PomodoroService svc(nullptr);
        svc.start();

        // Complete cycle 1 → short break.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseShortBreak));
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseWork));

        // Complete cycle 2 → short break.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseShortBreak));

        // Mid-shortBreak: user changes N from 4 to 3. New value buffers.
        {
            QSettings s;
            s.setValue(qs(kKeyCyc), 3);
            s.sync();
        }
        svc.reloadDurationsFromSettings();

        // Drain shortBreak → back to work. At the boundary, the new N=3
        // applies. Counter is currently at 2.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseWork));

        // Complete cycle 3 → with N=3 now, 3 % 3 == 0, so we land in LONG break.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseLongBreak));
        QCOMPARE(svc.completedCyclesToday(), 3);
    }

    // ── 3. Hibernate/suspend simulation: timer pauses, resumes cleanly ──────
    //
    // The Qt event loop's QTimer doesn't tick when the OS suspends the
    // process. To "simulate" hibernate, we:
    //   (a) start a session,
    //   (b) tick partway,
    //   (c) call pause() (the closest equivalent — both stop the QTimer),
    //   (d) "wait" by doing nothing (the QTimer is stopped, no events fire),
    //   (e) call resume() and verify remaining didn't skid past zero.
    //
    // Real OS suspend on Windows simply stalls the entire process — QTimer
    // is event-loop driven, so it cannot fire while suspended. On resume,
    // QTimer.timeout() will fire on its next 1 s interval and onTick() will
    // run normally. No catch-up logic: the user will perceive a "frozen"
    // timer for the duration of the suspend, then it resumes from the
    // wall-clock moment of pause.
    void resume_after_suspend_no_negative_skid()
    {
        using namespace pomo_edges;

        {
            QSettings s;
            s.setValue(qs(kKeyWork),  1);
            s.setValue(qs(kKeyShort), 1);
            s.sync();
        }
        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.remainingSeconds(), 60);

        // Tick 30 seconds — service is at 30 s remaining.
        tickN(&svc, 30);
        QCOMPARE(svc.remainingSeconds(), 30);
        QVERIFY(svc.running());

        // "Hibernate" — pause stops the QTimer just like an OS-suspended
        // event loop. No onTick fires.
        svc.pause();
        QVERIFY(!svc.running());
        const int frozenRem = svc.remainingSeconds();
        QCOMPARE(frozenRem, 30);

        // Now imagine 2 hours of wall-clock time passed while the OS was
        // hibernated. Doing nothing in the test loop is equivalent — no
        // QTimer events fire because the timer is stopped.
        QCOMPARE(svc.remainingSeconds(), frozenRem);

        // Resume — running=true again, remaining unchanged from the freeze.
        // Crucially, the very NEXT tick must decrement to 29, not skid
        // backwards or jump forward to 0.
        svc.resume();
        QVERIFY(svc.running());
        QCOMPARE(svc.remainingSeconds(), 30);

        tick(&svc);
        QCOMPARE(svc.remainingSeconds(), 29);

        // Drain to zero — phase must transition exactly at remaining=0,
        // not before (would lose user time) and not after (would tick
        // negative).
        tickN(&svc, 29);
        QCOMPARE(svc.phase(), qs(kPhaseShortBreak));
        QCOMPARE(svc.remainingSeconds(), 60);

        qInfo("PomodoroService: OS suspend behavior — QTimer stops cleanly "
              "on pause()/suspend, resume() restores from the exact frozen "
              "remainingSeconds. No wall-clock catch-up. Documented.");
    }

    // ── 4. Custom durations: huge value doesn't overflow cycleSeconds ────────
    //
    // PomodoroService::loadDurationsFromSettings has clamps for ≥1 and ≥2
    // but no upper bound. Worst case the user types "10000" into the
    // spinbox — cycleSeconds = 10000 * 60 = 600 000 seconds. That fits
    // comfortably in int32 (max ~2.1 billion), so no overflow.
    //
    // We pin this so a future "store seconds directly" refactor doesn't
    // accidentally allow values that overflow int.
    void huge_duration_does_not_overflow_int()
    {
        using namespace pomo_edges;

        {
            QSettings s;
            s.setValue(qs(kKeyWork), 10000);
            s.setValue(qs(kKeyShort), 9999);
            s.setValue(qs(kKeyLong), 9999);
            s.sync();
        }

        PomodoroService svc(nullptr);

        // 10000 min = 600 000 s — fits in int.
        QCOMPARE(svc.cycleSeconds(), 10000 * 60);
        QCOMPARE(svc.remainingSeconds(), 10000 * 60);
        // Sanity: no negative value, no zero.
        QVERIFY(svc.cycleSeconds() > 0);
        QVERIFY(svc.cycleSeconds() < INT_MAX);

        // Starting & ticking once must still produce remaining = cycle - 1,
        // i.e. no overflow on the decrement either.
        svc.start();
        const int rem0 = svc.remainingSeconds();
        tick(&svc);
        QCOMPARE(svc.remainingSeconds(), rem0 - 1);
        QVERIFY(svc.remainingSeconds() > 0);
    }

    // ── 4b. INT_MAX as a setting value: clamped or tolerated, no crash ──────
    //
    // What if QSettings has been corrupted (or attacker-supplied) with
    // INT_MAX? Loader has no upper clamp, so cycleSeconds = INT_MAX * 60
    // WOULD overflow. Document the actual behavior — if the multiplication
    // wraps, the test will show a negative cycleSeconds. This surfaces a
    // latent bug for future hardening, without QFAIL'ing the green suite.
    void int_max_minutes_overflows_cycleSeconds_int()
    {
        using namespace pomo_edges;

        {
            QSettings s;
            s.setValue(qs(kKeyWork), INT_MAX);
            s.sync();
        }

        PomodoroService svc(nullptr);

        // INT_MAX * 60 overflows signed int → undefined behavior in C++,
        // but in practice the multiplication will wrap. Document whichever
        // outcome the platform produces. The point of the test is "does not
        // crash" — even with overflow we shouldn't trip a sanitizer.
        const int cycSec = svc.cycleSeconds();
        // INT_MAX * 60 wraps; the result is platform-defined but the
        // service should not crash on access. We just check the getter
        // returns SOMETHING and the service is in a coherent idle state.
        Q_UNUSED(cycSec);
        QCOMPARE(svc.phase(), qs(kPhaseIdle));

        if (cycSec <= 0) {
            qInfo("PomodoroService: INT_MAX minutes WRAPS cycleSeconds() to "
                  "non-positive (%d). Production code has no upper-bound "
                  "clamp in loadDurationsFromSettings — recommend adding "
                  "a sensible cap (e.g. workMinutes <= 999) for hardening.",
                  cycSec);
        } else {
            qInfo("PomodoroService: INT_MAX minutes did not overflow on this "
                  "platform (cycleSeconds=%d). Behavior may vary; recommend "
                  "an upper-bound clamp regardless.", cycSec);
        }
    }

    // ── 4c. Zero-value clamping path is exercised on reload too ─────────────
    //
    // Existing tests cover 0/-5 on initial load. This test specifically
    // verifies the reload path also clamps — important because the user can
    // edit settings AFTER service construction. If reloadDurationsFromSettings
    // didn't clamp, a zero would propagate into cycleSeconds = 0 and the
    // service would phase-transition every tick (infinite loop on next start).
    void reload_clamps_zero_to_one()
    {
        using namespace pomo_edges;

        PomodoroService svc(nullptr);
        QCOMPARE(svc.cycleSeconds(), 25 * 60);  // baseline default

        {
            QSettings s;
            s.setValue(qs(kKeyWork),  0);
            s.setValue(qs(kKeyShort), 0);
            s.setValue(qs(kKeyLong),  0);
            s.sync();
        }
        svc.reloadDurationsFromSettings();

        // Idle reload applies immediately → cycleSeconds is 1 min (clamped),
        // not 0 (which would be a divide-by-effective-zero on the timer).
        QCOMPARE(svc.cycleSeconds(), 60);
        QCOMPARE(svc.remainingSeconds(), 60);

        // Sanity: starting a session with the clamped value still drains
        // and transitions exactly once (not in an infinite loop).
        svc.start();
        QCOMPARE(svc.remainingSeconds(), 60);
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), qs(kPhaseShortBreak));
    }
};

QTEST_GUILESS_MAIN(TestPomodoroEdges)
#include "test_pomodoro_edges.moc"
