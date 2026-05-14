#include "pomodoroservice.h"
#include "services/notificationservice.h"
#include <QSettings>

PomodoroService::PomodoroService(NotificationService *notif, QObject *parent)
    : QObject(parent), m_notif(notif)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &PomodoroService::onTick);
    loadDurationsFromSettings();
    // Initial idle remaining shows the configured work duration.
    m_remainingSeconds = m_workMinutes * 60;
    loadCompletedToday();
}

void PomodoroService::start()
{
    enterPhase(QStringLiteral("work"));
    m_running = true;
    emit runningChanged();
    m_timer.start();
}

void PomodoroService::pause()
{
    if (!m_running) return;
    m_running = false;
    emit runningChanged();
    m_timer.stop();
}

void PomodoroService::resume()
{
    if (m_running) return;
    if (m_phase == QStringLiteral("idle")) {
        start();
        return;
    }
    m_running = true;
    emit runningChanged();
    m_timer.start();
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
}

void PomodoroService::reset()
{
    m_timer.stop();
    m_running = false;
    m_phase = QStringLiteral("idle");
    m_workCyclesSinceLongBreak = 0;
    // Apply any pending duration changes now that we're idle.
    applyPendingDurationChanges();
    m_remainingSeconds = m_workMinutes * 60;
    emit runningChanged();
    emit phaseChanged();
    emit remainingSecondsChanged();
}

void PomodoroService::enterPhase(const QString &phase)
{
    // A phase boundary is the right time to apply any pending duration changes.
    applyPendingDurationChanges();

    m_phase = phase;
    if (phase == QStringLiteral("work"))
        m_remainingSeconds = m_workMinutes * 60;
    else if (phase == QStringLiteral("shortBreak"))
        m_remainingSeconds = m_shortBreakMinutes * 60;
    else if (phase == QStringLiteral("longBreak"))
        m_remainingSeconds = m_longBreakMinutes * 60;
    else
        m_remainingSeconds = m_workMinutes * 60;
    emit phaseChanged();
    emit remainingSecondsChanged();
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
    if (m_workMinutes           < 1)  m_workMinutes = 1;
    if (m_shortBreakMinutes     < 1)  m_shortBreakMinutes = 1;
    if (m_longBreakMinutes      < 1)  m_longBreakMinutes = 1;
    if (m_cyclesBeforeLongBreak < 2)  m_cyclesBeforeLongBreak = 2;

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

    m_pendingWorkMinutes           = newWork  >= 1 ? newWork  : 1;
    m_pendingShortBreakMinutes     = newShort >= 1 ? newShort : 1;
    m_pendingLongBreakMinutes      = newLong  >= 1 ? newLong  : 1;
    m_pendingCyclesBeforeLongBreak = newCyc   >= 2 ? newCyc   : 2;
    m_durationsDirty = true;

    // If no session is active (idle / not running), apply immediately so the
    // displayed cycle length reflects the new value right away.
    if (m_phase == QStringLiteral("idle") || !m_running) {
        applyPendingDurationChanges();
        if (m_phase == QStringLiteral("idle")) {
            m_remainingSeconds = m_workMinutes * 60;
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
