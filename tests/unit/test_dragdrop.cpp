#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: store with Inbox project
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// Returns the list index (in the VM's current filtered view) of the task with
// the given id, or -1 if not found.
static int vmIndexOf(TaskViewModel &vm, const QString &id)
{
    for (int i = 0; i < vm.rowCount(); ++i) {
        if (vm.data(vm.index(i), TaskViewModel::IdRole).toString() == id)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
class TestDragDrop : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // moveTaskOrder — basic reorder
    // -----------------------------------------------------------------------

    void moveTaskOrder_reorders_to_position()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("a"), inboxId);
        Task *t2 = store->addTask(QStringLiteral("b"), inboxId);
        Task *t3 = store->addTask(QStringLiteral("c"), inboxId);

        // Give explicit order values matching insertion order.
        t1->setOrder(0);
        t2->setOrder(1);
        t3->setOrder(2);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        QCOMPARE(vm.rowCount(), 3);
        // Initial order: a=0, b=1, c=2
        QCOMPARE(vmIndexOf(vm, t1->id()), 0);
        QCOMPARE(vmIndexOf(vm, t2->id()), 1);
        QCOMPARE(vmIndexOf(vm, t3->id()), 2);

        // B-3.35: moveTaskOrder now clamps to [0, size] (not size-1), so to move
        // to the true last position we must pass rowCount() (i.e. size = 3).
        // Passing 2 with 3 items means insertAt=(2>0)?2-1:2=1, which puts t1
        // at position 1 (between t2 and t3), not at the end.
        bool ok = vm.moveTaskOrder(t1->id(), vm.rowCount()); // append-to-end
        QVERIFY(ok);

        // After move: b at 0, c at 1, a at 2
        QCOMPARE(vmIndexOf(vm, t2->id()), 0);
        QCOMPARE(vmIndexOf(vm, t3->id()), 1);
        QCOMPARE(vmIndexOf(vm, t1->id()), 2);

        // B-3.36: non-Today views use spaced order values (10, 20, 30, ...) after reorder.
        QCOMPARE(t2->order(), 10);
        QCOMPARE(t3->order(), 20);
        QCOMPARE(t1->order(), 30);

        delete store;
    }

    void moveTaskOrder_move_to_front()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("first"),  inboxId);
        Task *t2 = store->addTask(QStringLiteral("second"), inboxId);
        Task *t3 = store->addTask(QStringLiteral("third"),  inboxId);

        t1->setOrder(0); t2->setOrder(1); t3->setOrder(2);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        // Move "third" (index 2) to the front (index 0)
        vm.moveTaskOrder(t3->id(), 0);

        QCOMPARE(vmIndexOf(vm, t3->id()), 0);
        QCOMPARE(vmIndexOf(vm, t1->id()), 1);
        QCOMPARE(vmIndexOf(vm, t2->id()), 2);

        delete store;
    }

    // -----------------------------------------------------------------------
    // moveTaskOrder — edge cases
    // -----------------------------------------------------------------------

    void moveTaskOrder_invalid_id_returns_false()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        bool ok = vm.moveTaskOrder(QStringLiteral("nonexistent-uuid"), 0);
        QVERIFY(!ok);

        delete store;
    }

    void moveTaskOrder_drop_on_self_is_noop()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("only"), inboxId);
        Task *t2 = store->addTask(QStringLiteral("other"), inboxId);
        t1->setOrder(0); t2->setOrder(1);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const int idxBefore = vmIndexOf(vm, t1->id());
        bool ok = vm.moveTaskOrder(t1->id(), idxBefore);
        QVERIFY(ok); // succeeds but is a no-op
        QCOMPARE(vmIndexOf(vm, t1->id()), idxBefore);

        delete store;
    }

    void moveTaskOrder_index_clamped_below_zero()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("a"), inboxId);
        Task *t2 = store->addTask(QStringLiteral("b"), inboxId);
        t1->setOrder(0); t2->setOrder(1);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        // newIndex = -5 should be clamped to 0
        vm.moveTaskOrder(t2->id(), -5);
        QCOMPARE(vmIndexOf(vm, t2->id()), 0);

        delete store;
    }

    void moveTaskOrder_index_clamped_above_count()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("a"), inboxId);
        Task *t2 = store->addTask(QStringLiteral("b"), inboxId);
        t1->setOrder(0); t2->setOrder(1);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        // newIndex = 999 should be clamped to rowCount()-1 = 1
        vm.moveTaskOrder(t1->id(), 999);
        QCOMPARE(vmIndexOf(vm, t1->id()), 1);

        delete store;
    }

    // -----------------------------------------------------------------------
    // moveTask — move to different project (sidebar drop)
    // -----------------------------------------------------------------------

    void moveTask_to_different_project_changes_projectId()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        Task *t = store->addTask(QStringLiteral("My task"), inboxId);
        QCOMPARE(t->projectId(), inboxId);

        TaskViewModel vm(store);
        bool ok = vm.moveTask(t->id(), workId, {});
        QVERIFY(ok);

        QCOMPARE(t->projectId(), workId);

        delete store;
    }

    void moveTask_to_same_project_is_noop_in_store()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Task *t = store->addTask(QStringLiteral("Task"), inboxId);

        TaskViewModel vm(store);
        bool ok = vm.moveTask(t->id(), inboxId, {});
        QVERIFY(ok);
        QCOMPARE(t->projectId(), inboxId); // unchanged

        delete store;
    }

    void moveTask_unknown_id_returns_false()
    {
        DataStore *store = makeStore();
        Project *work = store->addProject(QStringLiteral("Work"));

        TaskViewModel vm(store);
        bool ok = vm.moveTask(QStringLiteral("bad-id"), work->id(), {});
        QVERIFY(!ok);

        delete store;
    }

    // -----------------------------------------------------------------------
    // convertToSubtask — drop-on-task DnD (subtask creation)
    // -----------------------------------------------------------------------

    void convertToSubtask_sets_parentId()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *parent = store->addTask(QStringLiteral("Parent"), inboxId);
        Task *child  = store->addTask(QStringLiteral("Child"),  inboxId);

        TaskViewModel vm(store);
        bool ok = vm.convertToSubtask(child->id(), parent->id());
        QVERIFY(ok);
        QCOMPARE(child->parentId(), parent->id());

        delete store;
    }

    void convertToSubtask_self_returns_false()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Task *t = store->addTask(QStringLiteral("Task"), inboxId);

        TaskViewModel vm(store);
        bool ok = vm.convertToSubtask(t->id(), t->id());
        QVERIFY(!ok);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Order is persisted in Task objects after multiple reorders
    // -----------------------------------------------------------------------

    void moveTaskOrder_sequential_moves_maintain_correct_order()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t1 = store->addTask(QStringLiteral("1"), inboxId);
        Task *t2 = store->addTask(QStringLiteral("2"), inboxId);
        Task *t3 = store->addTask(QStringLiteral("3"), inboxId);
        Task *t4 = store->addTask(QStringLiteral("4"), inboxId);

        t1->setOrder(0); t2->setOrder(1); t3->setOrder(2); t4->setOrder(3);
        emit store->tasksChanged();

        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        // Move 4 to front: 4,1,2,3
        vm.moveTaskOrder(t4->id(), 0);
        QCOMPARE(vmIndexOf(vm, t4->id()), 0);

        // B-3.35: to move t2 to the true last position pass rowCount() (= 4),
        // not 3 — passing 3 with srcIndex=2 gives insertAt=2 (no-op).
        // Move 2 to back: 4,1,3,2
        vm.moveTaskOrder(t2->id(), vm.rowCount()); // append-to-end
        QCOMPARE(vmIndexOf(vm, t2->id()), 3);

        // B-3.36: non-Today views use spaced order values (10, 20, 30, 40, …).
        // Verify the values form a 4-element sequence of multiples of 10.
        QList<int> orders;
        for (int i = 0; i < vm.rowCount(); ++i)
            orders.append(vm.data(vm.index(i), TaskViewModel::OrderRole).toInt());
        std::sort(orders.begin(), orders.end());
        QCOMPARE(orders, QList<int>({10, 20, 30, 40}));

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestDragDrop)
#include "test_dragdrop.moc"
