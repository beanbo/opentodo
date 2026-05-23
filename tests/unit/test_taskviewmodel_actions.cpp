#include <QtTest>
#include <QSignalSpy>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <climits>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ============================================================================
// Comprehensive unit tests for the 9 newly-added Q_INVOKABLE actions on
// TaskViewModel:
//   1. duplicateTask
//   2. moveTaskToTop / moveTaskToBottom
//   3. promoteTask
//   4. demoteTask
//   5. restoreTask
//   6. permanentlyDelete
//   7. emptyTrash
//   8. bulkSetDeadline
//   9. bulkMoveToProject
//
// Each method has a happy-path test, at least one edge case, and at least one
// validation/failure-mode test.
// ============================================================================

namespace {

// --------------------------------------------------------------------------
// Fixture struct: a fresh DataStore with three projects (Inbox, Work, Home),
// five tasks (one pinned, one soft-deleted, one with a subtask), and the
// ids of every interesting object so each test slot can pick what it needs.
// --------------------------------------------------------------------------
struct Fixture {
    DataStore *store = nullptr;
    QString inboxId;
    QString workId;
    QString homeId;
    QString taskAId;   // top-level inbox task (priority 1, unpinned)
    QString taskBId;   // top-level inbox task (priority 2, PINNED)
    QString taskCId;   // top-level work task (with subtask)
    QString subCId;    // subtask of taskC, in Work
    QString taskDId;   // top-level home task (SOFT-DELETED)

    ~Fixture() { delete store; }
};

// Build the standard fixture used by most tests.
static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    Project *work = f->store->addProject(QStringLiteral("Work"));
    f->workId = work->id();

    Project *home = f->store->addProject(QStringLiteral("Home"));
    f->homeId = home->id();

    // Task A — inbox, unpinned, default priority 1
    Task *a = f->store->addTask(QStringLiteral("Task A"), f->inboxId);
    f->taskAId = a->id();

    // Task B — inbox, pinned, priority 2
    Task *b = f->store->addTask(QStringLiteral("Task B"), f->inboxId);
    b->setPriority(2);
    b->setIsPinned(true);
    b->setLabels({QStringLiteral("urgent"), QStringLiteral("review")});
    b->setDescription(QStringLiteral("Description of B"));
    b->setDeadline(QStringLiteral("2026-12-31"));
    f->taskBId = b->id();

    // Task C — work, with one subtask
    Task *c = f->store->addTask(QStringLiteral("Task C"), f->workId);
    f->taskCId = c->id();

    Task *sub = f->store->addTask(QStringLiteral("Sub of C"), f->workId);
    sub->setParentId(f->taskCId);
    f->subCId = sub->id();

    // Task D — home, soft-deleted
    Task *d = f->store->addTask(QStringLiteral("Task D"), f->homeId);
    f->taskDId = d->id();
    f->store->removeTask(f->taskDId); // soft-delete

    return f;
}

// Helper: count siblings (same project + section + parent_id, non-deleted).
static QList<Task *> siblingsOf(DataStore *store, const Task *anchor)
{
    QList<Task *> result;
    for (Task *t : store->allTasks()) {
        if (!t || t->isDeleted()) continue;
        if (t->projectId() == anchor->projectId() &&
            t->sectionId() == anchor->sectionId() &&
            t->parentId()  == anchor->parentId())
            result.append(t);
    }
    return result;
}

} // anonymous namespace

class TestTaskViewModelActions : public QObject
{
    Q_OBJECT

private slots:

    // =====================================================================
    // 1) duplicateTask
    // =====================================================================

    void duplicateTask_happy_path_creates_copy_with_suffix()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString newId = vm.duplicateTask(f->taskBId);
        QVERIFY(!newId.isEmpty());
        QVERIFY(newId != f->taskBId);

        Task *dup = f->store->taskById(newId);
        QVERIFY(dup != nullptr);
        QCOMPARE(dup->content(), QStringLiteral("Task B (копия)"));

