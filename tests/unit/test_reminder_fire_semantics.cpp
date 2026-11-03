// Reminder firing semantics (global bug-fix pass, F4).
//
// Pins:
//   P4  — a reminder armed while its task was open is cancelled when the task
//         is completed (it used to keep ticking and fire for a done task).
//   P3  — the reschedule pass cancels only the PENDING schedule
//         (cancelScheduled) for live reminders, never the full
//         cancelNotification that on Android also dismisses a displayed
//         notification on every edit / sync / app start.
//   P11 — reminderFired is emitted when the backend reports that a schedule
//         really fired (scheduledNotificationFired), mapped back to the TASK
//         id — not at scheduling time.
//   M9  — the mobile "Пуш-уведомления" setting (DataStore "mobile/push")
//         stops scheduling and cancels pending reminders when honoured.
//   M6  — the Linux/macOS StubNotificationService really fires scheduled
//         notifications (QTimer) instead of only logging them.
//   M8  — LocationService keeps the inside/outside state of an unchanged
//         geofence across a reload, so "entered" does not re-fire.

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/reminder.h"
#include "services/notificationservice.h"
#include "services/reminderscheduler.h"

#if !defined(Q_OS_WIN) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
#  define OPENTODO_TEST_STUB_SERVICE 1
#  include "services/notificationservice_stub.h"
#endif

#ifdef HAS_POSITIONING
#  include "services/locationservice.h"
#  include <QGeoPositionInfo>
#endif

class FireMockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    QStringList scheduled;
    QStringList cancelledFull;      // cancelNotification
    QStringList cancelledPending;   // cancelScheduled

    void showNotification(const QString &, const QString &, const QString & = {}) override {}
    void scheduleNotification(const QString &, const QString &, const QDateTime &,
                              const QString &id) override { scheduled.append(id); }
    void cancelNotification(const QString &id) override { cancelledFull.append(id); }
    void cancelScheduled(const QString &id) override { cancelledPending.append(id); }
    void cancelAll() override {}

    void clearAll() { scheduled.clear(); cancelledFull.clear(); cancelledPending.clear(); }
};

namespace {

DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// Open task with one absolute reminder one hour ahead.
Reminder *addFutureReminder(DataStore *store, Task **taskOut = nullptr)
{
    Task *task = store->addTask(QStringLiteral("Позвонить маме"),
                                store->inboxProject()->id());
    if (taskOut) *taskOut = task;
    Reminder *r = store->addReminder(task->id(), QStringLiteral("absolute"));
    ReminderDue due;
    due.datetime = QDateTime::currentDateTime().addSecs(3600).toString(Qt::ISODate);
    r->setDue(due);
    return r;
}

} // namespace

class TestReminderFireSemantics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_ReminderFire"));
        QCoreApplication::setApplicationName(QStringLiteral("test_reminder_fire_semantics"));
        QSettings().clear();
    }

    // P4: schedule, complete the task, reschedule → the armed reminder is
    // fully cancelled (and not re-armed).
    void completingTask_cancelsArmedReminder()
    {
        DataStore *store = makeStore();
        FireMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);

        Task *task = nullptr;
        Reminder *r = addFutureReminder(store, &task);
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(mock.scheduled.contains(r->id()));

        task->setIsCompleted(true);
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY2(mock.cancelledFull.contains(r->id()),
                 "reminder of a completed task must be cancelled");
        QVERIFY(!mock.scheduled.contains(r->id()));

        // Nothing left to cancel on the next pass.
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(!mock.cancelledFull.contains(r->id()));

        // Uncompleting re-arms it.
        task->setIsCompleted(false);
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(mock.scheduled.contains(r->id()));
        delete store;
    }

    // P3: a reschedule pass over live reminders never uses the full cancel.
    void reschedule_usesPendingOnlyCancel()
    {
        DataStore *store = makeStore();
        FireMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);

        Reminder *r = addFutureReminder(store);
        // A reminder whose time already passed (fired, maybe still on screen).
        Task *t2 = store->addTask(QStringLiteral("Fired"), store->inboxProject()->id());
        Reminder *past = store->addReminder(t2->id(), QStringLiteral("absolute"));
        ReminderDue pd;
        pd.datetime = QDateTime::currentDateTime().addSecs(-60).toString(Qt::ISODate);
        past->setDue(pd);

        mock.clearAll();
        scheduler.rescheduleAll();
        scheduler.rescheduleAll();
        QVERIFY(mock.cancelledPending.contains(r->id()));
        QVERIFY(mock.cancelledPending.contains(past->id()));
        QVERIFY2(!mock.cancelledFull.contains(r->id()),
                 "live reminder must not be fully cancelled on reschedule");
        QVERIFY2(!mock.cancelledFull.contains(past->id()),
                 "a fired reminder must stay on screen across reschedule passes");
        delete store;
    }

    // P11: scheduling does not emit reminderFired; the backend's real
    // firing does, with the task id resolved from the reminder id.
    void reminderFired_onlyWhenBackendFires()
    {
        DataStore *store = makeStore();
        FireMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        QSignalSpy fired(&mock, &NotificationService::reminderFired);

        Task *task = nullptr;
        Reminder *r = addFutureReminder(store, &task);
        scheduler.rescheduleAll();
        QVERIFY(mock.scheduled.contains(r->id()));
        QCOMPARE(fired.count(), 0);

        emit mock.scheduledNotificationFired(r->id(), task->content(), QString());
        QCOMPARE(fired.count(), 1);
        QCOMPARE(fired.at(0).at(0).toString(), task->id());
        QCOMPARE(fired.at(0).at(1).toString(), task->content());

        // Snooze ids map to the task too.
        emit mock.scheduledNotificationFired(QStringLiteral("snooze-") + task->id(),
                                             QStringLiteral("Отложенное напоминание"),
                                             task->content());
        QCOMPARE(fired.count(), 2);
        QCOMPARE(fired.at(1).at(0).toString(), task->id());

        // Non-task ids (e.g. the daily review) are not logged as reminders.
        emit mock.scheduledNotificationFired(QStringLiteral("daily-review"),
                                             QStringLiteral("Итог дня"), QString());
        QCOMPARE(fired.count(), 2);
        delete store;
    }

    // M9: "mobile/push" = false → nothing scheduled, pending cancelled;
    // flipping it back on re-arms (settingsChanged-driven).
    void pushSetting_off_stopsScheduling()
    {
        DataStore *store = makeStore();
        FireMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        scheduler.setHonourPushSetting(true);

        Reminder *r = addFutureReminder(store);
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(mock.scheduled.contains(r->id()));

        mock.clearAll();
        store->setSetting(QLatin1String(ReminderScheduler::kPushSettingKey), false);
        QVERIFY2(!mock.scheduled.contains(r->id()), "push off must not schedule");
        QVERIFY(mock.cancelledPending.contains(r->id()));

        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(!mock.scheduled.contains(r->id()));

        mock.clearAll();
        store->setSetting(QLatin1String(ReminderScheduler::kPushSettingKey), true);
        QVERIFY(mock.scheduled.contains(r->id()));
        delete store;
    }

    // M9 (desktop default): the mobile toggle does not silence desktop
    // reminders unless explicitly honoured.
    void pushSetting_ignoredWhenNotHonoured()
    {
        DataStore *store = makeStore();
        FireMockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        scheduler.setHonourPushSetting(false);
        store->setSetting(QLatin1String(ReminderScheduler::kPushSettingKey), false);

        Reminder *r = addFutureReminder(store);
        mock.clearAll();
        scheduler.rescheduleAll();
        QVERIFY(mock.scheduled.contains(r->id()));
        delete store;
    }

