// ---------------------------------------------------------------------------
// test_subtask_deep_hierarchy.cpp
//
// Quality round Q3.07: behavioural pinning for subtask hierarchies at
// depth >= 5 and other less-covered hierarchy edges.
//
// Sister-suites already in place:
//   * test_subtasks.cpp                 — depth-1 hasSubtasks / subtasksOf /
//                                         convertToSubtask happy paths +
//                                         "subtasks hidden from Inbox".
//   * test_subtask_hierarchy_edge.cpp   — promote/demote at depth ~3, cycle
//                                         prevention up to 3-node, candidate-
//                                         Parents filtering, soft-delete
//                                         cascade depth-1.
//
// What this suite adds (target: nine slots, App layer):
//   1. depth_5_hierarchy_subtasks_query_returns_only_direct_children
//      Build A -> B -> C -> D -> E -> F (six levels). Verify
//      DataStore::subtasks(B) returns {C} only — non-recursive query
//      contract; descendants D/E/F must NOT be in the result.
//   2. promote_subtask_at_depth_3_to_root_keeps_descendants_attached
//      Build A -> B -> C -> D; promote C. Verify C.parentId is empty AND
//      D.parentId == C (i.e. the subtree under C survives the promotion
//      unchanged).
//   3. demote_root_to_become_subtask_of_existing_subtask
//      A is top-level. C is A's subtask. B is top-level. Demote B under C.
//      Verify B.parentId == C.id. This pins that demoteTask itself does NOT
//      enforce "new parent must be top-level" — only candidateParents()
//      does. (Sister-suite already pins the candidateParents rule.)
//   4. move_subtree_across_projects_keeps_parent_link_but_changes_project
//      A in P1 with child B (also P1). moveTask(A, P2). Verify A.projectId
//      == P2 and pin whether B was cascaded (Todoist parity expectation:
//      cascade) or left in P1 with A.parentId still pointing at A
//      (current implementation does NOT cascade — surface the gap).
//   5. cycle_attempt_at_depth_5_is_rejected_by_viewmodel_demote
//      Build A -> B -> C -> D -> E (linear, five nodes). Attempt
//      vm.demoteTask(A, E) — must be rejected by the BFS descendant
//      scan. Pin that A.parentId stays empty.
//   6. subtask_of_completed_parent_remains_active
//      Complete A. Add B as A's subtask. Verify A.isCompleted == true,
//      B.isCompleted == false. No automatic completion of children.
//   7. complete_parent_does_not_cascade_to_children
//      Build A -> B (subtask). vm.completeTask(A). Verify A.isCompleted
//      true, B.isCompleted still false.
//   8. softdelete_parent_cascades_direct_children_only
//      Build A -> B -> C (three levels). vm.deleteTask(A). Verify the
//      cascade boundary: A and B become tombstones (direct children
//      cascade), but C does NOT cascade — pin that the cascade is depth-1
//      not depth-N.
//   9. subtasks_query_with_invalid_parent_id_returns_empty
//      Defensive contract: subtasks("non-existent-uuid") returns empty
//      list, even though the implementation does not look up parent
//      existence — every orphan that happens to point at the same id
//      would still appear. Test with a fresh UUID that no task is using
//      to verify empty result.
//
// Constraints:
//   * QTEST_GUILESS_MAIN — no GUI, pure DataStore + TaskViewModel fixture.
//   * Unique QSettings org/app per suite for parallel ctest isolation.
//   * NO production-code edits — assertions pin CURRENT behaviour with
//     qInfo() notes where the behaviour might surprise (e.g. cross-project
//     move does NOT cascade subtask projectIds).
//   * Registered as App layer in tests/unit/CMakeLists.txt to match the
//     existing test_subtasks / test_subtask_hierarchy_edge sister-suites.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QCoreApplication>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

namespace {

struct Fixture {
    DataStore *store = nullptr;
    QString inboxId;
    QString projP1Id;
    QString projP2Id;
    ~Fixture() { delete store; }
};

static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    Project *p1 = f->store->addProject(QStringLiteral("P1"));
    f->projP1Id = p1->id();

    Project *p2 = f->store->addProject(QStringLiteral("P2"));
    f->projP2Id = p2->id();

