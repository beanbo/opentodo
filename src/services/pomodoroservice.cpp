#include "pomodoroservice.h"
#include "services/notificationservice.h"
#include <QSettings>
#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <climits>

// Upper bound for any duration setting. A day's worth of minutes is the largest
// value that makes sense for a productivity timer; anything beyond it is either
// a typo, a corrupt QSettings value, or an attacker-supplied INT_MAX-style
// payload. Multiplying this by 60 (seconds) and by 1000 (milliseconds) stays
// well within signed 32-bit range, so cycleSeconds() / QTimer interval math
// never overflows. See test_pomodoro_edges.cpp::int_max_minutes_*.
constexpr int kMaxDurationMinutes = 24 * 60;     // 1 day
// Same idea for the long-break cycle counter. 100 cycles is already 100 hours
// of work — well beyond any realistic use case but still a safe int multiplier.
constexpr int kMaxCyclesBeforeLongBreak = 100;
constexpr int kMinCyclesBeforeLongBreak = 2;     // <2 would make every break long

// Centralised clamp helpers — used by both load paths so corrupt values cannot
// reach the multiplication sites. Out-of-band values are coerced silently;
// the caller decides whether to emit a change signal.
static inline int clampDurationMinutes(int v)
{
    if (v < 1) return 1;
    if (v > kMaxDurationMinutes) return kMaxDurationMinutes;
    return v;
}

static inline int clampCyclesBeforeLongBreak(int v)
{
    if (v < kMinCyclesBeforeLongBreak) return kMinCyclesBeforeLongBreak;
    if (v > kMaxCyclesBeforeLongBreak) return kMaxCyclesBeforeLongBreak;
    return v;
}

// Convert minutes (already clamped on the load path) to seconds with explicit
// 64-bit widening before the multiplication. Defensive: even if a future
// refactor introduces a new uncamped write path, this can't wrap to a negative
// or near-zero value. Output is clamped to int range; the upper bound is far
// above any realistic kMaxDurationMinutes * 60.
static inline int minutesToSecondsSafe(int minutes)
{
    const qint64 secs = qint64(minutes) * 60;
    if (secs <= 0) return 60;            // floor at 1 minute on degenerate input
    if (secs > INT_MAX) return INT_MAX;  // would never happen with clamps in place
    return static_cast<int>(secs);
}

PomodoroService::PomodoroService(NotificationService *notif, QObject *parent)
    : QObject(parent), m_notif(notif)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &PomodoroService::onTick);
    loadDurationsFromSettings();
    // Initial idle remaining shows the configured work duration. minutesToSecondsSafe
    // widens to qint64 before multiplying — defence-in-depth against future
    // refactors that might skip the clamp on the load path.
    m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
    loadCompletedToday();

    // Resolve the persistence root. Same shape as FileManager: $APPDATA/OpenTodo
    // on desktop, $APPDATA on Android (where QStandardPaths::AppDataLocation
    // already points at the app's per-package directory). On the rare platforms
    // where AppDataLocation returns an empty string (unconfigured Qt test
    // environments), the disk-write paths short-circuit and the service falls
    // back to in-memory-only behaviour.
#ifdef Q_OS_ANDROID
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/OpenTodo");
#endif

    // Attempt to resurrect a session that was in flight when the previous
    // process died. Order matters: loadStateFromDisk reads m_dataPath, so it
    // must run AFTER the path is set; it also consults the configured
    // durations, so it must run AFTER loadDurationsFromSettings. If a running
    // session is restored the timer is armed inside loadStateFromDisk and
    // sessionRestored() is emitted before this constructor returns.
    loadStateFromDisk();
}

void PomodoroService::start()
{
    enterPhase(QStringLiteral("work"));
    m_running = true;
    emit runningChanged();
    m_timer.start();
    // Persist the new running state. enterPhase already set m_phaseStartedAt
    // via its tail-end save, but we still need to flush the running=true flag.
    saveStateToDisk();
}

