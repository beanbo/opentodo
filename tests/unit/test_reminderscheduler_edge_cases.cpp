// Edge-case tests for ReminderScheduler.
//
// The base test (tests/unit/test_reminderscheduler.cpp) covers the happy
// paths: future reminder is scheduled, past reminder is skipped, deleted
// reminders are not re-scheduled, completed-task reminders are skipped, etc.
//
// This file extends that with edge cases:
//   - past-date reminder behaviour (cancel-but-don't-fire)
//   - future reminder is NOT scheduled until time/date is valid
//   - snooze cycle (schedule -> snooze -> snooze -> cancel/delete)
//   - explicit cancellation (delete reminder -> no schedule)
//   - multi-reminder isolation (cancel one, others stay scheduled)
//   - reschedule dedup (same id, different time -> last call wins, not duplicated)
//   - empty taskId orphan reminder -> graceful no-op
//   - invalid QDateTime -> graceful no-op
//   - "persistence" (rebuild scheduler against the same store -> rescheduled)
//   - location-type reminder -> no wall-clock timer set
//
// All tests drive time deterministically — no QTimer fires, no real waits.
// The 500ms debounce timer is bypassed by emitting `remindersChanged` (which
// is connected directly, not via the debouncer) or by calling `rescheduleAll()`
// on the scheduler explicitly.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/reminder.h"
#include "services/notificationservice.h"
#include "services/reminderscheduler.h"

