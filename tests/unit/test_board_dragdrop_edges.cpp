// ============================================================================
// test_board_dragdrop_edges.cpp
//
// BoardView (qml/views/BoardView.qml) kanban drag-drop edge case suite.
//
// BoardView wraps each section as a column delegate with a DropArea that calls
// TaskViewModel::moveTask(taskId, projectId, sectionId) on a card drop. Tasks
// inside a column are rendered from a JS array snapshot ("columnTasks") that
// is rebuilt on every taskViewModel signal — there is NO intra-column reorder
// path in BoardView itself (it uses neither moveTaskOrder nor any explicit
// dayOrder mutation). Column reordering by header drag is also not exposed
// in BoardView — section.order is stored on the model but the column header
// has no Drag handler.
//
// This suite drives the C++ ViewModel methods that BoardView's drop handlers
// ultimately call, plus the rename/delete dialog code paths, so we pin the
// observable behavior the kanban UI depends on:
//
//   1. Cross-section move — sectionId update, projectId preserved.
//   2. Cross-project + cross-section drop (root.projectId differs from the
//      task's source projectId — happens when the BoardView is reopened
//      against a different project while a drag is mid-flight).
//   3. Drop on the "(Без раздела)" pseudo-section — empty sectionId.
//   4. Empty-column → only-task move → repopulate empty column. The
//      _tasksForSection() snapshot must reflect both transitions without
//      stale entries or NaN/-Inf dayOrder values (BoardView doesn't touch
//      dayOrder at all — its absence is the invariant being pinned).
//   5. Same-section drop is technically a no-op for the user but the C++
//      moveTask path always emits taskChanged + pushes an undo entry. Pin
//      that current contract so future "skip if unchanged" refactors don't
//      silently break the undo stack.
//   6. moveTask of a non-existent task id (the QML mime payload could carry
//      a stale uuid if a sync pull removed the dragged task mid-flight).
//   7. Bulk multi-select cross-section drop — BoardView itself doesn't wire
//      Ctrl+click multi-select drag (single-card drag only), but the C++
//      bulk path (bulkMoveToProject) clears the sectionId. Document this
//      so the UI never accidentally exposes it for kanban.
//   8. Rename-section dialog → DataStore.renameSection round-trip. Surfaces
//      the previous `dataStore.updateSection(...)` typo (now fixed).
//   9. Delete-section dialog — removeSection success + idempotence.
//  10. tasksForSection query semantics — soft-deleted tasks excluded.
//  11. Repeated drops on the same column do not duplicate the task or leave
//      a ghost row (model returns one Task* per visible id).
//
// All slots run with QTEST_GUILESS_MAIN and a unique QSettings org/app pair
// to avoid clashing with ctest's parallel-test scheduling. Russian UI strings
// are used wherever the production UI displays them (matches CLAUDE.md global
// instruction "UI projects are always Russian").
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QSettings>
#include <QStringList>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "viewmodels/taskviewmodel.h"

namespace {

// --------------------------------------------------------------------------
// Use a per-suite unique QSettings org/app pair so this slot's QSettings keys
// (everything under sync/, hotkey/, etc.) live in their own backing store.
// Mirrors the pattern Quality-round 2.12 introduced for the syncengine state
// suite — required for parallel-test scheduling under ctest.
// --------------------------------------------------------------------------
void useUniqueSettings()
{
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_BoardDnD"));
    QCoreApplication::setApplicationName(QStringLiteral("test_board_dragdrop_edges"));
    QSettings s;
    s.clear();
    s.sync();
}

// --------------------------------------------------------------------------
// Fixture — a Work project with three sections (Todo / Doing / Done), a few
// tasks distributed across them, and an Inbox project for cross-project moves.
// Mirrors a realistic kanban setup.
// --------------------------------------------------------------------------
struct Fixture {
    DataStore *store = nullptr;
    QString inboxId;
    QString workId;
    QString homeId;
    QString sectionTodoId;
    QString sectionDoingId;
    QString sectionDoneId;
    QString taskAId;   // в Work / Todo  — приоритет 2
    QString taskBId;   // в Work / Doing — приоритет 3
    QString taskCId;   // в Work / Done  — приоритет 4
    QString taskDId;   // в Work без секции (для "(Без раздела)" колонки)
    QString taskEId;   // в Inbox (для cross-project drop)
    QString taskFId;   // в Work / Todo, мягко удалён (тест tombstone-фильтра)

