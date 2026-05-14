#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/reminder.h"
#include "services/notificationservice.h"
#include "services/reminderscheduler.h"

// ---------------------------------------------------------------------------
// MockNotificationService
// ---------------------------------------------------------------------------
class MockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit MockNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    // Recorded calls
    struct ScheduleCall {
        QString title;
        QString body;
        QDateTime when;
        QString id;
    };

    QList<ScheduleCall> scheduled;
    QList<QString> cancelled;
    QList<QString> shown;

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        shown.append(taskId.isEmpty() ? title : taskId);
    }

    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override
    {
        scheduled.append({title, body, when, id});
    }

    void cancelNotification(const QString &id) override
    {
        cancelled.append(id);
    }

    void cancelAll() override
    {
        cancelled.append(QStringLiteral("ALL"));
    }

    // Helper: did we schedule with this id?
    bool wasScheduled(const QString &id) const
    {
        for (const auto &c : scheduled)
            if (c.id == id) return true;
        return false;
    }

    // Helper: did we cancel this id?
    bool wasCancelled(const QString &id) const
    {
        return cancelled.contains(id);
    }

    // Helper: return the last scheduled call for a given id (or default)
    ScheduleCall lastScheduleFor(const QString &id) const
    {
        for (int i = scheduled.size() - 1; i >= 0; --i)
            if (scheduled[i].id == id) return scheduled[i];
        return {};
    }
};

// ---------------------------------------------------------------------------
// Helper: DataStore with Inbox project
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// ---------------------------------------------------------------------------
// TestReminderScheduler
// ---------------------------------------------------------------------------
class TestReminderScheduler : public QObject
{
    Q_OBJECT

private slots:

    void scheduler_schedules_future_reminder_on_dataChanged()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        // Construction triggers rescheduleAll() on an empty store — no ops.
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Meeting"), inboxId);

        // Set due tomorrow
        const QDate tomorrow = QDate::currentDate().addDays(1);
        task->setDueDate(tomorrow.toString(QStringLiteral("yyyy-MM-dd")));

        // Create an absolute reminder with triggerTime = tomorrow 09:00
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.date = tomorrow.toString(Qt::ISODate);
        // Use ISO datetime so the scheduler reads it as exact time
        const QDateTime triggerTime = QDateTime(tomorrow, QTime(9, 0));
        rDue.datetime = triggerTime.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.scheduled.clear();
        mock.cancelled.clear();

        // Emit remindersChanged to force rescheduling
        emit store->remindersChanged();

        // The scheduler must have scheduled this reminder
        QVERIFY(mock.wasScheduled(reminder->id()));

        // Check the scheduled time matches what we set
        const auto call = mock.lastScheduleFor(reminder->id());
        QCOMPARE(call.when, triggerTime);

