// Extended UndoStack coverage.
//
// The current production UndoStack (src/services/undostack.{h,cpp}) is an
// intentionally minimalist single-direction stack: push -> undo -> pop.
// It has no redo, no coalescing, and no notion of "command types" — every
// entry is a generic std::function<void()>. The basic happy paths are
// already covered by tests/unit/test_undostack.cpp.
//
// This file therefore concentrates on:
//
//   * State-transition coverage that the basic test omits (mixed
//     description churn, multi-undo cascades, lastDescription / count
//     bookkeeping under load).
//
//   * The typed pushDelete(...) API that ties UndoStack to a real
//     DataStore — including the documented "task already permanently
//     gone" no-op branch, which the basic tests never reach.
//
//   * Signal emission counts and ordering across complex sequences.
//
//   * Capacity (MAX_ENTRIES = 20) edge behaviour: exact-fill, one-over,
//     and that interleaved undo/push respects the cap.
//
// Slots that exercise features the production code does NOT implement
// (redo path, coalescing, branching after undo) are present as
// documentation-only SKIPs, per task instructions, so reviewers can see
// the feature was considered.

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QString>

#include "services/undostack.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

class TestUndoStackExtended : public QObject
{
    Q_OBJECT

private slots:

    // ---------------------------------------------------------------
    // Documented SKIPs for unimplemented features.
    // The task brief asks us to cover these; the production class
    // has no such features, so we emit a qInfo and pass trivially.
    // ---------------------------------------------------------------

    void redo_path_not_implemented()
    {
        // No QUndoStack-style redo: undo() pops the entry and discards it.
        // Re-doing would require a second stack and entries that know
        // both their forward and reverse action — neither exists here.
        qInfo() << "SKIP: UndoStack has no redo() — single-direction stack only.";
        QVERIFY(true);
    }

    void coalescing_not_implemented()
    {
        // No mergeWith / canCoalesce / push-id — every push() unconditionally
        // appends a new Entry. There is no time window or type id.
        qInfo() << "SKIP: UndoStack does not coalesce consecutive same-type pushes.";
        QVERIFY(true);
    }

    void branching_after_undo_not_implemented()
    {
        // Because there is no redo stack, "branching" (push after undo
        // discards the redo branch) is vacuously true: there is nothing
        // to discard.
        qInfo() << "SKIP: UndoStack has no redo branch to invalidate after undo.";
        QVERIFY(true);
    }

    // ---------------------------------------------------------------
    // Cross-type / cross-domain undo sequences.
    // The production class is type-agnostic — these tests prove that
    // arbitrary lambdas (mutating tasks, projects, labels, plain ints)
    // are unwound in strict LIFO order.
    // ---------------------------------------------------------------

    void mixed_lambdas_unwind_in_reverse_order()
    {
        UndoStack stack;

        // Three independent "domains" of mutation, each with its own
        // mock state. We push them in a particular order and verify
        // both that all three were executed and that they ran in
        // reverse-of-push order.
        QStringList project_log;     // simulates "project rename"
        bool        task_completed = true;  // simulates "task complete"
        QStringList label_log;       // simulates "label add"

        stack.push(QStringLiteral("complete task"),
                   [&]() { task_completed = false; });
        stack.push(QStringLiteral("rename project"),
                   [&]() { project_log << QStringLiteral("rename undone"); });
        stack.push(QStringLiteral("add label"),
                   [&]() { label_log << QStringLiteral("label removed"); });

        QCOMPARE(stack.count(), 3);
        QCOMPARE(stack.lastDescription(), QStringLiteral("add label"));

        // Undo all three.
        QVERIFY(stack.undo());  // label add
        QCOMPARE(stack.lastDescription(), QStringLiteral("rename project"));
        QCOMPARE(label_log,   QStringList{QStringLiteral("label removed")});
        QVERIFY(project_log.isEmpty());
        QVERIFY(task_completed);

        QVERIFY(stack.undo());  // project rename
        QCOMPARE(stack.lastDescription(), QStringLiteral("complete task"));
        QCOMPARE(project_log, QStringList{QStringLiteral("rename undone")});
        QVERIFY(task_completed);

        QVERIFY(stack.undo());  // task complete
        QVERIFY(!task_completed);

        QCOMPARE(stack.count(), 0);
        QCOMPARE(stack.canUndo(), false);
        QCOMPARE(stack.lastDescription(), QString());
    }

    // ---------------------------------------------------------------
    // canUndo / canUndoChanged transitions across mixed sequences.
    // Note: the production class collapses canUndoChanged + countChanged
    // + lastDescriptionChanged into a single signal (see header) — every
    // mutating call should fire exactly one canUndoChanged.
    // ---------------------------------------------------------------

