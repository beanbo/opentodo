#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QMetaObject>
#include <QStandardPaths>
#include <QFile>

#include "services/pomodoroservice.h"

// Integration test for PomodoroService walking through a full N-cycle session
// (3 cycles -> long break -> next round) end-to-end. Complements the per-method
// unit tests in test_pomodoroservice.cpp by exercising the full state machine
// in one continuous flow instead of isolated transitions.
//
// The service takes a NotificationService* but tolerates nullptr; passing null
// keeps the test focused on the timer/phase state machine without dragging in
// the platform notification stack.
class TestPomodoroFullSession : public QObject
{
    Q_OBJECT

private:
    // Drive PomodoroService::onTick() directly via the meta-object system so the
    // test doesn't have to wait real wall-clock seconds. onTick is a private
    // slot but moc still registers it under that name for invokeMethod.
    static void tick(PomodoroService *svc)
    {
        QMetaObject::invokeMethod(svc, "onTick", Qt::DirectConnection);
    }

    // Drive N ticks in a row.
    static void tickN(PomodoroService *svc, int n)
    {
        for (int i = 0; i < n; ++i)
            tick(svc);
    }

    // Custom durations chosen so the full-session test runs fast but still
    // exercises distinct work/short/long durations. cyclesBeforeLongBreak=3
    // means the third completed work cycle triggers the long break.
    static constexpr int kWorkMin       = 2;
    static constexpr int kShortBreakMin = 1;
    static constexpr int kLongBreakMin  = 3;
    static constexpr int kCyclesBeforeLong = 3;

    static int workSeconds()       { return kWorkMin * 60; }
    static int shortBreakSeconds() { return kShortBreakMin * 60; }
    static int longBreakSeconds()  { return kLongBreakMin * 60; }

    static void seedDurations()
    {
        QSettings s;
        s.setValue(QStringLiteral("pomodoro/workMinutes"),           kWorkMin);
        s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"),     kShortBreakMin);
        s.setValue(QStringLiteral("pomodoro/longBreakMinutes"),      kLongBreakMin);
        s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), kCyclesBeforeLong);
        s.sync();
    }

    // PomodoroService persists pomodoro_state.json on every mutation now
    // (crash-recovery). Scrub between slots so a prior session blob doesn't
    // resurrect inside the next slot's fresh service.
    static void removeStateFile()
    {
        const QString base = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        if (base.isEmpty()) return;
        const QString path = base + QStringLiteral("/OpenTodo/pomodoro_state.json");
        if (QFile::exists(path))
            QFile::remove(path);
    }

