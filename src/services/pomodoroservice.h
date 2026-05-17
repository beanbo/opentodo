#pragma once
#include <QObject>
#include <QTimer>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QQmlEngine>
#include <climits>

class NotificationService;

class PomodoroService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Pomodoro is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(int cycleSeconds READ cycleSeconds NOTIFY phaseChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)        // "work" | "shortBreak" | "longBreak" | "idle"
    Q_PROPERTY(int completedCyclesToday READ completedCyclesToday NOTIFY completedCyclesTodayChanged)
    Q_PROPERTY(QString currentTaskId READ currentTaskId WRITE setCurrentTaskId NOTIFY currentTaskIdChanged)

public:
    explicit PomodoroService(NotificationService *notif, QObject *parent = nullptr);

    int remainingSeconds() const { return m_remainingSeconds; }
    int cycleSeconds() const {
        // Cast to qint64 before *60 so a future relaxation of the upper-bound
        // clamp (or an unclamped path that slips through) can't silently
        // overflow int. The result is clamped back to int range below — values
        // outside [0, INT_MAX] indicate a corrupt/attacker-supplied setting
        // and are coerced to 60 s (1 min) as a safe fallback.
        qint64 totalSeconds = 0;
        if (m_phase == QStringLiteral("shortBreak"))     totalSeconds = qint64(m_shortBreakMinutes) * 60;
        else if (m_phase == QStringLiteral("longBreak")) totalSeconds = qint64(m_longBreakMinutes) * 60;
        else                                              totalSeconds = qint64(m_workMinutes) * 60;
        if (totalSeconds <= 0 || totalSeconds > INT_MAX) return 60;
        return static_cast<int>(totalSeconds);
    }
    bool running() const { return m_running; }
    QString phase() const { return m_phase; }
    int completedCyclesToday() const { return m_completedToday; }
    QString currentTaskId() const { return m_currentTaskId; }
    void setCurrentTaskId(const QString &taskId);

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void reset();

    // Re-read durations from QSettings. If a session is currently active,
    // the change is deferred until the next phase boundary.
    Q_INVOKABLE void reloadDurationsFromSettings();

    // Flush in-memory session state to disk. Intended for QCoreApplication::
    // aboutToQuit so the last second of a session reaches the file even when
    // every state-mutation path already writes on its own. Public so main.cpp
    // can wire it up; safe to call from any thread that owns the service.
    Q_INVOKABLE void flushStateToDisk();

    // Absolute path of the persisted session-state file. Exposed for tests so
    // they can assert "the file exists/doesn't exist after operation X" without
    // duplicating the QStandardPaths root resolution.
    Q_INVOKABLE QString stateFilePath() const;

signals:
    void remainingSecondsChanged();
    void runningChanged();
    void phaseChanged();
    void completedCyclesTodayChanged();
    void currentTaskIdChanged();

    // Emitted exactly once during construction if a running session was
    // restored from disk. The argument is the number of seconds between the
    // persisted started_at and the moment the service finished loading. UI can
    // surface "Resumed Pomodoro from N minutes ago" via this. Not emitted for
    // a clean idle restore (file says is_running=false) or when no file exists.
    void sessionRestored(int elapsedSecondsAgo);

private slots:
    void onTick();

private:
    void enterPhase(const QString &phase);
    void loadCompletedToday();
    void saveCompletedToday();
    void loadDurationsFromSettings();
    void applyPendingDurationChanges();

    // Session-state persistence ─ writes a small JSON blob to
    // $APPDATA/OpenTodo/pomodoro_state.json on every state mutation so a crash
    // mid-session can be resumed at the correct remaining time on next launch.
    // Atomic via QSaveFile (same pattern as FileManager::performSave).
    void saveStateToDisk();
    void loadStateFromDisk();
    void clearStateFromDisk();
    void ensureDataDirectory() const;

    NotificationService *m_notif;
    QTimer m_timer;
    int m_remainingSeconds = 25 * 60;
    bool m_running = false;
    QString m_phase = QStringLiteral("idle");
    int m_completedToday = 0;
    QDate m_completedDate;

    // Wall-clock at the moment the current phase was entered (or resumed). Used
    // by the on-disk persistence layer: on next launch we compute
    // (now - m_phaseStartedAt) and either resume mid-phase or advance through
    // any phases that completed while the app was dead. Stored in UTC for
    // DST-stability. Null QDateTime means "phase is idle, no anchor".
    QDateTime m_phaseStartedAt;

    // Optional caller-supplied task id the session is tied to. Persisted so a
    // crash-recovered session still knows which task it was about. Empty when
    // no task is associated. The UI may bind to this if/when it wires up
    // "start a Pomodoro for THIS task" — current QML doesn't.
    QString m_currentTaskId;

    // Path computed in the constructor; matches FileManager's
    // $APPDATA/OpenTodo (desktop) or $APPDATA (Android). Empty during
    // construction on platforms where QStandardPaths returns "" — in that
    // case persistence becomes a quiet no-op and the in-memory state machine
    // works exactly as before.
    QString m_dataPath;

    // User-configurable durations (read from QSettings, "pomodoro/...").
    int m_workMinutes = 25;
    int m_shortBreakMinutes = 5;
    int m_longBreakMinutes = 15;
    int m_cyclesBeforeLongBreak = 4;

    // Long-break tracking: counts completed work cycles since the last long break.
    int m_workCyclesSinceLongBreak = 0;

    // Pending values when settings change mid-session — applied on the next phase.
    bool m_durationsDirty = false;
    int m_pendingWorkMinutes = 25;
    int m_pendingShortBreakMinutes = 5;
    int m_pendingLongBreakMinutes = 15;
    int m_pendingCyclesBeforeLongBreak = 4;
};
