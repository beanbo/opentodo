// =============================================================================
// Iter-night-16 pin: SidebarViewModel::updateCounts() must count datetime-only
// tasks (dueDate empty, dueDatetime set) toward `today` and `upcoming`.
//
// Pre-fix the branch fired only on `!t->dueDate().isEmpty()`. A task carrying
// `due.datetime` but no `due.date` — produced by sync from a peer, by import
// of a hand-edited or older-schema state.json, or by a setter that touched
// only dueDatetime — silently dropped out of the badge math even though
// `DataStore::tasksForToday` (B-1.30) and `DataStore::overdueTasks` (B-1.28)
// already counted it, as does `DailyReviewService::checkAndNotify`. The
// sidebar VM was the only count consumer that hadn't received the matching
// branch, so the badge said "3" while the linked view listed 4 rows.
//
// This file is dedicated (rather than extending `test_sidebarviewmodel.cpp`)
// because the existing sidebar VM suite is one of the Q3.24-debounce-related
// pre-existing failures (task #19) — landing this pin in the same file would
// be shadowed by that unrelated flake.
// =============================================================================

#include <QtTest>
#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QString>
#include <QTimeZone>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/projectviewmodel.h"

namespace {

static QString isoLocal(const QDateTime &dt)
{
    return dt.toString(Qt::ISODate);
}

// SidebarViewModel debounces updateCounts() via a 50 ms QTimer (Q3.24).
// Tests must wait for the timer to fire before reading the counts. We block
// on either a countsChanged signal or a 500 ms ceiling — whichever wins.
// `spy` is constructed AT THE TOP of each slot so it captures the post-
// emit-tasksChanged firing of countsChanged that follows the debounce.
static void waitForCountsUpdate(QSignalSpy &spy)
{
    if (spy.count() > 0) return;
    spy.wait(500);
}

} // anonymous namespace


class TestSidebarCountsDatetimeOnly : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    ProjectViewModel *projectVM = nullptr;
    SidebarViewModel *sidebar = nullptr;
    QString inboxId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();

        projectVM = new ProjectViewModel(store);
        sidebar = new SidebarViewModel(store, projectVM);
    }

    void cleanup()
    {
        delete sidebar;   sidebar = nullptr;
        delete projectVM; projectVM = nullptr;
        delete store;     store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Datetime-only task whose date component IS today counts toward
    //    `todayCount`. Pre-fix it stayed at 0 because the entire
    //    today/upcoming branch was gated on `!dueDate().isEmpty()`.
    // ─────────────────────────────────────────────────────────────────────────
    void todayCount_includesDatetimeOnlyTaskWithTodayDateComponent()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        const QDate today = QDate::currentDate();
        const QDateTime atFive = QDateTime(today, QTime(17, 0));

        Task *t = store->addTask(QStringLiteral("Datetime-only today"),
                                 inboxId);
        t->setDueDate(QString());                  // explicit no-date
        t->setDueDatetime(isoLocal(atFive));

        emit store->tasksChanged();
        waitForCountsUpdate(spy);
        QVERIFY2(sidebar->todayCount() >= 1,
                 "Iter-night-16: datetime-only task with date()==today must "
                 "contribute to todayCount. Pre-fix the entire today/upcoming "
                 "branch was gated on !dueDate().isEmpty().");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Datetime-only task whose date IS tomorrow counts toward
    //    `upcomingCount`, not `todayCount`.
    // ─────────────────────────────────────────────────────────────────────────
    void upcomingCount_includesDatetimeOnlyTaskWithTomorrowDateComponent()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        const QDate tomorrow = QDate::currentDate().addDays(1);
        const QDateTime atNine = QDateTime(tomorrow, QTime(9, 0));

        Task *t = store->addTask(QStringLiteral("Datetime-only tomorrow"),
                                 inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(atNine));

        emit store->tasksChanged();
        waitForCountsUpdate(spy);
        QVERIFY(sidebar->upcomingCount() >= 1);
        QCOMPARE(sidebar->todayCount(), 0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Datetime-only task whose datetime is in the PAST still counts
    //    toward `todayCount` (today-or-overdue semantic), mirroring how
    //    DataStore::overdueTasks already treats datetime-only past dt.
    // ─────────────────────────────────────────────────────────────────────────
    void todayCount_includesDatetimeOnlyTaskWithPastDate()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        const QDate threeDaysAgo = QDate::currentDate().addDays(-3);
        const QDateTime past = QDateTime(threeDaysAgo, QTime(8, 0));

        Task *t = store->addTask(QStringLiteral("Datetime-only overdue"),
                                 inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(past));

        emit store->tasksChanged();
        waitForCountsUpdate(spy);
        QVERIFY2(sidebar->todayCount() >= 1,
                 "Past-dated datetime-only task must contribute to "
                 "todayCount, matching DataStore::overdueTasks's B-1.28 "
                 "behaviour and DataStore::tasksForToday's B-1.30.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Completed datetime-only tasks must NOT count, mirroring the
    //    existing dueDate path's `if (t->isCompleted()) continue;` guard.
    // ─────────────────────────────────────────────────────────────────────────
    void datetimeOnlyTask_completed_doesNotCount()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        const QDate today = QDate::currentDate();
        Task *t = store->addTask(QStringLiteral("Done datetime-only"),
                                 inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(QDateTime(today, QTime(12, 0))));
        t->setIsCompleted(true);

        emit store->tasksChanged();
        // No countsChanged is expected since 0→0 isn't a change, but we
        // still need the debounce timer to fire so updateCounts ran.
        spy.wait(120);
        QCOMPARE(sidebar->todayCount(), 0);
        QCOMPARE(sidebar->upcomingCount(), 0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) Mixed scenario — one dueDate-only today, one datetime-only today.
    //    Both contribute. Pin so the iter-night-16 elif branch can't
    //    accidentally shadow the original if branch.
    // ─────────────────────────────────────────────────────────────────────────
    void todayCount_dueDateAndDatetimeOnly_bothContribute()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        const QDate today = QDate::currentDate();
        Task *a = store->addTask(QStringLiteral("Date-only today"),
                                 inboxId);
        a->setDueDate(today.toString(QStringLiteral("yyyy-MM-dd")));

        Task *b = store->addTask(QStringLiteral("DT-only today"),
                                 inboxId);
        b->setDueDate(QString());
        b->setDueDatetime(isoLocal(QDateTime(today, QTime(15, 0))));

        emit store->tasksChanged();
        waitForCountsUpdate(spy);
        QCOMPARE(sidebar->todayCount(), 2);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6) Datetime-only with INVALID ISO does not crash, does not count.
    //    Defensive — DateTimeHelper::fromIsoString may return invalid for
    //    legacy/garbage payloads from older state.json.
    // ─────────────────────────────────────────────────────────────────────────
    void datetimeOnlyTask_invalidIso_isIgnored()
    {
        QSignalSpy spy(sidebar, &SidebarViewModel::countsChanged);
        Task *t = store->addTask(QStringLiteral("Garbage datetime"),
                                 inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(QStringLiteral("not-a-date"));
        emit store->tasksChanged();
        // No countsChanged is expected since 0→0 isn't a change. Wait
        // for the 50 ms debounce to fire either way.
        spy.wait(120);
        QCOMPARE(sidebar->todayCount(), 0);
        QCOMPARE(sidebar->upcomingCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSidebarCountsDatetimeOnly)
#include "test_sidebar_counts_datetime_only.moc"