private slots:
    // ── Test fixture lifecycle ────────────────────────────────────────────────

    void initTestCase()
    {
        // Dedicated org/app so the test never touches real OpenTodo settings or
        // the settings used by the sibling test_pomodoroservice test binary.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_PomoFullSession"));
        QCoreApplication::setApplicationName(QStringLiteral("test_pomodoro_full_session"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
        removeStateFile();
        seedDurations();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
        removeStateFile();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
        removeStateFile();
    }

    // ── The headline integration test: walk a complete 3-cycle session ──────

    void runFullThreeCycleSession()
    {
        PomodoroService svc(nullptr);

        // Sanity: configured durations propagated through QSettings.
        QCOMPARE(svc.cycleSeconds(), workSeconds());
        QCOMPARE(svc.remainingSeconds(), workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("idle"));
        QCOMPARE(svc.completedCyclesToday(), 0);

        // Spies to verify signal emissions match state changes (smoke check).
        QSignalSpy phaseSpy(&svc, &PomodoroService::phaseChanged);
        QSignalSpy completedSpy(&svc, &PomodoroService::completedCyclesTodayChanged);

        // ── Start ───────────────────────────────────────────────────────────
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QVERIFY(svc.running());
        QCOMPARE(svc.remainingSeconds(), workSeconds());
        QCOMPARE(svc.completedCyclesToday(), 0);

        // ── Cycle 1: work (2 min) → shortBreak ─────────────────────────────
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), 1);
        QCOMPARE(svc.cycleSeconds(), shortBreakSeconds());
        QCOMPARE(svc.remainingSeconds(), shortBreakSeconds());

        // ── shortBreak (1 min) → work ──────────────────────────────────────
        tickN(&svc, shortBreakSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.completedCyclesToday(), 1); // unchanged: breaks don't count
        QCOMPARE(svc.remainingSeconds(), workSeconds());

        // ── Cycle 2: work (2 min) → shortBreak ─────────────────────────────
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), 2);
        QCOMPARE(svc.remainingSeconds(), shortBreakSeconds());

        // ── shortBreak → work ──────────────────────────────────────────────
        tickN(&svc, shortBreakSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.completedCyclesToday(), 2);

        // ── Cycle 3 (== N): work → longBreak ───────────────────────────────
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("longBreak"));
        QCOMPARE(svc.completedCyclesToday(), 3);
        QCOMPARE(svc.cycleSeconds(), longBreakSeconds());
        QCOMPARE(svc.remainingSeconds(), longBreakSeconds());

        // ── longBreak (3 min) → work (4th cycle, 1st of new round) ─────────
        tickN(&svc, longBreakSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.completedCyclesToday(), 3); // breaks still don't count
        QCOMPARE(svc.remainingSeconds(), workSeconds());

        // ── Verify the long-break scheduler restarted ──────────────────────
        // After the long break, the internal m_workCyclesSinceLongBreak resets
        // to 0. So the next long break should occur after another N work
        // cycles. Quick check: completing the very next work cycle should land
        // on shortBreak (not longBreak), proving the counter restarted.
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), 4);

        // Continue the next round: cycle (1 of new round done)+1 more = 2/3
        // → still shortBreak.
        tickN(&svc, shortBreakSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), 5);

        // …and one more cycle (3/3 of new round) → longBreak.
        tickN(&svc, shortBreakSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        tickN(&svc, workSeconds());
        QCOMPARE(svc.phase(), QStringLiteral("longBreak"));
        QCOMPARE(svc.completedCyclesToday(), 6);

        // Spies recorded a phase change for every transition we walked through.
        // We don't pin an exact count (multiple emissions per phase are OK)
        // but it must be substantial — the bare minimum is one per phase
        // transition, of which there were many.
        QVERIFY(phaseSpy.count() >= 8);
        // completedCyclesToday changed exactly 6 times (one per work-cycle end).
        QCOMPARE(completedSpy.count(), 6);
    }

    // ── pause during work preserves remaining ───────────────────────────────

    void pauseDuringWorkPreservesRemaining()
    {
        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        const int initialRem = svc.remainingSeconds();
        QCOMPARE(initialRem, workSeconds());

        // Tick 30 simulated seconds in.
        tickN(&svc, 30);
        QCOMPARE(svc.remainingSeconds(), initialRem - 30);
        QVERIFY(svc.running());

        // Pause — remaining must NOT decrement until resume.
        svc.pause();
        QVERIFY(!svc.running());
        const int pausedRem = svc.remainingSeconds();
        QCOMPARE(pausedRem, initialRem - 30);

        // "Advance wall time" by simply doing nothing — the QTimer is stopped,
        // so no onTick fires. Remaining must be exactly what it was at pause.
        QCOMPARE(svc.remainingSeconds(), pausedRem);
        QCOMPARE(svc.phase(), QStringLiteral("work")); // phase preserved

        // Resume — service is running again, phase still work, remaining
        // unchanged from the pause point.
        svc.resume();
        QVERIFY(svc.running());
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.remainingSeconds(), pausedRem);

        // One more tick now actually progresses again.
        tick(&svc);
        QCOMPARE(svc.remainingSeconds(), pausedRem - 1);
    }

    // ── reset() returns to clean idle state ─────────────────────────────────

    void resetDuringSessionGoesBackToInitialState()
    {
        PomodoroService svc(nullptr);
        QCOMPARE(svc.completedCyclesToday(), 0);

        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));

        // Tick a bit so we're mid-work, running, with a non-default remaining.
        tickN(&svc, 15);
        QVERIFY(svc.running());
        QVERIFY(svc.remainingSeconds() < workSeconds());

        // Complete a full cycle so completedCyclesToday is non-zero — this
        // verifies that reset() does NOT zero the daily counter (which is a
        // persistent stat, not session state). The long-break counter IS
        // reset, but completedCyclesToday survives.
        tickN(&svc, svc.remainingSeconds()); // finish current work
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), 1);

        // Reset.
        svc.reset();
        QCOMPARE(svc.phase(), QStringLiteral("idle"));
        QVERIFY(!svc.running());
        // Remaining seconds reseed from the configured work duration.
        QCOMPARE(svc.cycleSeconds(), workSeconds());
        QCOMPARE(svc.remainingSeconds(), workSeconds());
        // completedCyclesToday is a persistent daily stat — reset() does not
        // wipe it. Verify the documented behavior.
        QCOMPARE(svc.completedCyclesToday(), 1);

        // After reset, a fresh start() should put us back in work cleanly.
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QVERIFY(svc.running());
        QCOMPARE(svc.remainingSeconds(), workSeconds());
    }

    // ── skip() during work jumps straight to the appropriate break ──────────

    void skipDuringWorkAdvancesToBreak()
    {
        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QVERIFY(svc.running());

        // Tick a bit so we're actually mid-work — skipping should NOT depend
        // on remaining seconds, and should NOT count as a completed cycle.
        tickN(&svc, 10);
        const int completedBefore = svc.completedCyclesToday();

        QSignalSpy phaseSpy(&svc, &PomodoroService::phaseChanged);

        svc.skip();

        // Phase advanced to shortBreak immediately (we're on cycle 1 of N=3,
        // so the next break is short, not long). Skip does NOT bump completed.
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), completedBefore);
        QCOMPARE(svc.remainingSeconds(), shortBreakSeconds());
        QVERIFY(phaseSpy.count() >= 1);
        // Service is still running — skip preserves the running state.
        QVERIFY(svc.running());

        // From break, skip again → back to work.
        svc.skip();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.remainingSeconds(), workSeconds());
        QCOMPARE(svc.completedCyclesToday(), completedBefore);
    }
};

QTEST_GUILESS_MAIN(TestPomodoroFullSession)
#include "test_pomodoro_full_session.moc"