// ---------------------------------------------------------------------------
// MockNotificationService — local copy, kept self-contained so this TU does
// not depend on the symbol from test_reminderscheduler.cpp.
// ---------------------------------------------------------------------------
class MockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit MockNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    struct ScheduleCall {
        QString title;
        QString body;
        QDateTime when;
        QString id;
    };

    QList<ScheduleCall> scheduled;
    QList<QString>      cancelled;
    QList<QString>      shown;

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        Q_UNUSED(body);
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

    bool wasScheduled(const QString &id) const
    {
        for (const auto &c : scheduled)
            if (c.id == id) return true;
        return false;
    }
    int scheduledCount(const QString &id) const
    {
        int n = 0;
        for (const auto &c : scheduled)
            if (c.id == id) ++n;
        return n;
    }
    int cancelledCount(const QString &id) const
    {
        int n = 0;
        for (const auto &c : cancelled)
            if (c == id) ++n;
        return n;
    }
    bool wasCancelled(const QString &id) const { return cancelled.contains(id); }

    ScheduleCall lastScheduleFor(const QString &id) const
    {
        for (int i = scheduled.size() - 1; i >= 0; --i)
            if (scheduled[i].id == id) return scheduled[i];
        return {};
    }

    void clearAll()
    {
        scheduled.clear();
        cancelled.clear();
        shown.clear();
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
// TestReminderSchedulerEdgeCases
// ---------------------------------------------------------------------------
class TestReminderSchedulerEdgeCases : public QObject
{
    Q_OBJECT

private slots:

    // -------- past-date reminders ------------------------------------------
    // A reminder with `triggerTime <= now` is silently skipped — the scheduler
    // calls cancelNotification() unconditionally, then bails before
    // scheduleNotification() runs. We verify both:
    //   (a) cancel was called for this id (the unconditional pre-cancel)
    //   (b) schedule was NEVER called for this id
    //   (c) showNotification (immediate fire) was NOT called either —
    //       past-date reminders do not fire-now in this implementation.
    void past_date_reminder_skipped_silently()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Stale task"), inboxId);

        // Reminder 1 hour in the past — well below the now-cutoff.
        const QDateTime past = QDateTime::currentDateTime().addSecs(-3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = past.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        // (a) pre-cancel happened
        QVERIFY(mock.wasCancelled(reminder->id()));
        // (b) NOT scheduled
        QVERIFY(!mock.wasScheduled(reminder->id()));
        // (c) NOT immediately fired via showNotification
        QVERIFY(mock.shown.isEmpty());

        delete store;
    }

    // Edge: reminder exactly at "now" — boundary condition.
    // Per scheduler source: `if (... triggerTime <= now) continue;` —
    // a reminder at exactly now is treated as past, NOT scheduled.
    void reminder_at_exact_now_treated_as_past()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Now task"), inboxId);

        // A few ms in the past — guaranteed <= now by the time rescheduleAll runs.
        const QDateTime nowish = QDateTime::currentDateTime().addMSecs(-50);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = nowish.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        QVERIFY(!mock.wasScheduled(reminder->id()));
        QVERIFY(mock.shown.isEmpty());

        delete store;
    }

    // -------- future reminder, no real timer involved ----------------------
    // The scheduler must call scheduleNotification(...) — it does NOT itself
    // arm a QTimer or wait for the wall clock. We verify the schedule call
    // happened and showNotification was NOT called (no premature fire).
    void future_reminder_scheduled_but_not_fired_yet()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Future task"), inboxId);

        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        QVERIFY(mock.wasScheduled(reminder->id()));
        QVERIFY(mock.shown.isEmpty()); // no immediate fire
        QCOMPARE(mock.scheduledCount(reminder->id()), 1);

        delete store;
    }

    // -------- snooze cycle -------------------------------------------------
    // Snooze is modelled as "update the reminder's due to a later time, then
    // re-emit remindersChanged". The scheduler always pre-cancels and
    // re-schedules with the new time — so after each snooze the schedule
    // call's `when` reflects the new time, and after final cancellation
    // (delete) the reminder is no longer in the schedule plan.
    void snooze_cycle_5m_then_10m_then_cancel()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Snoozable"), inboxId);

        // Original schedule: +1 hour
        QDateTime t1 = QDateTime::currentDateTime().addSecs(3600);
        t1 = t1.addMSecs(-t1.time().msec()); // ISO drops sub-second precision
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue d1; d1.datetime = t1.toString(Qt::ISODate);
        reminder->setDue(d1);

        mock.clearAll();
        emit store->remindersChanged();
        QVERIFY(mock.wasScheduled(reminder->id()));
        QCOMPARE(mock.lastScheduleFor(reminder->id()).when, t1);

        // Snooze +5 min
        QDateTime t2 = t1.addSecs(5 * 60);
        ReminderDue d2; d2.datetime = t2.toString(Qt::ISODate);
        reminder->setDue(d2);

        mock.clearAll();
        emit store->remindersChanged();
        QVERIFY(mock.wasCancelled(reminder->id())); // pre-cancel old
        QVERIFY(mock.wasScheduled(reminder->id())); // reschedule new
        QCOMPARE(mock.lastScheduleFor(reminder->id()).when, t2);

        // Snooze +10 min
        QDateTime t3 = t2.addSecs(10 * 60);
        ReminderDue d3; d3.datetime = t3.toString(Qt::ISODate);
        reminder->setDue(d3);

        mock.clearAll();
        emit store->remindersChanged();
        QVERIFY(mock.wasCancelled(reminder->id()));
        QVERIFY(mock.wasScheduled(reminder->id()));
        QCOMPARE(mock.lastScheduleFor(reminder->id()).when, t3);

        // Final cancel: soft-delete the reminder. Scheduler must NOT
        // reschedule, and showNotification must NOT have fired throughout.
        const QString rid = reminder->id();
        reminder->setIsDeleted(true);

        mock.clearAll();
        emit store->remindersChanged();
        QVERIFY(!mock.wasScheduled(rid));
        QVERIFY(mock.shown.isEmpty()); // no fire across the whole cycle

        delete store;
    }

    // -------- explicit cancellation ----------------------------------------
    // Schedule then delete-reminder cycle: after delete, the next reschedule
    // pass must NOT re-schedule the deleted reminder. (The previous pass'
    // cancel call is sufficient to tear down the OS-level notification.)
    void schedule_then_cancel_via_delete()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("To cancel"), inboxId);

        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue; rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        emit store->remindersChanged();
        QVERIFY(mock.wasScheduled(reminder->id()));

        const QString rid = reminder->id();
        reminder->setIsDeleted(true);

        mock.clearAll();
        emit store->remindersChanged();
        QVERIFY(!mock.wasScheduled(rid));
        QVERIFY(mock.shown.isEmpty());

        delete store;
    }

    // -------- multiple reminders, one cancelled ----------------------------
    // Three reminders, cancel one (delete it), re-emit remindersChanged.
    // The other two must still be re-scheduled (each pass re-schedules all
    // active reminders).
    void multiple_reminders_cancel_one_others_stay_pending()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();

        // Three tasks, each with one absolute future reminder.
        const QDateTime base = QDateTime::currentDateTime().addSecs(3600);
        QStringList rids;
        QList<Reminder *> reminders;
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("Task %1").arg(i), inboxId);
            Reminder *r = store->addReminder(t->id(), QStringLiteral("absolute"));
            ReminderDue d; d.datetime = base.addSecs(60 * (i + 1)).toString(Qt::ISODate);
            r->setDue(d);
            rids.append(r->id());
            reminders.append(r);
        }

        emit store->remindersChanged();
        for (const QString &rid : rids)
            QVERIFY2(mock.wasScheduled(rid),
                     qPrintable(QStringLiteral("expected scheduled for %1").arg(rid)));

        // Cancel the middle one.
        const QString cancelledId = rids[1];
        reminders[1]->setIsDeleted(true);

        mock.clearAll();
        emit store->remindersChanged();

        // The two survivors are re-scheduled.
        QVERIFY(mock.wasScheduled(rids[0]));
        QVERIFY(mock.wasScheduled(rids[2]));
        // The deleted one is NOT re-scheduled.
        QVERIFY(!mock.wasScheduled(cancelledId));

        delete store;
    }

    // -------- reschedule dedup --------------------------------------------
    // Same reminder id, two consecutive setDue calls with different times.
    // Each remindersChanged emission triggers exactly one schedule call for
    // this reminder — the 2nd call must reflect the *new* time and the
    // mock must not see two schedule entries for the same id from one pass.
    void reschedule_same_id_updates_time_no_double_schedule_per_pass()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Update me"), inboxId);

        QDateTime t1 = QDateTime::currentDateTime().addSecs(3600);
        t1 = t1.addMSecs(-t1.time().msec());
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue d1; d1.datetime = t1.toString(Qt::ISODate);
        reminder->setDue(d1);

        mock.clearAll();
        emit store->remindersChanged();

        // Exactly one schedule call for this id from this single pass.
        QCOMPARE(mock.scheduledCount(reminder->id()), 1);
        QCOMPARE(mock.lastScheduleFor(reminder->id()).when, t1);

        // Update time, single pass again.
        QDateTime t2 = QDateTime::currentDateTime().addSecs(7200);
        t2 = t2.addMSecs(-t2.time().msec());
        ReminderDue d2; d2.datetime = t2.toString(Qt::ISODate);
        reminder->setDue(d2);

        mock.clearAll();
        emit store->remindersChanged();

        QCOMPARE(mock.scheduledCount(reminder->id()), 1);
        QCOMPARE(mock.lastScheduleFor(reminder->id()).when, t2);
        // And cancel was called exactly once for the unconditional pre-cancel.
        QCOMPARE(mock.cancelledCount(reminder->id()), 1);

        delete store;
    }

    // -------- empty taskId reminder ---------------------------------------
    // The scheduler iterates tasks, then for each task fetches reminders by
    // taskId. A reminder with empty taskId is therefore unreachable —
    // confirms graceful no-op (no crash, no schedule, no cancel against it).
    void empty_taskId_reminder_is_graceful_noop()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Real task"), inboxId);
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);

        // Real reminder on the real task — should be scheduled.
        Reminder *good = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue gd; gd.datetime = future.toString(Qt::ISODate);
        good->setDue(gd);

        // Orphan reminder with empty taskId — addReminder("") still returns
        // an object; it's just unreachable from any task iteration.
        Reminder *orphan = store->addReminder(QString(), QStringLiteral("absolute"));
        QVERIFY(orphan != nullptr);
        ReminderDue od; od.datetime = future.toString(Qt::ISODate);
        orphan->setDue(od);

        mock.clearAll();
        // Must not crash.
        emit store->remindersChanged();

        // Real reminder scheduled, orphan is not touched at all.
        QVERIFY(mock.wasScheduled(good->id()));
        QVERIFY(!mock.wasScheduled(orphan->id()));
        QVERIFY(!mock.wasCancelled(orphan->id()));

        delete store;
    }

    // -------- invalid QDateTime ------------------------------------------
    // Reminder with empty due (no date, no datetime, type=absolute) → the
    // resolveTriggerTime() call returns an invalid QDateTime → scheduler
    // falls into the `!triggerTime.isValid()` branch and continues. The
    // unconditional pre-cancel still fires; no schedule call.
    void invalid_datetime_is_graceful_noop()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("No-time task"), inboxId);

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        // Leave ReminderDue empty AND don't set any due on the task either —
        // resolveTriggerTime cannot derive any valid time.
        ReminderDue empty;
        reminder->setDue(empty);

        mock.clearAll();
        // Must not crash.
        emit store->remindersChanged();

        QVERIFY(!mock.wasScheduled(reminder->id()));
        // pre-cancel ran but there's nothing to fire.
        QVERIFY(mock.wasCancelled(reminder->id()));
        QVERIFY(mock.shown.isEmpty());

        delete store;
    }

    // -------- "persistence" via DataStore reload ---------------------------
    // ReminderScheduler does NOT itself persist — the reminders live in
    // DataStore (which FileManager flushes to state.json). We simulate
    // "load on startup" by creating a *second* scheduler instance against
    // the same already-populated store: its constructor must run an initial
    // synchronous rescheduleAll() and pick up the same reminders.
    void second_scheduler_instance_picks_up_existing_reminders()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *task = store->addTask(QStringLiteral("Persistent task"), inboxId);
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue; rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        // First scheduler: just to mimic "this app session set things up".
        {
            MockNotificationService firstMock;
            ReminderScheduler scheduler(store, &firstMock);
            // construction triggers initial pass; verify it scheduled.
            QVERIFY(firstMock.wasScheduled(reminder->id()));
        } // first scheduler torn down — simulates app restart.

        // Second scheduler: fresh instance, same store. The initial pass
        // in its constructor must observe the existing reminder and
        // schedule it via the new mock.
        MockNotificationService secondMock;
        ReminderScheduler reloaded(store, &secondMock);

        QVERIFY(secondMock.wasScheduled(reminder->id()));
        const auto call = secondMock.lastScheduleFor(reminder->id());
        QCOMPARE(call.when, QDateTime::fromString(rDue.datetime, Qt::ISODate));

        delete store;
    }

    // -------- location reminders ------------------------------------------
    // A reminder with type=="location" and no datetime/date due fields
    // cannot be wall-clock scheduled — resolveTriggerTime() returns an
    // invalid QDateTime, so the scheduler skips scheduleNotification().
    // The geofence handling is the LocationService's responsibility, not
    // the scheduler's.
    void location_reminder_no_walltime_scheduling()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Buy milk near home"), inboxId);

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        // Switch to type=location via the convenience setter.
        reminder->setLocationCoords(55.7558, 37.6173, 200,
                                    QStringLiteral("Home"),
                                    QStringLiteral("on_enter"));
        // Sanity: type should now be "location".
        QCOMPARE(reminder->type(), QStringLiteral("location"));

        // Crucially: do NOT set any due.datetime/date on the reminder —
        // that's the whole point of a geofence reminder.
        ReminderDue empty; reminder->setDue(empty);

        mock.clearAll();
        emit store->remindersChanged();

        // No wall-clock schedule call.
        QVERIFY(!mock.wasScheduled(reminder->id()));
        // No immediate fire.
        QVERIFY(mock.shown.isEmpty());
        // Pre-cancel still happens (defensive).
        QVERIFY(mock.wasCancelled(reminder->id()));

        delete store;
    }

    // -------- explicit rescheduleAll() invocation -------------------------
    // Smoke-test the documented "tests can call rescheduleAll() directly
    // without an event loop" affordance — useful as a guard against future
    // regressions that move rescheduleAll() to a private slot.
    void direct_rescheduleAll_invocation_works()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Direct call"), inboxId);
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue; rDue.datetime = future.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        // Direct call — no signal, no event loop, no debouncer.
        scheduler.rescheduleAll();

        QVERIFY(mock.wasScheduled(reminder->id()));

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestReminderSchedulerEdgeCases)
#include "test_reminderscheduler_edge_cases.moc"