void PomodoroService::pause()
{
    if (!m_running) return;
    m_running = false;
    emit runningChanged();
    m_timer.stop();
    // On pause we collapse the elapsed-since-start time into the on-disk
    // remainingSeconds (m_remainingSeconds was already decremented by onTick).
    // Clearing m_phaseStartedAt prevents resume() from double-counting the
    // wall-clock elapsed time. The next resume() will re-anchor.
    m_phaseStartedAt = QDateTime();
    saveStateToDisk();
}

void PomodoroService::resume()
{
    if (m_running) return;
    if (m_phase == QStringLiteral("idle")) {
        start();
        return;
    }
    m_running = true;
    // Re-anchor the wall-clock start so on-disk planned_duration_ms still
    // matches (now - started_at) = remainingSeconds. Without this, a paused
    // session that resumes after N minutes would think it had been running
    // for N minutes when the load path computed elapsed time on the next crash.
    m_phaseStartedAt = QDateTime::currentDateTimeUtc();
    emit runningChanged();
    m_timer.start();
    saveStateToDisk();
}

void PomodoroService::skip()
{
    // Skip the current phase — go straight to the next.
    // Skipping a work phase does NOT count as a completed cycle.
    if (m_phase == QStringLiteral("work")) {
        // Decide which break type the next break would be — but since the work
        // cycle was skipped (not completed), do not bump the cycle counter.
        const bool dueLong = (m_workCyclesSinceLongBreak > 0)
                             && (m_workCyclesSinceLongBreak % m_cyclesBeforeLongBreak == 0);
        enterPhase(dueLong ? QStringLiteral("longBreak") : QStringLiteral("shortBreak"));
    } else if (m_phase == QStringLiteral("shortBreak") || m_phase == QStringLiteral("longBreak")) {
        enterPhase(QStringLiteral("work"));
    } else {
        enterPhase(QStringLiteral("work"));
    }
    // Keep the timer running if it was already running.
    if (m_running)
        m_timer.start();
    // enterPhase wrote to disk already; re-write to capture the (possibly
    // still-running) state alongside the new phase anchor.
    saveStateToDisk();
}

void PomodoroService::reset()
{
    m_timer.stop();
    m_running = false;
    m_phase = QStringLiteral("idle");
    m_workCyclesSinceLongBreak = 0;
    m_phaseStartedAt = QDateTime();
    // Apply any pending duration changes now that we're idle.
    applyPendingDurationChanges();
    m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
    emit runningChanged();
    emit phaseChanged();
    emit remainingSecondsChanged();
    // Going idle is the right time to drop the persisted blob — a fresh next
    // launch sees "no file" rather than a stale "running" record. This matches
    // the spec acceptance for "Restart app after session ended cleanly → no
    // stale 'running' state shown".
    clearStateFromDisk();
}

void PomodoroService::enterPhase(const QString &phase)
{
    // A phase boundary is the right time to apply any pending duration changes.
    applyPendingDurationChanges();

    m_phase = phase;
    if (phase == QStringLiteral("work"))
        m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
    else if (phase == QStringLiteral("shortBreak"))
        m_remainingSeconds = minutesToSecondsSafe(m_shortBreakMinutes);
    else if (phase == QStringLiteral("longBreak"))
        m_remainingSeconds = minutesToSecondsSafe(m_longBreakMinutes);
    else
        m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
    // Re-anchor wall-clock at the phase boundary so the crash-recovery path
    // can compute (now - started_at) against the new phase's
    // planned_duration_ms. Done unconditionally here — even an idle enterPhase
    // resets the anchor, and the persistence layer treats idle-phase rows
    // specially anyway (is_running=false → display-only restore).
    m_phaseStartedAt = QDateTime::currentDateTimeUtc();
    emit phaseChanged();
    emit remainingSecondsChanged();
    saveStateToDisk();
}

