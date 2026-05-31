// =============================================================================
// Pin test for iter-night-4: DataStore::removeTask cascades the full
// descendant subtree (depth-N), not just direct children. Before the fix
// grandchildren stayed live with their reminders/comments — those reminders
// kept firing OS-toasts for tasks the user thought were deleted, and the
// orphan grandchildren showed up under a tombstoned parent in any tree
// walker.
//
// This test pins:
//   1. removeTask(A) tombstones A → B → C → D (4 levels).
//   2. Reminders on each level are tombstoned.
//   3. Comments on each level are tombstoned.
//   4. A pre-existing parent_id cycle does NOT hang the walker.
//   5. restoreTask(A) brings the whole subtree back symmetrically.
// =============================================================================

#include <QtTest>
#include <QSet>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/reminder.h"
#include "model/comment.h"

class TestRemoveTaskDeepCascade : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    QString inboxId;

    // Build chain A → B → C → D under Inbox.
    struct Chain {
        QString aId, bId, cId, dId;
        QString reminderAId, reminderBId, reminderCId, reminderDId;
        QString commentAId, commentBId, commentCId, commentDId;
    };
    Chain seedDeepChain() {
        Chain ch;
        Task *a = store->addTask(QStringLiteral("A"), inboxId);
        Task *b = store->addTask(QStringLiteral("B"), inboxId);
        Task *c = store->addTask(QStringLiteral("C"), inboxId);
        Task *d = store->addTask(QStringLiteral("D"), inboxId);
        b->setParentId(a->id());
        c->setParentId(b->id());
        d->setParentId(c->id());
        ch.aId = a->id(); ch.bId = b->id(); ch.cId = c->id(); ch.dId = d->id();

        // One reminder + comment per level.
        ch.reminderAId = store->addReminder(a->id(), QStringLiteral("relative"))->id();
        ch.reminderBId = store->addReminder(b->id(), QStringLiteral("relative"))->id();
        ch.reminderCId = store->addReminder(c->id(), QStringLiteral("relative"))->id();
        ch.reminderDId = store->addReminder(d->id(), QStringLiteral("relative"))->id();
        ch.commentAId  = store->addComment(QStringLiteral("cA"), a->id(), inboxId)->id();
        ch.commentBId  = store->addComment(QStringLiteral("cB"), b->id(), inboxId)->id();
        ch.commentCId  = store->addComment(QStringLiteral("cC"), c->id(), inboxId)->id();
        ch.commentDId  = store->addComment(QStringLiteral("cD"), d->id(), inboxId)->id();
        return ch;
    }

