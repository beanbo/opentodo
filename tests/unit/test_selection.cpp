#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: create a fresh DataStore with an Inbox project.
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// ---------------------------------------------------------------------------
// Helper: add N tasks to the inbox and return their ids in insertion order.
// ---------------------------------------------------------------------------
static QStringList addTasks(DataStore *store, int n)
{
    const QString inboxId = store->inboxProject()->id();
    QStringList ids;
    for (int i = 0; i < n; ++i)
        ids.append(store->addTask(QStringLiteral("Task %1").arg(i), inboxId)->id());
    return ids;
}

// ---------------------------------------------------------------------------
// TestSelection
// ---------------------------------------------------------------------------
class TestSelection : public QObject
{
    Q_OBJECT

private slots:

    void selectTask_adds_to_set()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);

        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(!vm.isSelected(ids[1]));
        QCOMPARE(vm.selectionCount(), 1);
        QVERIFY(vm.selectedIds().contains(ids[0]));

        delete store;
    }

    void deselectTask_removes()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(vm.selectionCount(), 2);

        vm.deselectTask(ids[0]);
        QVERIFY(!vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));
        QCOMPARE(vm.selectionCount(), 1);

        delete store;
    }

    void toggleSelection_flips()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 2);

        vm.toggleSelection(ids[0]);
        QVERIFY(vm.isSelected(ids[0]));

        vm.toggleSelection(ids[0]);
        QVERIFY(!vm.isSelected(ids[0]));

        delete store;
    }

    void clearSelection_empties()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        vm.selectTask(ids[2]);
        QCOMPARE(vm.selectionCount(), 3);

        vm.clearSelection();
        QCOMPARE(vm.selectionCount(), 0);
        QVERIFY(vm.selectedIds().isEmpty());

        delete store;
    }

    void selectRange_includes_endpoints()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = addTasks(store, 5);

        // ids are in inbox order; selectRange from first to third (indices 0-2)
        vm.selectRange(ids[0], ids[2]);

        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));
        QVERIFY(vm.isSelected(ids[2]));
        QVERIFY(!vm.isSelected(ids[3]));
        QVERIFY(!vm.isSelected(ids[4]));

        delete store;
    }

    void selectRange_reversed_also_works()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = addTasks(store, 5);

        // Reverse order: from last to first in the range
        vm.selectRange(ids[3], ids[1]);

        QVERIFY(!vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));
        QVERIFY(vm.isSelected(ids[2]));
        QVERIFY(vm.isSelected(ids[3]));
        QVERIFY(!vm.isSelected(ids[4]));

        delete store;
    }

    void selectionChanged_signal_emitted_on_select()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 2);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        vm.selectTask(ids[0]);
        QCOMPARE(spy.count(), 1);

        // Selecting the same id again should not emit again
        vm.selectTask(ids[0]);
        QCOMPARE(spy.count(), 1);

        delete store;
    }

    void bulkComplete_marks_all()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        vm.bulkComplete();

        QVERIFY(store->taskById(ids[0])->isCompleted());
        QVERIFY(store->taskById(ids[1])->isCompleted());
        QVERIFY(!store->taskById(ids[2])->isCompleted());

        delete store;
    }

    void bulkDelete_marks_all_deleted()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[2]);
        vm.bulkDelete();

        // DataStore::removeTask sets is_deleted=true (soft delete)
        QVERIFY(store->taskById(ids[0]) == nullptr
                || store->taskById(ids[0])->isDeleted());
        QVERIFY(store->taskById(ids[1]) != nullptr
                && !store->taskById(ids[1])->isDeleted());
        QVERIFY(store->taskById(ids[2]) == nullptr
                || store->taskById(ids[2])->isDeleted());

        delete store;
    }

    void bulkMoveToProject_changes_projectId_for_all()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = addTasks(store, 3);

        Project *target = store->addProject(QStringLiteral("Work"));
        const QString targetId = target->id();

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        vm.bulkMoveToProject(targetId);

        QCOMPARE(store->taskById(ids[0])->projectId(), targetId);
        QCOMPARE(store->taskById(ids[1])->projectId(), targetId);
        // ids[2] was not selected — must stay in inbox
        QCOMPARE(store->taskById(ids[2])->projectId(),
                 store->inboxProject()->id());

        delete store;
    }

    void bulkSetPriority_sets_for_all()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        // Default priority is 1; set ids[0] and ids[2] to priority 2,
        // then verify only those two changed.
        vm.selectTask(ids[0]);
        vm.selectTask(ids[2]);
        vm.bulkSetPriority(2);

        QCOMPARE(store->taskById(ids[0])->priority(), 2);
        QCOMPARE(store->taskById(ids[2])->priority(), 2);
        // ids[1] was not selected — stays at default priority (1)
        QCOMPARE(store->taskById(ids[1])->priority(), 1);

        delete store;
    }

    void bulkAddLabel_adds_to_all_selected()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        // Pre-set a label on ids[0] to verify it doesn't get duplicated
        store->taskById(ids[0])->setLabels({QStringLiteral("existing")});

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        vm.bulkAddLabel(QStringLiteral("urgent"));

        QVERIFY(store->taskById(ids[0])->labels().contains(QStringLiteral("urgent")));
        QVERIFY(store->taskById(ids[1])->labels().contains(QStringLiteral("urgent")));
        // ids[0] should still have its original label too
        QVERIFY(store->taskById(ids[0])->labels().contains(QStringLiteral("existing")));
        // ids[2] was not selected — should not have the label
        QVERIFY(!store->taskById(ids[2])->labels().contains(QStringLiteral("urgent")));

        delete store;
    }

    void selection_cleared_after_bulk_op()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        const QStringList ids = addTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(vm.selectionCount(), 2);

        vm.bulkComplete();
        QCOMPARE(vm.selectionCount(), 0);

        vm.selectTask(ids[2]);
        vm.bulkSetPriority(2);
        QCOMPARE(vm.selectionCount(), 0);

        vm.selectTask(ids[0]);
        vm.bulkMoveToProject(store->inboxProject()->id());
        QCOMPARE(vm.selectionCount(), 0);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestSelection)
#include "test_selection.moc"