void PomodoroService::onTick()
{
    m_remainingSeconds--;
    emit remainingSecondsChanged();
    if (m_remainingSeconds > 0)
        return;

    const QString endedPhase = m_phase;
    if (endedPhase == QStringLiteral("work")) {
        // Bump completed counter — handle date rollover automatically.
        const QDate today = QDate::currentDate();
        if (m_completedDate != today) {
            m_completedDate = today;
            m_completedToday = 1;
        } else {
            m_completedToday++;
        }
        saveCompletedToday();
        emit completedCyclesTodayChanged();

        // Track cycles for long-break scheduling.
        m_workCyclesSinceLongBreak++;
        const bool dueLong = (m_workCyclesSinceLongBreak % m_cyclesBeforeLongBreak == 0);

        if (m_notif)
            m_notif->showNotification(QStringLiteral("Помодоро завершён"),
                                      dueLong ? QStringLiteral("Время длинного перерыва!")
                                              : QStringLiteral("Время отдыха!"),
                                      QString());
        enterPhase(dueLong ? QStringLiteral("longBreak") : QStringLiteral("shortBreak"));
        if (dueLong)
            m_workCyclesSinceLongBreak = 0;
    } else {
        if (m_notif)
            m_notif->showNotification(QStringLiteral("Перерыв окончен"),
                                      QStringLiteral("Время работать!"),
                                      QString());
        enterPhase(QStringLiteral("work"));
    }
    // Automatically start the next phase (continuous mode).
    m_timer.start();
}

void PomodoroService::loadDurationsFromSettings()
{
    QSettings s;
    m_workMinutes           = s.value(QStringLiteral("pomodoro/workMinutes"),           25).toInt();
    m_shortBreakMinutes     = s.value(QStringLiteral("pomodoro/shortBreakMinutes"),      5).toInt();
    m_longBreakMinutes      = s.value(QStringLiteral("pomodoro/longBreakMinutes"),      15).toInt();
    m_cyclesBeforeLongBreak = s.value(QStringLiteral("pomodoro/cyclesBeforeLongBreak"),  4).toInt();

    // Defensive clamps so a corrupt setting can't bring the app to its knees.
    // Lower bound: 1 min for durations (a 0-min timer would phase-transition
    // every tick), >=2 cycles (else every break is a long break). Upper bound:
    // kMaxDurationMinutes / kMaxCyclesBeforeLongBreak — see header at top of
    // this file for why these limits prevent the int-overflow flagged by
    // test_pomodoro_edges::int_max_minutes_is_clamped_no_overflow.
    m_workMinutes           = clampDurationMinutes(m_workMinutes);
    m_shortBreakMinutes     = clampDurationMinutes(m_shortBreakMinutes);
    m_longBreakMinutes      = clampDurationMinutes(m_longBreakMinutes);
    m_cyclesBeforeLongBreak = clampCyclesBeforeLongBreak(m_cyclesBeforeLongBreak);

    m_pendingWorkMinutes           = m_workMinutes;
    m_pendingShortBreakMinutes     = m_shortBreakMinutes;
    m_pendingLongBreakMinutes      = m_longBreakMinutes;
    m_pendingCyclesBeforeLongBreak = m_cyclesBeforeLongBreak;
    m_durationsDirty = false;
}

void PomodoroService::reloadDurationsFromSettings()
{
    QSettings s;
    const int newWork  = s.value(QStringLiteral("pomodoro/workMinutes"),           25).toInt();
    const int newShort = s.value(QStringLiteral("pomodoro/shortBreakMinutes"),      5).toInt();
    const int newLong  = s.value(QStringLiteral("pomodoro/longBreakMinutes"),      15).toInt();
    const int newCyc   = s.value(QStringLiteral("pomodoro/cyclesBeforeLongBreak"),  4).toInt();

    // Clamp both bounds — see loadDurationsFromSettings() for rationale.
    // Out-of-band values from QSettings (corrupt file, mis-bound QML spinbox,
    // attacker-supplied INT_MAX/INT_MIN) are silently coerced into the safe
    // range before any tick / multiplication uses them.
    m_pendingWorkMinutes           = clampDurationMinutes(newWork);
    m_pendingShortBreakMinutes     = clampDurationMinutes(newShort);
    m_pendingLongBreakMinutes      = clampDurationMinutes(newLong);
    m_pendingCyclesBeforeLongBreak = clampCyclesBeforeLongBreak(newCyc);
    m_durationsDirty = true;

    // If no session is active (idle / not running), apply immediately so the
    // displayed cycle length reflects the new value right away.
    if (m_phase == QStringLiteral("idle") || !m_running) {
        applyPendingDurationChanges();
        if (m_phase == QStringLiteral("idle")) {
            m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
            emit remainingSecondsChanged();
            emit phaseChanged();
        }
    }
    // Otherwise the changes are picked up at the next phase boundary
    // (enterPhase calls applyPendingDurationChanges).
}

