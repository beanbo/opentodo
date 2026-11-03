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

    // Iter-9: load persisted m_lastScheduledFor so the recurring-advance
    // detection and the "which reminders did we arm" set survive app restart
    // (Android alarms outlive the process, so a reminder armed in a previous
    // run must still be cancelled when its task is completed).
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

    // The push toggle lives in DataStore settings; settingsChanged fires for
    // every setting, so only reschedule when the effective push state flips.
    connect(m_dataStore, &DataStore::settingsChanged, this, [this]() {
        if (pushEnabled() != m_lastPushEnabled)
            rescheduleAll();
    });

    // Log a reminder in the notification centre when it really fires — the
    // platform backend reports the scheduling id, we resolve the task here.
    connect(m_notif, &NotificationService::scheduledNotificationFired,
            this, &ReminderScheduler::onScheduledNotificationFired);

    // Initial synchronous pass for reminders already in memory.
    rescheduleAll();
}

// B-5.10: Start (or restart) the debounce timer. Called on tasksChanged.
void ReminderScheduler::onDataSignal()
{
    m_debounceTimer.start();
}

void ReminderScheduler::setHonourPushSetting(bool honour)
{
    if (m_honourPushSetting == honour)
        return;
    m_honourPushSetting = honour;
    rescheduleAll();
}

bool ReminderScheduler::pushEnabled() const
{
    if (!m_honourPushSetting || !m_dataStore)
        return true;
    // The mobile settings screen writes the toggle through
    // settingsViewModel.updateSetting() → DataStore. Older builds kept it in
    // QSettings (QML Settings); honour that value when the store has none.
    QVariant v = m_dataStore->setting(QLatin1String(kPushSettingKey));
    if (!v.isValid() || v.isNull())
        v = QSettings().value(QLatin1String(kPushSettingKey));
    if (!v.isValid() || v.isNull())
        return true;
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().trimmed().toLower();
        if (s.isEmpty())
            return true;
        return s != QStringLiteral("false") && s != QStringLiteral("0");
    }
    return v.toBool();
}

void ReminderScheduler::onScheduledNotificationFired(const QString &id,
                                                     const QString &title,
                                                     const QString &body)
{
    if (!m_dataStore || !m_notif)
        return;
    // Ids that don't map to a live task (daily review, diagnostics) are not
    // task reminders and stay out of the notification centre.
    const QString taskId = m_dataStore->taskIdForNotification(id);
    if (taskId.isEmpty())
        return;
    const Task *task = m_dataStore->taskById(taskId);
    emit m_notif->reminderFired(taskId,
                                task ? task->content() : title,
                                task ? task->description() : body);
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
#if QT_CONFIG(timezone)
    QTimeZone tz = QTimeZone::systemTimeZone();
    if (!due.timezone.isEmpty()) {
        const QTimeZone parsed(due.timezone.toUtf8());
        if (parsed.isValid())
            tz = parsed;
    }
    const bool foreignZone = tz != QTimeZone::systemTimeZone();
#else
    // Qt for WebAssembly is built without the timezone feature: there is no
    // IANA database, so the reminder's own zone is ignored and local time used.
    const QTimeZone tz(QTimeZone::LocalTime);
    const bool foreignZone = false;
#endif

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
        if (triggerTime.isValid() && foreignZone)
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

    // Mobile "Пуш-уведомления" toggle: when off, pending reminders are
    // cancelled below (cancelScheduled) and nothing new is armed.
    const bool push = pushEnabled();
    m_lastPushEnabled = push;

    // Live reminders of OPEN tasks seen in this pass. Anything this scheduler
    // armed earlier (m_lastScheduledFor) that is not in here belongs to a task
    // that is now completed, deleted (by any path, including sync) or gone —
    // it is cancelled after the loop.
    QSet<QString> openTaskReminders;

    const auto tasks = m_dataStore->allTasks();
    for (auto *task : tasks) {
        if (!task || task->isDeleted() || task->isCompleted())
            continue;

        const auto reminders = m_dataStore->remindersForTask(task->id());
        for (auto *reminder : reminders) {
            if (!reminder || reminder->isDeleted())
                continue;
            openTaskReminders.insert(reminder->id());

            const QDateTime triggerTime = resolveTriggerTime(reminder, task);

            // B-5.13: Recurring task re-arming. When a recurring task's due
            // date advances (completion re-arm), the previous schedule is stale;
            // the unconditional cancel below + reschedule covers it, and the
            // recorded due date below lets future advances be told apart.
            const QString currentDueDate = task->dueDate();

            // Cancel the pending schedule before re-arming to avoid duplicates.
            // cancelScheduled (not cancelNotification): a reminder that already
            // fired and is still on screen must survive this pass — it runs on
            // every edit, sync apply and app start.
            m_notif->cancelScheduled(reminder->id());

            if (!push || !triggerTime.isValid() || triggerTime <= now)
                continue;

            m_notif->scheduleNotification(task->content(),
                                          task->description(),
                                          triggerTime,
                                          reminder->id());
            // NB: reminderFired is NOT emitted here — scheduling is not firing.
            // The backend reports the real expiry via scheduledNotificationFired
            // (see onScheduledNotificationFired).

            // B-5.13: Record the due date for which this reminder was scheduled.
            m_lastScheduledFor.insert(reminder->id(), currentDueDate);
        }
    }

    // A reminder armed while its task was open must not fire after the task
    // is completed or deleted (the loop above skips such tasks, so it used to
    // leave their pending timers / alarms running). Full cancel — a stale
    // reminder already on screen is dismissed too — and forget the entry so
    // later passes cost nothing. Uncompleting the task re-arms it normally.
    for (auto it = m_lastScheduledFor.begin(); it != m_lastScheduledFor.end(); ) {
        if (!openTaskReminders.contains(it.key())) {
            m_notif->cancelNotification(it.key());
            persistDrops.append(it.key());
            it = m_lastScheduledFor.erase(it);
        } else {
            ++it;
        }
    }

    // Iter-9: persist m_lastScheduledFor changes so they survive app restart.
    // We re-write the whole group rather than tracking deltas — the map is
    // small (one entry per active reminder) and rescheduleAll is already
    // debounced for tasksChanged bursts.
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
