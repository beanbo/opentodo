#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/karmaviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: create a fresh DataStore with an Inbox project already set up.
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// ---------------------------------------------------------------------------
// TestViewModels
// ---------------------------------------------------------------------------
class TestViewModels : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // TaskViewModel
    // -----------------------------------------------------------------------

    void task_filterMode_today_returns_only_todays_tasks()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString future = QDate::currentDate().addDays(5).toString(QStringLiteral("yyyy-MM-dd"));

        // Add two tasks: one due today, one due in the future.
        const QString inboxId = store->inboxProject()->id();
        Task *tToday = store->addTask(QStringLiteral("Due today"), inboxId);
        tToday->setDueDate(today);
        Task *tFuture = store->addTask(QStringLiteral("Due future"), inboxId);
        tFuture->setDueDate(future);

        vm.setFilterMode(TaskViewModel::FilterMode::Today);

        // Today filter includes overdue tasks too but must include today's task.
        bool foundToday = false;
        bool foundFuture = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            auto idx = vm.index(i);
            const QString id = vm.data(idx, TaskViewModel::IdRole).toString();
            if (id == tToday->id()) foundToday = true;
            if (id == tFuture->id()) foundFuture = true;
        }
        QVERIFY(foundToday);
        QVERIFY(!foundFuture);

        delete store;
    }

    void task_filterMode_inbox_returns_inbox_tasks()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Project *other = store->addProject(QStringLiteral("Work"));
        const QString otherId = other->id();

        Task *tInbox = store->addTask(QStringLiteral("Inbox task"), inboxId);
        Task *tOther = store->addTask(QStringLiteral("Work task"), otherId);

        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        bool foundInbox = false;
        bool foundOther = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            auto idx = vm.index(i);
            const QString id = vm.data(idx, TaskViewModel::IdRole).toString();
            if (id == tInbox->id()) foundInbox = true;
            if (id == tOther->id()) foundOther = true;
        }
        QVERIFY(foundInbox);
        QVERIFY(!foundOther);

        delete store;
    }

    void task_filterMode_upcoming_returns_future_tasks()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString tomorrow = QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd"));
        const QString inboxId = store->inboxProject()->id();

        Task *tToday = store->addTask(QStringLiteral("Today task"), inboxId);
        tToday->setDueDate(today);
        Task *tTomorrow = store->addTask(QStringLiteral("Tomorrow task"), inboxId);
        tTomorrow->setDueDate(tomorrow);

        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);

        bool foundToday = false;
        bool foundTomorrow = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            auto idx = vm.index(i);
            const QString id = vm.data(idx, TaskViewModel::IdRole).toString();
            if (id == tToday->id()) foundToday = true;
            if (id == tTomorrow->id()) foundTomorrow = true;
        }

        // "upcoming" includes tasks with dueDate >= today per the implementation
        // (it uses d >= today). Today's task should appear; tomorrow certainly too.
        QVERIFY(foundTomorrow);

        delete store;
    }

    void task_filterMode_completed_returns_completed_tasks()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Task *tActive = store->addTask(QStringLiteral("Active"), inboxId);
        Task *tDone   = store->addTask(QStringLiteral("Done"), inboxId);
        tDone->setIsCompleted(true);
        emit store->tasksChanged(); // notify VM to refresh

        vm.setFilterMode(TaskViewModel::FilterMode::Completed);

        bool foundActive = false;
        bool foundDone = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            auto idx = vm.index(i);
            const QString id = vm.data(idx, TaskViewModel::IdRole).toString();
            if (id == tActive->id()) foundActive = true;
            if (id == tDone->id()) foundDone = true;
        }
        QVERIFY(foundDone);
        QVERIFY(!foundActive);

        delete store;
    }

    void task_filterMode_project_returns_tasks_of_that_project()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        Project *proj = store->addProject(QStringLiteral("ProjectA"));
        const QString pid = proj->id();
        const QString inboxId = store->inboxProject()->id();

        Task *tProj  = store->addTask(QStringLiteral("In ProjectA"), pid);
        Task *tInbox = store->addTask(QStringLiteral("In Inbox"), inboxId);

        vm.setCurrentProjectId(pid);
        vm.setFilterMode(TaskViewModel::FilterMode::Project);

        bool foundProj = false;
        bool foundInbox = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            auto idx = vm.index(i);
            const QString id = vm.data(idx, TaskViewModel::IdRole).toString();
            if (id == tProj->id()) foundProj = true;
            if (id == tInbox->id()) foundInbox = true;
        }
        QVERIFY(foundProj);
        QVERIFY(!foundInbox);

        delete store;
    }

    void task_addTask_appears_in_inbox_view()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const int before = vm.rowCount();
        const QString id = vm.addTask(QStringLiteral("New inbox task"));
        QVERIFY(!id.isEmpty());

        // After addTask, a dataChanged / tasksChanged signal should cause VM to refresh.
        QCOMPARE(vm.rowCount(), before + 1);

        delete store;
    }

    void task_addTask_stores_in_datastore()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Persisted task"));
        QVERIFY(!id.isEmpty());
        QVERIFY(store->taskById(id) != nullptr);
        QCOMPARE(store->taskById(id)->content(), QStringLiteral("Persisted task"));

        delete store;
    }

    void task_completeTask_hides_from_active_filter()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const QString id = vm.addTask(QStringLiteral("Will be completed"));
        QVERIFY(vm.rowCount() > 0);

        bool ok = vm.completeTask(id);
        QVERIFY(ok);

        // Task should no longer appear in the inbox (active) view.
        for (int i = 0; i < vm.rowCount(); ++i) {
            const QString tid = vm.data(vm.index(i), TaskViewModel::IdRole).toString();
            QVERIFY(tid != id);
        }

        delete store;
    }

    void task_completeTask_marks_completed_in_store()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Complete me"));
        vm.completeTask(id);

        Task *task = store->taskById(id);
        QVERIFY(task != nullptr);
        QVERIFY(task->isCompleted());

        delete store;
    }

    // -----------------------------------------------------------------------
    // ITER3 #10: updateTask("projectId") / updateTask("sectionId") delegation
    // -----------------------------------------------------------------------

    void updateTask_projectId_delegates_to_moveTask()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        const QString tid = vm.addTask(QStringLiteral("Move me"), inboxId);
        QVERIFY(!tid.isEmpty());
        QCOMPARE(store->taskById(tid)->projectId(), inboxId);

        bool ok = vm.updateTask(tid, QStringLiteral("projectId"), workId);
        QVERIFY(ok);
        QCOMPARE(store->taskById(tid)->projectId(), workId);

        delete store;
    }

    void updateTask_sectionId_delegates_to_moveTask()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Section *sec = store->addSection(QStringLiteral("Sprint 1"), inboxId);
        const QString secId = sec->id();

        const QString tid = vm.addTask(QStringLiteral("Sectioned task"), inboxId);
        QVERIFY(!tid.isEmpty());
        QVERIFY(store->taskById(tid)->sectionId().isEmpty());

        bool ok = vm.updateTask(tid, QStringLiteral("sectionId"), secId);
        QVERIFY(ok);
        QCOMPARE(store->taskById(tid)->sectionId(), secId);

        delete store;
    }

    // -----------------------------------------------------------------------
    // ITER3 #11: updateTask for deadline / durationAmount / durationUnit
    // -----------------------------------------------------------------------

    void updateTask_deadline_sets_field()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Deadline task"));
        QVERIFY(!id.isEmpty());

        bool ok = vm.updateTask(id, QStringLiteral("deadline"), QStringLiteral("2026-12-31"));
        QVERIFY(ok);
        QCOMPARE(store->taskById(id)->deadline(), QStringLiteral("2026-12-31"));

        delete store;
    }

    void updateTask_durationAmount_sets_field()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Duration task"));
        QVERIFY(!id.isEmpty());

        bool ok = vm.updateTask(id, QStringLiteral("durationAmount"), 30);
        QVERIFY(ok);
        QCOMPARE(store->taskById(id)->durationAmount(), 30);

        delete store;
    }

    void updateTask_durationUnit_sets_field()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Duration unit task"));
        QVERIFY(!id.isEmpty());

        bool ok = vm.updateTask(id, QStringLiteral("durationUnit"), QStringLiteral("minute"));
        QVERIFY(ok);
        QCOMPARE(store->taskById(id)->durationUnit(), QStringLiteral("minute"));

        delete store;
    }

    // -----------------------------------------------------------------------
    // ITER3 #12: SectionGroupKeyRole sentinel "__none__"
    // -----------------------------------------------------------------------

    void sectionGroupKey_empty_sectionId_returns_sentinel()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const QString id = vm.addTask(QStringLiteral("No section task"));
        QVERIFY(!id.isEmpty());
        // Task has no section — SectionGroupKeyRole must return "__none__"
        Task *t = store->taskById(id);
        QVERIFY(t);
        QVERIFY(t->sectionId().isEmpty());

        // Find the row in the view model
        for (int i = 0; i < vm.rowCount(); ++i) {
            const auto idx = vm.index(i);
            if (vm.data(idx, TaskViewModel::IdRole).toString() == id) {
                const QString key = vm.data(idx, TaskViewModel::SectionGroupKeyRole).toString();
                QCOMPARE(key, QStringLiteral("__none__"));
                delete store;
                return;
            }
        }
        QFAIL("Task not found in view model");
        delete store;
    }

    void sectionGroupKey_non_empty_sectionId_returns_sectionId()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const QString inboxId = store->inboxProject()->id();
        Section *sec = store->addSection(QStringLiteral("Alpha"), inboxId);
        const QString secId = sec->id();

        const QString id = vm.addTask(QStringLiteral("Sectioned task"), inboxId);
        QVERIFY(!id.isEmpty());
        store->taskById(id)->setSectionId(secId);
        emit store->tasksChanged();

        for (int i = 0; i < vm.rowCount(); ++i) {
            const auto idx = vm.index(i);
            if (vm.data(idx, TaskViewModel::IdRole).toString() == id) {
                const QString key = vm.data(idx, TaskViewModel::SectionGroupKeyRole).toString();
                QCOMPARE(key, secId);
                delete store;
                return;
            }
        }
        QFAIL("Task not found in view model");
        delete store;
    }

    // -----------------------------------------------------------------------
    // ProjectViewModel
    // -----------------------------------------------------------------------

    void project_addProject_exists_in_store_with_correct_fields()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("My Project"), QStringLiteral("#ff0000"));
        QVERIFY(!id.isEmpty());

        Project *p = store->projectById(id);
        QVERIFY(p != nullptr);
        QCOMPARE(p->name(), QStringLiteral("My Project"));
        QCOMPARE(p->color(), QStringLiteral("#ff0000"));

        delete store;
    }

    void project_projectById_invalid_returns_nullptr()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        QVERIFY(vm.projectById(QStringLiteral("nonexistent-id-xyz")) == nullptr);

        delete store;
    }

    void project_addProject_empty_name_still_creates_project()
    {
        // DataStore::addProject does not validate name; empty-name project is created.
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const int before = vm.count();
        const QString id = vm.addProject(QString());
        // An id is returned and the project exists in the store.
        QVERIFY(!id.isEmpty());
        QVERIFY(store->projectById(id) != nullptr);
        QCOMPARE(vm.count(), before + 1);

        delete store;
    }

    void project_rowCount_reflects_added_projects()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        // Inbox is already in the store.
        const int before = vm.count();
        vm.addProject(QStringLiteral("A"), QStringLiteral("red"));
        vm.addProject(QStringLiteral("B"), QStringLiteral("blue"));
        QCOMPARE(vm.count(), before + 2);

        delete store;
    }

    // -----------------------------------------------------------------------
    // SidebarViewModel
    // -----------------------------------------------------------------------

    void sidebar_default_currentView_is_inbox()
    {
        DataStore *store = makeStore();
        ProjectViewModel projectVM(store);
        SidebarViewModel vm(store, &projectVM);

        QCOMPARE(vm.currentView(), QStringLiteral("inbox"));

        delete store;
    }

    void sidebar_navigateTo_updates_currentView_and_emits_signal()
    {
        DataStore *store = makeStore();
        ProjectViewModel projectVM(store);
        SidebarViewModel vm(store, &projectVM);

        QSignalSpy spy(&vm, &SidebarViewModel::navigationRequested);
        vm.navigateTo(QStringLiteral("settings"));

        QCOMPARE(vm.currentView(), QStringLiteral("settings"));
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("settings"));
        QCOMPARE(args.at(1).toString(), QString());

        delete store;
    }

    void sidebar_navigateToProject_sets_project_view_and_itemId()
    {
        DataStore *store = makeStore();
        ProjectViewModel projectVM(store);
        SidebarViewModel vm(store, &projectVM);

        Project *p = store->addProject(QStringLiteral("Proj"));
        const QString pid = p->id();

        QSignalSpy spy(&vm, &SidebarViewModel::navigationRequested);
        vm.navigateToProject(pid);

        QCOMPARE(vm.currentView(), QStringLiteral("project"));
        QCOMPARE(vm.currentItemId(), pid);
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("project"));
        QCOMPARE(args.at(1).toString(), pid);

        delete store;
    }

    void sidebar_toggleSidebar_flips_visibility()
    {
        DataStore *store = makeStore();
        ProjectViewModel projectVM(store);
        SidebarViewModel vm(store, &projectVM);

        const bool initial = vm.sidebarVisible();
        vm.toggleSidebar();
        QCOMPARE(vm.sidebarVisible(), !initial);
        vm.toggleSidebar();
        QCOMPARE(vm.sidebarVisible(), initial);

        delete store;
    }

    // -----------------------------------------------------------------------
    // KarmaViewModel
    // -----------------------------------------------------------------------

    void karma_initial_counters_are_zero()
    {
        DataStore *store = makeStore();
        KarmaViewModel vm(store);

        QCOMPARE(vm.completedToday(), 0);
        QCOMPARE(vm.completedThisWeek(), 0);

        delete store;
    }

    void karma_onTaskCompleted_increments_score()
    {
        DataStore *store = makeStore();
        KarmaViewModel vm(store);

        const int before = vm.karmaScore();
        vm.onTaskCompleted();
        // Score increments by at least 1 (may also get a goal bonus if goal==0,
        // but that edge case is handled: completedToday==0 != dailyGoal()==0 is
        // false so a bonus fires; regardless the score must be >= before+1).
        QVERIFY(vm.karmaScore() >= before + 1);

        delete store;
    }

    void karma_onTaskCompleted_multiple_times_increases_completedToday_via_tasks()
    {
        // completedToday is computed from completed tasks in the store, not from
        // onTaskCompleted() calls directly.  We complete real tasks and then verify
        // that the VM counts them.
        DataStore *store = makeStore();

        // Set daily_goal to something large so bonus doesn't interfere.
        store->setSetting(QStringLiteral("daily_goal"), 100);
        store->setSetting(QStringLiteral("weekly_goal"), 500);

        KarmaViewModel vm(store);
        const QString inboxId = store->inboxProject()->id();

        // Add and complete 3 tasks today.
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("Task %1").arg(i), inboxId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        // Trigger refresh.
        emit store->tasksChanged();

        QCOMPARE(vm.completedToday(), 3);
        QVERIFY(vm.completedThisWeek() >= 3);

        delete store;
    }

    void karma_karmaScore_initial_from_store_settings()
    {
        DataStore *store = makeStore();
        store->setSetting(QStringLiteral("karma_score"), 42);
        KarmaViewModel vm(store);

        QCOMPARE(vm.karmaScore(), 42);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestViewModels)
#include "test_viewmodels.moc"