void PomodoroService::applyPendingDurationChanges()
{
    if (!m_durationsDirty) return;
    m_workMinutes           = m_pendingWorkMinutes;
    m_shortBreakMinutes     = m_pendingShortBreakMinutes;
    m_longBreakMinutes      = m_pendingLongBreakMinutes;
    m_cyclesBeforeLongBreak = m_pendingCyclesBeforeLongBreak;
    m_durationsDirty = false;
}

void PomodoroService::loadCompletedToday()
{
    QSettings s;
    const QString dateStr = s.value(QStringLiteral("pomodoro/completed_date")).toString();
    m_completedDate = QDate::fromString(dateStr, Qt::ISODate);
    if (m_completedDate == QDate::currentDate())
        m_completedToday = s.value(QStringLiteral("pomodoro/completed_count")).toInt();
    else {
        // New day — reset in-memory counter; date is updated on the next save.
        m_completedToday = 0;
        m_completedDate = QDate::currentDate();
    }
}

void PomodoroService::saveCompletedToday()
{
    QSettings s;
    s.setValue(QStringLiteral("pomodoro/completed_date"), m_completedDate.toString(Qt::ISODate));
    s.setValue(QStringLiteral("pomodoro/completed_count"), m_completedToday);
}

void PomodoroService::setCurrentTaskId(const QString &taskId)
{
    if (m_currentTaskId == taskId) return;
    m_currentTaskId = taskId;
    emit currentTaskIdChanged();
    // The on-disk blob carries task_id so a crash-recovered session still
    // knows what the user was focusing on. Persist immediately.
    saveStateToDisk();
}

void PomodoroService::flushStateToDisk()
{
    // Public alias for the same writer the state-mutation paths already use.
    // Provided so QCoreApplication::aboutToQuit can call it without exposing
    // the private writer. Cheap to call repeatedly — QSaveFile rewrites the
    // file atomically each time.
    saveStateToDisk();
}

QString PomodoroService::stateFilePath() const
{
    if (m_dataPath.isEmpty()) return QString();
    return m_dataPath + QStringLiteral("/pomodoro_state.json");
}

void PomodoroService::ensureDataDirectory() const
{
    if (m_dataPath.isEmpty()) return;
    QDir dir(m_dataPath);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
}

void PomodoroService::saveStateToDisk()
{
    // Cheap no-op when persistence root couldn't be resolved (rare; happens
    // only in QStandardPaths-less test environments). The in-memory state
    // machine still works — we simply skip the disk hop.
    const QString path = stateFilePath();
    if (path.isEmpty()) return;

    ensureDataDirectory();

    // Build the JSON blob. Schema (snake_case to match the rest of the
    // codebase's on-disk shape — see CLAUDE.md "Conventions"):
    //
    //   {
    //     "is_running":          bool,
    //     "phase":               "work" | "shortBreak" | "longBreak" | "idle",
    //     "started_at":          ISO 8601 with ms in UTC ("" when idle/paused),
    //     "planned_duration_ms": int (current phase's full length),
    //     "remaining_seconds":   int (in-memory countdown — used for pause restore),
    //     "task_id":             string ("" when none),
    //     "work_cycles_since_long_break": int,
    //     "updated_at":          ISO 8601 with ms in UTC
    //   }
    //
    // remaining_seconds is included so a paused session restores at its exact
    // countdown value (started_at is cleared on pause so wall-clock math would
    // give the wrong answer). For a running session, the load path prefers
    // wall-clock arithmetic over remaining_seconds.
    QJsonObject root;
    root[QStringLiteral("is_running")]          = m_running;
    root[QStringLiteral("phase")]               = m_phase;
    root[QStringLiteral("started_at")]          = m_phaseStartedAt.isValid()
        ? m_phaseStartedAt.toString(Qt::ISODateWithMs)
        : QString();
    root[QStringLiteral("planned_duration_ms")] = qint64(cycleSeconds()) * 1000;
    root[QStringLiteral("remaining_seconds")]   = m_remainingSeconds;
    root[QStringLiteral("task_id")]             = m_currentTaskId;
    root[QStringLiteral("work_cycles_since_long_break")] = m_workCyclesSinceLongBreak;
    root[QStringLiteral("updated_at")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    const QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    // QSaveFile = temp + atomic rename. Same pattern as FileManager. A
    // commit() failure (out of disk space, perms changed) is logged but does
    // NOT abort the caller — losing the disk write is preferable to dropping
    // the in-memory state mutation that triggered it.
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning("PomodoroService: cannot open %s for writing: %s",
                 qPrintable(path), qPrintable(out.errorString()));
        return;
    }
    out.write(data);
    if (!out.commit()) {
        qWarning("PomodoroService: failed to commit %s: %s",
                 qPrintable(path), qPrintable(out.errorString()));
    }
}