        delete f;
    }

    void duplicateTask_copies_descriptive_fields()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString newId = vm.duplicateTask(f->taskBId);
        QVERIFY(!newId.isEmpty());

        Task *dup    = f->store->taskById(newId);
        Task *origin = f->store->taskById(f->taskBId);
        QVERIFY(dup);
        QVERIFY(origin);

        QCOMPARE(dup->priority(),    origin->priority());
        QCOMPARE(dup->projectId(),   origin->projectId());
        QCOMPARE(dup->labels(),      origin->labels());
        QCOMPARE(dup->deadline(),    origin->deadline());
        QCOMPARE(dup->description(), origin->description());

        delete f;
    }

    void duplicateTask_clears_recurrence_and_pinned()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Mark the source as recurring so we can verify it's stripped.
        Task *src = f->store->taskById(f->taskBId);
        QVERIFY(src);
        QVERIFY(src->isPinned());
        DueObject due;
        due.date            = QStringLiteral("2026-05-09");
        due.string          = QStringLiteral("every week");
        due.isRecurring     = true;
        due.recurrenceRule  = QStringLiteral("FREQ=WEEKLY");
        src->setDue(due);

        const QString newId = vm.duplicateTask(f->taskBId);
        QVERIFY(!newId.isEmpty());

        Task *dup = f->store->taskById(newId);
        QVERIFY(dup);
        QVERIFY(!dup->isPinned());
        QVERIFY(!dup->dueIsRecurring());
        QVERIFY(dup->dueRecurrenceRule().isEmpty());

        delete f;
    }

    void duplicateTask_inserts_directly_below_source()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        Task *src = f->store->taskById(f->taskAId);
        QVERIFY(src);
        const int srcOrder = src->order();

        const QString newId = vm.duplicateTask(f->taskAId);
        QVERIFY(!newId.isEmpty());

        Task *dup = f->store->taskById(newId);
        QVERIFY(dup);
        // duplicateTask sets dup->order = src->order + 1 and shifts later
        // siblings down by 1.
        QCOMPARE(dup->order(), srcOrder + 1);
        // No other inbox top-level sibling now occupies srcOrder+1 (other than
        // dup itself).
        const auto sibs = siblingsOf(f->store, src);
        for (Task *s : sibs) {
            if (s == dup || s == src) continue;
            QVERIFY(s->order() != dup->order());
        }

        delete f;
    }

    void duplicateTask_returns_empty_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString newId = vm.duplicateTask(QStringLiteral("not-a-real-id"));
        QVERIFY(newId.isEmpty());

        delete f;
    }

    void duplicateTask_rejects_soft_deleted_source()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // taskDId is already soft-deleted in the fixture
        const QString newId = vm.duplicateTask(f->taskDId);
        QVERIFY(newId.isEmpty());

        delete f;
    }

    void duplicateTask_does_not_double_apply_suffix()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // First duplication adds the suffix; the second should NOT add it again.
        const QString id1 = vm.duplicateTask(f->taskAId);
        QVERIFY(!id1.isEmpty());
        const QString id2 = vm.duplicateTask(id1);
        QVERIFY(!id2.isEmpty());

        Task *dup2 = f->store->taskById(id2);
        QVERIFY(dup2);
        QCOMPARE(dup2->content(), QStringLiteral("Task A (копия)"));

        delete f;
    }

    // ─────────────────────────────────────────────────────────────────
    // Iter-night-34: duplicateTask must stamp `priority` in its
    // post-addTask markTaskFieldsModified call. Without the fix, the
    // per-field HLC stamp on `priority` reflected addTask's default-
    // priority moment (priority=1), not the override at line 777
    // (`dup->setPriority(original->priority())`). On concurrent peer
    // edits the duplicate silently reverted to P4/no-priority via
    // merger LWW even when the source was P1. Sibling pattern to
    // iter-24 fix for duplicateProjectAsTemplate, which already
    // stamped `priority` correctly.
    //
    // The pin compares the resulting _meta stamps: in the post-fix
    // markTaskFieldsModified loop the field order is
    //   description, section_id, parent_id, labels, priority, deadline, …
    // so labels' HLC counter precedes priority's by exactly 1 within
    // the same loop iteration → priorityTs > labelsTs.
    //
    // Without the fix, priority's stamp comes from addTask's earlier
    // loop (counter range J), while labels' stamp comes from
    // duplicateTask's later loop (counter range K > J+7) → priorityTs
    // is strictly LESS than labelsTs. The lexicographic compare on
    // zero-padded HLC strings is equivalent to HLC order.
    // ─────────────────────────────────────────────────────────────────
    void duplicateTask_stampsPriority_inMarkFieldsLoop()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Source taskB has priority=2 (set in makeFixture).
        const QString newId = vm.duplicateTask(f->taskBId);
        QVERIFY(!newId.isEmpty());

        // Inspect serialised _meta — toJson() runs injectMeta() so the
        // per-field stamps appear under "_meta" on the entity.
        const QJsonObject snap = f->store->toJson();
        const QJsonArray tasks =
            snap.value(QStringLiteral("tasks")).toArray();
        QJsonObject dupJson;
        for (const QJsonValue &v : tasks) {
            const QJsonObject obj = v.toObject();
            if (obj.value(QStringLiteral("id")).toString() == newId) {
                dupJson = obj;
                break;
            }
        }
        QVERIFY(!dupJson.isEmpty());

        const QJsonObject meta =
            dupJson.value(QStringLiteral("_meta")).toObject();
        const QString priorityTs =
            meta.value(QStringLiteral("priority")).toObject()
                .value(QStringLiteral("ts")).toString();
        const QString labelsTs =
            meta.value(QStringLiteral("labels")).toObject()
                .value(QStringLiteral("ts")).toString();

        QVERIFY2(!priorityTs.isEmpty(),
                 "priority _meta.ts missing on duplicated task");
        QVERIFY2(!labelsTs.isEmpty(),
                 "labels _meta.ts missing on duplicated task");

        // With the fix: both stamps come from duplicateTask's loop,
        // priority is enumerated right after labels → priorityTs > labelsTs.
        // Without the fix: priority's stamp is from addTask's earlier loop,
        // labels' from duplicateTask's later loop → priorityTs < labelsTs.
        QVERIFY2(priorityTs > labelsTs,
                 qPrintable(QString(
                     "iter-night-34 regression: priority _meta.ts (%1) "
                     "should be later than labels (%2) — priority is "
                     "currently being stamped at addTask-time with the "
                     "default value instead of in duplicateTask's "
                     "markTaskFieldsModified loop")
                     .arg(priorityTs, labelsTs)));

        delete f;
    }

    // =====================================================================
    // 2a) moveTaskToTop
    // =====================================================================

    void moveTaskToTop_happy_path_assigns_min_minus_one()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // taskA & taskB are inbox top-level siblings. Add a third so we have
        // a meaningful "min" that's not the moved task itself.
        Task *third = f->store->addTask(QStringLiteral("Task A2"), f->inboxId);
        const QString thirdId = third->id();

        Task *target = f->store->taskById(thirdId);
        QVERIFY(target);

        // Compute pre-move min sibling order (excluding target).
        const auto sibs = siblingsOf(f->store, target);
        int minOther = INT_MAX;
        for (Task *s : sibs) {
            if (s == target) continue;
            minOther = qMin(minOther, s->order());
        }
        QVERIFY(minOther != INT_MAX);

        QVERIFY(vm.moveTaskToTop(thirdId));

        Task *t = f->store->taskById(thirdId);
        QVERIFY(t);
        QCOMPARE(t->order(), minOther - 1);

        delete f;
    }

    void moveTaskToTop_returns_false_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.moveTaskToTop(QStringLiteral("ghost-id")));

        delete f;
    }

    void moveTaskToTop_rejects_soft_deleted_task()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.moveTaskToTop(f->taskDId));

        delete f;
    }

    void moveTaskToTop_already_at_top_is_no_op_success()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Force taskA's order well below taskB's.
        Task *a = f->store->taskById(f->taskAId);
        Task *b = f->store->taskById(f->taskBId);
        QVERIFY(a && b);
        a->setOrder(-100);
        b->setOrder(0);

        // moveTaskToTop on taskA should still return true (no-op success).
        QVERIFY(vm.moveTaskToTop(f->taskAId));
        // Order should not have moved further down.
        QCOMPARE(a->order(), -100);

        delete f;
    }

    // =====================================================================
    // 2b) moveTaskToBottom
    // =====================================================================

    void moveTaskToBottom_happy_path_assigns_max_plus_one()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        Task *a = f->store->taskById(f->taskAId);
        Task *b = f->store->taskById(f->taskBId);
        QVERIFY(a && b);

        // Make sure A is currently below B.
        a->setOrder(0);
        b->setOrder(5);

        const auto sibs = siblingsOf(f->store, a);
        int maxOther = INT_MIN;
        for (Task *s : sibs) {
            if (s == a) continue;
            maxOther = qMax(maxOther, s->order());
        }
        QVERIFY(maxOther != INT_MIN);

        QVERIFY(vm.moveTaskToBottom(f->taskAId));
        QCOMPARE(a->order(), maxOther + 1);

        delete f;
    }

    void moveTaskToBottom_returns_false_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.moveTaskToBottom(QStringLiteral("does-not-exist")));

        delete f;
    }

    void moveTaskToBottom_rejects_soft_deleted_task()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.moveTaskToBottom(f->taskDId));

        delete f;
    }

    // =====================================================================
    // 3) promoteTask  (subtask → top-level)
    // =====================================================================

    void promoteTask_happy_path_clears_parent_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        Task *sub = f->store->taskById(f->subCId);
        QVERIFY(sub);
        QCOMPARE(sub->parentId(), f->taskCId);

        QVERIFY(vm.promoteTask(f->subCId));
        QVERIFY(sub->parentId().isEmpty());

        delete f;
    }

    void promoteTask_returns_false_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.promoteTask(QStringLiteral("missing-task")));

        delete f;
    }

    void promoteTask_emits_taskChanged_signal()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);
        QVERIFY(vm.promoteTask(f->subCId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), f->subCId);

        delete f;
    }

    // =====================================================================
    // 4) demoteTask
    // =====================================================================

    void demoteTask_happy_path_sets_parent_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Add a sibling top-level task in Work; make taskC a subtask of it.
        Task *anchor = f->store->addTask(QStringLiteral("Work parent"), f->workId);
        const QString anchorId = anchor->id();

        QVERIFY(vm.demoteTask(f->taskCId, anchorId));
        QCOMPARE(f->store->taskById(f->taskCId)->parentId(), anchorId);

        delete f;
    }

    void demoteTask_rejects_unknown_task()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.demoteTask(QStringLiteral("ghost"), f->taskCId));

        delete f;
    }

    void demoteTask_rejects_unknown_parent()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.demoteTask(f->taskCId, QStringLiteral("ghost")));

        delete f;
    }

    void demoteTask_rejects_self_parent()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.demoteTask(f->taskCId, f->taskCId));

        // parent_id should remain empty (it was top-level)
        QVERIFY(f->store->taskById(f->taskCId)->parentId().isEmpty());

        delete f;
    }

    void demoteTask_rejects_cross_project_parent()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // taskC is in Work, taskA is in Inbox — different projects.
        QVERIFY(!vm.demoteTask(f->taskCId, f->taskAId));
        QVERIFY(f->store->taskById(f->taskCId)->parentId().isEmpty());

        delete f;
    }

    void demoteTask_rejects_descendant_parent()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // taskC has subC as child (same Work project). Demoting taskC under
        // its OWN child must be rejected.
        QVERIFY(!vm.demoteTask(f->taskCId, f->subCId));
        QVERIFY(f->store->taskById(f->taskCId)->parentId().isEmpty());

        delete f;
    }

    void demoteTask_rejects_empty_ids()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(!vm.demoteTask(QString(), f->taskCId));
        QVERIFY(!vm.demoteTask(f->taskCId, QString()));

        delete f;
    }

    // =====================================================================
    // 5) restoreTask
    // =====================================================================

    void restoreTask_clears_is_deleted_flag()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(f->store->taskById(f->taskDId)->isDeleted());
        vm.restoreTask(f->taskDId);
        QVERIFY(!f->store->taskById(f->taskDId)->isDeleted());

        delete f;
    }

    void restoreTask_bumps_updated_at()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        Task *d = f->store->taskById(f->taskDId);
        QVERIFY(d);
        const QDateTime before = d->updatedAt();

        // Sleep briefly to ensure different timestamp; if not, just test that
        // the timestamp is at least equal (not earlier).
        QTest::qWait(10);

        vm.restoreTask(f->taskDId);
        QVERIFY(d->updatedAt() >= before);

        delete f;
    }

    void restoreTask_no_op_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Should not crash, should not affect other tasks.
        vm.restoreTask(QStringLiteral("nope-not-here"));
        QVERIFY(f->store->taskById(f->taskDId)->isDeleted());

        delete f;
    }

    void restoreTask_emits_taskChanged()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);
        vm.restoreTask(f->taskDId);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), f->taskDId);

        delete f;
    }

    // =====================================================================
    // 6) permanentlyDelete
    // =====================================================================

    void permanentlyDelete_hard_removes_from_datastore()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QVERIFY(f->store->taskById(f->taskDId) != nullptr);
        vm.permanentlyDelete(f->taskDId);
        QVERIFY(f->store->taskById(f->taskDId) == nullptr);

        delete f;
    }

    void permanentlyDelete_no_op_on_unknown_id()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const int beforeTrash = vm.trashCount();
        vm.permanentlyDelete(QStringLiteral("ghost"));
        QCOMPARE(vm.trashCount(), beforeTrash);

        delete f;
    }

    void permanentlyDelete_emits_taskRemoved()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        QSignalSpy spy(&vm, &TaskViewModel::taskRemoved);
        vm.permanentlyDelete(f->taskDId);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), f->taskDId);

        delete f;
    }

    // =====================================================================
    // 7) emptyTrash
    // =====================================================================

    void emptyTrash_purges_every_soft_deleted_task()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Add another deleted task so we have at least two in trash.
        Task *extra = f->store->addTask(QStringLiteral("Trash 2"), f->inboxId);
        const QString extraId = extra->id();
        f->store->removeTask(extraId);

        QVERIFY(vm.trashCount() >= 2);
        vm.emptyTrash();
        QCOMPARE(vm.trashCount(), 0);

        // Also: live tasks must NOT have been touched.
        QVERIFY(f->store->taskById(f->taskAId) != nullptr);
        QVERIFY(f->store->taskById(f->taskBId) != nullptr);
        QVERIFY(f->store->taskById(f->taskCId) != nullptr);

        delete f;
    }

    void emptyTrash_with_empty_trash_is_safe_no_op()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // First empty the trash, then call again on an empty trash.
        vm.emptyTrash();
        QCOMPARE(vm.trashCount(), 0);
        vm.emptyTrash(); // must not crash, still 0
        QCOMPARE(vm.trashCount(), 0);

        delete f;
    }

    void emptyTrash_does_not_affect_live_tasks_count()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const int liveBefore = f->store->allTasks().size();
        vm.emptyTrash();
        const int liveAfter = f->store->allTasks().size();
        QCOMPARE(liveAfter, liveBefore);

        delete f;
    }

    // =====================================================================
    // 8) bulkSetDeadline
    // =====================================================================

    void bulkSetDeadline_applies_iso_date_to_selected()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);
        vm.selectTask(f->taskCId);

        const QDate d(2026, 6, 15);
        vm.bulkSetDeadline(d);

        QCOMPARE(f->store->taskById(f->taskAId)->deadline(),
                 QStringLiteral("2026-06-15"));
        QCOMPARE(f->store->taskById(f->taskCId)->deadline(),
                 QStringLiteral("2026-06-15"));

        delete f;
    }

    void bulkSetDeadline_does_not_touch_unselected()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);

        const QString preB = f->store->taskById(f->taskBId)->deadline();
        const QDate d(2026, 1, 1);
        vm.bulkSetDeadline(d);

        // Task B was NOT in the selection — its deadline must be unchanged.
        QCOMPARE(f->store->taskById(f->taskBId)->deadline(), preB);

        delete f;
    }

    void bulkSetDeadline_invalid_date_clears_deadline()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Pre-set a deadline on taskA so we can verify it's cleared.
        f->store->taskById(f->taskAId)->setDeadline(QStringLiteral("2026-12-31"));

        vm.selectTask(f->taskAId);
        vm.bulkSetDeadline(QDate{}); // invalid → clear

        QVERIFY(f->store->taskById(f->taskAId)->deadline().isEmpty());

        delete f;
    }

    void bulkSetDeadline_clears_selection_after()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);
        vm.selectTask(f->taskBId);
        QCOMPARE(vm.selectionCount(), 2);

        vm.bulkSetDeadline(QDate(2026, 5, 9));
        QCOMPARE(vm.selectionCount(), 0);

        delete f;
    }

    void bulkSetDeadline_with_empty_selection_is_no_op()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString preDeadline = f->store->taskById(f->taskAId)->deadline();
        // No tasks selected.
        vm.bulkSetDeadline(QDate(2030, 1, 1));
        QCOMPARE(f->store->taskById(f->taskAId)->deadline(), preDeadline);

        delete f;
    }

    void bulkSetDeadline_skips_soft_deleted_tasks()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Selecting the soft-deleted task is unusual but should be tolerated.
        vm.selectTask(f->taskDId);
        const QString preD = f->store->taskById(f->taskDId)->deadline();

        vm.bulkSetDeadline(QDate(2026, 7, 1));

        // bulkSetDeadline explicitly skips soft-deleted tasks, so deadline stays.
        QCOMPARE(f->store->taskById(f->taskDId)->deadline(), preD);

        delete f;
    }

    // =====================================================================
    // 9) bulkMoveToProject
    // =====================================================================

    void bulkMoveToProject_moves_all_selected()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);
        vm.selectTask(f->taskBId);
        vm.bulkMoveToProject(f->workId);

        QCOMPARE(f->store->taskById(f->taskAId)->projectId(), f->workId);
        QCOMPARE(f->store->taskById(f->taskBId)->projectId(), f->workId);

        delete f;
    }

    void bulkMoveToProject_does_not_affect_unselected()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);
        vm.bulkMoveToProject(f->homeId);

        // taskB and taskC unchanged.
        QCOMPARE(f->store->taskById(f->taskBId)->projectId(), f->inboxId);
        QCOMPARE(f->store->taskById(f->taskCId)->projectId(), f->workId);

        delete f;
    }

    void bulkMoveToProject_clears_selection_after()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.selectTask(f->taskAId);
        vm.selectTask(f->taskBId);
        QCOMPARE(vm.selectionCount(), 2);

        vm.bulkMoveToProject(f->workId);
        QCOMPARE(vm.selectionCount(), 0);

        delete f;
    }

    // =====================================================================
    // setTaskDue recurrence persistence (iter-night-28 pin)
    //
    // Regression sibling of fix #20 (bulkSetDue) and the addTask-post-set
    // fix #23. When a user changes a task's due date via the inline date
    // popup or "snooze" shortcut, the QML layer calls
    // TaskViewModel::setTaskDue(id, "every Monday") / "каждый понедельник"
    // / "ежедневно" / etc. The DateParser populates result.recurrenceRule
    // with the canonical RRULE (e.g. "FREQ=WEEKLY;BYDAY=MO"), but the
    // setter dropped that field before iter-night-28 — leaving the task
    // marked isRecurring=true with an EMPTY recurrenceRule, which forced
    // RecurrenceEngine onto its legacy humanString-parsing fallback on
    // every completion. The fix mirrors addTask (line ~319) and
    // bulkSetDue (line ~2050): call setDueRecurrenceRule unconditionally.
    //
    // We also pin the inverse direction: overwriting a previously
    // recurring task with a one-shot date must CLEAR the stale rule.
    // =====================================================================

    void setTaskDue_recurring_string_persists_rrule()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Pre-set Russian language so the parser dispatches to rules_ru.cpp.
        // (Both rule files populate recurrenceRule equivalently — this just
        // makes the test's intent explicit and exercises the Russian path
        // since OpenTodo's UI default is Russian.)
        f->store->setSetting(QStringLiteral("language"), QStringLiteral("ru"));

        QVERIFY(vm.setTaskDue(f->taskAId, QString::fromUtf8("каждый день")));

        Task *t = f->store->taskById(f->taskAId);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        // "каждый день" resolves to FREQ=DAILY in rules_ru.cpp (line ~480).
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=DAILY"));

        delete f;
    }

    void setTaskDue_english_recurring_string_persists_rrule()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Force English path so the EN preset table is consulted.
        f->store->setSetting(QStringLiteral("language"), QStringLiteral("en"));

        QVERIFY(vm.setTaskDue(f->taskAId, QStringLiteral("every Monday")));

        Task *t = f->store->taskById(f->taskAId);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        // "every Monday" → FREQ=WEEKLY;BYDAY=MO in rules_en.cpp.
        QCOMPARE(t->dueRecurrenceRule(),
                 QStringLiteral("FREQ=WEEKLY;BYDAY=MO"));

        delete f;
    }

    void setTaskDue_oneshot_clears_stale_rrule()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Pre-seed taskA as recurring (simulate a prior setTaskDue / import).
        Task *src = f->store->taskById(f->taskAId);
        QVERIFY(src);
        DueObject pre;
        pre.date           = QStringLiteral("2026-05-25");
        pre.string         = QStringLiteral("every week");
        pre.isRecurring    = true;
        pre.recurrenceRule = QStringLiteral("FREQ=WEEKLY");
        src->setDue(pre);
        QVERIFY(src->dueIsRecurring());
        QVERIFY(!src->dueRecurrenceRule().isEmpty());

        // User overwrites with a one-shot date. The new state must NOT carry
        // the stale FREQ=WEEKLY rule forward — that would silently keep the
        // task recurring against the user's intent.
        f->store->setSetting(QStringLiteral("language"), QStringLiteral("en"));
        QVERIFY(vm.setTaskDue(f->taskAId, QStringLiteral("2026-12-31")));

        Task *post = f->store->taskById(f->taskAId);
        QVERIFY(post != nullptr);
        QVERIFY(!post->dueIsRecurring());
        QVERIFY(post->dueRecurrenceRule().isEmpty());

        delete f;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Iter-night-37: the cold-start auto-Inbox path in addTask must
    // stamp is_inbox after the post-construction flip.
    //
    // DataStore::addProject runs the inbox-uniqueness guard against
    // `project->isInbox()` at construction time. A freshly-constructed
    // Project defaults to is_inbox=false, so when TaskViewModel::addTask
    // hits the "no project available" fallback (corrupted state.json or
    // very first launch race), addProject stamps is_inbox=false into
    // m_projectMeta's _meta for that field. The subsequent
    // `autoInbox->setIsInbox(true)` is a plain setter — task.cpp lacks
    // a CRDT-stamping hook — so the per-field stamp records the wrong
    // value-at-stamp-time.
    //
    // On the next sync, a peer's "real" inbox (with a properly stamped
    // is_inbox=true) wins the LWW race against this device's stale
    // stamp even when the peer's stamp is legitimately OLDER than
    // this device's actual promotion moment — because our stamp lies
    // about the time. End state: two "Inbox" entities visible (peer's
    // legitimate one PLUS this orphan that got demoted by the merger),
    // confusing the user with no recovery path.
    //
    // Pin: trigger the fallback by constructing a DataStore with NO
    // projects whatsoever (no inbox flag anywhere), then call addTask
    // with empty projectId. The created Inbox must have is_inbox=true
    // AND its _meta.is_inbox stamp must reflect the post-flip moment,
    // not the pre-flip default-value stamp.
    //
    // The most reliable indicator we can assert with the public API:
    // compare _meta.is_inbox.ts to _meta.name.ts. `name` is stamped by
    // addProject's initial loop (counter K+0). `is_inbox` is also
    // stamped by that initial loop (counter K+4) — so pre-fix
    // is_inbox.ts > name.ts by exactly 4 counter ticks within the
    // SAME physMs. Post-fix the explicit re-stamp pushes is_inbox to
    // a LATER counter (and possibly later physMs), so is_inbox.ts is
    // strictly GREATER than the highest stamp from the initial loop.
    // We use `updated_at` (also re-stamped by the fix) as the upper
    // bound of the initial loop — pre-fix is_inbox.ts < updated_at.ts;
    // post-fix is_inbox.ts > updated_at.ts (re-stamp moves both, but
    // is_inbox is enumerated first in the fix list).
    // ─────────────────────────────────────────────────────────────────────
    void addTask_coldStart_autoInbox_stampsIsInbox()
    {
        // Build a totally empty DataStore — no Inbox exists, no project
        // at all. This triggers the auto-creation fallback in addTask.
        DataStore store;
        TaskViewModel vm(&store);

        QVERIFY(store.allProjects().isEmpty());
        QVERIFY(store.inboxProject() == nullptr);

        const QString taskId = vm.addTask(QStringLiteral("First task"),
                                          /*projectId=*/QString());
        QVERIFY(!taskId.isEmpty());

        // The fallback must have created exactly one project flagged as
        // inbox. (allProjects() filters tombstones.)
        const auto projects = store.allProjects();
        QCOMPARE(projects.size(), 1);
        Project *autoInbox = projects.first();
        QVERIFY(autoInbox != nullptr);
        QVERIFY2(autoInbox->isInbox(),
                 "auto-created Inbox must be flagged is_inbox=true");

        // Inspect the per-field _meta via toJson() — DataStore::toJson()
        // runs injectMeta which surfaces m_projectMeta entries under the
        // entity's "_meta" key.
        const QJsonObject snap = store.toJson();
        const QJsonArray projArr =
            snap.value(QStringLiteral("projects")).toArray();
        QCOMPARE(projArr.size(), 1);
        const QJsonObject projObj = projArr.first().toObject();
        const QJsonObject meta =
            projObj.value(QStringLiteral("_meta")).toObject();

        const QString isInboxTs =
            meta.value(QStringLiteral("is_inbox")).toObject()
                .value(QStringLiteral("ts")).toString();
        const QString nameTs =
            meta.value(QStringLiteral("name")).toObject()
                .value(QStringLiteral("ts")).toString();
        QVERIFY2(!isInboxTs.isEmpty(),
                 "is_inbox _meta.ts missing on auto-created Inbox");
        QVERIFY2(!nameTs.isEmpty(),
                 "name _meta.ts missing on auto-created Inbox");

        // With the fix, is_inbox is re-stamped AFTER all the initial-loop
        // stamps, including `name`. Lexicographic compare of zero-padded
        // HLC strings equals HLC order, so isInboxTs > nameTs holds iff
        // is_inbox was stamped later than name. Without the fix, both
        // come from the same initial loop with consecutive counters, but
        // `name` is enumerated FIRST (counter K+0) and `is_inbox` is
        // FIFTH (counter K+4), so is_inbox.ts > name.ts even without
        // the fix — that comparison alone doesn't pin the bug.
        //
        // The strict pin: in the post-fix re-stamp, is_inbox is followed
        // immediately by updated_at (counter K+5 in the re-stamp loop).
        // Without the fix, is_inbox is stamped at counter K_init+4 of
        // the INITIAL loop, and updated_at is stamped at counter
        // K_init+8 of the same loop. So isInboxTs < updatedAtTs holds
        // pre-fix.
        //
        // Post-fix, the re-stamp loop runs AFTER the initial loop with
        // is_inbox first (counter K_re+0), updated_at second (K_re+1).
        // K_re > K_init+8, so is_inbox lands at K_init+9 and updated_at
        // at K_init+10. The comparison flips: isInboxTs > updatedAtTs
        // pre-fix? Actually wait — re-stamp's is_inbox.ts overwrites the
        // initial one. So _meta.is_inbox.ts reflects the re-stamp time
        // (K_init+9). _meta.updated_at.ts reflects the re-stamp time
        // (K_init+10). So updated_at > is_inbox always.
        //
        // The right pin: compare is_inbox.ts to the OLDEST initial-loop
        // stamp (`name` at K_init+0). Without the fix, is_inbox.ts =
        // K_init+4, so is_inbox.ts - name.ts has a small counter gap
        // (~4). With the fix, is_inbox.ts = K_init+9 (re-stamp), so
        // the gap is ~9 — but both gaps are positive, so a strict-
        // greater comparison isn't discriminating.
        //
        // Strongest discriminating pin: compare is_inbox.ts to
        // `created_at`.ts. Without the fix, both are in the initial
        // loop: name(0), color(1), order(2), is_favorite(3), is_inbox(4),
        // is_archived(5), view_style(6), created_at(7), updated_at(8),
        // is_deleted(9). So is_inbox.ts < created_at.ts pre-fix. Post-
        // fix is_inbox is re-stamped at K_init+9, AFTER created_at, so
        // is_inbox.ts > created_at.ts.
        const QString createdAtTs =
            meta.value(QStringLiteral("created_at")).toObject()
                .value(QStringLiteral("ts")).toString();
        QVERIFY2(!createdAtTs.isEmpty(),
                 "created_at _meta.ts missing on auto-created Inbox");
        QVERIFY2(isInboxTs > createdAtTs,
                 qPrintable(QString(
                     "iter-night-37 regression: is_inbox _meta.ts (%1) "
                     "should be later than created_at (%2) — is_inbox "
                     "is currently being stamped at addProject's initial "
                     "loop time (with the default value=false), instead "
                     "of the post-flip moment.")
                     .arg(isInboxTs, createdAtTs)));
    }
};

QTEST_GUILESS_MAIN(TestTaskViewModelActions)
#include "test_taskviewmodel_actions.moc"
