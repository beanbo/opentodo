#pragma once
#include <QObject>
#include <QTimer>
#include <QDate>
#include <QQmlEngine>

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

public:
    explicit PomodoroService(NotificationService *notif, QObject *parent = nullptr);

    int remainingSeconds() const { return m_remainingSeconds; }
    int cycleSeconds() const {
        if (m_phase == QStringLiteral("shortBreak")) return m_shortBreakMinutes * 60;
        if (m_phase == QStringLiteral("longBreak"))  return m_longBreakMinutes * 60;
        return m_workMinutes * 60;
    }
    bool running() const { return m_running; }
    QString phase() const { return m_phase; }
    int completedCyclesToday() const { return m_completedToday; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void reset();

    // Re-read durations from QSettings. If a session is currently active,
    // the change is deferred until the next phase boundary.
    Q_INVOKABLE void reloadDurationsFromSettings();

signals:
    void remainingSecondsChanged();
    void runningChanged();
    void phaseChanged();
    void completedCyclesTodayChanged();

private slots:
    void onTick();

private:
    void enterPhase(const QString &phase);
    void loadCompletedToday();
    void saveCompletedToday();
    void loadDurationsFromSettings();
    void applyPendingDurationChanges();

    NotificationService *m_notif;
    QTimer m_timer;
    int m_remainingSeconds = 25 * 60;
    bool m_running = false;
    QString m_phase = QStringLiteral("idle");
    int m_completedToday = 0;
    QDate m_completedDate;

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