#ifdef OPENTODO_TEST_STUB_SERVICE
    // M6: the stub backend fires a near-future schedule (banner + fired
    // signal), honours cancel, and never fires a far-future one early.
    void stubService_firesScheduledNotification()
    {
        StubNotificationService svc;
        QSignalSpy banner(&svc, &NotificationService::inAppBannerRequested);
        QSignalSpy fired(&svc, &NotificationService::scheduledNotificationFired);

        svc.scheduleNotification(QStringLiteral("T"), QStringLiteral("B"),
                                 QDateTime::currentDateTime().addMSecs(150),
                                 QStringLiteral("r-1"));
        QVERIFY(svc.isScheduled(QStringLiteral("r-1")));
        QTRY_COMPARE_WITH_TIMEOUT(fired.count(), 1, 3000);
        QCOMPARE(fired.at(0).at(0).toString(), QStringLiteral("r-1"));
        QCOMPARE(banner.count(), 1);
        QVERIFY(!svc.isScheduled(QStringLiteral("r-1")));

        // Cancelled before it is due → never fires.
        svc.scheduleNotification(QStringLiteral("T2"), QStringLiteral("B2"),
                                 QDateTime::currentDateTime().addMSecs(150),
                                 QStringLiteral("r-2"));
        svc.cancelNotification(QStringLiteral("r-2"));
        QTest::qWait(400);
        QCOMPARE(fired.count(), 1);

        // Far future (beyond QTimer's int range) is armed, not fired.
        svc.scheduleNotification(QStringLiteral("T3"), QStringLiteral("B3"),
                                 QDateTime::currentDateTime().addDays(60),
                                 QStringLiteral("r-3"));
        QTest::qWait(100);
        QCOMPARE(fired.count(), 1);
        QVERIFY(svc.isScheduled(QStringLiteral("r-3")));
        svc.cancelAll();
        QVERIFY(!svc.isScheduled(QStringLiteral("r-3")));
    }
#endif

#ifdef HAS_POSITIONING
    // M8: re-adding an unchanged fence (what reloadGeofences does on every
    // remindersChanged) keeps it "inside" → no duplicate enteredGeofence.
    void geofenceReload_keepsInsideState()
    {
        LocationService loc;
        QSignalSpy entered(&loc, &LocationService::enteredGeofence);
        QSignalSpy left(&loc, &LocationService::leftGeofence);
        QGeoPositionInfo here(QGeoCoordinate(55.75, 37.62), QDateTime::currentDateTime());
        auto feed = [&](const QGeoPositionInfo &info) {
            QVERIFY(QMetaObject::invokeMethod(&loc, "onPositionUpdated",
                                              Qt::DirectConnection,
                                              Q_ARG(QGeoPositionInfo, info)));
        };

        loc.addGeofence(QStringLiteral("rem-1"), 55.75, 37.62, 200.0);
        feed(here);                       // startup evaluation: inside → entered
        QCOMPARE(entered.count(), 1);

        loc.addGeofence(QStringLiteral("rem-1"), 55.75, 37.62, 200.0); // reload
        feed(here);
        QCOMPARE(entered.count(), 1);     // still inside: no duplicate
        QCOMPARE(left.count(), 0);

        // A moved fence starts fresh (the user is now outside it).
        loc.addGeofence(QStringLiteral("rem-1"), 56.75, 37.62, 200.0);
        feed(here);
        QCOMPARE(left.count(), 0);
        QCOMPARE(loc.geofenceIds(), QStringList{QStringLiteral("rem-1")});
        loc.removeGeofence(QStringLiteral("rem-1"));
        QVERIFY(loc.geofenceIds().isEmpty());
    }
#endif
};

QTEST_GUILESS_MAIN(TestReminderFireSemantics)
#include "test_reminder_fire_semantics.moc"