void PomodoroService::clearStateFromDisk()
{
    const QString path = stateFilePath();
    if (path.isEmpty()) return;
    if (QFile::exists(path)) {
        if (!QFile::remove(path))
            qWarning("PomodoroService: failed to remove %s", qPrintable(path));
    }
}

void PomodoroService::loadStateFromDisk()
{
    const QString path = stateFilePath();
    if (path.isEmpty()) return;

    QFile in(path);
    if (!in.exists())
        return; // No prior session — nothing to restore, normal first launch.
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("PomodoroService: cannot open %s for reading: %s",
                 qPrintable(path), qPrintable(in.errorString()));
        return;
    }
    const QByteArray data = in.readAll();
    in.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("PomodoroService: %s is invalid JSON (%s); deleting and starting fresh",
                 qPrintable(path), qPrintable(err.errorString()));
        clearStateFromDisk();
        return;
    }

    const QJsonObject root = doc.object();
    const bool wasRunning = root.value(QStringLiteral("is_running")).toBool(false);
    const QString phase   = root.value(QStringLiteral("phase")).toString(QStringLiteral("idle"));
    const QString startedAtStr = root.value(QStringLiteral("started_at")).toString();
    const qint64 plannedMs = qint64(root.value(QStringLiteral("planned_duration_ms")).toDouble(0));
    const int storedRemaining = root.value(QStringLiteral("remaining_seconds")).toInt(0);
    const QString taskId  = root.value(QStringLiteral("task_id")).toString();
    const int workCycles  = root.value(QStringLiteral("work_cycles_since_long_break")).toInt(0);

    // Restore caller-supplied bits regardless of running/idle path.
    if (!taskId.isEmpty()) {
        m_currentTaskId = taskId;
        emit currentTaskIdChanged();
    }
    if (workCycles >= 0)
        m_workCyclesSinceLongBreak = workCycles;

    // Reject obviously-corrupt phase tokens. Falling back to idle is safer
    // than constructing a session with an unknown phase string the state
    // machine has no transition for.
    const bool phaseValid = (phase == QStringLiteral("work")
                          || phase == QStringLiteral("shortBreak")
                          || phase == QStringLiteral("longBreak")
                          || phase == QStringLiteral("idle"));
    if (!phaseValid) {
        qWarning("PomodoroService: %s contains unknown phase %s; ignoring",
                 qPrintable(path), qPrintable(phase));
        clearStateFromDisk();
        return;
    }

    if (!wasRunning) {
        // Clean / paused state: just restore the phase + remaining counter
        // for display. No timer arm, no signal beyond the property updates.
        // QML will show "Продолжить" because phase != idle.
        m_phase = phase;
        if (storedRemaining > 0)
            m_remainingSeconds = storedRemaining;
        emit phaseChanged();
        emit remainingSecondsChanged();
        return;
    }

    // Running case: figure out how much wall-clock time has elapsed since the
    // session was last saved. If started_at is missing or unparseable, fall
    // back to storedRemaining (we know the user was running, but we don't
    // know for how long — give them the benefit of the doubt and resume at
    // the recorded countdown rather than skipping the phase entirely).
    const QDateTime startedAt = QDateTime::fromString(startedAtStr, Qt::ISODateWithMs);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    qint64 elapsedMs = 0;
    if (startedAt.isValid() && startedAt.timeSpec() == Qt::UTC) {
        const qint64 deltaMs = startedAt.msecsTo(nowUtc);
        // Negative delta = clock moved backwards (DST? user changed system
        // clock?). Treat as zero-elapsed so we don't fast-forward incorrectly.
        elapsedMs = deltaMs > 0 ? deltaMs : 0;
    } else {
        // No reliable anchor → resume at storedRemaining.
        m_phase = phase;
        if (storedRemaining > 0)
            m_remainingSeconds = storedRemaining;
        m_running = true;
        m_phaseStartedAt = nowUtc; // re-anchor so future saves are sane
        m_timer.start();
        emit phaseChanged();
        emit remainingSecondsChanged();
        emit runningChanged();
        saveStateToDisk();
        emit sessionRestored(0);
        return;
    }

    // Phase still in flight: arm the timer at the remaining duration and emit
    // sessionRestored so UI can show "Pomodoro resumed (was running N seconds
    // ago)". We re-anchor m_phaseStartedAt to (now - elapsedSoFar) so the
    // on-disk planned_duration_ms vs started_at math stays consistent across
    // any subsequent saves.
    if (plannedMs > 0 && elapsedMs < plannedMs) {
        m_phase = phase;
        const qint64 remainingMs = plannedMs - elapsedMs;
        // Clamp to int range; planned values are bounded by clampDurationMinutes
        // so this is conservative, not actually reachable.
        m_remainingSeconds = static_cast<int>(qMin<qint64>(remainingMs / 1000,
                                                            INT_MAX));
        if (m_remainingSeconds < 1) m_remainingSeconds = 1;
        m_running = true;
        m_phaseStartedAt = startedAt;
        m_timer.start();
        emit phaseChanged();
        emit remainingSecondsChanged();
        emit runningChanged();
        // Rewrite to disk so updated_at reflects "we restored at T". Future
        // crash recovery starts from this fresh anchor.
        saveStateToDisk();
        emit sessionRestored(static_cast<int>(elapsedMs / 1000));
        return;
    }

    // Phase boundary already passed while the app was dead — replay the
    // phase-end machinery (bump completion counter, transition to next phase)
    // exactly as if onTick() had reached zero. We do NOT fire the user-facing
    // notification (no point — the user wasn't there to see it land in real
    // time, and a stale toast on startup would be annoying).
    //
    // If multiple phases have elapsed (user crashed 2h ago with 25-min cycles),
    // we only advance ONE phase boundary. Walking the full chain would require
    // simulating the entire onTick state machine; for simplicity we drop the
    // user at the start of the next phase. Better than skipping them to
    // mid-future-work.
    if (phase == QStringLiteral("work")) {
        // Replicate the completion accounting onTick does at remainingSeconds=0.
        const QDate today = QDate::currentDate();
        if (m_completedDate != today) {
            m_completedDate = today;
            m_completedToday = 1;
        } else {
            m_completedToday++;
        }
        saveCompletedToday();
        emit completedCyclesTodayChanged();

        m_workCyclesSinceLongBreak++;
        const bool dueLong = (m_cyclesBeforeLongBreak > 0)
            && (m_workCyclesSinceLongBreak % m_cyclesBeforeLongBreak == 0);
        enterPhase(dueLong ? QStringLiteral("longBreak") : QStringLiteral("shortBreak"));
        if (dueLong)
            m_workCyclesSinceLongBreak = 0;
    } else {
        // shortBreak / longBreak / idle-but-marked-running (paranoia): roll
        // into the next work phase, fresh anchor.
        enterPhase(QStringLiteral("work"));
    }
    // Continuous mode — keep the timer running, same as onTick's tail.
    m_running = true;
    m_timer.start();
    emit runningChanged();
    saveStateToDisk();
    // Tell the UI the previous phase ended while the app was gone. The
    // argument is "how long ago we observe the session restarting" — the
    // number of seconds the now-completed phase had been running for.
    emit sessionRestored(static_cast<int>(elapsedMs / 1000));
}
