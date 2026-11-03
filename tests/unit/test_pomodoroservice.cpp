#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QMetaObject>
#include <QStandardPaths>
#include <QFile>

#include "services/pomodoroservice.h"

// PomodoroService takes a NotificationService* but checks for null before use.
// Passing nullptr keeps the test focused on configuration / state-machine logic
// without dragging the platform-specific notification stack into the test.
class TestPomodoroService : public QObject
{
    Q_OBJECT

private:
    // Drive PomodoroService::onTick() directly via the meta-object system so
    // the test doesn't have to wait real wall-clock seconds. onTick is a
    // private slot but moc still registers it under that name.
    static void tick(PomodoroService *svc)
    {
        QMetaObject::invokeMethod(svc, "onTick", Qt::DirectConnection);
    }

    // Tick repeatedly until the phase changes (or we've ticked far more times
    // than the longest plausible phase, which would mean the state machine is
    // broken). Returns the number of ticks consumed.
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
    // ── Test fixture lifecycle ────────────────────────────────────────────────

    void initTestCase()
    {
        // Use a dedicated org/app so the test never touches real OpenTodo settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_PomoSvc"));
        QCoreApplication::setApplicationName(QStringLiteral("test_pomodoroservice"));
    }

    // PomodoroService persists session state to pomodoro_state.json on every
    // mutation (added for crash-recovery). Without scrubbing it between slots,
    // a previous slot's running session would resurrect inside the next slot's
    // fresh PomodoroService constructor. Recompute the path the same way the
    // service does so we don't have to expose a private getter.
    static void removeStateFile()
    {
        const QString base = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        if (base.isEmpty()) return;
        const QString path = base + QStringLiteral("/OpenTodo/pomodoro_state.json");
        if (QFile::exists(path))
            QFile::remove(path);
    }