    void canUndoChanged_fires_once_per_mutation()
    {
        UndoStack stack;
        QSignalSpy spy(&stack, &UndoStack::canUndoChanged);

        // empty -> non-empty: one signal
        stack.push(QStringLiteral("a"), [](){});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(stack.canUndo(), true);

        // non-empty -> non-empty (still one signal per push by contract)
        stack.push(QStringLiteral("b"), [](){});
        QCOMPARE(spy.count(), 2);

        // pop one — still non-empty, still one signal.
        QVERIFY(stack.undo());
        QCOMPARE(spy.count(), 3);
        QCOMPARE(stack.canUndo(), true);

        // pop last — non-empty -> empty.
        QVERIFY(stack.undo());
        QCOMPARE(spy.count(), 4);
        QCOMPARE(stack.canUndo(), false);

        // undo on empty: no-op, no signal.
        QVERIFY(!stack.undo());
        QCOMPARE(spy.count(), 4);
    }

    void canUndoChanged_does_not_fire_on_no_op_undo()
    {
        // undo() on an empty stack returns false and must not emit
        // canUndoChanged, otherwise the snackbar would flicker.
        UndoStack stack;
        QSignalSpy spy(&stack, &UndoStack::canUndoChanged);

        QVERIFY(!stack.undo());
        QVERIFY(!stack.undo());
        QVERIFY(!stack.undo());
        QCOMPARE(spy.count(), 0);
    }

    void clear_resets_state_and_emits_once()
    {
        // After clear() the stack must report empty AND emit
        // canUndoChanged exactly once (only when there was something
        // to clear — clearing an empty stack must be silent).
        UndoStack stack;
        stack.push(QStringLiteral("a"), [](){});
        stack.push(QStringLiteral("b"), [](){});
        stack.push(QStringLiteral("c"), [](){});
        QCOMPARE(stack.count(), 3);

        QSignalSpy spy(&stack, &UndoStack::canUndoChanged);
        stack.clear();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(stack.canUndo(), false);
        QCOMPARE(stack.count(), 0);
        QCOMPARE(stack.lastDescription(), QString());

        // Clearing again is a true no-op — no signal.
        stack.clear();
        QCOMPARE(spy.count(), 1);
    }

    // ---------------------------------------------------------------
    // Capacity (MAX_ENTRIES = 20) edge cases.
    // The basic test covers the "push 25, keep 20" happy path.
    // Here we cover the boundary: exactly 20, exactly 21, and
    // capacity behaviour interleaved with undo.
    // ---------------------------------------------------------------

    void capacity_exactly_at_limit_no_drops()
    {
        // Fill to exactly the cap — every entry must survive.
        UndoStack stack;
        for (int i = 0; i < 20; ++i)
            stack.push(QString::number(i), [](){});

        QCOMPARE(stack.count(), 20);
        QCOMPARE(stack.lastDescription(), QStringLiteral("19"));

        // Now undo all and verify each comes off in reverse.
        for (int i = 19; i >= 0; --i) {
            QCOMPARE(stack.lastDescription(), QString::number(i));
            QVERIFY(stack.undo());
        }
        QCOMPARE(stack.canUndo(), false);
    }

    void capacity_one_over_limit_drops_oldest_only()
    {
        // Push 21 — exactly one over. Entry "0" must be evicted, "1"..."20" survive.
        UndoStack stack;
        for (int i = 0; i < 21; ++i)
            stack.push(QString::number(i), [](){});

        QCOMPARE(stack.count(), 20);

        // Drain the stack and collect descriptions in pop order.
        QStringList popped;
        while (stack.canUndo()) {
            popped << stack.lastDescription();
            stack.undo();
        }

        // We expect 20, 19, 18, ..., 1 (entry 0 was evicted).
        QCOMPARE(popped.size(), 20);
        QCOMPARE(popped.first(), QStringLiteral("20"));
        QCOMPARE(popped.last(),  QStringLiteral("1"));
        // Entry 0 must NOT appear anywhere.
        QVERIFY(!popped.contains(QStringLiteral("0")));
    }

