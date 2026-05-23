#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "viewmodels/taskviewmodel.h"
#include "services/undostack.h"

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
    // Iter-night-35: cross-project moveTask undo must restore descendant
    // section_ids that the forward BFS cleared.
    //
    // The forward path (Q3.08-C, line 569+) cascades the new projectId
    // across every descendant subtask AND clears each one's section_id to
    // empty (sections are project-scoped, so the source section's UUID is
    // meaningless in the destination project). Before iter-night-35, the
    // undo lambda captured ONLY the root task's (oldProject, oldSection),
    // so the descendants' original section_ids were silently lost on every
    // move-then-undo round-trip. The recursive moveTask call inside the
    // undo also re-ran the BFS, clearing them again — so even if memory
    // had preserved them between push and undo, the recursive call would
    // wipe them.
    //
    // Pin: parent T with subtask D, D originally in section S_D within
    // project P1. moveTask(T, P2) → undo → expect D back in P1 AND in S_D.
    // -----------------------------------------------------------------------
    void moveTask_undo_restoresDescendantSectionIds()
    {
        DataStore *store = makeStore();
        UndoStack undo;

        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        // Inbox has a section S_inbox; Work has its own section so the
        // forward move clears the descendant's section_id (because S_inbox
        // is meaningless in the Work project).
        Section *sInbox = store->addSection(QStringLiteral("S_inbox"), inboxId);
        const QString sInboxId = sInbox->id();

        Task *root  = store->addTask(QStringLiteral("Root"),  inboxId);
        Task *child = store->addTask(QStringLiteral("Child"), inboxId);
        const QString rootId  = root->id();
        const QString childId = child->id();
        root->setSectionId(sInboxId);
        child->setSectionId(sInboxId);
        child->setParentId(rootId);

        // Sanity preconditions.
        QCOMPARE(root->projectId(),  inboxId);
        QCOMPARE(root->sectionId(),  sInboxId);
        QCOMPARE(child->projectId(), inboxId);
        QCOMPARE(child->sectionId(), sInboxId);

        TaskViewModel vm(store);
        vm.setUndoStack(&undo);

        // Forward: move root to Work with empty section. Forward BFS clears
        // child's section_id and updates its project_id.
        QVERIFY(vm.moveTask(rootId, workId, QString()));
        QCOMPARE(root->projectId(),  workId);
        QCOMPARE(root->sectionId(),  QString());
        QCOMPARE(child->projectId(), workId);
        QCOMPARE(child->sectionId(), QString());
        QCOMPARE(undo.count(), 1);

        // Undo: root should return to Inbox/S_inbox AND child should also
        // return to Inbox/S_inbox. Without the iter-night-35 fix the child
        // would land at Inbox with sectionId="" — the section_id snapshot
        // was never taken.
        QVERIFY(undo.undo());

        QCOMPARE(root->projectId(),  inboxId);
        QCOMPARE(root->sectionId(),  sInboxId);
        QCOMPARE(child->projectId(), inboxId);
        QVERIFY2(child->sectionId() == sInboxId,
                 qPrintable(QString(
                     "iter-night-35 regression: subtask landed at "
                     "section_id='%1' after undo of cross-project move; "
                     "expected the original '%2'.")
                     .arg(child->sectionId(), sInboxId)));

        delete store;
    }

    // Companion: deep subtree — grandchild's section_id must also survive
    // the undo. The undo lambda's snapshot is keyed by id, so all visited
    // descendants are restored regardless of depth.
    void moveTask_undo_restoresDeepDescendantSectionIds()
    {
        DataStore *store = makeStore();
        UndoStack undo;

        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        Section *secA = store->addSection(QStringLiteral("A"), inboxId);
        Section *secB = store->addSection(QStringLiteral("B"), inboxId);
        const QString secAId = secA->id();
        const QString secBId = secB->id();

        Task *root  = store->addTask(QStringLiteral("Root"),  inboxId);
        Task *child = store->addTask(QStringLiteral("Child"), inboxId);
        Task *grand = store->addTask(QStringLiteral("Grand"), inboxId);
        const QString rootId  = root->id();
        const QString childId = child->id();
        const QString grandId = grand->id();

        // Mix sections so the snapshot proves it's keyed per-id, not
        // a single "old section" value.
        root->setSectionId(secAId);
        child->setSectionId(secBId);  // different section from root
        child->setParentId(rootId);
        grand->setSectionId(secAId);
        grand->setParentId(childId);

        TaskViewModel vm(store);
        vm.setUndoStack(&undo);

        QVERIFY(vm.moveTask(rootId, workId, QString()));
        QCOMPARE(child->sectionId(), QString());
        QCOMPARE(grand->sectionId(), QString());

        QVERIFY(undo.undo());

        QCOMPARE(root->sectionId(),  secAId);
        QCOMPARE(child->sectionId(), secBId);
        QCOMPARE(grand->sectionId(), secAId);

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
    // Iter-night-32: convertToSubtask must cascade project_id and
    // section_id from the new parent — a subtask's container is
    // semantically implicit (matches addSubtask which already does this).
    //
    // The previous implementation only stamped parent_id, leaving stale
    // project_id/section_id on the dragged task. Symptom: dragging a task
    // onto a parent that lives in a different section made the dragged
    // row render under its OLD section header (ProjectView groups by
    // task->sectionId()). Cross-project drops desynced peers (parent_id
    // pointed at the new project, but section_id still pointed at the
    // old project's section UUID — irrecoverable without manual fixup).
    // -----------------------------------------------------------------------
    void convertToSubtask_cascadesSectionFromNewParent_sameProject()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        // Two sections in the same project. P lives in S2; child lives in S1.
        Section *s1 = store->addSection(QStringLiteral("Backlog"), inboxId);
        Section *s2 = store->addSection(QStringLiteral("In Progress"), inboxId);

        Task *parent = store->addTask(QStringLiteral("Parent"), inboxId);
        parent->setSectionId(s2->id());
        Task *child  = store->addTask(QStringLiteral("Child"),  inboxId);
        child->setSectionId(s1->id());

        TaskViewModel vm(store);
        bool ok = vm.convertToSubtask(child->id(), parent->id());
        QVERIFY(ok);

        // parent_id set — already covered by sets_parentId, re-asserted
        // here for completeness.
        QCOMPARE(child->parentId(), parent->id());
        // section_id cascaded from the new parent — the key pin.
        QCOMPARE(child->sectionId(), s2->id());
        // project_id unchanged (both tasks were in Inbox).
        QCOMPARE(child->projectId(), inboxId);

        delete store;
    }

    void convertToSubtask_cascadesProjectAndClearsDescendantsSection()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        // Second project + a section in it.
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();
        Section *workSec = store->addSection(QStringLiteral("Active"), workId);

        // New parent lives in Work / Active.
        Task *parent = store->addTask(QStringLiteral("Parent"), workId);
        parent->setSectionId(workSec->id());

        // Dragged subtree: child in Inbox with its own grandchild in Inbox.
        Section *inboxSec = store->addSection(QStringLiteral("Misc"), inboxId);
        Task *child = store->addTask(QStringLiteral("Child"), inboxId);
        child->setSectionId(inboxSec->id());
        Task *grandchild = store->addTask(QStringLiteral("Grandchild"), inboxId);
        grandchild->setSectionId(inboxSec->id());
        grandchild->setParentId(child->id());

        TaskViewModel vm(store);
        bool ok = vm.convertToSubtask(child->id(), parent->id());
        QVERIFY(ok);

        // The dragged task itself: parent_id set, project_id and section_id
        // both inherited from the new parent.
        QCOMPARE(child->parentId(),  parent->id());
        QCOMPARE(child->projectId(), workId);
        QCOMPARE(child->sectionId(), workSec->id());

        // Descendants follow the project move. Section is cleared on the
        // descendant — its old section UUID belongs to a different project
        // and would render the row in an unreachable section in the new
        // project's view. Matches moveTask's existing BFS contract.
        QCOMPARE(grandchild->projectId(), workId);
        QCOMPARE(grandchild->sectionId(), QString());
        // Parent link unchanged — grandchild is still a child of `child`.
        QCOMPARE(grandchild->parentId(), child->id());

        delete store;
    }

    void convertToSubtask_unknownNewParent_returnsFalse()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Task *t = store->addTask(QStringLiteral("Task"), inboxId);

        TaskViewModel vm(store);
        // Iter-night-32 added an explicit "new parent must exist" guard
        // alongside the cascade. Sanity-pin that ghost ids are rejected.
        bool ok = vm.convertToSubtask(t->id(),
                                      QStringLiteral("nonexistent-parent-id"));
        QVERIFY(!ok);
        // The dragged task is unchanged.
        QCOMPARE(t->parentId(), QString());

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