private slots:
    void init() {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
    }
    void cleanup() { delete store; store = nullptr; }

    void removeTask_cascadesDepth4_subtree()
    {
        Chain ch = seedDeepChain();

        QVERIFY(store->removeTask(ch.aId));

        // All four task levels must be tombstoned, not just A + B.
        QVERIFY(store->taskById(ch.aId)->isDeleted());
        QVERIFY(store->taskById(ch.bId)->isDeleted());
        QVERIFY2(store->taskById(ch.cId)->isDeleted(),
                 "Grandchild C must be cascade-tombstoned (was a depth-1 bug)");
        QVERIFY2(store->taskById(ch.dId)->isDeleted(),
                 "Great-grandchild D must be cascade-tombstoned");
    }

    void removeTask_cascadesRemindersAndCommentsForEveryLevel()
    {
        Chain ch = seedDeepChain();

        QVERIFY(store->removeTask(ch.aId));

        const QList<Reminder *> reminders = store->allRemindersIncludingDeleted();
        QMap<QString, bool> deleted;
        for (const Reminder *r : reminders) deleted[r->id()] = r->isDeleted();
        QVERIFY(deleted.value(ch.reminderAId));
        QVERIFY(deleted.value(ch.reminderBId));
        QVERIFY2(deleted.value(ch.reminderCId),
                 "Grandchild reminder must be tombstoned (else fires OS toast for invisible task)");
        QVERIFY2(deleted.value(ch.reminderDId),
                 "Great-grandchild reminder must be tombstoned");

        // Comments: query through commentsForTask (which filters tombstones).
        QVERIFY(store->commentsForTask(ch.aId).isEmpty());
        QVERIFY(store->commentsForTask(ch.bId).isEmpty());
        QVERIFY2(store->commentsForTask(ch.cId).isEmpty(),
                 "Grandchild comments must be cascade-tombstoned");
        QVERIFY2(store->commentsForTask(ch.dId).isEmpty(),
                 "Great-grandchild comments must be cascade-tombstoned");
    }

    void removeTask_handlesPreExistingCycle_doesNotHang()
    {
        // Build A → B → A cycle (defensive — applyCommit guards against this
        // but legacy state.json files could carry one). The BFS walker must
        // terminate via the visited set, not loop forever.
        Task *a = store->addTask(QStringLiteral("A"), inboxId);
        Task *b = store->addTask(QStringLiteral("B"), inboxId);
        b->setParentId(a->id());
        a->setParentId(b->id());

        // If the walker hangs this never returns; QtTest will time out.
        QVERIFY(store->removeTask(a->id()));
        QVERIFY(store->taskById(a->id())->isDeleted());
        QVERIFY(store->taskById(b->id())->isDeleted());
    }

    void restoreTask_revivesDepth4_subtree()
    {
        Chain ch = seedDeepChain();
        QVERIFY(store->removeTask(ch.aId));
        QVERIFY(store->restoreTask(ch.aId));

        // Tasks: all four levels alive again.
        QVERIFY(!store->taskById(ch.aId)->isDeleted());
        QVERIFY(!store->taskById(ch.bId)->isDeleted());
        QVERIFY2(!store->taskById(ch.cId)->isDeleted(),
                 "restoreTask must symmetrically un-tombstone grandchildren");
        QVERIFY2(!store->taskById(ch.dId)->isDeleted(),
                 "restoreTask must symmetrically un-tombstone great-grandchildren");

        // Reminders + comments for every level revived.
        const auto reminders = store->allRemindersIncludingDeleted();
        QMap<QString, bool> deleted;
        for (const Reminder *r : reminders) deleted[r->id()] = r->isDeleted();
        QVERIFY(!deleted.value(ch.reminderAId));
        QVERIFY(!deleted.value(ch.reminderBId));
        QVERIFY(!deleted.value(ch.reminderCId));
        QVERIFY(!deleted.value(ch.reminderDId));
        QCOMPARE(store->commentsForTask(ch.aId).size(), 1);
        QCOMPARE(store->commentsForTask(ch.bId).size(), 1);
        QCOMPARE(store->commentsForTask(ch.cId).size(), 1);
        QCOMPARE(store->commentsForTask(ch.dId).size(), 1);
    }

    // night iter-137: restoring a cascade-deleted CHILD alone (parent still in
    // Trash) must not leave it an invisible orphan. A subtask renders only
    // nested under its parent, so a restored child of a still-deleted parent
    // would show nowhere. restoreTask promotes it to top-level (clears parentId)
    // so the restore is actually visible.
    void restoreTask_childAlone_whenParentStillDeleted_promotesToTopLevel()
    {
        Task *a = store->addTask(QStringLiteral("Parent"), inboxId);
        Task *b = store->addTask(QStringLiteral("Child"),  inboxId);
        b->setParentId(a->id());
        const QString aId = a->id(), bId = b->id();

        QVERIFY(store->removeTask(aId));              // cascade: A + B tombstoned
        QVERIFY(store->taskById(aId)->isDeleted());
        QVERIFY(store->taskById(bId)->isDeleted());

        QVERIFY(store->restoreTask(bId));             // restore the CHILD alone

        QVERIFY(!store->taskById(bId)->isDeleted());
        QVERIFY2(store->taskById(bId)->parentId().isEmpty(),
                 "Restored child of a still-deleted parent must be promoted to "
                 "top-level (parentId cleared), else it renders nowhere");
        QVERIFY2(store->taskById(aId)->isDeleted(),
                 "Restoring the child alone must NOT resurrect the parent");
    }

    // Control: restoring the PARENT cascade-restores the child WITH its parent
    // link intact — the orphan guard must not over-reach and strip a link whose
    // parent is being revived in the same call.
    void restoreTask_parentFirst_keepsChildNested()
    {
        Task *a = store->addTask(QStringLiteral("Parent"), inboxId);
        Task *b = store->addTask(QStringLiteral("Child"),  inboxId);
        b->setParentId(a->id());
        const QString aId = a->id(), bId = b->id();

        QVERIFY(store->removeTask(aId));
        QVERIFY(store->restoreTask(aId));             // restore the PARENT

        QVERIFY(!store->taskById(aId)->isDeleted());
        QVERIFY(!store->taskById(bId)->isDeleted());
        QCOMPARE(store->taskById(bId)->parentId(), aId);   // link preserved
    }
};

QTEST_GUILESS_MAIN(TestRemoveTaskDeepCascade)
#include "test_remove_task_deep_cascade.moc"