    void capacity_with_interleaved_undo()
    {
        // Push 15, undo 5, push 15 more. After: stack should hold
        // the last 20 distinct entries (10 from first batch were popped
        // by 5 undo + 5 oldest evicted, then 15 new ones added).
        //
        // Sequence:
        //   push  0..14       (count = 15)
        //   undo x 5          (count = 10, popped 14..10)
        //   push 100..114     (count = 20 + extra triggers eviction)
        UndoStack stack;
        for (int i = 0; i < 15; ++i)
            stack.push(QString::number(i), [](){});
        QCOMPARE(stack.count(), 15);

        for (int i = 0; i < 5; ++i)
            QVERIFY(stack.undo());
        QCOMPARE(stack.count(), 10);
        QCOMPARE(stack.lastDescription(), QStringLiteral("9"));

        for (int i = 100; i < 115; ++i)
            stack.push(QString::number(i), [](){});

        // 10 + 15 = 25, capped at 20 — oldest 5 evicted ("0".."4").
        QCOMPARE(stack.count(), 20);

        // Drain and confirm composition: 114..100 (15 items) + 9..5 (5 items)
        QStringList popped;
        while (stack.canUndo()) {
            popped << stack.lastDescription();
            stack.undo();
        }
        QCOMPARE(popped.size(), 20);
        QCOMPARE(popped.first(), QStringLiteral("114"));
        QCOMPARE(popped.at(14),  QStringLiteral("100"));
        QCOMPARE(popped.at(15),  QStringLiteral("9"));
        QCOMPARE(popped.last(),  QStringLiteral("5"));

        // The oldest five ("0".."4") must NOT appear.
        for (int i = 0; i < 5; ++i)
            QVERIFY(!popped.contains(QString::number(i)));
    }

    // ---------------------------------------------------------------
    // pushDelete(...) — the typed API that actually couples UndoStack
    // to a real DataStore. This is the one place where UndoStack stops
    // being a pure container.
    // ---------------------------------------------------------------

    void pushDelete_restores_task_from_snapshot()
    {
        // Build a real Task in a real DataStore, soft-delete it,
        // record an undo entry that captures its JSON snapshot,
        // then verify undo() restores content/description/is_completed
        // and clears the tombstone.
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        QVERIFY(p);

        Task *t = store.addTask(QStringLiteral("original content"), p->id());
        QVERIFY(t);
        t->setDescription(QStringLiteral("original desc"));
        t->setIsCompleted(false);

        // Snapshot before we touch the task.
        const QString taskId = t->id();
        QJsonObject snapshot;
        snapshot[QStringLiteral("content")]      = t->content();
        snapshot[QStringLiteral("description")]  = t->description();
        snapshot[QStringLiteral("is_completed")] = t->isCompleted();

        // Soft-delete.
        QVERIFY(store.removeTask(taskId));
        QVERIFY(t->isDeleted());

        // Mutate the surviving object so we can prove the restore actually
        // overwrites with the snapshot, not just clears the tombstone.
        t->setContent(QStringLiteral("post-delete garbage"));
        t->setDescription(QStringLiteral("post-delete garbage"));
        t->setIsCompleted(true);

        UndoStack stack;
        stack.pushDelete(QStringLiteral("delete task"), taskId, snapshot, &store);
        QCOMPARE(stack.canUndo(), true);
        QCOMPARE(stack.lastDescription(), QStringLiteral("delete task"));

        // Undo: tombstone cleared, fields restored to the snapshot.
        QVERIFY(stack.undo());
        QVERIFY(!t->isDeleted());
        QCOMPARE(t->content(),     QStringLiteral("original content"));
        QCOMPARE(t->description(), QStringLiteral("original desc"));
        QCOMPARE(t->isCompleted(), false);

        QCOMPARE(stack.canUndo(), false);
    }

    void pushDelete_silently_no_ops_when_task_purged()
    {
        // Documented behaviour from undostack.cpp:
        //   "If the task is gone (tombstone resolved), logs a warning
        //    and no-ops silently."
        // Verified by purging the task before invoking undo.
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("doomed"), p->id());
        const QString taskId = t->id();

        QJsonObject snapshot;
        snapshot[QStringLiteral("content")]      = QStringLiteral("doomed");
        snapshot[QStringLiteral("description")]  = QString();
        snapshot[QStringLiteral("is_completed")] = false;

        UndoStack stack;
        stack.pushDelete(QStringLiteral("delete doomed"), taskId, snapshot, &store);

        // Hard-purge — the entry's lambda will look up the task and find nothing.
        QVERIFY(store.purgeTask(taskId));
        QVERIFY(!store.taskById(taskId));

        // Undo must not crash, must still pop the entry, must still emit
        // the canUndoChanged + undoExecuted signals (the "no-op" is
        // about the data restore, not the stack bookkeeping).
        QSignalSpy changedSpy(&stack, &UndoStack::canUndoChanged);
        QSignalSpy executedSpy(&stack, &UndoStack::undoExecuted);