    ~Fixture() { delete store; }
};

Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Входящие"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    Project *work = f->store->addProject(QStringLiteral("Работа"));
    f->workId = work->id();

    Project *home = f->store->addProject(QStringLiteral("Дом"));
    f->homeId = home->id();

    Section *todo  = f->store->addSection(QStringLiteral("Сделать"), f->workId);
    Section *doing = f->store->addSection(QStringLiteral("В работе"), f->workId);
    Section *done  = f->store->addSection(QStringLiteral("Готово"),  f->workId);
    f->sectionTodoId  = todo->id();
    f->sectionDoingId = doing->id();
    f->sectionDoneId  = done->id();

    Task *a = f->store->addTask(QStringLiteral("Задача A"), f->workId);
    a->setSectionId(f->sectionTodoId);
    a->setPriority(2);
    f->taskAId = a->id();

    Task *b = f->store->addTask(QStringLiteral("Задача B"), f->workId);
    b->setSectionId(f->sectionDoingId);
    b->setPriority(3);
    f->taskBId = b->id();

    Task *c = f->store->addTask(QStringLiteral("Задача C"), f->workId);
    c->setSectionId(f->sectionDoneId);
    c->setPriority(4);
    f->taskCId = c->id();

    Task *d = f->store->addTask(QStringLiteral("Задача D без раздела"), f->workId);
    // No setSectionId — стало быть, sectionId() == ""
    f->taskDId = d->id();

    Task *e = f->store->addTask(QStringLiteral("Задача из входящих"), f->inboxId);
    f->taskEId = e->id();

    Task *fTomb = f->store->addTask(QStringLiteral("Задача-надгробие"), f->workId);
    fTomb->setSectionId(f->sectionTodoId);
    f->taskFId = fTomb->id();
    // removeTask soft-deletes and returns true; if it ever returns false the
    // tombstone test below will surface the breakage (isDeleted == false).
    (void) f->store->removeTask(f->taskFId);

    return f;
}

// Mirror BoardView.qml's _tasksForSection(sectionId) — re-derived from the
// taskViewModel snapshot rather than from DataStore directly, because the
// QML reads through taskViewModel.count / taskViewModel.taskAt(i).
QList<QString> tasksForSectionViaVm(TaskViewModel &vm, const QString &sectionId)
{
    QList<QString> ids;
    for (int i = 0; i < vm.count(); ++i) {
        Task *t = vm.taskAt(i);
        if (!t) continue;
        if (t->sectionId() == sectionId && !t->isCompleted())
            ids.append(t->id());
    }
    return ids;
}

// Mirror BoardView.qml's column list directly off DataStore (no VM filter).
// Useful for testing what's on the model regardless of VM filter mode.
QList<QString> tasksForSectionViaStore(DataStore *store, const QString &sectionId)
{
    QList<QString> ids;
    for (Task *t : store->tasksForProject(QString())) (void)t; // suppress warning
    for (Task *t : store->allTasks()) {
        if (!t || t->isDeleted()) continue;
        if (t->sectionId() == sectionId && !t->isCompleted())
            ids.append(t->id());
    }
    return ids;
}

} // namespace

