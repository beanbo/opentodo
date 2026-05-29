#include <QtTest>
#include <QSignalSpy>
#include <QStringList>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ============================================================================
// TaskViewModel selection state-machine tests.
//
// Documents the *contract* of the multi-select state held by TaskViewModel
// (the `selectedTask` Q_PROPERTY pointer is a SEPARATE concern — see
// test_taskviewmodel_actions.cpp / openTaskById). The methods covered here:
//
//     selectTask(id)       -- add to set; idempotent on duplicates
//     deselectTask(id)     -- remove from set; no-op on unknown id
//     toggleSelection(id)  -- flip in/out (always emits selectionChanged)
//     clearSelection()     -- empty the set; no-op when already empty
//     isSelected(id)       -- membership predicate
//     selectedIds          -- Q_PROPERTY returning insertion-order QStringList
//     selectionCount       -- size of the set
//     selectionChanged     -- single signal for ALL of the above
//
// Notes on naming: the task brief mentions addToSelection/removeFromSelection,
// but the actual public API on TaskViewModel uses selectTask/deselectTask
// (see src/viewmodels/taskviewmodel.h around line 130). These tests use the
// real method names. There is no `selectAll()` / `addToSelection()` /
// `removeFromSelection()` / batch-select helper on the class — the "select all
// visible tasks" workflow is reproduced here by iterating the visible set
// from QML.
//
// The existing test_selection.cpp already covers the very basic happy paths
// (select one, deselect one, toggle once, clear after select, selectRange).
// This file fills the gaps: bootstrap state, idempotency assertions, signal
// precision, multi-add behaviour, and several edge cases including
// filterMode-change persistence and stale-id behaviour after soft-delete.
// ============================================================================

namespace {

// --------------------------------------------------------------------------
// Build a fresh DataStore with an inbox project. Caller owns the pointer.
// --------------------------------------------------------------------------
DataStore *makeStoreWithInbox()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// --------------------------------------------------------------------------
// Add `n` tasks to the inbox; returns their ids in insertion order.
// --------------------------------------------------------------------------
QStringList seedInboxTasks(DataStore *store, int n)
{
    const QString inboxId = store->inboxProject()->id();
    QStringList ids;
    ids.reserve(n);
    for (int i = 0; i < n; ++i) {
        Task *t = store->addTask(QStringLiteral("Task %1").arg(i), inboxId);
        ids.append(t->id());
    }
    return ids;
}

// --------------------------------------------------------------------------
// Mirror QML's "select-every-visible-task" workflow: iterate the model rows
// and call selectTask on each id role. Used to exercise what a hypothetical
// selectAll() would do without depending on a method that does not exist.
// --------------------------------------------------------------------------
void selectAllVisible(TaskViewModel *vm)
{
    const int n = vm->rowCount();
    for (int i = 0; i < n; ++i) {
        const QString id = vm->data(vm->index(i, 0),
                                    TaskViewModel::IdRole).toString();
        if (!id.isEmpty())
            vm->selectTask(id);
    }
}

} // namespace

class TestTaskViewModelSelection : public QObject
{
    Q_OBJECT

private slots:

    // ----------------------------------------------------------------------
    // Bootstrap: a freshly-constructed VM has zero selection.
    // ----------------------------------------------------------------------
    void bootstrap_state_is_empty()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        QCOMPARE(vm.selectionCount(), 0);
        QVERIFY(vm.selectedIds().isEmpty());
        QVERIFY(!vm.isSelected(QStringLiteral("anything")));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Bootstrap: even with tasks present (but never touched), nothing is
    // pre-selected and the property is empty.
    // ----------------------------------------------------------------------
    void bootstrap_with_tasks_still_empty()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = seedInboxTasks(store, 3);