    return f;
}

// Add a task in `projectId` and return its id.
static QString addTask(DataStore *store, const QString &content, const QString &projectId)
{
    Task *t = store->addTask(content, projectId);
    return t ? t->id() : QString();
}

// Wire a parent_id directly on the underlying Task. We deliberately bypass
// vm.demoteTask/convertToSubtask so that the test setup does not depend on
// the very APIs whose hierarchy semantics we are pinning.
static void setTaskParent(DataStore *store, const QString &childId, const QString &parentId)
{
    Task *t = store->taskById(childId);
    QVERIFY(t != nullptr);
    t->setParentId(parentId);
}

} // namespace

class TestSubtaskDeepHierarchy : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Unique QSettings namespace so parallel ctest runs don't collide
        // with the existing test_subtasks* / test_subtask_hierarchy_edge
        // suites or with anything else writing through QSettings.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_SubtaskDeepHier"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_subtask_deep_hierarchy"));
    }

    // -----------------------------------------------------------------------
    // 1. depth_5_hierarchy_subtasks_query_returns_only_direct_children
    // -----------------------------------------------------------------------
    //
    // Build A -> B -> C -> D -> E -> F (six tasks, five parent edges).
    // The DataStore::subtasks(parentId) query is non-recursive: it filters
    // m_tasks for entries whose parentId() == arg. So subtasks(B) must
    // return {C} only, never the deeper descendants D/E/F.
    //
    // This is the load-bearing assumption for the TaskListView's per-level
    // expand/collapse model and for vm.subtasksOf(). If a future refactor
    // ever makes subtasks() recursive, this test fails and surfaces the
    // semantic change — callers everywhere would need to be re-audited.
    // -----------------------------------------------------------------------
    void depth_5_hierarchy_subtasks_query_returns_only_direct_children()
    {
        Fixture *f = makeFixture();

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        const QString cId = addTask(f->store, QStringLiteral("C"), f->projP1Id);
        const QString dId = addTask(f->store, QStringLiteral("D"), f->projP1Id);
        const QString eId = addTask(f->store, QStringLiteral("E"), f->projP1Id);
        const QString fId = addTask(f->store, QStringLiteral("F"), f->projP1Id);

        // A -> B -> C -> D -> E -> F  (five parent edges, depth 5)
        setTaskParent(f->store, bId, aId);
        setTaskParent(f->store, cId, bId);
        setTaskParent(f->store, dId, cId);
        setTaskParent(f->store, eId, dId);
        setTaskParent(f->store, fId, eId);

        // Direct-child query at each level.
        const auto subsOfA = f->store->subtasks(aId);
        const auto subsOfB = f->store->subtasks(bId);
        const auto subsOfC = f->store->subtasks(cId);
        const auto subsOfD = f->store->subtasks(dId);
        const auto subsOfE = f->store->subtasks(eId);
        const auto subsOfF = f->store->subtasks(fId);

        QCOMPARE(subsOfA.size(), 1);
        QCOMPARE(subsOfA.first()->id(), bId);
        QCOMPARE(subsOfB.size(), 1);
        QCOMPARE(subsOfB.first()->id(), cId);
        QCOMPARE(subsOfC.size(), 1);
        QCOMPARE(subsOfC.first()->id(), dId);
        QCOMPARE(subsOfD.size(), 1);
        QCOMPARE(subsOfD.first()->id(), eId);
        QCOMPARE(subsOfE.size(), 1);
        QCOMPARE(subsOfE.first()->id(), fId);
        QCOMPARE(subsOfF.size(), 0); // leaf has no children

        // Cross-check: subtasksOf(B) on TaskViewModel returns the same
        // single-child contract.
        TaskViewModel vm(f->store);
        const QVariantList vmSubsOfB = vm.subtasksOf(bId);
        QCOMPARE(vmSubsOfB.size(), 1);
        QCOMPARE(vmSubsOfB.first().toMap()
                     .value(QStringLiteral("id")).toString(), cId);

        qInfo("subtasks() is NON-recursive — descendants below the direct "
              "child layer are NOT returned. UI must walk the tree itself "
              "(see TaskListView's expand/collapse model).");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 2. promote_subtask_at_depth_3_to_root_keeps_descendants_attached
    // -----------------------------------------------------------------------
    //
    // Promote a middle node and check that its own children stay
    // attached. promoteTask only clears the promoted task's own
    // parent_id — the subtree below is untouched, which means the
    // promoted node arrives at the root with its entire descendant
    // tree intact.
    //
    // This is the "lift and keep your children" contract. If a future
    // refactor decides to flatten children on promote (or vice-versa),
    // this slot fails and the maintainer has to make the choice
    // intentionally rather than accidentally.
    // -----------------------------------------------------------------------
    void promote_subtask_at_depth_3_to_root_keeps_descendants_attached()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        const QString cId = addTask(f->store, QStringLiteral("C"), f->projP1Id);
        const QString dId = addTask(f->store, QStringLiteral("D"), f->projP1Id);

        // A -> B -> C -> D  (depth-3 chain; C is at depth 2 from A's root)
        setTaskParent(f->store, bId, aId);
        setTaskParent(f->store, cId, bId);
        setTaskParent(f->store, dId, cId);

        // Promote C to root.
        QVERIFY(vm.promoteTask(cId));

        // C is now a top-level task — parent_id cleared.
        Task *cTask = f->store->taskById(cId);
        QVERIFY(cTask != nullptr);
        QVERIFY(cTask->parentId().isEmpty());

        // D's parent_id is still C (subtree stayed intact, "lifted with").
        Task *dTask = f->store->taskById(dId);
        QVERIFY(dTask != nullptr);
        QCOMPARE(dTask->parentId(), cId);

        // B's parent_id is still A (the chain above C is also untouched).
        Task *bTask = f->store->taskById(bId);
        QVERIFY(bTask != nullptr);
        QCOMPARE(bTask->parentId(), aId);

        // subtasks(C) now returns {D} — the descendant relationship is
        // preserved even though C is now top-level.
        const auto subsOfC = f->store->subtasks(cId);
        QCOMPARE(subsOfC.size(), 1);
        QCOMPARE(subsOfC.first()->id(), dId);

        // subtasks(B) is now empty — C is no longer B's child.
        const auto subsOfB = f->store->subtasks(bId);
        QCOMPARE(subsOfB.size(), 0);

        delete f;
    }

    // -----------------------------------------------------------------------
    // 3. demote_root_to_become_subtask_of_existing_subtask
    // -----------------------------------------------------------------------
    //
    // demoteTask only validates: not-empty, not-self, parent-exists,
    // same-project, not-ancestor (cycle), not-descendant. It does NOT
    // enforce a "new parent must be top-level" rule. That rule lives ONLY
    // in candidateParents() so the picker UI shows top-level options.
    //
    // This slot pins the API-level behaviour: programmatically you CAN
    // demote a root task under an already-subtask node. The result is a
    // four-level chain.
    // -----------------------------------------------------------------------
    void demote_root_to_become_subtask_of_existing_subtask()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString cId = addTask(f->store, QStringLiteral("C"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);

        // A -> C  (C is A's subtask). B is top-level.
        setTaskParent(f->store, cId, aId);

        // Sanity: B is top-level.
        QVERIFY(f->store->taskById(bId)->parentId().isEmpty());

        // Demote B under C (an existing subtask). API-level: must succeed.
        QVERIFY(vm.demoteTask(bId, cId));

        Task *bTask = f->store->taskById(bId);
        QVERIFY(bTask != nullptr);
        QCOMPARE(bTask->parentId(), cId);

        // The chain is now A -> C -> B. Walk upward from B to verify.
        QStringList chain;
        chain << bId;
        QString cur = bTask->parentId();
        int safety = 10;
        while (!cur.isEmpty() && safety-- > 0) {
            chain << cur;
            const Task *p = f->store->taskById(cur);
            QVERIFY(p != nullptr);
            cur = p->parentId();
        }
        QCOMPARE(chain.size(), 3);
        QCOMPARE(chain.at(0), bId);
        QCOMPARE(chain.at(1), cId);
        QCOMPARE(chain.at(2), aId);

        // Cross-check: candidateParents(B) would NOT have listed C as an
        // option (top-level rule), but demoteTask itself accepted it.
        // This dichotomy is intentional — pin it.
        const QVariantList cands = vm.candidateParents(bId);
        bool foundC = false;
        for (const QVariant &v : cands) {
            if (v.toMap().value(QStringLiteral("id")).toString() == cId) {
                foundC = true; break;
            }
        }
        QVERIFY(!foundC);
        qInfo("candidateParents() hides non-top-level candidates, but "
              "demoteTask() itself accepts them — the picker UI is the "
              "only enforcement of the top-level rule.");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 4. move_subtree_across_projects_keeps_parent_link_but_changes_project
    // -----------------------------------------------------------------------
    //
    // A in P1, B is A's subtask (also in P1). vm.moveTask(A, P2) moves
    // A to P2 but does NOT cascade to B — B stays in P1 with its
    // parent_id still pointing at A. The result is a parent in P2 and
    // a child in P1, which Todoist-style UIs treat as "orphaned across
    // projects" because the project-filtered view of P1 still shows B
    // under no visible parent, and the project-filtered view of P2
    // shows A as childless.
    //
    // This is a known gap — pin it. A future fix would cascade
    // projectId across the entire descendant tree in moveTask.
    // -----------------------------------------------------------------------
    void move_subtree_across_projects_keeps_parent_link_but_changes_project()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        setTaskParent(f->store, bId, aId);

        // Move A to P2.
        QVERIFY(vm.moveTask(aId, f->projP2Id, QString()));

        Task *aTask = f->store->taskById(aId);
        Task *bTask = f->store->taskById(bId);
        QVERIFY(aTask != nullptr);
        QVERIFY(bTask != nullptr);

        // A: project changed to P2.
        QCOMPARE(aTask->projectId(), f->projP2Id);

        // B: project did NOT cascade. Pin current behaviour.
        QCOMPARE(bTask->projectId(), f->projP1Id);

        // The parent link survives the cross-project move.
        QCOMPARE(bTask->parentId(), aId);

        // subtasks(A) is still {B} — the relationship is preserved
        // even though the two now live in different projects.
        const auto subsOfA = f->store->subtasks(aId);
        QCOMPARE(subsOfA.size(), 1);
        QCOMPARE(subsOfA.first()->id(), bId);

        // tasksForProject() filters out subtasks (parentId().isEmpty()
        // check in DataStore::tasksForProject), so the visible project-view
        // contents look like this AFTER the cross-project move:
        //   * P2's visible list contains A (top-level after move).
        //   * P1's visible list is EMPTY — B is in P1 but has a non-empty
        //     parentId, so the project view never surfaces it. The orphan
        //     subtask becomes invisible in the project sidebar.
        const auto p2Tasks = f->store->tasksForProject(f->projP2Id);
        QSet<QString> p2Ids;
        for (const Task *t : p2Tasks) p2Ids.insert(t->id());
        QVERIFY(p2Ids.contains(aId));
        QVERIFY(!p2Ids.contains(bId));

        const auto p1Tasks = f->store->tasksForProject(f->projP1Id);
        QSet<QString> p1Ids;
        for (const Task *t : p1Tasks) p1Ids.insert(t->id());
        // B is NOT in tasksForProject(P1) — even though its raw projectId
        // is still P1, the subtask filter (parentId().isEmpty()) excludes
        // it. A is also NOT in P1 (its projectId moved to P2).
        QVERIFY(!p1Ids.contains(aId));
        QVERIFY(!p1Ids.contains(bId));
        // raw allTasks() still contains B, so the data is recoverable.
        bool foundBInAllTasks = false;
        for (const Task *t : f->store->allTasks())
            if (t->id() == bId) { foundBInAllTasks = true; break; }
        QVERIFY(foundBInAllTasks);

        qInfo("moveTask does NOT cascade projectId to subtasks — child B "
              "stayed in P1 even though parent A moved to P2. Combined "
              "with the parentId-non-empty filter in tasksForProject(), "
              "B becomes invisible in P1's project view. Pin this as a "
              "known gap; Todoist parity would cascade projectId on move.");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 5. cycle_attempt_at_depth_5_is_rejected_by_viewmodel_demote
    // -----------------------------------------------------------------------
    //
    // Build a depth-5 linear chain A -> B -> C -> D -> E. Then attempt
    // vm.demoteTask(A, E) — would close a cycle. The demoteTask BFS
    // descendant scan walks down from A through every level until it
    // finds E, then rejects. Pin that:
    //   * the return value is false,
    //   * A.parentId stays empty (no partial mutation on rejection),
    //   * the chain B -> C -> D -> E is untouched.
    //
    // This complements test_subtask_hierarchy_edge's three-node cycle
    // tests (cycle_two_node_rejected, cycle_three_node_rejected,
    // cycle_demote_under_own_grandchild_rejected) by going deeper.
    // -----------------------------------------------------------------------
    void cycle_attempt_at_depth_5_is_rejected_by_viewmodel_demote()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        const QString cId = addTask(f->store, QStringLiteral("C"), f->projP1Id);
        const QString dId = addTask(f->store, QStringLiteral("D"), f->projP1Id);
        const QString eId = addTask(f->store, QStringLiteral("E"), f->projP1Id);

        // A -> B -> C -> D -> E  (four parent edges)
        setTaskParent(f->store, bId, aId);
        setTaskParent(f->store, cId, bId);
        setTaskParent(f->store, dId, cId);
        setTaskParent(f->store, eId, dId);

        // Attempt to demote A under E — would form a cycle through five
        // levels. demoteTask BFS must reject.
        const bool ok = vm.demoteTask(aId, eId);
        QVERIFY(!ok);

        // A's parent_id stays empty — no partial mutation.
        QVERIFY(f->store->taskById(aId)->parentId().isEmpty());

        // The chain B -> C -> D -> E is unchanged.
        QCOMPARE(f->store->taskById(bId)->parentId(), aId);
        QCOMPARE(f->store->taskById(cId)->parentId(), bId);
        QCOMPARE(f->store->taskById(dId)->parentId(), cId);
        QCOMPARE(f->store->taskById(eId)->parentId(), dId);

        qInfo("demoteTask BFS rejects cycles at depth 5 — pin verified.");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 6. subtask_of_completed_parent_remains_active
    // -----------------------------------------------------------------------
    //
    // Pin that adding a subtask under an already-completed parent does
    // NOT auto-complete the child. The data model is intentionally
    // independent: each Task has its own isCompleted flag and no
    // dependency-based gating. (Todoist itself allows incomplete
    // subtasks of completed parents, so this matches user expectation.)
    // -----------------------------------------------------------------------
    void subtask_of_completed_parent_remains_active()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        QVERIFY(!aId.isEmpty());

        // Complete A first.
        QVERIFY(vm.completeTask(aId));
        Task *aTask = f->store->taskById(aId);
        QVERIFY(aTask != nullptr);
        QVERIFY(aTask->isCompleted());

        // Now add B as A's subtask (parent is completed).
        const QString bId = vm.addSubtask(aId, QStringLiteral("B"));
        QVERIFY(!bId.isEmpty());

        Task *bTask = f->store->taskById(bId);
        QVERIFY(bTask != nullptr);

        // The subtask must be active despite the parent being completed.
        QVERIFY(!bTask->isCompleted());
        QCOMPARE(bTask->parentId(), aId);

        // Parent remains completed — adding a subtask doesn't reopen it.
        QVERIFY(aTask->isCompleted());

        delete f;
    }

    // -----------------------------------------------------------------------
    // 7. complete_parent_does_not_cascade_to_children
    // -----------------------------------------------------------------------
    //
    // Pin the no-cascade contract on the other direction: a pre-existing
    // subtask of a parent stays active when the parent is completed.
    // -----------------------------------------------------------------------
    void complete_parent_does_not_cascade_to_children()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = vm.addSubtask(aId, QStringLiteral("B"));
        QVERIFY(!aId.isEmpty());
        QVERIFY(!bId.isEmpty());

        // Both incomplete initially.
        QVERIFY(!f->store->taskById(aId)->isCompleted());
        QVERIFY(!f->store->taskById(bId)->isCompleted());

        // Complete parent.
        QVERIFY(vm.completeTask(aId));

        // Parent completed; child stays active.
        QVERIFY(f->store->taskById(aId)->isCompleted());
        QVERIFY(!f->store->taskById(bId)->isCompleted());

        qInfo("Completing a parent does NOT cascade isCompleted to "
              "subtasks — matches Todoist behaviour.");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 8. softdelete_parent_cascades_direct_children_only
    // -----------------------------------------------------------------------
    //
    // DataStore::removeTask cascades is_deleted to DIRECT children
    // (depth 1) ONLY. Grandchildren are NOT cascaded — they become
    // orphans pointing at a (now-tombstoned) middle node.
    //
    // This is the explicit B-1.6 cascade contract — pin both halves:
    // direct cascade is on, transitive cascade is off. The sister-suite
    // test_subtask_hierarchy_edge already pins the direct cascade; here
    // we add the transitive-cascade-OFF half.
    // -----------------------------------------------------------------------
    void softdelete_parent_cascades_direct_children_only()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        const QString cId = addTask(f->store, QStringLiteral("C"), f->projP1Id);

        // A -> B -> C  (three levels)
        setTaskParent(f->store, bId, aId);
        setTaskParent(f->store, cId, bId);

        // Soft-delete A. Cascade should hit B (direct child) but NOT C
        // (grandchild — depth 2 from A).
        QVERIFY(vm.deleteTask(aId));

        Task *aTask = f->store->taskById(aId);
        Task *bTask = f->store->taskById(bId);
        Task *cTask = f->store->taskById(cId);
        QVERIFY(aTask != nullptr);
        QVERIFY(bTask != nullptr);
        QVERIFY(cTask != nullptr);

        QVERIFY(aTask->isDeleted());
        QVERIFY(bTask->isDeleted());          // direct child cascaded
        QVERIFY(!cTask->isDeleted());         // grandchild did NOT cascade

        // C still points at B (its parent_id is preserved across the
        // tombstone — historical structure stays for sync).
        QCOMPARE(cTask->parentId(), bId);

        qInfo("removeTask cascades is_deleted to DIRECT children only "
              "(B-1.6 contract). Grandchild C is orphaned: it remains "
              "live but its parent_id points at a tombstoned task.");

        delete f;
    }

    // -----------------------------------------------------------------------
    // 9. subtasks_query_with_invalid_parent_id_returns_empty
    // -----------------------------------------------------------------------
    //
    // Defensive contract for the subtasks() query: when called with a
    // parent id that does NOT exist in the store, the result is empty.
    // Implementation does not validate parent existence — it just
    // filters m_tasks for entries whose parentId() matches. So the
    // emptiness depends entirely on no orphan having the same
    // (fresh-UUID) parent_id. Using a freshly-generated UUID guarantees
    // that.
    // -----------------------------------------------------------------------
    void subtasks_query_with_invalid_parent_id_returns_empty()
    {
        Fixture *f = makeFixture();

        // Populate with a couple of unrelated tasks.
        const QString aId = addTask(f->store, QStringLiteral("A"), f->projP1Id);
        const QString bId = addTask(f->store, QStringLiteral("B"), f->projP1Id);
        setTaskParent(f->store, bId, aId);

        // Generate a fresh UUID that no task in this store has ever used.
        const QString bogus = QUuid::createUuid().toString(QUuid::WithoutBraces);

        // No task points at `bogus`, so subtasks(bogus) is empty.
        const auto subs = f->store->subtasks(bogus);
        QCOMPARE(subs.size(), 0);

        // Empty-string arg: matches all tasks with empty parent_id, i.e.
        // every top-level task (Inbox project root + the test task A).
        // Pin that the implementation does NOT special-case "" — that's
        // the historical behaviour and several callers rely on it (e.g.
        // tasksForToday vs subtasks differentiation in views).
        const auto emptyArg = f->store->subtasks(QString());
        // We added two tasks in P1 (A, B). B has a parent (A), so only A
        // qualifies. Inbox itself has no tasks. So exactly one match.
        QCOMPARE(emptyArg.size(), 1);
        QCOMPARE(emptyArg.first()->id(), aId);

        qInfo("subtasks(\"\") matches all top-level tasks (parent_id is "
              "empty). subtasks(<fresh-uuid>) returns empty. Implementation "
              "is a pure filter — no parent-existence check.");

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestSubtaskDeepHierarchy)
#include "test_subtask_deep_hierarchy.moc"