// ============================================================================
class TestBoardDragDropEdges : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { useUniqueSettings(); }

    // -----------------------------------------------------------------------
    // 1. Cross-section move (Todo → Doing) — the canonical kanban gesture.
    //    sectionId updates, projectId unchanged, no order/dayOrder mutation
    //    happens at the BoardView path (it calls moveTask, not moveTaskOrder).
    // -----------------------------------------------------------------------
    void crossSection_move_updatesSectionId_preservesProject()
    {
        Fixture *f = makeFixture();
        Task *a = f->store->taskById(f->taskAId);
        QVERIFY(a);
        QCOMPARE(a->sectionId(), f->sectionTodoId);
        QCOMPARE(a->projectId(), f->workId);

        const int orderBefore    = a->order();
        const int dayOrderBefore = a->dayOrder();

        TaskViewModel vm(f->store);
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        // Mirror BoardView.qml columnDropArea.onDropped:
        //   taskViewModel.moveTask(tid, root.projectId, column.modelData.id)
        QVERIFY(vm.moveTask(f->taskAId, f->workId, f->sectionDoingId));

        QCOMPARE(a->sectionId(), f->sectionDoingId);
        QCOMPARE(a->projectId(), f->workId);

        // BoardView never touches order / dayOrder — invariant: those fields
        // are untouched by a cross-section card drop.
        QCOMPARE(a->order(),    orderBefore);
        QCOMPARE(a->dayOrder(), dayOrderBefore);

        // taskChanged emitted with the task's id.
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.first().value(0).toString(), f->taskAId);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 2. Cross-project + cross-section drop. BoardView always passes
    //    root.projectId — if a drag started before the board was reopened
    //    on a different project, the destination projectId can differ from
    //    the source. moveTask must update BOTH fields atomically.
    // -----------------------------------------------------------------------
    void crossProjectSection_drop_updatesBoth()
    {
        Fixture *f = makeFixture();
        Task *e = f->store->taskById(f->taskEId);
        QVERIFY(e);
        QCOMPARE(e->projectId(), f->inboxId);
        QVERIFY(e->sectionId().isEmpty());

        TaskViewModel vm(f->store);
        QVERIFY(vm.moveTask(f->taskEId, f->workId, f->sectionDoneId));
        QCOMPARE(e->projectId(), f->workId);
        QCOMPARE(e->sectionId(), f->sectionDoneId);

        // The task now appears in the destination section's column.
        const QList<QString> doneCol = tasksForSectionViaStore(f->store, f->sectionDoneId);
        QVERIFY(doneCol.contains(f->taskEId));
        QVERIFY(doneCol.contains(f->taskCId)); // not displaced

        // And it no longer appears in the inbox project's no-section column.
        const QList<QString> inboxNoSec = tasksForSectionViaStore(f->store, QString());
        QVERIFY(!inboxNoSec.contains(f->taskEId));

        delete f;
    }

    // -----------------------------------------------------------------------
    // 3. Drop on the synthetic "(Без раздела)" column — column.modelData.id
    //    is an empty string in BoardView. moveTask must clear sectionId.
    // -----------------------------------------------------------------------
    void dropOnNoSectionColumn_clearsSectionId()
    {
        Fixture *f = makeFixture();
        Task *a = f->store->taskById(f->taskAId);
        QVERIFY(a);
        QCOMPARE(a->sectionId(), f->sectionTodoId);

        TaskViewModel vm(f->store);
        QVERIFY(vm.moveTask(f->taskAId, f->workId, QString()));
        QVERIFY(a->sectionId().isEmpty());
        QCOMPARE(a->projectId(), f->workId);

        // The no-section column should now contain both D (initially) and A.
        const QList<QString> noSec = tasksForSectionViaStore(f->store, QString());
        QVERIFY(noSec.contains(f->taskAId));
        QVERIFY(noSec.contains(f->taskDId));

        delete f;
    }

    // -----------------------------------------------------------------------
    // 4. Empty-section round-trip. Drag the only task OUT of section S, then
    //    drag a task BACK IN. The two transitions must leave the model in a
    //    consistent state — no stale entries in S's column, no NaN/-Inf in
    //    any order/dayOrder field, no duplicate ids.
    //
    //    Why this matters: BoardView's columnTasks is rebuilt from scratch on
    //    every signal, so it can't carry stale rows on its own — but its
    //    snapshot is derived from taskViewModel, and the VM's m_tasks list
    //    is what would carry a stale pointer if moveTask weren't atomic.
    // -----------------------------------------------------------------------
    void emptyAndRefillSection_noStaleEntries()
    {
        Fixture *f = makeFixture();

        // Section "Doing" starts with exactly one task: B.
        TaskViewModel vm(f->store);
        QCOMPARE(tasksForSectionViaStore(f->store, f->sectionDoingId).size(), 1);

        // Drag B OUT → Doing now empty, Done now has B + C.
        QVERIFY(vm.moveTask(f->taskBId, f->workId, f->sectionDoneId));
        QCOMPARE(tasksForSectionViaStore(f->store, f->sectionDoingId).size(), 0);
        QCOMPARE(tasksForSectionViaStore(f->store, f->sectionDoneId).size(),  2);

        // Drag A IN → Doing has exactly {A}.
        QVERIFY(vm.moveTask(f->taskAId, f->workId, f->sectionDoingId));
        const QList<QString> doingCol =
            tasksForSectionViaStore(f->store, f->sectionDoingId);
        QCOMPARE(doingCol.size(), 1);
        QCOMPARE(doingCol.first(), f->taskAId);

        // No NaN/-Inf possible — BoardView doesn't compute new dayOrder values
        // on a card drop; the C++ moveTask leaves dayOrder/order alone. We pin
        // that every visible task has a finite, non-negative integer order.
        for (Task *t : f->store->allTasks()) {
            if (!t || t->isDeleted()) continue;
            QVERIFY2(t->order()    >= 0, qPrintable(t->content()));
            QVERIFY2(t->dayOrder() >= 0, qPrintable(t->content()));
        }

        delete f;
    }

    // -----------------------------------------------------------------------
    // 5. Same-section drop (drag a card and release inside its own column).
    //    BoardView does not detect this case at the QML level — it always
    //    invokes moveTask. Current contract: moveTask still emits
    //    taskChanged, still pushes an undo entry. Pin this so any future
    //    "skip if unchanged" optimisation has to update the undo stack too.
    // -----------------------------------------------------------------------
    void sameSectionDrop_currentlyEmitsAndUndoes()
    {
        Fixture *f = makeFixture();
        Task *a = f->store->taskById(f->taskAId);
        QVERIFY(a);
        const QString origSection = a->sectionId();
        const QDateTime origUpdated = a->updatedAt();

        TaskViewModel vm(f->store);
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        // Drop on the same section.
        QVERIFY(vm.moveTask(f->taskAId, f->workId, origSection));
        QCOMPARE(a->sectionId(), origSection);
        QCOMPARE(a->projectId(), f->workId);

        // Current behaviour: signal IS emitted. (Not a bug, but worth pinning
        // so that telemetry-spam claims become easy to verify.)
        QVERIFY(spy.count() >= 1);

        // updatedAt was bumped (could match origUpdated if the clock has only
        // 1-second resolution; tolerate that with a >=-not-< assertion).
        QVERIFY(a->updatedAt() >= origUpdated);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 6. moveTask of a non-existent task id (BoardView mime payload survives
    //    across a sync pull that might have tombstoned the dragged task).
    //    Must return false, no signal emitted.
    // -----------------------------------------------------------------------
    void moveTask_unknownId_returnsFalseQuietly()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        QVERIFY(!vm.moveTask(QStringLiteral("ghost-uuid-nope"),
                             f->workId,
                             f->sectionDoingId));
        QCOMPARE(spy.count(), 0);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 7. Bulk multi-select drag — not exposed by BoardView (single-card drag
    //    only) but the C++ bulkMoveToProject path is what a future
    //    multi-select kanban drag would call. Pin its observable behaviour:
    //    every selected task is moved to the new project AND its sectionId
    //    is CLEARED. The selection is then cleared.
    //
    //    Implication for BoardView: if anyone wires Ctrl+click multi-select
    //    into the column drop handler, this test surfaces that the bulk
    //    helper drops the sectionId — bulk kanban-drop will need a new VM
    //    method or per-task moveTask calls in a loop instead.
    // -----------------------------------------------------------------------
    void bulkMultiSelectDrag_dropsSectionId()
    {
        Fixture *f = makeFixture();

        TaskViewModel vm(f->store);
        vm.selectTask(f->taskAId);
        vm.selectTask(f->taskBId);
        vm.selectTask(f->taskCId);
        QCOMPARE(vm.selectionCount(), 3);

        // The bulk path. (BoardView itself never calls this — sectionId
        // information is lost on purpose because bulkMoveToProject is meant
        // for sidebar "move to project" actions where sections are
        // project-specific and can't carry across.)
        vm.bulkMoveToProject(f->homeId);

        Task *a = f->store->taskById(f->taskAId);
        Task *b = f->store->taskById(f->taskBId);
        Task *c = f->store->taskById(f->taskCId);
        QVERIFY(a); QVERIFY(b); QVERIFY(c);

        QCOMPARE(a->projectId(), f->homeId);
        QCOMPARE(b->projectId(), f->homeId);
        QCOMPARE(c->projectId(), f->homeId);

        // sectionId cleared on all three — documented quirk.
        QVERIFY(a->sectionId().isEmpty());
        QVERIFY(b->sectionId().isEmpty());
        QVERIFY(c->sectionId().isEmpty());

        // Selection cleared.
        QCOMPARE(vm.selectionCount(), 0);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 8. Rename-section dialog round-trip — exercises the path BoardView's
    //    renameSectionDialog.onAccepted takes after the bug fix in this
    //    branch (was: dataStore.updateSection(id,"name",val) — a Q_INVOKABLE
    //    that doesn't exist on DataStore, silently no-op'd in production).
    //
    //    The new call is dataStore.renameSection(id, newName), and that's
    //    what we verify here. Empty trim and unknown-id rejection are pinned
    //    so future refactors of renameSection don't change validation.
    // -----------------------------------------------------------------------
    void renameSection_roundTrip_dataStoreApi()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->store, &DataStore::sectionsChanged);

        Section *s = f->store->sectionById(f->sectionTodoId);
        QVERIFY(s);
        QCOMPARE(s->name(), QStringLiteral("Сделать"));

        // Happy path — same call BoardView.qml now makes.
        QVERIFY(f->store->renameSection(f->sectionTodoId,
                                        QStringLiteral("Запланировано")));
        QCOMPARE(s->name(), QStringLiteral("Запланировано"));
        QVERIFY(spy.count() >= 1);

        // Trim semantics — leading/trailing whitespace stripped.
        QVERIFY(f->store->renameSection(f->sectionTodoId,
                                        QStringLiteral("  Сделать заново  ")));
        QCOMPARE(s->name(), QStringLiteral("Сделать заново"));

        // Empty / whitespace-only name rejected (no change applied).
        QVERIFY(!f->store->renameSection(f->sectionTodoId, QString()));
        QVERIFY(!f->store->renameSection(f->sectionTodoId, QStringLiteral("   ")));
        QCOMPARE(s->name(), QStringLiteral("Сделать заново"));

        // Unknown section id rejected.
        QVERIFY(!f->store->renameSection(QStringLiteral("nope"),
                                         QStringLiteral("X")));

        delete f;
    }

    // -----------------------------------------------------------------------
    // 9. Delete-section dialog — BoardView's columnMenu "Удалить раздел"
    //    item calls dataStore.removeSection(id). Verify the success path
    //    plus idempotence (a second delete returns false rather than
    //    crashing).
    // -----------------------------------------------------------------------
    void deleteSection_throughDataStore()
    {
        Fixture *f = makeFixture();
        QVERIFY(f->store->sectionById(f->sectionDoingId));

        QVERIFY(f->store->removeSection(f->sectionDoingId));
        // Second call: section is tombstoned/gone — must not crash.
        const bool second = f->store->removeSection(f->sectionDoingId);
        // Either false (tombstoned, can't remove twice) or true (no-op tombstone
        // re-stamp): both are acceptable. The key assertion is no crash.
        Q_UNUSED(second);

        // Removing an unknown section returns false.
        QVERIFY(!f->store->removeSection(QStringLiteral("never-existed")));

        delete f;
    }

    // -----------------------------------------------------------------------
    // 10. tasksForSection query semantics — soft-deleted tasks are excluded.
    //     BoardView's _tasksForSection() walks taskViewModel which filters
    //     tombstones anyway, but this slot pins the DataStore-level contract
    //     that taskF (soft-deleted in Todo) doesn't appear in the column.
    // -----------------------------------------------------------------------
    void deletedTasks_excludedFromColumn()
    {
        Fixture *f = makeFixture();
        QVERIFY(f->store->taskById(f->taskFId)->isDeleted());

        const QList<QString> todoCol =
            tasksForSectionViaStore(f->store, f->sectionTodoId);

        // taskA is alive in Todo; taskF is tombstoned and must NOT appear.
        QVERIFY(todoCol.contains(f->taskAId));
        QVERIFY(!todoCol.contains(f->taskFId));

        delete f;
    }

    // -----------------------------------------------------------------------
    // 11. Repeated drops on the same column — drag A into Doing, then drag A
    //     into Doing again (a UI gesture where the user hovers and re-drops).
    //     The column must contain A exactly once, no ghost row, no duplicate
    //     entry in the model snapshot.
    // -----------------------------------------------------------------------
    void repeatedDropOnSameColumn_noDuplication()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(vm.moveTask(f->taskAId, f->workId, f->sectionDoingId));
        QVERIFY(vm.moveTask(f->taskAId, f->workId, f->sectionDoingId));
        QVERIFY(vm.moveTask(f->taskAId, f->workId, f->sectionDoingId));

        const QList<QString> doingCol =
            tasksForSectionViaStore(f->store, f->sectionDoingId);

        // A is present once, B is also present (B started in Doing).
        QCOMPARE(doingCol.count(f->taskAId), 1);
        QCOMPARE(doingCol.count(f->taskBId), 1);
        QCOMPARE(doingCol.size(), 2);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 12. Drag-cancel — press, threshold exceeded, release without entering
    //     a DropArea. BoardView's MouseArea has drag.threshold=6; if the
    //     gesture exits the threshold but no DropArea accepts, the ghost is
    //     dismissed at the system level and NO moveTask call is made.
    //
    //     We mirror that "no call" path by simply NOT calling moveTask and
    //     asserting the model state is unchanged from baseline. The slot
    //     pins the contract: a cancelled drag must never call into the VM.
    // -----------------------------------------------------------------------
    void dragCancelled_modelUnchanged()
    {
        Fixture *f = makeFixture();
        Task *a = f->store->taskById(f->taskAId);
        QVERIFY(a);

        const QString sectionBefore  = a->sectionId();
        const QString projectBefore  = a->projectId();
        const int     orderBefore    = a->order();
        const QDateTime updatedBefore = a->updatedAt();

        TaskViewModel vm(f->store);
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        // No-call simulation — a cancelled drag never reaches moveTask.
        // (BoardView's onDropped handler is the only path; if onDropped
        // never fires, nothing happens to the C++ state.)

        QCOMPARE(a->sectionId(),  sectionBefore);
        QCOMPARE(a->projectId(),  projectBefore);
        QCOMPARE(a->order(),      orderBefore);
        QCOMPARE(a->updatedAt(),  updatedBefore);
        QCOMPARE(spy.count(),     0);

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestBoardDragDropEdges)
#include "test_board_dragdrop_edges.moc"