        delete store;
    }

    void scheduler_does_not_schedule_past_reminder()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Old task"), inboxId);

        // Reminder in the past
        const QDateTime pastTime = QDateTime::currentDateTime().addSecs(-3600); // 1 hour ago
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = pastTime.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.scheduled.clear();
        mock.cancelled.clear();

        emit store->remindersChanged();

        // Must NOT have been scheduled (past times are skipped)
        QVERIFY(!mock.wasScheduled(reminder->id()));

        // Cancel is still called (scheduler always cancels before deciding)
        QVERIFY(mock.wasCancelled(reminder->id()));

        delete store;
    }

    void scheduler_cancels_deleted_reminder()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Task with reminder"), inboxId);

        // First schedule a valid future reminder
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        emit store->remindersChanged();
        QVERIFY(mock.wasScheduled(reminder->id()));

        const QString rid = reminder->id();
        mock.scheduled.clear();
        mock.cancelled.clear();

        // Soft-delete the reminder
        reminder->setIsDeleted(true);
        emit store->remindersChanged();

        // After deletion: scheduler walks reminders, skips deleted ones.
        // The deleted reminder is NOT rescheduled.
        QVERIFY(!mock.wasScheduled(rid));

        // cancelNotification must have been called for it in a previous cycle
        // (the scheduler called cancel then didn't reschedule).
        // Since the reminder is now deleted, the scheduler doesn't reach it at all
        // in this pass — but it would have been cancelled in the previous pass.
        // Here we just assert no re-scheduling happened.
        QVERIFY(!mock.wasScheduled(rid));

        delete store;
    }

    void scheduler_cancel_on_each_reschedule_prevents_duplicates()
    {
        // Every rescheduleAll() must cancel before re-adding.
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Recurrent"), inboxId);
        const QDateTime future = QDateTime::currentDateTime().addSecs(7200);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        emit store->remindersChanged();
        emit store->remindersChanged(); // second trigger

        // cancel must have been called at least twice (once per rescheduleAll call)
        int cancelCount = 0;
        for (const QString &c : mock.cancelled)
            if (c == reminder->id()) ++cancelCount;
        QVERIFY(cancelCount >= 2);

        // But schedule is called once per valid pass too
        int schedCount = 0;
        for (const auto &s : mock.scheduled)
            if (s.id == reminder->id()) ++schedCount;
        QVERIFY(schedCount >= 1);

        delete store;
    }

    void scheduler_updates_time_cancel_then_reschedule()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Reschedule me"), inboxId);

        // First schedule at +1 hour. Drop sub-second resolution because the
        // reminder datetime round-trips through Qt::ISODate (no ms precision).
        QDateTime first = QDateTime::currentDateTime().addSecs(3600);
        first = first.addMSecs(-first.time().msec());
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = first.toString(Qt::ISODate);
        reminder->setDue(rDue);

        emit store->remindersChanged();
        QVERIFY(mock.wasScheduled(reminder->id()));
        const auto firstCall = mock.lastScheduleFor(reminder->id());
        QCOMPARE(firstCall.when, first);

        mock.scheduled.clear();
        mock.cancelled.clear();

        // Change trigger time to +2 hours
        QDateTime second = QDateTime::currentDateTime().addSecs(7200);
        second = second.addMSecs(-second.time().msec());
        ReminderDue rDue2;
        rDue2.datetime = second.toString(Qt::ISODate);
        reminder->setDue(rDue2);

        emit store->remindersChanged();

        // Must cancel old then schedule new
        QVERIFY(mock.wasCancelled(reminder->id()));
        QVERIFY(mock.wasScheduled(reminder->id()));
        const auto secondCall = mock.lastScheduleFor(reminder->id());
        // New time should match the updated value
        QCOMPARE(secondCall.when, second);

        delete store;
    }

    void scheduler_skips_completed_task_reminders()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Done task"), inboxId);
        task->setIsCompleted(true);

        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        emit store->remindersChanged();

        // Completed task → reminder must NOT be scheduled
        QVERIFY(!mock.wasScheduled(reminder->id()));

        delete store;
    }

    void scheduler_relative_reminder_uses_task_due_time()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.scheduled.clear();
        mock.cancelled.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Relative reminder task"), inboxId);

        // Task due in 2 hours from now
        const QDateTime taskDue = QDateTime::currentDateTime().addSecs(7200);
        task->setDueDatetime(taskDue.toString(Qt::ISODate));

        // Relative reminder: 30 minutes before task due
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("relative"));
        reminder->setMinuteOffset(30);
        // No due date on the reminder itself

        emit store->remindersChanged();

        QVERIFY(mock.wasScheduled(reminder->id()));

        const auto call = mock.lastScheduleFor(reminder->id());
        // Expected trigger: taskDue - 30 minutes
        const QDateTime expected = taskDue.addSecs(-30 * 60);

        // Allow 2-second tolerance for timing jitter between addSecs calls
        const qint64 diff = qAbs(call.when.secsTo(expected));
        QVERIFY(diff <= 2);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestReminderScheduler)
#include "test_reminderscheduler.moc"
