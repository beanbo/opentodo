// =============================================================================
// Iter-night-13 pin: TaskViewModel::completeTask must register an undo entry
// for the standard-completion path even when the task was flagged recurring
// but the engine couldn't advance the rule.
//
// Before the fix, the undo registration at the top of completeTask was
// gated on `!task->dueIsRecurring()`. The branch below it tried to advance
// the recurrence and either:
//   (a) succeeded → set due_date forward, returned → no undo (intentional)
//   (b) failed (UNTIL exhausted, garbage RRULE, unparseable dueString from
//       a legacy import) → fell through to the standard completion path,
//       which mutated `is_completed=true` with NO undo entry pushed
//
// The fall-through path is the bug: the task IS being completed, the user
// SHOULD be able to Ctrl+Z, but the top-of-function gate locked out the
// undo registration. The most user-visible trigger is a recurring task
// with an UNTIL=past-date constraint — when the last legitimate occurrence
// fires, advance returns invalid and the task quietly transitions to
// "completed" with no recovery option.
//
// This pin file locks in the post-fix shape:
//   1. Recurring + unparseable rule → fall-through → task is completed
//      AND undo is registered AND undo restores incomplete state.
//   2. Recurring + parseable rule → advance succeeds → task is NOT
//      "completed" (due date moved forward instead) AND undo is NOT
//      registered (preserving the existing semantic).
//   3. Non-recurring task → standard completion + undo entry, as before.
// =============================================================================

#include <QtTest>
#include <QString>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"
#include "services/undostack.h"

class TestCompleteTaskUndo : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    TaskViewModel *vm = nullptr;
    UndoStack *undo = nullptr;
    QString inboxId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();

        vm = new TaskViewModel(store);
        undo = new UndoStack;
        vm->setUndoStack(undo);
    }

    void cleanup()
    {
        delete vm; vm = nullptr;
        delete undo; undo = nullptr;
        delete store; store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Non-recurring completion pushes undo and Ctrl+Z restores the task.
    //    Regression guard — was already correct pre-fix and must stay so.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_nonRecurring_pushesUndoAndCtrlZRestores()
    {
        const QString id = vm->addTask(QStringLiteral("Buy milk"), inboxId,
                                       0, QString(), QStringList());
        QVERIFY(!id.isEmpty());
        Task *t = store->taskById(id);
        QVERIFY(t && !t->isCompleted());

        QCOMPARE(undo->count(), 0);
        QVERIFY(vm->completeTask(id));
        QVERIFY(t->isCompleted());
        QCOMPARE(undo->count(), 1);

        QVERIFY(undo->undo());
        QVERIFY2(!t->isCompleted(),
                 "Non-recurring completeTask undo must restore the task to "
                 "incomplete (calls uncompleteTask under the hood).");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Recurring task with a parseable RRULE: advance succeeds, due date
    //    moves forward, task is NOT marked completed, no undo entry is
    //    pushed. Pin the existing "no undo on advance" semantic so the
    //    iter-night-13 fix doesn't accidentally extend undo into the
    //    advance branch.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_recurringValidAdvance_noUndoNoCompletion()
    {
        const QString id = vm->addTask(QStringLiteral("Stand-up"), inboxId,
                                       0, QString(), QStringList());
        Task *t = store->taskById(id);
        QVERIFY(t);
        // Set up a recurring task with a valid daily RRULE.
        t->setDueDate(QStringLiteral("2026-05-04"));
        t->setDueString(QStringLiteral("every day"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY"));
        t->setDueIsRecurring(true);
        QVERIFY(t->dueIsRecurring());

        QCOMPARE(undo->count(), 0);
        QVERIFY(vm->completeTask(id));

        QVERIFY2(!t->isCompleted(),
                 "Recurring task with a valid advance MUST NOT be marked "
                 "completed — the date is bumped forward instead.");
        QVERIFY2(t->dueDate() > QStringLiteral("2026-05-04"),
                 "Recurring advance must move due_date forward.");
        QCOMPARE(undo->count(), 0); // no undo on advance — pinned semantic
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) THE BUG: recurring task with an UNPARSEABLE rule. The advance
    //    returns invalid, completeTask falls through to standard
    //    completion. Pre-fix: NO undo entry (top-of-function gate excluded
    //    every recurring task). Post-fix: undo entry IS registered and
    //    Ctrl+Z restores incomplete state.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_recurringUnparseableRule_pushesUndoAndCtrlZRestores()
    {
        const QString id = vm->addTask(QStringLiteral("Half-broken recurring"),
                                       inboxId, 0, QString(), QStringList());
        Task *t = store->taskById(id);
        QVERIFY(t);
        // Recurring flag + dueString that triggers the recurring branch,
        // but the RRULE is garbage (`FREQ=WIBBLE` → recurrenceengine.cpp:287
        // returns QDate() / invalid). The fall-through path runs.
        t->setDueDate(QStringLiteral("2026-05-04"));
        t->setDueString(QStringLiteral("garbage every fortnight"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=WIBBLE"));
        t->setDueIsRecurring(true);

        QCOMPARE(undo->count(), 0);
        QVERIFY(vm->completeTask(id));

        // Fall-through path completed the task.
        QVERIFY2(t->isCompleted(),
                 "Recurring task with an unparseable rule should fall "
                 "through to standard completion (task IS completed).");

        // Was the bug — was 0 before iter-night-13 because the top gate
        // `!task->dueIsRecurring()` blocked the undo push.
        QCOMPARE(undo->count(), 1);

        QVERIFY(undo->undo());
        QVERIFY2(!t->isCompleted(),
                 "Iter-night-13: undo of recurring-fall-through completion "
                 "must restore the task to incomplete state. Pre-fix this "
                 "path had no undo entry at all and the user lost the "
                 "ability to recover from accidental Ctrl+Click on a "
                 "recurring task whose rule had run out.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Recurring task whose RRULE has UNTIL=<past>. This is the most
    //    real-world reproducible trigger: the user's recurring task
    //    legitimately terminated, the last instance fires, advance returns
    //    invalid, the task completes — and pre-fix the user couldn't undo.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_recurringWithUntilExhausted_pushesUndo()
    {
        const QString id = vm->addTask(QStringLiteral("Series ended"),
                                       inboxId, 0, QString(), QStringList());
        Task *t = store->taskById(id);
        QVERIFY(t);
        // FREQ=DAILY but UNTIL has already passed. Advance returns invalid;
        // fall-through fires.
        t->setDueDate(QStringLiteral("2024-01-15"));
        t->setDueString(QStringLiteral("every day until Jan 1 2024"));
        t->setDueRecurrenceRule(
            QStringLiteral("FREQ=DAILY;UNTIL=20240101T000000Z"));
        t->setDueIsRecurring(true);

        QCOMPARE(undo->count(), 0);
        QVERIFY(vm->completeTask(id));
        QVERIFY(t->isCompleted());
        QCOMPARE(undo->count(), 1);

        QVERIFY(undo->undo());
        QVERIFY(!t->isCompleted());
    }
};

QTEST_GUILESS_MAIN(TestCompleteTaskUndo)
#include "test_complete_task_undo.moc"
