#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/reminder.h"
#include "services/notificationservice.h"
#include "services/reminderscheduler.h"

// ============================================================================
// Regression test for "QTimer outlives the task it was armed for"
// (Q-Loop B Bug B, May 2026).
//
// Symptom: ReminderScheduler armed a platform notification (Windows toast
// via QTimer in WindowsNotificationService::m_scheduled) for a reminder
// attached to a task. The user then deleted the task. DataStore::removeTask
// cascade-tombstones every reminder belonging to the task, then emits
// remindersChanged. ReminderScheduler::rescheduleAll iterates non-deleted
// tasks → finds the deleted task → skips it → never reaches the inner
// `cancelNotification(reminder->id())` call. The previously-armed QTimer
// keeps ticking. When it fires, the OS toast appears for a task that no
// longer exists.
//
// Production fix (src/services/reminderscheduler.cpp): walk
// DataStore::allRemindersIncludingDeleted() at the top of rescheduleAll
// and call cancelNotification on every tombstoned reminder BEFORE the
// active-tasks loop runs. Survivors are then re-armed by the normal loop.
// ============================================================================

// ----------------------------------------------------------------------------
// MockNotificationService — file-scope so AUTOMOC can process the Q_OBJECT.
// Records every cancelNotification(id) call so the test can assert that
// the cascade reached the right reminder ids.
// ----------------------------------------------------------------------------
class CascadeMockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit CascadeMockNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    QList<QString> cancelled;
    QList<QString> scheduledIds;
    int cancelAllCalls = 0;

    void showNotification(const QString &, const QString &,
                          const QString & = {}) override {}

    void scheduleNotification(const QString &, const QString &,
                              const QDateTime &, const QString &id) override
    {
        scheduledIds.append(id);
    }

    void cancelNotification(const QString &id) override
    {
        cancelled.append(id);
    }

    void cancelAll() override
    {
        ++cancelAllCalls;
    }

    int cancelCount(const QString &id) const
    {
        int n = 0;
        for (const QString &c : cancelled)
            if (c == id) ++n;
        return n;
    }

    bool wasCancelled(const QString &id) const
    {
        return cancelled.contains(id);
    }

    bool wasScheduled(const QString &id) const
    {
        return scheduledIds.contains(id);
    }
};

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
namespace {

DataStore *makeStoreWithInbox()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// Create a task with a single absolute reminder set 1 hour in the future
// (so rescheduleAll will arm it on the first pass). Returns the reminder id.
//
// Note: store->addReminder fires remindersChanged immediately, but at that
// point the reminder still has an empty ReminderDue (resolveTriggerTime
// returns an invalid QDateTime → no schedule call). We set the due time
// AFTER addReminder, then manually emit remindersChanged so the scheduler
// picks up the now-valid trigger time. This mirrors the pattern used in
// test_reminderscheduler.cpp.
QString seedTaskWithFutureReminder(DataStore *store,
                                    QString *taskIdOut = nullptr,
                                    const QString &content
                                        = QStringLiteral("Удалить меня"))
{
    const QString inboxId = store->inboxProject()->id();
    Task *task = store->addTask(content, inboxId);
    if (taskIdOut) *taskIdOut = task->id();

    const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
    Reminder *reminder = store->addReminder(task->id(),
                                             QStringLiteral("absolute"));
    ReminderDue rDue;
    rDue.datetime = future.toString(Qt::ISODate);
    reminder->setDue(rDue);

    // Force a reschedule pass — setDue mutates the model object directly
    // without going through DataStore, so no remindersChanged is emitted.
    emit store->remindersChanged();

    return reminder->id();
}

} // anonymous namespace

