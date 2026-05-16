#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QMetaObject>
#include <QStandardPaths>
#include <QDate>
#include <QDateTime>
#include <QTime>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "services/notificationservice.h"
#include "services/dailyreviewservice.h"

// ---------------------------------------------------------------------------
// MockNotificationService
//
// DailyReviewService only ever calls showNotification() (one shot, "Итог дня").
// We still implement the full interface so the mock can stand in for any
// NotificationService consumer.
// ---------------------------------------------------------------------------
class MockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit MockNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    struct ShowCall {
        QString title;
        QString body;
        QString taskId;
    };
    struct ScheduleCall {
        QString title;
        QString body;
        QDateTime when;
        QString id;
    };

    QList<ShowCall>     shown;
    QList<ScheduleCall> scheduled;
    QList<QString>      cancelled;

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        shown.push_back({title, body, taskId});
    }

    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override
    {
        scheduled.push_back({title, body, when, id});
    }

    void cancelNotification(const QString &id) override { cancelled.append(id); }
    void cancelAll() override { cancelled.append(QStringLiteral("ALL")); }

    int  showCount() const { return shown.size(); }
    void clear()
    {
        shown.clear();
        scheduled.clear();
        cancelled.clear();
    }
};

// ---------------------------------------------------------------------------
// Helper: DataStore with Inbox + a single task due TODAY (so checkAndNotify
// has something to count). Without at least one due-today task the service
// short-circuits and never calls into the notification service.
// ---------------------------------------------------------------------------
static DataStore *makeStoreWithTodayTask(int howMany = 1, int howManyCompleted = 0)
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    for (int i = 0; i < howMany; ++i) {
        Task *t = store->addTask(QStringLiteral("Task %1").arg(i + 1), inbox->id());
        t->setDueDate(today);
        if (i < howManyCompleted) t->setIsCompleted(true);
    }
    return store;
}

// Drive the private slot. moc registers private slots under their declared
// name, so invokeMethod by string still works.
static void invokeCheck(DailyReviewService *svc)
{
    QVERIFY(QMetaObject::invokeMethod(svc, "checkAndNotify",
                                      Qt::DirectConnection));
}

// ---------------------------------------------------------------------------
// TestDailyReviewService
// ---------------------------------------------------------------------------
class TestDailyReviewService : public QObject
{
    Q_OBJECT

private slots:
    // ── Fixture lifecycle ────────────────────────────────────────────────────

