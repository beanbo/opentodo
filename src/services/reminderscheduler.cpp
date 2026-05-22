#include "reminderscheduler.h"

#include "storage/datastore.h"
#include "services/notificationservice.h"
#include "model/reminder.h"
#include "model/task.h"

#include <QDateTime>
#include <QSet>
#include <QSettings>
#include <QTime>
#include <QTimeZone>

static constexpr int DEBOUNCE_MS = 500;
static constexpr auto kLastScheduledForGroup = "reminder/lastScheduledFor";

ReminderScheduler::ReminderScheduler(DataStore *dataStore,
                                     NotificationService *notifService,
                                     QObject *parent)
    : QObject(parent)
    , m_dataStore(dataStore)
    , m_notif(notifService)
{
    if (!m_dataStore || !m_notif)
        return;

    // Iter-9: load persisted m_lastScheduledFor so the iter-8 emit-once-per-
    // reminder guard survives app restart. Without this, every cold start
    // would treat all already-scheduled reminders as "first scheduling" and
    // re-emit reminderFired, duplicating entries in the in-app notification
    // centre on every launch.
    {
        QSettings s;
        s.beginGroup(QLatin1String(kLastScheduledForGroup));
        const auto keys = s.allKeys();
        for (const QString &key : keys)
            m_lastScheduledFor.insert(key, s.value(key).toString());
        s.endGroup();
    }

    // B-5.10: Replace the old aggregate dataChanged() connection with specific
    // signals only. dataChanged() fires for every single field mutation, causing
    // reschedule storms during bulk import or sync apply.

    // remindersChanged → direct connection so the scheduler fires synchronously
    // when a reminder is added/modified (important for initial population and tests).
    connect(m_dataStore, &DataStore::remindersChanged,
            this, &ReminderScheduler::rescheduleAll);

    // tasksChanged → debounced, because tasks fire very rapidly during bulk import
    // (one signal per task added). The 500ms window coalesces the burst into one pass.
    connect(m_dataStore, &DataStore::tasksChanged,
            this, &ReminderScheduler::onDataSignal);

    // B-5.10: 500ms single-shot debounce timer for tasksChanged bursts.
    m_debounceTimer.setInterval(DEBOUNCE_MS);
    m_debounceTimer.setSingleShot(true);
    connect(&m_debounceTimer, &QTimer::timeout, this, &ReminderScheduler::rescheduleAll);

    // Initial synchronous pass for reminders already in memory.
    rescheduleAll();
}

// B-5.10: Start (or restart) the debounce timer. Called on tasksChanged.
void ReminderScheduler::onDataSignal()
{
    m_debounceTimer.start();
}

// B-5.11: Parse reminder trigger time, respecting the reminder's timezone field.
QDateTime ReminderScheduler::resolveTriggerTime(const Reminder *reminder,
                                                 const Task *task) const
{
    // B-5.12: Read default reminder time from DataStore settings; fall back to 09:00.
    // Setting key: "default_reminder_time", format "HH:MM".
    QTime defaultTime(9, 0);
    if (m_dataStore) {
        const QString dtSetting =
            m_dataStore->setting(QStringLiteral("default_reminder_time")).toString().trimmed();
        if (!dtSetting.isEmpty()) {
            const QTime parsed = QTime::fromString(dtSetting, QStringLiteral("HH:mm"));
            if (parsed.isValid())
                defaultTime = parsed;
        }
    }

    const ReminderDue due = reminder->due();

    // B-5.11: Determine which timezone to use.
    // Priority: reminder.due.timezone → local timezone.
    QTimeZone tz = QTimeZone::systemTimeZone();
    if (!due.timezone.isEmpty()) {
        const QTimeZone parsed(due.timezone.toUtf8());
        if (parsed.isValid())
            tz = parsed;
    }

    QDateTime triggerTime;
    if (!due.datetime.isEmpty()) {
        // Full ISO datetime on the reminder.
        triggerTime = QDateTime::fromString(due.datetime, Qt::ISODate);
        // BUG-FIX (C-5): QDateTime::fromString with Qt::ISODate (no zone
        // suffix on the string) parses the wall-clock as *local* time.
        // The original code then called .toTimeZone(tz) which CONVERTS
        // the absolute instant into the target zone — fine for display,
        // but wrong for a reminder: the user wrote "14:00 Europe/Moscow"
        // expecting the alarm to fire at 14:00 in Moscow. With the old
        // code, a user on UTC+0 would see the alarm fire at 14:00 UTC
        // (= 17:00 Moscow), not 14:00 Moscow (= 11:00 UTC).
        //
        // setTimeZone() *retags* the QDateTime without shifting the
        // wall-clock value, which is what the reminder semantics
        // actually require.
        if (triggerTime.isValid() && tz != QTimeZone::systemTimeZone())
            triggerTime.setTimeZone(tz);
    } else if (!due.date.isEmpty()) {
        // Date-only on the reminder — use the default reminder time. B-5.12
        const QDate d = QDate::fromString(due.date, Qt::ISODate);
        if (d.isValid())
            triggerTime = QDateTime(d, defaultTime, tz);
    }

    if (!triggerTime.isValid() && reminder->type() == QStringLiteral("relative")) {
        // Relative reminder: subtract minuteOffset from the parent task's due time.
        const QString taskDt   = task->dueDatetime();
        const QString taskDate = task->dueDate();

        QDateTime base;
        if (!taskDt.isEmpty()) {
            base = QDateTime::fromString(taskDt, Qt::ISODate);
        } else if (!taskDate.isEmpty()) {
            const QDate d = QDate::fromString(taskDate, Qt::ISODate);
            if (d.isValid())
                base = QDateTime(d, defaultTime, tz);  // B-5.12
        }

        if (base.isValid())
            triggerTime = base.addSecs(-static_cast<qint64>(reminder->minuteOffset()) * 60);
    }

    return triggerTime;
}

