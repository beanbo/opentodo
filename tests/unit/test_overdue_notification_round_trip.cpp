// =============================================================================
// Pin test for iter-night-8: NotificationsViewModel::scanOverdueTasks must
// clear `m_overdueTaskIds` for ANY task that is no longer overdue, so re-
// overduing the same task later fires a fresh "Просроченная задача"
// notification.
//
// Before the fix, the cleanup gate at line 304 only fired when the task was
// null/deleted/completed/dueDate-cleared. Moving the due date to a FUTURE
// non-empty value (or any past day that wasn't "today" → still overdue)
// failed that gate; the id stayed pinned in the set, and the contains()
// guard at line 280 suppressed the next notification.
//
// The user-visible scenario: a recurring task bumps its due date forward
// on completion. After the next cycle, when it slides back into the past,
// no overdue toast/notification-centre entry appears for the rest of the
// session.
//
// This test pins:
//   1. Past dueDate → scan fires entry. Set holds the id.
//   2. Move dueDate to future → scan removes the id (was the bug).
//   3. Move dueDate back to past → scan fires a SECOND entry.
//   4. Same round-trip for datetime-only tasks (dueDate empty,
//      dueDatetime set).
//   5. Clearing dueDate of an overdue task removes it from the set
//      (already worked, pin protects against regression).
//   6. Completing an overdue task removes it from the set (pin).
// =============================================================================

#include <QtTest>
#include <QString>
#include <QDateTime>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "sync/syncengine.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/notificationsviewmodel.h"

namespace {

static int countOverdueEntries(NotificationsViewModel *vm)
{
    // KindRole is Qt::UserRole + 5 (IdRole=+1, Title=+2, Body=+3, Ts=+4,
    // Kind=+5). NotificationsViewModel::Notification::TaskOverdue = 1.
    static constexpr int KindRoleIdx = Qt::UserRole + 5;
    int n = 0;
    for (int i = 0; i < vm->rowCount(); ++i) {
        const QModelIndex idx = vm->index(i, 0);
        const int kind = vm->data(idx, KindRoleIdx).toInt();
        if (kind == int(NotificationsViewModel::KindTaskOverdue)) ++n;
    }
    return n;
}

static void triggerScan(NotificationsViewModel *vm)
{
    // scanOverdueTasks is a private slot; invoke via the meta object.
    QMetaObject::invokeMethod(vm, "scanOverdueTasks", Qt::DirectConnection);
}

static QString isoDayString(const QDate &d)
{
    return d.toString(Qt::ISODate);
}

} // namespace

class TestOverdueNotificationRoundTrip : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    QString inboxId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
    }
    void cleanup() { delete store; store = nullptr; }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) past → future → past round-trip: TWO overdue entries.
    // ─────────────────────────────────────────────────────────────────────────
    void overdueTask_movedToFutureThenBack_firesTwice()
    {
        Task *t = store->addTask(QStringLiteral("Recurring"), inboxId);
        const QDate yesterday = QDate::currentDate().addDays(-1);
        t->setDueDate(isoDayString(yesterday));

        NotificationsViewModel vm(store, nullptr);
        triggerScan(&vm);
        QCOMPARE(countOverdueEntries(&vm), 1);

        // Move the due date to a future day.
        const QDate nextWeek = QDate::currentDate().addDays(7);
        t->setDueDate(isoDayString(nextWeek));
        triggerScan(&vm);
        // Still 1 entry visible (previous one isn't removed from the
        // notification centre — that's a display concern, not the bug).
        // The KEY thing we pin: the tracking set was cleared, so the next
        // round-trip into the past fires AGAIN.

        // Move back to the past.
        const QDate threeDaysAgo = QDate::currentDate().addDays(-3);
        t->setDueDate(isoDayString(threeDaysAgo));
        triggerScan(&vm);

        QVERIFY2(countOverdueEntries(&vm) >= 2,
                 "Past → future → past round-trip must produce TWO overdue "
                 "notifications. Before iter-night-8 the cleanup gate's "
                 "`dueDate().isEmpty()` only caught date-CLEARED tasks, so "
                 "future-moved-then-back tasks were stuck in the set and "
                 "the second notification was suppressed.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Same round-trip for datetime-only tasks.
    //    (dueDate empty + dueDatetime set is the second overdue path in
    //    DataStore::overdueTasks — datastore.cpp:600-604.)
    // ─────────────────────────────────────────────────────────────────────────
    void datetimeOnlyOverdueTask_movedToFutureThenBack_firesTwice()
    {
        Task *t = store->addTask(QStringLiteral("DTOnly"), inboxId);
        const QDateTime pastDt = QDateTime::currentDateTime().addSecs(-3600);
        t->setDueDate(QString());
        t->setDueDatetime(pastDt.toUTC().toString(Qt::ISODate));

        NotificationsViewModel vm(store, nullptr);
        triggerScan(&vm);
        QCOMPARE(countOverdueEntries(&vm), 1);

        // Forward to future, then back.
        const QDateTime futureDt = QDateTime::currentDateTime().addSecs(86400);
        t->setDueDatetime(futureDt.toUTC().toString(Qt::ISODate));
        triggerScan(&vm);

        const QDateTime past2 = QDateTime::currentDateTime().addSecs(-7200);
        t->setDueDatetime(past2.toUTC().toString(Qt::ISODate));
        triggerScan(&vm);

        QVERIFY2(countOverdueEntries(&vm) >= 2,
                 "Datetime-only round-trip must also fire twice — the "
                 "single-set-source-of-truth fix unifies both code paths.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Clearing the due date of an overdue task removes its id from the set
    //    so a fresh re-add fires again. (Regression guard — this case used to
    //    work via the now-removed `dueDate().isEmpty()` gate.)
    // ─────────────────────────────────────────────────────────────────────────
    void overdueTask_dueCleared_thenReSet_firesTwice()
    {
        Task *t = store->addTask(QStringLiteral("Clearable"), inboxId);
        t->setDueDate(isoDayString(QDate::currentDate().addDays(-2)));

        NotificationsViewModel vm(store, nullptr);
        triggerScan(&vm);
        QCOMPARE(countOverdueEntries(&vm), 1);

        t->setDueDate(QString());     // user cleared the due date
        triggerScan(&vm);

        t->setDueDate(isoDayString(QDate::currentDate().addDays(-1)));
        triggerScan(&vm);

        QVERIFY(countOverdueEntries(&vm) >= 2);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Completing an overdue task releases the tracking slot.
    // ─────────────────────────────────────────────────────────────────────────
    void overdueTask_completedThenUncompleted_firesTwice()
    {
        Task *t = store->addTask(QStringLiteral("Completable"), inboxId);
        t->setDueDate(isoDayString(QDate::currentDate().addDays(-1)));

        NotificationsViewModel vm(store, nullptr);
        triggerScan(&vm);
        QCOMPARE(countOverdueEntries(&vm), 1);

        t->setIsCompleted(true);
        triggerScan(&vm);

        t->setIsCompleted(false);
        // dueDate is still in the past from step 1, so it's overdue again.
        triggerScan(&vm);

        QVERIFY(countOverdueEntries(&vm) >= 2);
    }
};

QTEST_GUILESS_MAIN(TestOverdueNotificationRoundTrip)
#include "test_overdue_notification_round_trip.moc"