        QCOMPARE(vm.selectionCount(), 0);
        QVERIFY(vm.selectedIds().isEmpty());
        for (const QString &id : ids)
            QVERIFY(!vm.isSelected(id));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Adding a single id moves the count from 0 to 1 and exposes that id.
    // ----------------------------------------------------------------------
    void selectTask_single_brings_count_to_one()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[0]);

        QCOMPARE(vm.selectionCount(), 1);
        QCOMPARE(vm.selectedIds(), QStringList{ids[0]});
        QVERIFY(vm.isSelected(ids[0]));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Idempotency: selecting the same id twice keeps the count at 1.
    // The property must NOT contain duplicates.
    // ----------------------------------------------------------------------
    void selectTask_duplicate_is_idempotent()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[0]);
        vm.selectTask(ids[0]);

        QCOMPARE(vm.selectionCount(), 1);
        QCOMPARE(vm.selectedIds().count(ids[0]), 1);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Adding distinct ids accumulates them in INSERTION order
    // (m_selectedIds is a QList — ordering is part of the contract; see
    // taskviewmodel.h header comment "B-3.61/62").
    // ----------------------------------------------------------------------
    void selectTask_multiple_ids_accumulate_in_order()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 4);

        vm.selectTask(ids[2]);
        vm.selectTask(ids[0]);
        vm.selectTask(ids[3]);

        QCOMPARE(vm.selectionCount(), 3);
        const QStringList sel = vm.selectedIds();
        QCOMPARE(sel.size(), 3);
        QCOMPARE(sel[0], ids[2]);
        QCOMPARE(sel[1], ids[0]);
        QCOMPARE(sel[2], ids[3]);

        delete store;
    }

    // ----------------------------------------------------------------------
    // deselectTask removes the targeted id only, leaves others intact.
    // ----------------------------------------------------------------------
    void deselectTask_removes_only_target()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(vm.selectionCount(), 2);

        vm.deselectTask(ids[0]);

        QCOMPARE(vm.selectionCount(), 1);
        QVERIFY(!vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));
        QCOMPARE(vm.selectedIds(), QStringList{ids[1]});

        delete store;
    }

    // ----------------------------------------------------------------------
    // deselectTask on an unknown id is a no-op: count and contents unchanged
    // and (verified separately below) no signal is emitted.
    // ----------------------------------------------------------------------
    void deselectTask_unknown_id_is_noop()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        const QStringList before = vm.selectedIds();
        QCOMPARE(vm.selectionCount(), 2);

        vm.deselectTask(QStringLiteral("not-a-real-id"));
        vm.deselectTask(QString());

        QCOMPARE(vm.selectionCount(), 2);
        QCOMPARE(vm.selectedIds(), before);

        delete store;
    }

    // ----------------------------------------------------------------------
    // toggleSelection: id absent -> added; id present -> removed.
    // Two toggles of the same id return to the initial state.
    // ----------------------------------------------------------------------
    void toggleSelection_flips_in_and_out()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        QVERIFY(!vm.isSelected(ids[0]));

        vm.toggleSelection(ids[0]);
        QVERIFY(vm.isSelected(ids[0]));
        QCOMPARE(vm.selectionCount(), 1);

        vm.toggleSelection(ids[0]);
        QVERIFY(!vm.isSelected(ids[0]));
        QCOMPARE(vm.selectionCount(), 0);

        // Toggle a second id on top of an existing selection.
        vm.selectTask(ids[0]);
        vm.toggleSelection(ids[1]);
        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));
        QCOMPARE(vm.selectionCount(), 2);

        delete store;
    }

    // ----------------------------------------------------------------------
    // clearSelection drops EVERY id and isSelected returns false for all.
    // ----------------------------------------------------------------------
    void clearSelection_empties_set()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 4);

        for (const QString &id : ids)
            vm.selectTask(id);
        QCOMPARE(vm.selectionCount(), 4);

        vm.clearSelection();

        QCOMPARE(vm.selectionCount(), 0);
        QVERIFY(vm.selectedIds().isEmpty());
        for (const QString &id : ids)
            QVERIFY(!vm.isSelected(id));

        delete store;
    }

    // ----------------------------------------------------------------------
    // "Select all visible" (caller-driven loop, since the class has no
    // selectAll() method) selects every task currently in m_tasks.
    // ----------------------------------------------------------------------
    void selectAllVisible_picks_up_every_task()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = seedInboxTasks(store, 5);

        // Q3.24: refresh() is debounced and setFilterMode(Inbox) is a no-op
        // (Inbox is default), so wait for the rows before selecting all visible.
        QTRY_COMPARE(vm.rowCount(), ids.size());
        selectAllVisible(&vm);

        QCOMPARE(vm.selectionCount(), ids.size());
        for (const QString &id : ids)
            QVERIFY(vm.isSelected(id));

        delete store;
    }

    // ----------------------------------------------------------------------
    // selectAll on an empty visible list yields an empty selection
    // (degenerate case — guards against off-by-one / accidental sentinel).
    // ----------------------------------------------------------------------
    void selectAllVisible_empty_list_stays_empty()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        // No tasks added — vm.rowCount() should be 0.
        QCOMPARE(vm.rowCount(), 0);

        selectAllVisible(&vm);

        QCOMPARE(vm.selectionCount(), 0);
        QVERIFY(vm.selectedIds().isEmpty());

        delete store;
    }

    // ----------------------------------------------------------------------
    // Signal precision: each meaningful state change emits selectionChanged
    // exactly once. Three selects, two deselects -> 5 emissions.
    // ----------------------------------------------------------------------
    void selectionChanged_emitted_once_per_change()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 3);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);

        vm.selectTask(ids[0]);
        QCOMPARE(spy.count(), 1);
        vm.selectTask(ids[1]);
        QCOMPARE(spy.count(), 2);
        vm.selectTask(ids[2]);
        QCOMPARE(spy.count(), 3);
        vm.deselectTask(ids[0]);
        QCOMPARE(spy.count(), 4);
        vm.deselectTask(ids[1]);
        QCOMPARE(spy.count(), 5);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Signal precision: a duplicate selectTask MUST NOT emit selectionChanged
    // (the selectTask body in taskviewmodel.cpp guards on
    // !m_selectedIds.contains(id)).
    // ----------------------------------------------------------------------
    void duplicate_selectTask_does_not_emit()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        vm.selectTask(ids[0]);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        vm.selectTask(ids[0]);
        vm.selectTask(ids[0]);
        vm.selectTask(ids[0]);

        QCOMPARE(spy.count(), 0);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Signal precision: deselectTask on an absent id MUST NOT emit (the
    // emit is guarded on the bool return of QList::removeOne).
    // ----------------------------------------------------------------------
    void deselectTask_unknown_does_not_emit()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        vm.selectTask(ids[0]);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        vm.deselectTask(ids[1]); // never selected
        vm.deselectTask(QStringLiteral("ghost"));
        vm.deselectTask(QString());

        QCOMPARE(spy.count(), 0);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Signal precision: clearSelection on an already-empty set MUST NOT
    // emit. clearSelection on a non-empty set emits exactly once.
    // ----------------------------------------------------------------------
    void clearSelection_on_empty_does_not_emit()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);

        // Already-empty branch: should be silent.
        vm.clearSelection();
        QCOMPARE(spy.count(), 0);

        // Now seed selection, then clear once -> exactly one emit.
        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(spy.count(), 2); // two selectTask emissions

        vm.clearSelection();
        QCOMPARE(spy.count(), 3);

        // Second clear is a no-op.
        vm.clearSelection();
        QCOMPARE(spy.count(), 3);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Bulk add (5 selectTask calls in a row) with the CURRENT API emits
    // selectionChanged 5 times — there is no batched bulkAdd helper. This
    // test pins down that observable behaviour so a future refactor that
    // collapses it into a single emission will trip the test and force a
    // conscious update.
    // ----------------------------------------------------------------------
    void bulk_add_emits_one_signal_per_call()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 5);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        for (const QString &id : ids)
            vm.selectTask(id);

        QCOMPARE(spy.count(), 5);
        QCOMPARE(vm.selectionCount(), 5);

        delete store;
    }

    // ----------------------------------------------------------------------
    // toggleSelection always emits, even when it's a "remove" toggle. This
    // matches the implementation (the emit is unconditional, after the
    // contains/removeOne branch). Verifies 4 toggles -> 4 emissions.
    // ----------------------------------------------------------------------
    void toggleSelection_emits_on_every_call()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        vm.toggleSelection(ids[0]); // add  -> emit
        vm.toggleSelection(ids[0]); // remove -> emit
        vm.toggleSelection(ids[1]); // add  -> emit
        vm.toggleSelection(ids[1]); // remove -> emit

        QCOMPARE(spy.count(), 4);
        QCOMPARE(vm.selectionCount(), 0);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Edge: isSelected on a never-seen id is false. isSelected on the empty
    // string is also false (covers both contain-paths through QList).
    // ----------------------------------------------------------------------
    void isSelected_unknown_id_is_false()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        QVERIFY(!vm.isSelected(QStringLiteral("not-a-real-id")));
        QVERIFY(!vm.isSelected(QString()));

        // After selecting only ids[0], a fresh garbage id is still false.
        vm.selectTask(ids[0]);
        QVERIFY(!vm.isSelected(QStringLiteral("still-not-real")));
        QVERIFY(vm.isSelected(ids[0]));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Edge: removeFromSelection (deselectTask) on an empty selection set
    // does not crash and produces no emission.
    // ----------------------------------------------------------------------
    void deselectTask_on_empty_selection_no_crash()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 2);

        QCOMPARE(vm.selectionCount(), 0);

        QSignalSpy spy(&vm, &TaskViewModel::selectionChanged);
        vm.deselectTask(ids[0]);
        vm.deselectTask(QString());
        vm.deselectTask(QStringLiteral("missing"));

        QCOMPARE(vm.selectionCount(), 0);
        QCOMPARE(spy.count(), 0);

        delete store;
    }

    // ----------------------------------------------------------------------
    // Documented behaviour: changing filterMode does NOT clear selection.
    // setFilterMode in taskviewmodel.cpp only refreshes m_tasks; it never
    // touches m_selectedIds. So an id selected while in Inbox is still
    // selected after switching to Today (even if it's no longer in the
    // visible list).
    // ----------------------------------------------------------------------
    void selection_survives_filterMode_change()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(vm.selectionCount(), 2);

        // Switch view; tasks above have no due_date so they will not appear
        // in Today, but their ids must still be selected.
        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QCOMPARE(vm.selectionCount(), 2);
        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));

        // And switching back leaves the same selection.
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        QCOMPARE(vm.selectionCount(), 2);
        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Documented behaviour: an id remains in the selection set after the
    // task is soft-deleted via DataStore::removeTask. The selection set is
    // a flat list of ids — it does NOT subscribe to data changes — so a
    // stale id will linger until the user calls clearSelection or
    // deselectTask explicitly. Bulk operations defensively re-resolve via
    // taskById and skip nullptr/deleted tasks (see bulkComplete et al.),
    // so the staleness is benign at the action layer.
    // ----------------------------------------------------------------------
    void selection_lingers_after_soft_delete()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[0]);
        vm.selectTask(ids[1]);
        QCOMPARE(vm.selectionCount(), 2);

        // Soft-delete one of the selected tasks at the store level.
        QVERIFY(store->removeTask(ids[0]));

        // The id sticks around in the selection set — this is the
        // current contract; if it ever changes, this test will need
        // to flip its expectations.
        QCOMPARE(vm.selectionCount(), 2);
        QVERIFY(vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));

        // Explicitly removing the stale id works.
        vm.deselectTask(ids[0]);
        QCOMPARE(vm.selectionCount(), 1);
        QVERIFY(!vm.isSelected(ids[0]));
        QVERIFY(vm.isSelected(ids[1]));

        delete store;
    }

    // ----------------------------------------------------------------------
    // Round-trip property accessor: the same QStringList contents are
    // visible through both selectedIds() and a plain QML-style read of the
    // Q_PROPERTY. (Sanity check that selectedIds() and the property are
    // wired to the same underlying list.)
    // ----------------------------------------------------------------------
    void selectedIds_property_matches_method()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);
        const QStringList ids = seedInboxTasks(store, 3);

        vm.selectTask(ids[1]);
        vm.selectTask(ids[0]);

        const QStringList viaMethod = vm.selectedIds();
        const QStringList viaProp =
            vm.property("selectedIds").toStringList();
        const int viaCountProp = vm.property("selectionCount").toInt();

        QCOMPARE(viaMethod, viaProp);
        QCOMPARE(viaCountProp, viaMethod.size());
        QCOMPARE(viaCountProp, 2);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestTaskViewModelSelection)
#include "test_taskviewmodel_selection.moc"