void ReminderScheduler::rescheduleAll()
{
    if (!m_dataStore || !m_notif)
        return;

    const QDateTime now = QDateTime::currentDateTime();

    // Bug #2 (May 2026): cascade-cancel pre-scheduled QTimers for any
    // reminder that is now tombstoned. removeTask() tombstones every
    // reminder attached to the deleted task, but the loop below filters
    // tombstones out via remindersForTask(), so any QTimer armed by an
    // earlier scheduleNotification() call would keep ticking and the
    // toast would fire for a task that no longer exists. Walk the full
    // list (including tombstones) once up-front and cancel each deleted
    // reminder explicitly. Survivors are re-armed by the main loop below.
    // Iter-9: collect drops so QSettings is touched once per rescheduleAll
    // rather than once per tombstoned reminder.
    QStringList persistDrops;
    // Iter-10: track every reminder id we observed in this pass — survivors
    // are added in the main loop below; the rest is the orphan set we drop
    // from QSettings to avoid the lastScheduledFor blob growing forever when
    // a reminder vanishes by a path that doesn't go through removeReminder
    // (e.g. state.json restore from an older backup, or sync apply hard-
    // tombstone followed by vacuum).
    QSet<QString> knownIds;
    const auto allReminders = m_dataStore->allRemindersIncludingDeleted();
    for (auto *r : allReminders) {
        if (!r) continue;
        knownIds.insert(r->id());
        if (r->isDeleted()) {
            m_notif->cancelNotification(r->id());
            // Iter-8: also drop the per-reminder tracking entry so the map
            // doesn't grow unbounded as users create + delete reminders
            // over a long session.
            if (m_lastScheduledFor.remove(r->id()) > 0)
                persistDrops.append(r->id());
        }
    }
    // Drop in-memory entries for reminders no longer present in DataStore
    // at all. Collect dropped ids into persistDrops so the QSettings
    // group reflects reality at the end of this pass.
    for (auto it = m_lastScheduledFor.begin(); it != m_lastScheduledFor.end(); ) {
        if (!knownIds.contains(it.key())) {
            persistDrops.append(it.key());
            it = m_lastScheduledFor.erase(it);
        } else {
            ++it;
        }
    }

    const auto tasks = m_dataStore->allTasks();
    for (auto *task : tasks) {
        if (!task || task->isDeleted() || task->isCompleted())
            continue;

        const auto reminders = m_dataStore->remindersForTask(task->id());
        for (auto *reminder : reminders) {
            if (!reminder || reminder->isDeleted())
                continue;

            const QDateTime triggerTime = resolveTriggerTime(reminder, task);

            // B-5.13: Recurring task re-arming detection.
            // Track the due date at last scheduling time; if the recurring task's
            // due date has advanced (after completion and re-arm), the previous
            // notification is already stale — we cancel and reschedule unconditionally
            // below, so no extra cancel step is needed. We record the new date so we
            // can detect future advances.
            const QString currentDueDate = task->dueDate();
            const QString lastScheduled  = m_lastScheduledFor.value(reminder->id());
            if (task->dueIsRecurring() && !currentDueDate.isEmpty()
                    && !lastScheduled.isEmpty() && currentDueDate != lastScheduled) {
                // Due date has advanced — reschedule for new instance (handled below).
                // The old notification is cancelled by the unconditional cancel call.
            }

            // Always cancel before re-scheduling to avoid duplicate notifications.
            m_notif->cancelNotification(reminder->id());

            if (!triggerTime.isValid() || triggerTime <= now)
                continue;

            m_notif->scheduleNotification(task->content(),
                                          task->description(),
                                          triggerTime,
                                          reminder->id());

            // Iter-8: only log to the in-app notification centre on the FIRST
            // scheduling of this reminder, or when a recurring task's due date
            // advances. rescheduleAll() runs on every tasksChanged /
            // remindersChanged tick — emitting unconditionally was producing
            // duplicate "Напоминание: …" entries in the centre on every keypress
            // edit of any task. The OS-side toast (real "fired" event) is
            // already tracked separately when the QTimer actually expires;
            // the per-platform showNotification() is the right surface for a
            // "true fired" signal.
            const bool isFirstScheduling = !m_lastScheduledFor.contains(reminder->id());
            const bool recurringAdvance  = task->dueIsRecurring()
                && !lastScheduled.isEmpty()
                && currentDueDate != lastScheduled;
            if (isFirstScheduling || recurringAdvance) {
                emit m_notif->reminderFired(task->id(),
                                            task->content(),
                                            task->description());
            }

            // B-5.13: Record the due date for which this reminder was scheduled.
            m_lastScheduledFor.insert(reminder->id(), currentDueDate);
        }
    }

    // Iter-9: persist m_lastScheduledFor changes so the iter-8 emit-once-per-
    // reminder guard survives app restart. We re-write the whole group rather
    // than tracking deltas — the map is small (one entry per active reminder)
    // and rescheduleAll is already debounced for tasksChanged bursts.
    if (!m_lastScheduledFor.isEmpty() || !persistDrops.isEmpty()) {
        QSettings s;
        s.beginGroup(QLatin1String(kLastScheduledForGroup));
        for (const QString &id : persistDrops)
            s.remove(id);
        for (auto it = m_lastScheduledFor.constBegin();
             it != m_lastScheduledFor.constEnd(); ++it) {
            s.setValue(it.key(), it.value());
        }
        s.endGroup();
    }
}