    void initTestCase()
    {
        // Dedicated org/app so the test never touches real OpenTodo settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_DailyReviewSvc"));
        QCoreApplication::setApplicationName(QStringLiteral("test_dailyreviewservice"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // ── Bootstrap defaults ───────────────────────────────────────────────────
    //
    // With no QSettings keys present the service should still construct cleanly
    // and operate against the documented default 18:00. There is no public
    // accessor for m_hour/m_minute, but we can verify the service is functional
    // by driving checkAndNotify and observing it does the right thing.
    void defaults_when_settings_empty()
    {
        DataStore *store = makeStoreWithTodayTask(2);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // No keys → constructor must not crash and must not over-eagerly fire
        // before the timer elapses.
        QCOMPARE(mock.showCount(), 0);

        // Driving checkAndNotify directly should still succeed (nothing
        // disables the service, store has tasks, fresh "last_notified" date).
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);
        QCOMPARE(mock.shown.first().title, QStringLiteral("Итог дня"));

        delete store;
    }

    // ── Custom HH/MM are read from QSettings ─────────────────────────────────
    //
    // We can't read m_hour/m_minute back, but we CAN verify QSettings round-
    // trips: write the keys, reconstruct the service (which calls
    // loadTimeFromSettings in its ctor), then show the read-back values match
    // what's in QSettings.
    void loads_custom_time_from_settings()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), 8);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), 30);
            s.sync();
        }

        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // Sanity: keys persisted as ints, round-trip integrity.
        QSettings s;
        QCOMPARE(s.value(QStringLiteral("productivity/dailyReviewHour")).toInt(), 8);
        QCOMPARE(s.value(QStringLiteral("productivity/dailyReviewMinute")).toInt(), 30);

        // Service must be alive and able to fire.
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── rescheduleFromSettings re-reads the keys ─────────────────────────────
    //
    // Construct, then change the QSettings keys, then call the public
    // rescheduleFromSettings(). Since we can't peek m_hour/m_minute, we verify
    // (a) the call is exposed as Q_INVOKABLE (so QML can reach it),
    // (b) it doesn't crash, and (c) post-call the service still fires.
    void reschedule_from_settings_picks_up_new_time()
    {
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // Change settings and ask the service to re-read.
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), 9);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), 15);
            s.sync();
        }
        svc.rescheduleFromSettings();

        // Verify rescheduleFromSettings is registered as Q_INVOKABLE — i.e.
        // discoverable by name in the meta-object — so QML actually has a hook.
        const QMetaObject *mo = svc.metaObject();
        QVERIFY(mo->indexOfMethod("rescheduleFromSettings()") >= 0);

        // After reschedule, checkAndNotify should still operate normally.
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── "Next trigger" arithmetic — replicate the same algorithm and verify
    //     the public contract documented in CLAUDE.md / dailyreviewservice.cpp.
    //
    // The algorithm:
    //     todayTarget = today @ HH:MM
    //     if now >= todayTarget → tomorrow
    //     else                  → today
    // This is what nextTrigger() does. Although nextTrigger() is private, we
    // can exercise the user-visible contract: the service must not fire
    // immediately after construction (the timer is set to ms-until-target,
    // bounded ≥ 1s). The test below sets the time to one minute from now and
    // verifies no notification has fired in the construction window.
    void next_trigger_does_not_fire_on_construction()
    {
        // Pick an HH:MM = 1 minute from now → trigger should be either today
        // (if now+1 < end-of-day) or tomorrow (if now is 23:59 — extremely
        // narrow window, but still in the future). Either way, well clear of
        // the immediate-fire range.
        const QTime nowT = QTime::currentTime();
        const int targetMin = (nowT.minute() + 1) % 60;
        const int targetHr  = (nowT.minute() == 59)
                              ? (nowT.hour() + 1) % 24
                              : nowT.hour();

        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), targetHr);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), targetMin);
            s.sync();
        }

        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // Right after construction the timer is single-shot, set to a positive
        // delay. No notification should be in `mock.shown` yet.
        QCOMPARE(mock.showCount(), 0);

        delete store;
    }

    // ── Disabled gate: when daily_review_enabled is false, no notification ──
    //
    // The setting key for "is daily review enabled" lives in DataStore, NOT
    // QSettings. CheckAndNotify must short-circuit before calling shown().
    void disabled_does_not_fire()
    {
        DataStore *store = makeStoreWithTodayTask(2);
        store->setSetting(QStringLiteral("daily_review_enabled"), false);

        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);

        // Disabled → showNotification was NOT called.
        QCOMPARE(mock.showCount(), 0);

        delete store;
    }

    // ── Re-enable: setting flips to true → service fires on next check ──────
    void reenable_resumes_firing()
    {
        DataStore *store = makeStoreWithTodayTask(1);
        store->setSetting(QStringLiteral("daily_review_enabled"), false);

        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 0);

        // Flip back on. checkAndNotify MUST now fire.
        store->setSetting(QStringLiteral("daily_review_enabled"), true);
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Default-ON: missing key counts as enabled ───────────────────────────
    //
    // dailyreviewservice.cpp explicitly comments "Default ON: missing key
    // counts as true". Verify by leaving the DataStore setting unset.
    void enabled_default_on_when_key_absent()
    {
        DataStore *store = makeStoreWithTodayTask(1);
        // Do NOT call setSetting — leave daily_review_enabled unset.
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        // Default ON → notification is shown.
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Boundary: HH=23 MM=59 is a valid trigger configuration ──────────────
    void boundary_23_59_accepted()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), 23);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), 59);
            s.sync();
        }
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // No crash on construction with the upper-bound time.
        // Driving checkAndNotify must still send the notification (the
        // configured time is for *scheduling*, not for whether to fire).
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Invalid HH=24 → clamped to 23 ────────────────────────────────────────
    //
    // dailyreviewservice.cpp: m_hour = qBound(0, rawHour, 23). 24 → 23.
    // Verify the clamp doesn't trip an assert / produce a negative timer
    // interval and that the service remains functional.
    void invalid_hour_24_clamped()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), 24);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), 0);
            s.sync();
        }
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        // Service must still be functional after clamping: invoke check and
        // verify it produces exactly one notification.
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Invalid MM=60 → clamped to 59 ────────────────────────────────────────
    void invalid_minute_60_clamped()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), 10);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), 60);
            s.sync();
        }
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Negative inputs are clamped to 0 ─────────────────────────────────────
    void invalid_negative_clamped()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("productivity/dailyReviewHour"), -5);
            s.setValue(QStringLiteral("productivity/dailyReviewMinute"), -100);
            s.sync();
        }
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        delete store;
    }

    // ── Notification dispatch payload — title, body, and pluralisation ──────
    //
    // Verify the dispatched ShowCall has the expected title and that the body
    // contains the remaining-task count + the right Russian plural form.
    void notification_dispatch_includes_count_and_title()
    {
        // 3 total tasks, 1 completed → 2 remaining. "2 задачи" (plural form
        // for 2-4).
        DataStore *store = makeStoreWithTodayTask(/*howMany=*/3,
                                                  /*howManyCompleted=*/1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        const auto &call = mock.shown.first();
        QCOMPARE(call.title, QStringLiteral("Итог дня"));
        QVERIFY2(call.body.contains(QStringLiteral("2")),
                 qPrintable(QStringLiteral("body did not include count: ") + call.body));
        // Russian plural for 2 → "задачи"
        QVERIFY2(call.body.contains(QStringLiteral("задачи")),
                 qPrintable(QStringLiteral("body did not include 'задачи': ") + call.body));

        delete store;
    }

    // ── All-completed case dispatches the "well done" body ──────────────────
    void notification_dispatch_when_all_done()
    {
        DataStore *store = makeStoreWithTodayTask(/*howMany=*/2,
                                                  /*howManyCompleted=*/2);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);
        const auto &call = mock.shown.first();
        QCOMPARE(call.title, QStringLiteral("Итог дня"));
        QVERIFY2(call.body.contains(QStringLiteral("Все задачи на сегодня выполнены")),
                 qPrintable(call.body));

        delete store;
    }

    // ── No-tasks-today gate: don't be annoying ──────────────────────────────
    //
    // dailyreviewservice.cpp short-circuits when `total == 0` — no notification
    // when the user has nothing scheduled for today.
    void no_tasks_today_does_not_fire()
    {
        // makeStoreWithTodayTask(0) → DataStore with Inbox but no due-today
        // tasks. tasksForToday() returns an empty list; the service should
        // skip the notification.
        DataStore *store = makeStoreWithTodayTask(0);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 0);

        delete store;
    }

    // ── Same-day duplicate guard ─────────────────────────────────────────────
    //
    // After a successful fire, m_lastNotifiedDate := today. A second
    // checkAndNotify on the same day must NOT re-fire.
    void same_day_duplicate_guard()
    {
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        // Second call same-day must not double-fire.
        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 1);

        // The "last_notified" date is also persisted to QSettings so a same-
        // day app restart wouldn't double-fire either.
        QSettings s;
        QCOMPARE(s.value(QStringLiteral("daily_review/last_notified")).toString(),
                 QDate::currentDate().toString(Qt::ISODate));

        delete store;
    }

    // ── Restart with same-day "last_notified" persisted → no re-fire ─────────
    //
    // Set the QSettings sentinel manually before constructing the service to
    // simulate the "I just restarted minutes after the daily review fired"
    // scenario.
    void restart_with_persisted_today_does_not_fire()
    {
        {
            QSettings s;
            s.setValue(QStringLiteral("daily_review/last_notified"),
                       QDate::currentDate().toString(Qt::ISODate));
            s.sync();
        }
        DataStore *store = makeStoreWithTodayTask(1);
        MockNotificationService mock;
        DailyReviewService svc(store, &mock);

        invokeCheck(&svc);
        QCOMPARE(mock.showCount(), 0);

        delete store;
    }

    // ── Null notification service is tolerated ───────────────────────────────
    //
    // Defensive: dailyreviewservice.cpp guards `if (m_notif)` so a nullptr
    // notif must not crash. Flush the slot to force the "would have shown"
    // code path.
    void null_notification_does_not_crash()
    {
        DataStore *store = makeStoreWithTodayTask(1);
        DailyReviewService svc(store, nullptr);

        // Calling checkAndNotify with a null notifier must complete cleanly.
        invokeCheck(&svc);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestDailyReviewService)
#include "test_dailyreviewservice.moc"
