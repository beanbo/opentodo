#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QDateTime>

#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "viewmodels/projectviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: build a DataStore with a minimal Inbox project so addTask works.
// ---------------------------------------------------------------------------
static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// ---------------------------------------------------------------------------
// TestProjectViewModelDefaults
//
// Covers per-project default-priority and default-labels persistence
// (QSettings-backed) plus the weeklyGoalReached signal latched by ISO week.
// ---------------------------------------------------------------------------
class TestProjectViewModelDefaults : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Throw-away QSettings scope so tests don't pollute real user settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_ProjVMDefaults"));
        QCoreApplication::setApplicationName(QStringLiteral("test_projectviewmodel_defaults"));
    }

    void init()
    {
        // Wipe any keys this test fixture cares about between cases.
        QSettings s;
        s.remove(QStringLiteral("projects"));
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("projects"));
        s.sync();
    }

    // -----------------------------------------------------------------------
    // defaultPriorityForProject — read default
    // -----------------------------------------------------------------------
    void defaultPriority_unset_returns_4()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        QVERIFY(!id.isEmpty());

        // Nothing was ever written for this project — must return the
        // documented default of 4 (no-priority).
        QCOMPARE(vm.defaultPriorityForProject(id), 4);

        delete store;
    }

    // -----------------------------------------------------------------------
    // defaultPriorityForProject — write/read round-trip
    // -----------------------------------------------------------------------
    void defaultPriority_round_trip_priority_1()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        vm.setDefaultPriorityForProject(id, 1);
        QCOMPARE(vm.defaultPriorityForProject(id), 1);

        // Confirm persistence to QSettings under the documented key.
        QSettings s;
        QCOMPARE(s.value(QStringLiteral("projects/%1/defaultPriority").arg(id)).toInt(), 1);

        delete store;
    }

    void defaultPriority_round_trip_priority_2()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        vm.setDefaultPriorityForProject(id, 2);
        QCOMPARE(vm.defaultPriorityForProject(id), 2);

        delete store;
    }

    // -----------------------------------------------------------------------
    // defaultPriorityForProject — clamping
    // -----------------------------------------------------------------------
    void defaultPriority_clamps_zero_to_one()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        vm.setDefaultPriorityForProject(id, 0);
        // Setter clamps to [1, 4] — value below 1 stored as 1.
        QCOMPARE(vm.defaultPriorityForProject(id), 1);

        delete store;
    }

    void defaultPriority_clamps_five_to_four()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        vm.setDefaultPriorityForProject(id, 5);
        // Setter clamps to [1, 4] — value above 4 stored as 4.
        QCOMPARE(vm.defaultPriorityForProject(id), 4);

        delete store;
    }

    void defaultPriority_clamps_negative_to_one()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        vm.setDefaultPriorityForProject(id, -10);
        QCOMPARE(vm.defaultPriorityForProject(id), 1);

        delete store;
    }

    void defaultPriority_isolated_per_project()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"));

        vm.setDefaultPriorityForProject(a, 1);
        vm.setDefaultPriorityForProject(b, 3);

        QCOMPARE(vm.defaultPriorityForProject(a), 1);
        QCOMPARE(vm.defaultPriorityForProject(b), 3);

        delete store;
    }

    // -----------------------------------------------------------------------
    // defaultLabelsForProject — empty default
    // -----------------------------------------------------------------------
    void defaultLabels_unset_returns_empty()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        const QStringList got = vm.defaultLabelsForProject(id);
        QVERIFY(got.isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // defaultLabelsForProject — round-trip preserves order
    // -----------------------------------------------------------------------
    void defaultLabels_round_trip_preserves_order()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        const QStringList in = {
            QStringLiteral("label-id-aaa"),
            QStringLiteral("label-id-bbb"),
            QStringLiteral("label-id-ccc"),
        };
        vm.setDefaultLabelsForProject(id, in);

        const QStringList out = vm.defaultLabelsForProject(id);
        QCOMPARE(out.size(), 3);
        QCOMPARE(out, in); // QStringList comparison is element-wise and order-sensitive.

        delete store;
    }

    // -----------------------------------------------------------------------
    // defaultLabelsForProject — empty list write overwrites previous value
    // -----------------------------------------------------------------------
    void defaultLabels_empty_list_write_overwrites_previous()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P1"));
        const QStringList in = {
            QStringLiteral("label-id-aaa"),
            QStringLiteral("label-id-bbb"),
        };
        vm.setDefaultLabelsForProject(id, in);
        QCOMPARE(vm.defaultLabelsForProject(id).size(), 2);

        // Overwrite with an empty list.
        vm.setDefaultLabelsForProject(id, QStringList{});
        QVERIFY(vm.defaultLabelsForProject(id).isEmpty());

        delete store;
    }

    void defaultLabels_isolated_per_project()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"));

        vm.setDefaultLabelsForProject(a, {QStringLiteral("la1"), QStringLiteral("la2")});
        vm.setDefaultLabelsForProject(b, {QStringLiteral("lb1")});

        QCOMPARE(vm.defaultLabelsForProject(a).size(), 2);
        QCOMPARE(vm.defaultLabelsForProject(b).size(), 1);
        QCOMPARE(vm.defaultLabelsForProject(b).first(), QStringLiteral("lb1"));

        delete store;
    }

    // -----------------------------------------------------------------------
    // weeklyGoalReached signal — fires once with the right projectId
    // -----------------------------------------------------------------------
    void weeklyGoalReached_fires_when_goal_reached()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        // Project "Goal" has weekly goal of 3; project "NoGoal" has 0.
        const QString goalId = vm.addProject(QStringLiteral("Goal"));
        Project *goalP = store->projectById(goalId);
        QVERIFY(goalP);
        goalP->setWeeklyGoal(3);

        const QString noGoalId = vm.addProject(QStringLiteral("NoGoal"));
        Project *noGoalP = store->projectById(noGoalId);
        QVERIFY(noGoalP);
        noGoalP->setWeeklyGoal(0);

        QSignalSpy spy(&vm, &ProjectViewModel::weeklyGoalReached);

        // Complete 3 tasks in "Goal" project, all timestamped now (current ISO week).
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("Task %1").arg(i), goalId);
            QVERIFY(t);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        // Notify VM that tasks changed so onTasksChanged() runs and evaluates goals.
        emit store->tasksChanged();

        QTRY_COMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), goalId);

        delete store;
    }

    // -----------------------------------------------------------------------
    // weeklyGoalReached — does NOT fire again on the 4th completion same week
    // -----------------------------------------------------------------------
    void weeklyGoalReached_does_not_refire_after_goal()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString goalId = vm.addProject(QStringLiteral("Goal"));
        Project *goalP = store->projectById(goalId);
        QVERIFY(goalP);
        goalP->setWeeklyGoal(3);

        QSignalSpy spy(&vm, &ProjectViewModel::weeklyGoalReached);

        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("Task %1").arg(i), goalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        emit store->tasksChanged();
        QTRY_COMPARE(spy.count(), 1);

        // Add and complete a 4th task in the same week — the (projectId, week)
        // latch in m_celebratedWeek must suppress a second fire.
        Task *fourth = store->addTask(QStringLiteral("Fourth"), goalId);
        fourth->setIsCompleted(true);
        fourth->setCompletedAt(now);
        emit store->tasksChanged();

        QTest::qWait(80); // let the debounced goal scan run
        QCOMPARE(spy.count(), 1); // still exactly one — no second fire.

        delete store;
    }

    // -----------------------------------------------------------------------
    // weeklyGoalReached — never fires for a project with weeklyGoal == 0
    // -----------------------------------------------------------------------
    void weeklyGoalReached_never_fires_when_goal_is_zero()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString noGoalId = vm.addProject(QStringLiteral("NoGoal"));
        Project *p = store->projectById(noGoalId);
        QVERIFY(p);
        p->setWeeklyGoal(0);

        QSignalSpy spy(&vm, &ProjectViewModel::weeklyGoalReached);

        const QDateTime now = QDateTime::currentDateTime();
        // Even completing many tasks: weeklyGoal of 0 short-circuits before
        // the >= comparison (goal <= 0 -> continue).
        for (int i = 0; i < 10; ++i) {
            Task *t = store->addTask(QStringLiteral("Task %1").arg(i), noGoalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        emit store->tasksChanged();

        QTest::qWait(80); // let the debounced goal scan run
        QCOMPARE(spy.count(), 0);

        delete store;
    }

    // -----------------------------------------------------------------------
    // weeklyGoalReached — re-arms after a "new week" simulation:
    // when all completions move out of the current ISO week, the count drops
    // below goal and the latch un-arms (see the `else if` branch in
    // ProjectViewModel::onTasksChanged). Re-completing inside this week
    // re-fires the signal.
    // -----------------------------------------------------------------------
    void weeklyGoalReached_rearms_after_week_resets()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString goalId = vm.addProject(QStringLiteral("Goal"));
        Project *p = store->projectById(goalId);
        QVERIFY(p);
        p->setWeeklyGoal(3);

        QSignalSpy spy(&vm, &ProjectViewModel::weeklyGoalReached);

        // Phase 1 — complete 3 tasks within current ISO week.
        const QDateTime now = QDateTime::currentDateTime();
        QList<Task *> initialTasks;
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("InitTask %1").arg(i), goalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
            initialTasks.append(t);
        }
        emit store->tasksChanged();
        QTRY_COMPARE(spy.count(), 1);

        // Phase 2 — push every initial task's completedAt into the past
        // (last week). This drops weeklyCompletedForProject() below goal,
        // tripping the un-arm branch (`completed < goal && lastWeekKey == weekKey`).
        const QDateTime lastWeek = now.addDays(-14);
        for (Task *t : initialTasks)
            t->setCompletedAt(lastWeek);
        emit store->tasksChanged(); // un-arms the latch.
        QTest::qWait(80); // let the debounced un-arm scan run before phase 3

        // Phase 3 — add 3 fresh completions inside the current week.
        // The signal must fire again (re-armed).
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("FreshTask %1").arg(i), goalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        emit store->tasksChanged();

        QTRY_COMPARE(spy.count(), 2);
        const QList<QVariant> last = spy.takeLast();
        QCOMPARE(last.at(0).toString(), goalId);

        delete store;
    }

    // -----------------------------------------------------------------------
    // weeklyGoalReached — fires only for the project whose goal was reached
    // -----------------------------------------------------------------------
    void weeklyGoalReached_emits_correct_project_id_with_two_projects()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString goalId = vm.addProject(QStringLiteral("Goal"));
        Project *goalP = store->projectById(goalId);
        goalP->setWeeklyGoal(3);

        const QString noGoalId = vm.addProject(QStringLiteral("NoGoal"));
        Project *noGoalP = store->projectById(noGoalId);
        noGoalP->setWeeklyGoal(0);

        QSignalSpy spy(&vm, &ProjectViewModel::weeklyGoalReached);

        const QDateTime now = QDateTime::currentDateTime();

        // Distractor: complete some tasks in the goal-less project — must not fire.
        for (int i = 0; i < 5; ++i) {
            Task *t = store->addTask(QStringLiteral("NoGoalTask %1").arg(i), noGoalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        emit store->tasksChanged();
        QTest::qWait(80); // let the debounced goal scan run
        QCOMPARE(spy.count(), 0);

        // Now complete 3 in the goal project — must fire exactly once with goalId.
        for (int i = 0; i < 3; ++i) {
            Task *t = store->addTask(QStringLiteral("GoalTask %1").arg(i), goalId);
            t->setIsCompleted(true);
            t->setCompletedAt(now);
        }
        emit store->tasksChanged();

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), goalId);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestProjectViewModelDefaults)
#include "test_projectviewmodel_defaults.moc"