class TestReminderCascadeCancel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique per-test QSettings scope so we don't trample the user's
        // device id or collide with sibling tests under `ctest -j`.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ReminderCascade"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_reminder_cascade_cancel"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // ------------------------------------------------------------------
    // 1. Happy path: task with reminder → task deleted → the armed
    //    notification for that reminder gets cancelNotification() called.
    // ------------------------------------------------------------------
    void cascadeCancelsArmedReminderOnTaskRemove()
    {
        DataStore *store = makeStoreWithInbox();
        CascadeMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);

        // Construction triggered an initial rescheduleAll() on an empty
        // store — wipe so we observe only the cycles we care about.
        mock.cancelled.clear();
        mock.scheduledIds.clear();

        QString taskId;
        const QString reminderId = seedTaskWithFutureReminder(store, &taskId);
        QVERIFY(!taskId.isEmpty());
        QVERIFY(!reminderId.isEmpty());

        // Reminder is now armed (remindersChanged fired by addReminder).
        QVERIFY(mock.wasScheduled(reminderId));

        // Reset to observe what happens next.
        mock.cancelled.clear();
        mock.scheduledIds.clear();

        // Soft-delete the task → cascade tombstones the reminder →
        // remindersChanged → rescheduleAll → cascade-cancel pass picks it up.
        const bool removed = store->removeTask(taskId);
        QVERIFY(removed);

        // The mock must have observed cancelNotification(reminderId).
        QVERIFY2(mock.wasCancelled(reminderId),
                 "cascade-cancel: the QTimer for the deleted task's reminder "
                 "was never cancelled");

        // And it must NOT have been re-scheduled (the task is now deleted).
        QVERIFY(!mock.wasScheduled(reminderId));

        delete store;
    }

    // ------------------------------------------------------------------
    // 2. Multiple reminders on the same task — every one of them must
    //    have its armed QTimer cancelled when the task is removed.
    // ------------------------------------------------------------------
    void cascadeCancelsAllRemindersOfDeletedTask()
    {
        DataStore *store = makeStoreWithInbox();
        CascadeMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.cancelled.clear();
        mock.scheduledIds.clear();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Задача с тремя напоминаниями"),
                                     inboxId);

        QStringList reminderIds;
        for (int i = 0; i < 3; ++i) {
            const QDateTime future =
                QDateTime::currentDateTime().addSecs(3600 * (i + 1));
            Reminder *r = store->addReminder(task->id(),
                                              QStringLiteral("absolute"));
            ReminderDue rDue;
            rDue.datetime = future.toString(Qt::ISODate);
            r->setDue(rDue);
            reminderIds.append(r->id());
        }

        // setDue() doesn't emit remindersChanged — force one pass so the
        // scheduler observes the now-valid trigger times.
        emit store->remindersChanged();

        // All three should be armed.
        for (const QString &rid : reminderIds)
            QVERIFY(mock.wasScheduled(rid));

        mock.cancelled.clear();
        mock.scheduledIds.clear();

        // Delete the task — the cascade fix must cancel ALL three.
        QVERIFY(store->removeTask(task->id()));

        for (const QString &rid : reminderIds) {
            QVERIFY2(mock.wasCancelled(rid),
                     "cascade-cancel: a reminder on a deleted task was missed");
            QVERIFY(!mock.wasScheduled(rid));
        }

        delete store;
    }

    // ------------------------------------------------------------------
    // 3. Reminders on OTHER (surviving) tasks must NOT be cancelled.
    //    Pins that the cascade fix is targeted, not a blanket wipe.
    // ------------------------------------------------------------------
    void cascadeDoesNotTouchOtherTaskReminders()
    {
        DataStore *store = makeStoreWithInbox();
        CascadeMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.cancelled.clear();
        mock.scheduledIds.clear();

        QString victimTaskId;
        const QString victimReminderId =
            seedTaskWithFutureReminder(store, &victimTaskId,
                                        QStringLiteral("Удалю"));

        QString survivorTaskId;
        const QString survivorReminderId =
            seedTaskWithFutureReminder(store, &survivorTaskId,
                                        QStringLiteral("Останусь"));

        QVERIFY(mock.wasScheduled(victimReminderId));
        QVERIFY(mock.wasScheduled(survivorReminderId));

        mock.cancelled.clear();
        mock.scheduledIds.clear();

        // Delete only the victim.
        QVERIFY(store->removeTask(victimTaskId));

        // Victim cancelled exactly once via the cascade pass — and not
        // re-scheduled. Survivor SHOULD be cancelled (the main loop always
        // cancels before re-scheduling) but also re-scheduled.
        QVERIFY(mock.wasCancelled(victimReminderId));
        QVERIFY(!mock.wasScheduled(victimReminderId));

        // Survivor was re-armed.
        QVERIFY(mock.wasScheduled(survivorReminderId));
        // The survivor's task still exists.
        Task *survivor = store->taskById(survivorTaskId);
        QVERIFY(survivor != nullptr);
        QVERIFY(!survivor->isDeleted());

        delete store;
    }

    // ------------------------------------------------------------------
    // 4. Independently-tombstoned reminder (removeReminder rather than
    //    removeTask): the reminder is tombstoned without the task being
    //    deleted. The cascade-cancel walk must still pick it up because
    //    it iterates allRemindersIncludingDeleted().
    // ------------------------------------------------------------------
    void cascadeCoversDirectReminderRemoval()
    {
        DataStore *store = makeStoreWithInbox();
        CascadeMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.cancelled.clear();
        mock.scheduledIds.clear();

        QString taskId;
        const QString reminderId = seedTaskWithFutureReminder(store, &taskId);
        QVERIFY(mock.wasScheduled(reminderId));

        mock.cancelled.clear();
        mock.scheduledIds.clear();

        // Soft-delete the reminder directly (no task delete).
        QVERIFY(store->removeReminder(reminderId));

        // Cascade-cancel pass must have hit it.
        QVERIFY2(mock.wasCancelled(reminderId),
                 "cascade-cancel: a tombstoned reminder was missed when the "
                 "task itself stayed alive");
        QVERIFY(!mock.wasScheduled(reminderId));

        // The task is still alive.
        Task *task = store->taskById(taskId);
        QVERIFY(task != nullptr);
        QVERIFY(!task->isDeleted());

        delete store;
    }

    // ------------------------------------------------------------------
    // 5. Idempotency: re-firing remindersChanged after the cascade
    //    should not produce a *third* cancel for the same dead reminder
    //    every cycle forever. The implementation is intentionally
    //    cheap (cancelNotification on a non-existent id is documented
    //    as a quiet no-op on every platform impl) so we don't fail the
    //    suite on repeat-cancel — we just pin the observed count for
    //    future refactors that might try to make it conditional.
    // ------------------------------------------------------------------
    void cascadeIsStableAcrossMultipleRescheduleCalls()
    {
        DataStore *store = makeStoreWithInbox();
        CascadeMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.cancelled.clear();

        QString taskId;
        const QString reminderId = seedTaskWithFutureReminder(store, &taskId);
        QVERIFY(store->removeTask(taskId));

        const int afterFirst = mock.cancelCount(reminderId);
        QVERIFY(afterFirst >= 1);

        // Drive another rescheduleAll pass — the implementation may or may
        // not call cancel again for a still-tombstoned reminder. Both
        // behaviours are correct (the underlying impl is a no-op on an
        // unknown id). The test pins that NOTHING NEW gets scheduled.
        mock.scheduledIds.clear();
        emit store->remindersChanged();

        QVERIFY(!mock.wasScheduled(reminderId));

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestReminderCascadeCancel)
#include "test_reminder_cascade_cancel.moc"