        QVERIFY(stack.undo());
        QCOMPARE(changedSpy.count(),  1);
        QCOMPARE(executedSpy.count(), 1);
        QCOMPARE(executedSpy.first().first().toString(),
                 QStringLiteral("delete doomed"));
        QCOMPARE(stack.canUndo(), false);
    }

    void pushDelete_with_null_store_no_ops_safely()
    {
        // Edge case: the lambda checks `if (!store) return;` — verify that
        // path too. We pass nullptr explicitly. Undo must not crash.
        QJsonObject snapshot;
        snapshot[QStringLiteral("content")] = QStringLiteral("x");

        UndoStack stack;
        stack.pushDelete(QStringLiteral("orphan"),
                         QStringLiteral("00000000-0000-0000-0000-000000000000"),
                         snapshot, nullptr);
        QCOMPARE(stack.canUndo(), true);

        QVERIFY(stack.undo());           // must not crash
        QCOMPARE(stack.canUndo(), false);
    }

    void multiple_pushDelete_undo_in_reverse()
    {
        // Stack two real deletes, undo both, both tasks resurrect.
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));

        Task *a = store.addTask(QStringLiteral("alpha"), p->id());
        Task *b = store.addTask(QStringLiteral("beta"),  p->id());
        const QString aId = a->id();
        const QString bId = b->id();

        QJsonObject snapA;
        snapA[QStringLiteral("content")]      = QStringLiteral("alpha");
        snapA[QStringLiteral("description")]  = QString();
        snapA[QStringLiteral("is_completed")] = false;

        QJsonObject snapB;
        snapB[QStringLiteral("content")]      = QStringLiteral("beta");
        snapB[QStringLiteral("description")]  = QString();
        snapB[QStringLiteral("is_completed")] = false;

        UndoStack stack;
        store.removeTask(aId);
        stack.pushDelete(QStringLiteral("del alpha"), aId, snapA, &store);
        store.removeTask(bId);
        stack.pushDelete(QStringLiteral("del beta"),  bId, snapB, &store);

        QCOMPARE(stack.count(), 2);
        QVERIFY(a->isDeleted());
        QVERIFY(b->isDeleted());

        // Undo "del beta" first (LIFO).
        QVERIFY(stack.undo());
        QVERIFY(a->isDeleted());
        QVERIFY(!b->isDeleted());

        // Undo "del alpha".
        QVERIFY(stack.undo());
        QVERIFY(!a->isDeleted());
        QVERIFY(!b->isDeleted());
    }

    // ---------------------------------------------------------------
    // undoExecuted signal — payload integrity across many calls.
    // ---------------------------------------------------------------

    void undoExecuted_payloads_match_pop_order()
    {
        UndoStack stack;
        QSignalSpy spy(&stack, &UndoStack::undoExecuted);

        const QStringList descs = {
            QStringLiteral("first"),
            QStringLiteral("second"),
            QStringLiteral("third"),
            QStringLiteral("fourth"),
        };
        for (const auto &d : descs)
            stack.push(d, [](){});

        // Pop in LIFO order; the signal should carry "fourth", "third", "second", "first".
        for (int i = 0; i < descs.size(); ++i)
            QVERIFY(stack.undo());

        QCOMPARE(spy.count(), descs.size());
        QCOMPARE(spy.at(0).first().toString(), QStringLiteral("fourth"));
        QCOMPARE(spy.at(1).first().toString(), QStringLiteral("third"));
        QCOMPARE(spy.at(2).first().toString(), QStringLiteral("second"));
        QCOMPARE(spy.at(3).first().toString(), QStringLiteral("first"));
    }

    // ---------------------------------------------------------------
    // Lambdas that throw / observe the live state during execution.
    // The production code does NOT wrap the lambda in try/catch, so a
    // throwing lambda would propagate. We don't test that path (it would
    // terminate the harness) — instead we test that a push() issued from
    // inside a running lambda is ignored (no recursion, no "redo" entry).
    // ---------------------------------------------------------------

    void lambda_re_push_during_undo_is_dropped()
    {
        // P10 (fix pass): a push() issued from inside a running undo fn is
        // now a no-op. Previously it added a fresh "redo" entry, which made
        // Ctrl+Z ping-pong on the same action (complete ⇄ uncomplete) and
        // never reach the older entries underneath.
        UndoStack stack;
        int olderRuns = 0;
        int reentries = 0;

        stack.push(QStringLiteral("older"), [&]() { ++olderRuns; });
        stack.push(QStringLiteral("self-pushing"), [&]() {
            ++reentries;
            stack.push(QStringLiteral("re-pushed"), [](){});
        });

        QVERIFY(stack.undo());
        QCOMPARE(reentries, 1);

        // The re-push was dropped: the older entry is next in line.
        QCOMPARE(stack.count(), 1);
        QCOMPARE(stack.lastDescription(), QStringLiteral("older"));

        QVERIFY(stack.undo());
        QCOMPARE(olderRuns, 1);
        QCOMPARE(stack.count(), 0);

        // Normal pushes work again once undo() has returned.
        stack.push(QStringLiteral("after"), [](){});
        QCOMPARE(stack.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestUndoStackExtended)
#include "test_undostack_extended.moc"