    void init()
    {
        // Fully clear settings before each test for isolation.
        QSettings s;
        s.clear();
        s.sync();
        removeStateFile();
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

    // ── Defaults ──────────────────────────────────────────────────────────────

    void defaults_when_settings_empty()
    {
        // No keys set → constructor should fall back to 25/5/15/4.
        PomodoroService svc(nullptr);

        QCOMPARE(svc.phase(), QStringLiteral("idle"));
        // While idle, cycleSeconds() returns the work-cycle duration.
        QCOMPARE(svc.cycleSeconds(), 25 * 60);
        // The constructor seeds remainingSeconds from the work duration too.
        QCOMPARE(svc.remainingSeconds(), 25 * 60);
    }

    void defaults_visible_after_start_in_each_phase()
    {
        // Verify the default short/long break durations are wired through too.
        // This requires actually entering each phase, which we do by ticking
        // through cycles with the default config (4 cycles to long break).
        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.cycleSeconds(), 25 * 60);

        // Drive ticks until the work phase ends — should land in shortBreak
        // (this is the 1st completed work cycle, well before the 4th).
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.cycleSeconds(), 5 * 60);
    }

    // ── Custom values from QSettings ──────────────────────────────────────────

    void loads_custom_values_from_settings()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 50);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 10);
            s.setValue(QStringLiteral("pomodoro/longBreakMinutes"), 30);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 3);
            s.sync();
        }

        PomodoroService svc(nullptr);
        // Idle → cycleSeconds reflects the configured work duration.
        QCOMPARE(svc.cycleSeconds(), 50 * 60);
        QCOMPARE(svc.remainingSeconds(), 50 * 60);

        // Verify break durations propagate too — start, finish work, observe
        // shortBreak length.
        svc.start();
        QCOMPARE(svc.cycleSeconds(), 50 * 60);
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.cycleSeconds(), 10 * 60);
    }

    // ── Clamping of corrupt values ────────────────────────────────────────────

    void clamps_out_of_range_values()
    {
        // 0 / negative work minutes must clamp to >=1; cyclesBeforeLongBreak
        // must clamp to >=2 (otherwise every break would be a long break and
        // the cycle counter can never advance).
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 0);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), -5);
            s.setValue(QStringLiteral("pomodoro/longBreakMinutes"), 0);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 1);
            s.sync();
        }

        PomodoroService svc(nullptr);
        // workMinutes clamped to 1 → 60 seconds.
        QCOMPARE(svc.cycleSeconds(), 1 * 60);
        QCOMPARE(svc.remainingSeconds(), 1 * 60);

        // After starting & finishing a work phase we should land in a break
        // whose length is the clamped 1-minute short-break (not 0, not -5).
        svc.start();
        tickUntilPhaseChange(&svc);
        // shortBreak clamped to 1 minute = 60 seconds.
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.cycleSeconds(), 1 * 60);
    }

    // ── reloadDurationsFromSettings while idle applies immediately ────────────

    void reload_while_idle_applies_immediately()
    {
        PomodoroService svc(nullptr);
        QCOMPARE(svc.cycleSeconds(), 25 * 60); // sanity

        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 40);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 8);
            s.sync();
        }

        QSignalSpy phaseSpy(&svc, &PomodoroService::phaseChanged);
        QSignalSpy remSpy(&svc, &PomodoroService::remainingSecondsChanged);

        svc.reloadDurationsFromSettings();

        // Idle path explicitly emits phaseChanged + remainingSecondsChanged so
        // the QML binding for the displayed cycle length updates.
        QVERIFY(phaseSpy.count() >= 1);
        QVERIFY(remSpy.count() >= 1);
        QCOMPARE(svc.cycleSeconds(), 40 * 60);
        QCOMPARE(svc.remainingSeconds(), 40 * 60);
        QCOMPARE(svc.phase(), QStringLiteral("idle"));
    }

    // ── Pending change semantics: mid-session reload defers to next phase ────

    void reload_during_active_session_defers_until_next_phase()
    {
        // Start with a small initial config so the test is fast.
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/longBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 4);
            s.sync();
        }

        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.cycleSeconds(), 1 * 60);

        // Tick once so we're truly mid-cycle (running, partway through work).
        tick(&svc);
        QVERIFY(svc.running());
        // Still inside the same work phase.
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        const int beforeRem = svc.remainingSeconds();
        QVERIFY(beforeRem > 0 && beforeRem < 1 * 60);

        // Change settings and reload mid-session — running, work phase, so the
        // new value should be BUFFERED, not applied yet.
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 2);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 3);
            s.sync();
        }

        svc.reloadDurationsFromSettings();

        // Current phase is unchanged; cycleSeconds for the active work phase
        // is still the OLD value (1 min) since the change is pending.
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.cycleSeconds(), 1 * 60);
        // Remaining seconds were not reset by the reload either.
        QCOMPARE(svc.remainingSeconds(), beforeRem);

        // Drain the rest of the current work phase. At the boundary, pending
        // values are applied → next phase (shortBreak) uses the NEW value (3 min).
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.cycleSeconds(), 3 * 60);

        // Drain shortBreak; next work phase should reflect the new 2-min value.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.cycleSeconds(), 2 * 60);
    }

    // ── reset() applies pending changes immediately ───────────────────────────

    void reset_applies_pending_durations()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 1);
            s.sync();
        }
        PomodoroService svc(nullptr);
        svc.start();
        // Mid-session reload buffers the change.
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 7);
            s.sync();
        }
        svc.reloadDurationsFromSettings();
        QCOMPARE(svc.cycleSeconds(), 1 * 60); // still old (pending)

        // reset() goes idle, applies pending, and reseeds remaining from the
        // new work duration.
        svc.reset();
        QCOMPARE(svc.phase(), QStringLiteral("idle"));
        QVERIFY(!svc.running());
        QCOMPARE(svc.cycleSeconds(), 7 * 60);
        QCOMPARE(svc.remainingSeconds(), 7 * 60);
    }

    // ── Long-break trigger every Nth cycle ────────────────────────────────────

    void long_break_triggers_after_n_work_cycles()
    {
        // Configure short cycles + N=3 so the test is bounded and fast.
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/longBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 3);
            s.sync();
        }

        PomodoroService svc(nullptr);
        svc.start();
        QCOMPARE(svc.phase(), QStringLiteral("work"));

        // Cycle 1: complete work → expect shortBreak.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        // Drain break → back to work.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("work"));

        // Cycle 2: complete work → expect shortBreak.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("work"));

        // Cycle 3 (== N): complete work → expect longBreak.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("longBreak"));

        // After the long break the counter resets — next break should be short.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
    }

    // ── completedCyclesToday counter reflects work completions ────────────────

    void completed_cycles_counter_advances_per_work_completion()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 4);
            s.sync();
        }
        PomodoroService svc(nullptr);
        const int before = svc.completedCyclesToday();

        QSignalSpy spy(&svc, &PomodoroService::completedCyclesTodayChanged);

        svc.start();
        // Drain one full work phase → counter should bump by exactly 1.
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        QCOMPARE(svc.completedCyclesToday(), before + 1);
        QCOMPARE(spy.count(), 1);

        // Drain break (does NOT count) and another work phase → +1 more.
        tickUntilPhaseChange(&svc); // break ends → work
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.completedCyclesToday(), before + 1);
        tickUntilPhaseChange(&svc); // 2nd work ends
        QCOMPARE(svc.completedCyclesToday(), before + 2);
        QCOMPARE(spy.count(), 2);
    }

    // ── pause / resume preserves remainingSeconds ─────────────────────────────

    void pause_and_resume_preserves_state()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), 1);
            s.sync();
        }
        PomodoroService svc(nullptr);
        svc.start();
        const int rem0 = svc.remainingSeconds();
        tick(&svc);
        const int rem1 = svc.remainingSeconds();
        QCOMPARE(rem1, rem0 - 1);

        svc.pause();
        QVERIFY(!svc.running());
        const int rem2 = svc.remainingSeconds();
        QCOMPARE(rem2, rem1); // pause doesn't reset

        svc.resume();
        QVERIFY(svc.running());
        // Phase preserved; remaining seconds preserved.
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        QCOMPARE(svc.remainingSeconds(), rem1);
    }

    // ── reload pre-default sentinel: clamping happens on reload too ──────────

    void reload_clamps_invalid_values()
    {
        PomodoroService svc(nullptr);
        QCOMPARE(svc.cycleSeconds(), 25 * 60);

        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/workMinutes"), -10);
            s.setValue(QStringLiteral("pomodoro/cyclesBeforeLongBreak"), 0);
            s.sync();
        }
        svc.reloadDurationsFromSettings();

        // Idle reload applies immediately. workMinutes clamps to 1.
        QCOMPARE(svc.cycleSeconds(), 1 * 60);
        QCOMPARE(svc.remainingSeconds(), 1 * 60);

        // cyclesBeforeLongBreak clamps to 2 → starting fresh and completing 2
        // work cycles should land on a longBreak (since with the clamped value
        // every 2nd break is the long one).
        {
            QSettings s;
            s.setValue(QStringLiteral("pomodoro/shortBreakMinutes"), 1);
            s.setValue(QStringLiteral("pomodoro/longBreakMinutes"), 1);
            s.sync();
            // Re-load so shortBreak/longBreak are also picked up.
            svc.reloadDurationsFromSettings();
        }

        svc.start();
        // Cycle 1 work → shortBreak (cyc%2=1).
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("shortBreak"));
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("work"));
        // Cycle 2 work → longBreak (cyc%2=0 → due long).
        tickUntilPhaseChange(&svc);
        QCOMPARE(svc.phase(), QStringLiteral("longBreak"));
    }

    // F4/P14: after pause → resume, a restart must restore the countdown
    // where it was, not (almost) a full phase. resume() used to anchor
    // started_at at "now", so the crash-recovery math (planned − elapsed)
    // gave back nearly the whole phase.
    void pause_resume_then_restart_restoresRemaining()
    {
        int remainingBefore = 0;
        {
            PomodoroService svc(nullptr);   // default 25-min work phase
            svc.start();
            for (int i = 0; i < 600; ++i)   // 10 "minutes" into the phase
                tick(&svc);
            remainingBefore = svc.remainingSeconds();
            QCOMPARE(remainingBefore, 25 * 60 - 600);
            svc.pause();
            svc.resume();
            svc.flushStateToDisk();
        }
        PomodoroService restored(nullptr);
        QVERIFY(restored.running());
        QCOMPARE(restored.phase(), QStringLiteral("work"));
        QVERIFY2(qAbs(restored.remainingSeconds() - remainingBefore) <= 2,
                 qPrintable(QStringLiteral("restored %1 s, expected ~%2 s")
                                .arg(restored.remainingSeconds()).arg(remainingBefore)));
    }
};

QTEST_GUILESS_MAIN(TestPomodoroService)
#include "test_pomodoroservice.moc"
