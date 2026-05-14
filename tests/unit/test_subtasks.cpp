#include <QtTest>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: store with an Inbox project
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// ---------------------------------------------------------------------------
// TestSubtasks
// ---------------------------------------------------------------------------
class TestSubtasks : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // hasSubtasks
    // -----------------------------------------------------------------------

    void hasSubtasks_false_for_no_children()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Parent"));
        QVERIFY(!id.isEmpty());
        QVERIFY(!vm.hasSubtasks(id));

        delete store;
    }

    void hasSubtasks_true_after_addSubtask()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        QVERIFY(!parentId.isEmpty());

        const QString subId = vm.addSubtask(parentId, QStringLiteral("Child"));
        QVERIFY(!subId.isEmpty());

        QVERIFY(vm.hasSubtasks(parentId));

        delete store;
    }

    // -----------------------------------------------------------------------
    // subtasksOf
    // -----------------------------------------------------------------------

    void subtasksOf_returns_only_direct_children()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        const QString otherId  = vm.addTask(QStringLiteral("Other"));

        // Add two children of parentId and one child of otherId
        const QString sub1 = vm.addSubtask(parentId, QStringLiteral("Child 1"));
        const QString sub2 = vm.addSubtask(parentId, QStringLiteral("Child 2"));
        const QString sub3 = vm.addSubtask(otherId,  QStringLiteral("Other child"));

        QVERIFY(!sub1.isEmpty());
        QVERIFY(!sub2.isEmpty());
        QVERIFY(!sub3.isEmpty());

        const QVariantList result = vm.subtasksOf(parentId);
        QCOMPARE(result.size(), 2);

        QStringList ids;
        for (const QVariant &v : result)
            ids << v.toMap()[QStringLiteral("id")].toString();

        QVERIFY(ids.contains(sub1));
        QVERIFY(ids.contains(sub2));
        QVERIFY(!ids.contains(sub3));

        delete store;
    }

    void subtasksOf_returns_in_order()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));

        const QString sub1 = vm.addSubtask(parentId, QStringLiteral("First"));
        const QString sub2 = vm.addSubtask(parentId, QStringLiteral("Second"));
        const QString sub3 = vm.addSubtask(parentId, QStringLiteral("Third"));

        // Manually override order to test sorting
        Task *t1 = store->taskById(sub1);
        Task *t2 = store->taskById(sub2);
        Task *t3 = store->taskById(sub3);
        QVERIFY(t1 && t2 && t3);

        t1->setOrder(3);
        t2->setOrder(1);
        t3->setOrder(2);

        const QVariantList result = vm.subtasksOf(parentId);
        QCOMPARE(result.size(), 3);

        // Expect sorted: sub2 (order 1), sub3 (order 2), sub1 (order 3)
        QCOMPARE(result.at(0).toMap()[QStringLiteral("id")].toString(), sub2);
        QCOMPARE(result.at(1).toMap()[QStringLiteral("id")].toString(), sub3);
        QCOMPARE(result.at(2).toMap()[QStringLiteral("id")].toString(), sub1);

        delete store;
    }

    void subtasksOf_map_contains_expected_fields()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        const QString subId    = vm.addSubtask(parentId, QStringLiteral("Child Task"));
        QVERIFY(!subId.isEmpty());

        const QVariantList result = vm.subtasksOf(parentId);
        QCOMPARE(result.size(), 1);

        const QVariantMap map = result.first().toMap();
        QCOMPARE(map[QStringLiteral("id")].toString(), subId);
        QCOMPARE(map[QStringLiteral("content")].toString(), QStringLiteral("Child Task"));
        QVERIFY(map.contains(QStringLiteral("isCompleted")));
        QVERIFY(map.contains(QStringLiteral("priority")));
        QVERIFY(map.contains(QStringLiteral("dueDate")));
        QVERIFY(map.contains(QStringLiteral("parentId")));
        QCOMPARE(map[QStringLiteral("parentId")].toString(), parentId);

        delete store;
    }

    // -----------------------------------------------------------------------
    // convertToSubtask
    // -----------------------------------------------------------------------

    void convertToSubtask_sets_parentId()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        const QString childId  = vm.addTask(QStringLiteral("Child"));

        // Verify childId has no parent initially
        Task *child = store->taskById(childId);
        QVERIFY(child != nullptr);
        QVERIFY(child->parentId().isEmpty());

        const bool ok = vm.convertToSubtask(childId, parentId);
        QVERIFY(ok);
        QCOMPARE(child->parentId(), parentId);
        QVERIFY(vm.hasSubtasks(parentId));

        delete store;
    }

    void convertToSubtask_rejects_self_reference()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString id = vm.addTask(QStringLiteral("Task"));
        const bool ok = vm.convertToSubtask(id, id);
        QVERIFY(!ok);

        Task *task = store->taskById(id);
        QVERIFY(task != nullptr);
        QVERIFY(task->parentId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // promoteToTopLevel
    // -----------------------------------------------------------------------

    void promoteToTopLevel_clears_parentId()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        const QString subId    = vm.addSubtask(parentId, QStringLiteral("Sub"));
        QVERIFY(!subId.isEmpty());

        Task *sub = store->taskById(subId);
        QVERIFY(sub != nullptr);
        QCOMPARE(sub->parentId(), parentId);

        const bool ok = vm.promoteToTopLevel(subId);
        QVERIFY(ok);
        QVERIFY(sub->parentId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Top-level list must not show subtasks in inbox
    // -----------------------------------------------------------------------

    void parent_filter_excludes_subtasks_from_inbox()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const QString parentId = vm.addTask(QStringLiteral("Top-level parent"));
        const QString subId    = vm.addSubtask(parentId, QStringLiteral("Subtask"));
        QVERIFY(!subId.isEmpty());

        // The subtask must NOT appear in the main list
        bool foundParent  = false;
        bool foundSubtask = false;
        for (int i = 0; i < vm.rowCount(); ++i) {
            const QString id = vm.data(vm.index(i), TaskViewModel::IdRole).toString();
            if (id == parentId)  foundParent  = true;
            if (id == subId)     foundSubtask = true;
        }

        QVERIFY(foundParent);
        QVERIFY(!foundSubtask);

        delete store;
    }

    // -----------------------------------------------------------------------
    // addSubtask
    // -----------------------------------------------------------------------

    void addSubtask_returns_empty_for_unknown_parent()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString subId = vm.addSubtask(QStringLiteral("no-such-id"),
                                             QStringLiteral("Content"));
        QVERIFY(subId.isEmpty());

        delete store;
    }

    void addSubtask_returns_empty_for_blank_content()
    {
        DataStore *store = makeStore();
        TaskViewModel vm(store);

        const QString parentId = vm.addTask(QStringLiteral("Parent"));
        const QString subId    = vm.addSubtask(parentId, QStringLiteral("   "));
        QVERIFY(subId.isEmpty());

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestSubtasks)
#include "test_subtasks.moc"
