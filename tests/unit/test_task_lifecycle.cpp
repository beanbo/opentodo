// ============================================================================
// test_task_lifecycle.cpp
//
// Depth-first integration test: walks ONE task through every state transition
// (create → edit → priority → due → deadline → labels → move → pin → comment →
//  reminder → subtask → complete → reopen → soft-delete → restore →
//  promote subtask → permanent purge).
//
// Complementary to test_mega_integration.cpp (breadth-first across the API).
// The point here is to catch *state-transition* regressions: signal emission,
// field mutation, JSON round-trip stability, query consistency at each step.
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QSettings>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QDebug>

#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/reminder.h"
#include "model/comment.h"

#include "storage/datastore.h"

#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"

// ============================================================================
// TestTaskLifecycle
// ============================================================================
//
// Single test slot — runFullLifecycle — sequentially exercises every
// transition the user can drive on a real task. Each step:
//   1. mutates the task,
//   2. asserts the field reflects the change,
//   3. verifies a taskChanged signal was emitted (where applicable),
//   4. checks that the task is still findable until step 18 hard-purges it.
//
// Two JSON round-trips (midpoint after step 9, end before step 18) verify
// nothing is silently dropped from persistence at the most informative moments.
//
// The test is a single slot because the steps are inherently sequential; using
// QSignalSpy across data() slots is more brittle than a linear narrative with
// `qInfo() << "STEP X: ..."` between phases.
// ============================================================================
class TestTaskLifecycle : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Unique org/app so this test never touches real OpenTodo settings
        // and does not collide with other parallel tests in the suite.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoLifecycleTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_task_lifecycle"));
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void runFullLifecycle()
    {
        qInfo() << "STEP 0: bootstrap (DataStore + ViewModels + Inbox project)";

        DataStore store;
        TaskViewModel taskVm(&store);
        ProjectViewModel projectVm(&store);

        // Create the canonical Inbox plus a destination project for step 8 ("Move").
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        QVERIFY(inbox);
        inbox->setIsInbox(true);
        const QString inboxId = inbox->id();

        const QString workId = projectVm.addProject(QStringLiteral("Work"));
        QVERIFY(!workId.isEmpty());
        QVERIFY(store.projectById(workId) != nullptr);

        // Pre-create two labels so step 7 can attach by name (Task::labels stores
        // label *names*, matching Todoist's wire format — see Task::setLabels).
        Label *labA = store.addLabel(QStringLiteral("urgent"));
        Label *labB = store.addLabel(QStringLiteral("review"));
        QVERIFY(labA && labB);

        // QSignalSpy on the VM's per-task signal (the canonical "something changed
        // for this task" channel). We sample its count before/after each phase.
        QSignalSpy taskChangedSpy(&taskVm, &TaskViewModel::taskChanged);

        // --------------------------------------------------------------------
        qInfo() << "STEP 1: create";
        // --------------------------------------------------------------------
        const QString id = taskVm.addTask(QStringLiteral("Lifecycle task"), inboxId);
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QCOMPARE(t->content(), QStringLiteral("Lifecycle task"));
        QCOMPARE(t->projectId(), inboxId);
        QVERIFY(!t->isCompleted());
        QVERIFY(!t->isDeleted());
        QCOMPARE(store.allTasks().size(), 1);

        // --------------------------------------------------------------------
        qInfo() << "STEP 2: edit content";
        // --------------------------------------------------------------------
        int spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.updateTask(id, QStringLiteral("content"),
                                  QStringLiteral("Lifecycle task — edited")));
        QCOMPARE(t->content(), QStringLiteral("Lifecycle task — edited"));
        QVERIFY(taskChangedSpy.count() > spyBefore);
        QVERIFY(store.taskById(id) != nullptr);

        // --------------------------------------------------------------------
        qInfo() << "STEP 3: edit description";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.updateTask(id, QStringLiteral("description"),
                                  QStringLiteral("Detailed description for the lifecycle task.")));
        QCOMPARE(t->description(),
                 QStringLiteral("Detailed description for the lifecycle task."));
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 4: set priority P1 (priority value = 4 — Todoist convention)";
        // --------------------------------------------------------------------
        // Internal scale: 4 = P1 (highest), 1 = P4 (lowest); see TaskViewModel docs.
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.setTaskPriority(id, 4));
        QCOMPARE(t->priority(), 4);
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 5: set due date today";
        // --------------------------------------------------------------------
        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.setTaskDue(id, today));
        QCOMPARE(t->dueDate(), today);
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 6: set deadline tomorrow";
        // --------------------------------------------------------------------
        const QString tomorrow = QDate::currentDate().addDays(1)
                                     .toString(QStringLiteral("yyyy-MM-dd"));
        spyBefore = taskChangedSpy.count();
        // No VM helper for deadline on a single task; mutate the model directly
        // and notify the store (mirrors how the editor's deadline picker wires up).
        t->setDeadline(tomorrow);
        store.markTaskModified(id);
        // markTaskModified does not emit VM-level taskChanged, so we don't assert
        // on the spy here — the mutation itself is the contract. We do still
        // require that the field is observable.
        QCOMPARE(t->deadline(), tomorrow);
        QVERIFY(store.taskById(id) != nullptr);

        // --------------------------------------------------------------------
        qInfo() << "STEP 7: add 2 labels (in order added)";
        // --------------------------------------------------------------------
        const QStringList wantedLabels{QStringLiteral("urgent"),
                                       QStringLiteral("review")};
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.setTaskLabels(id, wantedLabels));
        QCOMPARE(t->labels(), wantedLabels);
        QCOMPARE(t->labels().at(0), QStringLiteral("urgent"));
        QCOMPARE(t->labels().at(1), QStringLiteral("review"));
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 8: move to project Work";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.moveTask(id, workId));
        QCOMPARE(t->projectId(), workId);
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 9: pin";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.updateTask(id, QStringLiteral("isPinned"), true));
        QVERIFY(t->isPinned());
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "MIDPOINT: JSON round-trip preserves all changes so far";
        // --------------------------------------------------------------------
        {
            const QJsonObject snapshot = store.toJson();
            DataStore freshStore;
            freshStore.loadFromJson(snapshot);
            Task *roundTripped = freshStore.taskById(id);
            QVERIFY(roundTripped != nullptr);
            QCOMPARE(roundTripped->content(),
                     QStringLiteral("Lifecycle task — edited"));
            QCOMPARE(roundTripped->description(),
                     QStringLiteral("Detailed description for the lifecycle task."));
            QCOMPARE(roundTripped->priority(), 4);
            QCOMPARE(roundTripped->dueDate(), today);
            QCOMPARE(roundTripped->deadline(), tomorrow);
            QCOMPARE(roundTripped->labels(), wantedLabels);
            QCOMPARE(roundTripped->projectId(), workId);
            QVERIFY(roundTripped->isPinned());
            QVERIFY(!roundTripped->isCompleted());
            QVERIFY(!roundTripped->isDeleted());
        }

        // --------------------------------------------------------------------
        qInfo() << "STEP 10: add comment";
        // --------------------------------------------------------------------
        QSignalSpy commentsSpy(&store, &DataStore::commentsChanged);
        Comment *cm = store.addComment(QStringLiteral("First lifecycle comment"),
                                       id, QString());
        QVERIFY(cm != nullptr);
        const QString commentId = cm->id();
        QCOMPARE(store.commentsForTask(id).size(), 1);
        QCOMPARE(store.commentsForTask(id).first()->content(),
                 QStringLiteral("First lifecycle comment"));
        QVERIFY(commentsSpy.count() >= 1);
        QVERIFY(store.taskById(id) != nullptr);

        // --------------------------------------------------------------------
        qInfo() << "STEP 11: add reminder";
        // --------------------------------------------------------------------
        QSignalSpy remindersSpy(&store, &DataStore::remindersChanged);
        Reminder *rm = store.addReminder(id, QStringLiteral("absolute"));
        QVERIFY(rm != nullptr);
        const QString reminderId = rm->id();
        rm->setDueDatetime(QDateTime::currentDateTimeUtc().addSecs(3600)
                               .toString(Qt::ISODate));
        QCOMPARE(store.remindersForTask(id).size(), 1);
        QCOMPARE(store.remindersForTask(id).first()->id(), reminderId);
        QVERIFY(remindersSpy.count() >= 1);

        // --------------------------------------------------------------------
        qInfo() << "STEP 12: add subtask";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        const QString subId = taskVm.addSubtask(id, QStringLiteral("Lifecycle subtask"));
        QVERIFY(!subId.isEmpty());
        Task *sub = store.taskById(subId);
        QVERIFY(sub != nullptr);
        QCOMPARE(sub->parentId(), id);
        const QList<Task *> kids = store.subtasks(id);
        QCOMPARE(kids.size(), 1);
        QCOMPARE(kids.first()->id(), subId);
        QVERIFY(taskVm.hasSubtasks(id));

        // --------------------------------------------------------------------
        qInfo() << "STEP 13: complete";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        // Task carries no recurring rule, so completeTask actually completes
        // (rather than advancing the due date as it would for a recurring task).
        QVERIFY(taskVm.completeTask(id));
        QVERIFY(t->isCompleted());
        QVERIFY(t->completedAt().isValid());
        QVERIFY(taskChangedSpy.count() > spyBefore);
        // Still findable while soft-deletion has not happened.
        QVERIFY(store.taskById(id) != nullptr);

        // --------------------------------------------------------------------
        qInfo() << "STEP 14: reopen (un-complete) — completedAt is cleared by Task::setIsCompleted(false)";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.uncompleteTask(id));
        QVERIFY(!t->isCompleted());
        // Task::setIsCompleted(false) explicitly resets m_completedAt to default —
        // verify the documented behavior so any future change to that contract
        // breaks this test loudly.
        QVERIFY(!t->completedAt().isValid());
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 15: soft-delete (move to trash)";
        // --------------------------------------------------------------------
        QVERIFY(taskVm.deleteTask(id));
        QVERIFY(t->isDeleted());

        // allTasks() filters out tombstones; deletedTasks() / trash listing should
        // expose it.
        const auto live = store.allTasks();
        bool foundLive = false;
        for (const Task *lt : live) {
            if (lt && lt->id() == id) { foundLive = true; break; }
        }
        QVERIFY(!foundLive);

        const QVariantList trashed = taskVm.deletedTasks();
        bool foundInTrash = false;
        for (const QVariant &v : trashed) {
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                foundInTrash = true;
                break;
            }
        }
        QVERIFY(foundInTrash);
        // Soft-delete keeps the QObject alive — tombstones must remain findable.
        QVERIFY(store.taskById(id) != nullptr);

        // --------------------------------------------------------------------
        qInfo() << "STEP 16: restore from trash";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        taskVm.restoreTask(id);
        QVERIFY(!t->isDeleted());

        const auto liveAfterRestore = store.allTasks();
        bool foundAfter = false;
        for (const Task *lt : liveAfterRestore) {
            if (lt && lt->id() == id) { foundAfter = true; break; }
        }
        QVERIFY(foundAfter);
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "STEP 17: promote subtask to standalone";
        // --------------------------------------------------------------------
        spyBefore = taskChangedSpy.count();
        QVERIFY(taskVm.promoteTask(subId));
        QVERIFY(sub->parentId().isEmpty());
        // Parent's child list should now be empty.
        QCOMPARE(store.subtasks(id).size(), 0);
        QVERIFY(taskChangedSpy.count() > spyBefore);

        // --------------------------------------------------------------------
        qInfo() << "FINAL ROUND-TRIP: JSON snapshot before purge";
        // --------------------------------------------------------------------
        {
            const QJsonObject snapshot = store.toJson();
            DataStore freshStore;
            freshStore.loadFromJson(snapshot);
            Task *finalRt = freshStore.taskById(id);
            QVERIFY(finalRt != nullptr);
            // After the full ride: no longer completed, not deleted, all the
            // earlier mutations still present.
            QVERIFY(!finalRt->isCompleted());
            QVERIFY(!finalRt->isDeleted());
            QCOMPARE(finalRt->priority(), 4);
            QCOMPARE(finalRt->labels(), wantedLabels);
            QCOMPARE(finalRt->projectId(), workId);
            QVERIFY(finalRt->isPinned());
            // The promoted child is now a top-level task in the fresh store too.
            Task *finalSub = freshStore.taskById(subId);
            QVERIFY(finalSub != nullptr);
            QVERIFY(finalSub->parentId().isEmpty());
            // Comment + reminder must survive the soft-delete → restore cycle
            // and the full JSON round-trip. The cascade in DataStore::removeTask
            // tombstones comments/reminders, and DataStore::restoreTask
            // reverses that cascade by matching updatedAt timestamps. So the
            // single comment added in STEP 10 and the single reminder added
            // in STEP 11 should both be live in the fresh store.
            QCOMPARE(freshStore.commentsForTask(id).size(), 1);
            QCOMPARE(freshStore.remindersForTask(id).size(), 1);
        }

        // --------------------------------------------------------------------
        qInfo() << "STEP 18: permanently delete (hard-purge)";
        // --------------------------------------------------------------------
        // Soft-delete first — permanentlyDelete() expects the task to exist in
        // the store. The earlier restore put it back into live state.
        QVERIFY(taskVm.deleteTask(id));
        QVERIFY(t->isDeleted());
        QVERIFY(store.taskById(id) != nullptr);

        taskVm.permanentlyDelete(id);
        QVERIFY(store.taskById(id) == nullptr);
        // After purge, trashCount no longer counts this task.
        const QVariantList finalTrash = taskVm.deletedTasks();
        for (const QVariant &v : finalTrash) {
            QVERIFY(v.toMap().value(QStringLiteral("id")).toString() != id);
        }

        qInfo() << "DONE: full lifecycle traversal complete";
    }
};

QTEST_GUILESS_MAIN(TestTaskLifecycle)
#include "test_task_lifecycle.moc"
