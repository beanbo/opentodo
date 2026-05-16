#include "pomodoroservice.h"
#include "services/notificationservice.h"
#include <QSettings>
#include <QtGlobal>
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
    m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
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
        m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
    else if (phase == QStringLiteral("shortBreak"))
        m_remainingSeconds = minutesToSecondsSafe(m_shortBreakMinutes);
    else if (phase == QStringLiteral("longBreak"))
        m_remainingSeconds = minutesToSecondsSafe(m_longBreakMinutes);
    else
        m_remainingSeconds = minutesToSecondsSafe(m_workMinutes);
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
