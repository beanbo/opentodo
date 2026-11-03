// =============================================================================
// Storage-layer data-integrity pins (global fix pass F2):
//
//   D2  purgeTask ("Delete permanently" / "Empty trash") keeps a tombstone
//       flagged is_purged instead of erasing the task, so the next sync's
//       union merge cannot resurrect it; loadFromJson no longer prunes old
//       tombstones (a long-offline peer's live copy resurrected them).
//   D3  loadFromJson collapses duplicate Inboxes (two fresh devices' first
//       sync) into one canonical Inbox — earliest created_at, then lowest id.
//   D6  restoreTask re-homes a task whose project / section was deleted.
//   D12 labelByName never resolves a tombstoned label.
// =============================================================================

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "sync/merger.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/comment.h"
#include "model/reminder.h"

namespace {

QString isoDaysAgo(int days)
{
    return QDateTime::currentDateTimeUtc().addDays(-days).toString(Qt::ISODateWithMs);
}

QJsonObject projectJson(const QString &id, const QString &name, bool inbox,
                        const QString &createdAt, bool deleted = false)
{
    QJsonObject p;
    p[QStringLiteral("id")] = id;
    p[QStringLiteral("name")] = name;
    p[QStringLiteral("is_inbox")] = inbox;
    p[QStringLiteral("is_deleted")] = deleted;
    p[QStringLiteral("created_at")] = createdAt;
    p[QStringLiteral("updated_at")] = createdAt;
    return p;
}

QJsonObject taskJson(const QString &id, const QString &projectId,
                     const QString &updatedAt, bool deleted = false,
                     const QString &sectionId = QString())
{
    QJsonObject t;
    t[QStringLiteral("id")] = id;
    t[QStringLiteral("content")] = id;
    t[QStringLiteral("project_id")] = projectId;
    if (!sectionId.isEmpty())
        t[QStringLiteral("section_id")] = sectionId;
    t[QStringLiteral("is_deleted")] = deleted;
    t[QStringLiteral("created_at")] = updatedAt;
    t[QStringLiteral("updated_at")] = updatedAt;
    return t;
}

QJsonObject stateWith(const QJsonArray &projects, const QJsonArray &tasks,
                      const QJsonArray &sections = {}, const QJsonArray &labels = {})
{
    QJsonObject root;
    root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
    root[QStringLiteral("settings")] = QJsonObject();
    root[QStringLiteral("projects")] = projects;
    root[QStringLiteral("tasks")] = tasks;
    root[QStringLiteral("sections")] = sections;
    root[QStringLiteral("labels")] = labels;
    return root;
}

// The serialised entity (what sync sees), or {} if the store dropped it.
QJsonObject taskInJson(const DataStore &ds, const QString &id)
{
    for (const QJsonValue &v : ds.toJson()[QStringLiteral("tasks")].toArray())
        if (v.toObject()[QStringLiteral("id")].toString() == id)
            return v.toObject();
    return {};
}

bool trashContains(const DataStore &ds, const QString &id)
{
    for (Task *t : ds.allTasksIncludingDeleted())
        if (t->id() == id && t->isDeleted())
            return true;
    return false;
}

int liveInboxCount(const DataStore &ds)
{
    int n = 0;
    for (Project *p : ds.allProjects())
        if (p->isInbox()) ++n;
    return n;
}

} // namespace

class TestDataStoreTombstones : public QObject
{
    Q_OBJECT

    DataStore *m_ds = nullptr;
    QString m_inboxId;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_Tombstones"));
        QCoreApplication::setApplicationName(QStringLiteral("test_datastore_tombstones"));
    }

    void init()
    {
        m_ds = new DataStore;
        auto *inbox = new Project(m_ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        m_ds->addExistingProject(inbox);
        m_inboxId = inbox->id();
    }

    void cleanup()
    {
        delete m_ds;
        m_ds = nullptr;
    }

    // ── D2: purge keeps a tombstone ─────────────────────────────────────────

    void purge_keepsTombstone_andLeavesTrash()
    {
        Task *t = m_ds->addTask(QStringLiteral("doomed"), m_inboxId);
        const QString id = t->id();
        QVERIFY(m_ds->removeTask(id));
        QVERIFY(trashContains(*m_ds, id));

        QVERIFY(m_ds->purgeTask(id));
        QVERIFY(t->isDeleted());   // object kept alive, flagged
        QVERIFY(t->isPurged());
        // Gone for every caller …
        QVERIFY2(!m_ds->taskById(id), "a purged task is gone for callers");
        QVERIFY2(!trashContains(*m_ds, id), "a purged task must leave the Trash");
        QVERIFY(!m_ds->allTasks().contains(t));
        QVERIFY2(!m_ds->restoreTask(id), "a purged task cannot be restored");
        QVERIFY2(!m_ds->purgeTask(id), "purging twice is a no-op");

        // … but the tombstone is still serialised for sync, flag stamped.
        const QJsonObject o = taskInJson(*m_ds, id);
        QVERIFY2(!o.isEmpty(), "purgeTask must keep the tombstone for sync");
        QCOMPARE(o[QStringLiteral("is_deleted")].toBool(), true);
        QCOMPARE(o[QStringLiteral("is_purged")].toBool(), true);
        QVERIFY(o[QStringLiteral("_meta")].toObject().contains(QStringLiteral("is_purged")));

        DataStore reloaded;
        reloaded.loadFromJson(m_ds->toJson());
        QVERIFY(!reloaded.taskById(id));
        QVERIFY(!trashContains(reloaded, id));
        QCOMPARE(taskInJson(reloaded, id)[QStringLiteral("is_purged")].toBool(), true);
    }

    void addExistingTask_staleReAddOfPurgedId_isIgnored()
    {
        Task *t = m_ds->addTask(QStringLiteral("x"), m_inboxId);
        const QString id = t->id();
        const QJsonObject liveCopy = t->toJson();
        m_ds->purgeTask(id);

        // SyncEngine's task_add does "if (!taskById(id)) addExistingTask(...)".
        m_ds->addExistingTask(Task::fromJson(liveCopy));
        QVERIFY(!m_ds->taskById(id));
        int copies = 0;
        for (const QJsonValue &v : m_ds->toJson()[QStringLiteral("tasks")].toArray())
            if (v.toObject()[QStringLiteral("id")].toString() == id) ++copies;
        QCOMPARE(copies, 1);
    }

    void purge_liveTask_tombstonesOnlyThatTask_andItsDependents()
    {
        Task *t = m_ds->addTask(QStringLiteral("live"), m_inboxId);
        Task *child = m_ds->addTask(QStringLiteral("child"), m_inboxId);
        child->setParentId(t->id());
        Comment *c = m_ds->addComment(QStringLiteral("note"), t->id(), QString());
        Reminder *r = m_ds->addReminder(t->id(), QStringLiteral("relative"));

        QVERIFY(m_ds->purgeTask(t->id()));
        QVERIFY(t->isDeleted() && t->isPurged());
        // Same contract as before: no subtree cascade.
        QVERIFY(!child->isDeleted());
        // Dependents are tombstoned, not erased.
        QVERIFY(m_ds->commentById(c->id()) && m_ds->commentById(c->id())->isDeleted());
        QVERIFY(m_ds->reminderById(r->id()) && m_ds->reminderById(r->id())->isDeleted());
    }

    void purge_thenSyncWithRemoteLiveCopy_staysDeleted()
    {
        // Remote copy = what was uploaded before the delete: the task is live.
        Task *t = m_ds->addTask(QStringLiteral("x"), m_inboxId);
        const QString id = t->id();
        const QJsonObject remote = m_ds->toJson();

        m_ds->removeTask(id);
        m_ds->purgeTask(id);

        Merger merger;
        const QJsonObject merged = merger.mergeStates(m_ds->toJson(), remote);
        DataStore after;
        after.loadFromJson(merged);
        const QJsonObject m = taskInJson(after, id);
        QVERIFY2(!m.isEmpty(), "merged state must still carry the tombstone");
        QVERIFY2(m[QStringLiteral("is_deleted")].toBool(),
                 "a purged task must not come back live after sync");
        for (Task *live : after.allTasks())
            QVERIFY(live->id() != id);
    }

    void purge_outranksOlderRestoreOnPeer()
    {
        Task *t = m_ds->addTask(QStringLiteral("x"), m_inboxId);
        const QString id = t->id();
        m_ds->removeTask(id);

        // Peer restores it (newer is_deleted stamp)…
        DataStore peer;
        peer.loadFromJson(m_ds->toJson());
        QVERIFY(peer.restoreTask(id));
        const QJsonObject peerState = peer.toJson();

        // …then this device deletes it permanently, later still.
        QTest::qWait(5);
        QVERIFY(m_ds->purgeTask(id));

        Merger merger;
        DataStore after;
        after.loadFromJson(merger.mergeStates(m_ds->toJson(), peerState));
        const QJsonObject m = taskInJson(after, id);
        QVERIFY(!m.isEmpty());
        QVERIFY2(m[QStringLiteral("is_deleted")].toBool(),
                 "the later permanent delete must win over the peer's older restore");
    }

    void removeTask_clearsStalePurgedFlag()
    {
        // A live task can carry is_purged from a peer's restore-after-purge.
        Task *t = m_ds->addTask(QStringLiteral("x"), m_inboxId);
        t->setIsPurged(true);
        QVERIFY(m_ds->removeTask(t->id()));
        QVERIFY(!t->isPurged());
        QVERIFY2(trashContains(*m_ds, t->id()), "a fresh delete must land in the Trash");
    }

    void restoreParent_doesNotBringBackPurgedChild()
    {
        Task *parent = m_ds->addTask(QStringLiteral("parent"), m_inboxId);
        Task *child = m_ds->addTask(QStringLiteral("child"), m_inboxId);
        child->setParentId(parent->id());
        QVERIFY(m_ds->removeTask(parent->id()));   // cascades to child
        QVERIFY(m_ds->purgeTask(child->id()));      // child deleted permanently
        QVERIFY(m_ds->restoreTask(parent->id()));
        QVERIFY(!parent->isDeleted());
        QVERIFY(child->isDeleted());
        QVERIFY(!m_ds->taskById(child->id()));
    }

    void load_keepsOldTombstones_ofEveryEntityType()
    {
        const QString old = isoDaysAgo(60);
        QJsonObject root = stateWith(
            {projectJson(QStringLiteral("inbox"), QStringLiteral("Inbox"), true, old),
             projectJson(QStringLiteral("p-dead"), QStringLiteral("Gone"), false, old, true)},
            {taskJson(QStringLiteral("t-dead"), QStringLiteral("inbox"), old, true)});
        QJsonObject sec{{QStringLiteral("id"), QStringLiteral("s-dead")},
                        {QStringLiteral("project_id"), QStringLiteral("inbox")},
                        {QStringLiteral("is_deleted"), true},
                        {QStringLiteral("updated_at"), old}};
        root[QStringLiteral("sections")] = QJsonArray{sec};
        QJsonObject lbl{{QStringLiteral("id"), QStringLiteral("l-dead")},
                        {QStringLiteral("name"), QStringLiteral("gone")},
                        {QStringLiteral("is_deleted"), true},
                        {QStringLiteral("updated_at"), old}};
        root[QStringLiteral("labels")] = QJsonArray{lbl};
        QJsonObject cmt{{QStringLiteral("id"), QStringLiteral("c-dead")},
                        {QStringLiteral("task_id"), QStringLiteral("t-dead")},
                        {QStringLiteral("is_deleted"), true},
                        {QStringLiteral("updated_at"), old}};
        root[QStringLiteral("comments")] = QJsonArray{cmt};

        DataStore ds;
        ds.loadFromJson(root);
        QVERIFY(ds.taskById(QStringLiteral("t-dead")));
        QVERIFY(ds.projectById(QStringLiteral("p-dead")));
        QVERIFY(ds.sectionById(QStringLiteral("s-dead")));
        QVERIFY(ds.labelById(QStringLiteral("l-dead")));
        QVERIFY(ds.commentById(QStringLiteral("c-dead")));
    }

    void load_oldTombstone_survivesMergeWithStaleLiveCopy()
    {
        // Device was offline > 30 days and still has the task live.
        const QString old = isoDaysAgo(60);
        const QJsonObject local = stateWith(
            {projectJson(QStringLiteral("inbox"), QStringLiteral("Inbox"), true, old)},
            {taskJson(QStringLiteral("t1"), QStringLiteral("inbox"), isoDaysAgo(45), true)});
        const QJsonObject staleRemote = stateWith(
            {projectJson(QStringLiteral("inbox"), QStringLiteral("Inbox"), true, old)},
            {taskJson(QStringLiteral("t1"), QStringLiteral("inbox"), isoDaysAgo(50), false)});

        DataStore ds;
        ds.loadFromJson(local);   // used to drop the 45-day-old tombstone here
        Merger merger;
        DataStore after;
        after.loadFromJson(merger.mergeStates(ds.toJson(), staleRemote));
        QVERIFY(after.taskById(QStringLiteral("t1")));
        QVERIFY2(after.taskById(QStringLiteral("t1"))->isDeleted(),
                 "an old tombstone must not be resurrected by a stale live copy");
    }

    void vacuum_stillDropsOldPurgedTombstones()
    {
        Task *t = m_ds->addTask(QStringLiteral("x"), m_inboxId);
        m_ds->removeTask(t->id());
        m_ds->purgeTask(t->id());
        const QString id = t->id();
        t->setUpdatedAt(QDateTime::currentDateTimeUtc().addDays(-120));
        QCOMPARE(m_ds->vacuum(90), 1);
        QVERIFY(taskInJson(*m_ds, id).isEmpty());
    }

    // ── D3: one Inbox after loading a merged state ──────────────────────────

    void load_twoInboxes_collapsesToEarliest()
    {
        const QJsonObject root = stateWith(
            {projectJson(QStringLiteral("inbox-b"), QStringLiteral("Inbox"), true, isoDaysAgo(1)),
             projectJson(QStringLiteral("inbox-a"), QStringLiteral("Inbox"), true, isoDaysAgo(2))},
            {taskJson(QStringLiteral("ta"), QStringLiteral("inbox-a"), isoDaysAgo(1)),
             taskJson(QStringLiteral("tb"), QStringLiteral("inbox-b"), isoDaysAgo(1),
                      false, QStringLiteral("sb"))},
            {QJsonObject{{QStringLiteral("id"), QStringLiteral("sb")},
                         {QStringLiteral("project_id"), QStringLiteral("inbox-b")},
                         {QStringLiteral("name"), QStringLiteral("B-section")}}});

        DataStore ds;
        ds.loadFromJson(root);
        QVERIFY(ds.lastLoadModifiedState());
        QCOMPARE(liveInboxCount(ds), 1);
        QCOMPARE(ds.inboxProject()->id(), QStringLiteral("inbox-a"));
        QCOMPARE(ds.taskById(QStringLiteral("tb"))->projectId(), QStringLiteral("inbox-a"));
        QCOMPARE(ds.taskById(QStringLiteral("tb"))->sectionId(), QStringLiteral("sb"));
        QCOMPARE(ds.sectionById(QStringLiteral("sb"))->projectId(), QStringLiteral("inbox-a"));
        QVERIFY(ds.projectById(QStringLiteral("inbox-b"))->isDeleted());

        // The repair is stamped so it wins the next merge.
        const QJsonObject out = ds.toJson();
        for (const QJsonValue &v : out[QStringLiteral("tasks")].toArray()) {
            const QJsonObject o = v.toObject();
            if (o[QStringLiteral("id")].toString() == QStringLiteral("tb"))
                QVERIFY(o[QStringLiteral("_meta")].toObject().contains(QStringLiteral("project_id")));
        }

        // Loading the repaired state again is a no-op.
        DataStore again;
        again.loadFromJson(out);
        QVERIFY(!again.lastLoadModifiedState());
        QCOMPARE(liveInboxCount(again), 1);
    }

    void load_twoInboxes_tieBreaksOnLowestId_independentOfOrder()
    {
        const QString same = isoDaysAgo(3);
        const QJsonObject ab = stateWith(
            {projectJson(QStringLiteral("inbox-2"), QStringLiteral("Inbox"), true, same),
             projectJson(QStringLiteral("inbox-1"), QStringLiteral("Inbox"), true, same)}, {});
        const QJsonObject ba = stateWith(
            {projectJson(QStringLiteral("inbox-1"), QStringLiteral("Inbox"), true, same),
             projectJson(QStringLiteral("inbox-2"), QStringLiteral("Inbox"), true, same)}, {});
        DataStore d1, d2;
        d1.loadFromJson(ab);
        d2.loadFromJson(ba);
        QCOMPARE(d1.inboxProject()->id(), QStringLiteral("inbox-1"));
        QCOMPARE(d2.inboxProject()->id(), QStringLiteral("inbox-1"));
    }

    void load_lateTaskInTombstonedDuplicateInbox_isRehomed()
    {
        // A previous load already demoted inbox-b; a peer that had not synced
        // yet filed a new task into it.
        const QJsonObject root = stateWith(
            {projectJson(QStringLiteral("inbox-a"), QStringLiteral("Inbox"), true, isoDaysAgo(2)),
             projectJson(QStringLiteral("inbox-b"), QStringLiteral("Inbox"), true, isoDaysAgo(1), true)},
            {taskJson(QStringLiteral("late"), QStringLiteral("inbox-b"), isoDaysAgo(0))});
        DataStore ds;
        ds.loadFromJson(root);
        QCOMPARE(ds.taskById(QStringLiteral("late"))->projectId(), QStringLiteral("inbox-a"));
        QVERIFY(ds.lastLoadModifiedState());
    }

    void load_singleInbox_isUntouched()
    {
        const QJsonObject root = stateWith(
            {projectJson(QStringLiteral("inbox"), QStringLiteral("Inbox"), true, isoDaysAgo(1)),
             projectJson(QStringLiteral("work"), QStringLiteral("Work"), false, isoDaysAgo(1))},
            {taskJson(QStringLiteral("t"), QStringLiteral("work"), isoDaysAgo(1))});
        DataStore ds;
        ds.loadFromJson(root);
        QVERIFY(!ds.lastLoadModifiedState());
        QCOMPARE(ds.taskById(QStringLiteral("t"))->projectId(), QStringLiteral("work"));
    }

    // ── D6: restore re-homes into live containers ───────────────────────────

    void restore_intoDeletedProject_movesToInbox()
    {
        Project *p = m_ds->addProject(QStringLiteral("Doomed"));
        Section *s = m_ds->addSection(QStringLiteral("S"), p->id());
        Task *t = m_ds->addTask(QStringLiteral("t"), p->id());
        t->setSectionId(s->id());
        Task *child = m_ds->addTask(QStringLiteral("child"), p->id());
        child->setParentId(t->id());

        QVERIFY(m_ds->removeProject(p->id()));   // cascade-tombstones t + child
        QVERIFY(t->isDeleted());
        QVERIFY(m_ds->restoreTask(t->id()));
        QCOMPARE(t->projectId(), m_inboxId);
        QVERIFY(t->sectionId().isEmpty());
        QVERIFY(m_ds->tasksForProject(m_inboxId).contains(t));
    }

    void restore_intoDeletedSection_clearsSection()
    {
        Project *p = m_ds->addProject(QStringLiteral("Keep"));
        Section *s = m_ds->addSection(QStringLiteral("S"), p->id());
        Task *t = m_ds->addTask(QStringLiteral("t"), p->id());
        t->setSectionId(s->id());
        m_ds->removeTask(t->id());
        // Section deleted while the task sits in the Trash.
        m_ds->removeSection(s->id());
        QCOMPARE(t->sectionId(), s->id());   // removeSection skips tombstones

        QVERIFY(m_ds->restoreTask(t->id()));
        QCOMPARE(t->projectId(), p->id());
        QVERIFY(t->sectionId().isEmpty());
        QVERIFY(m_ds->tasksForSection(QString()).contains(t));
    }

    void restore_cascadedDescendant_isRehomedToo()
    {
        Project *p = m_ds->addProject(QStringLiteral("Doomed"));
        Task *parent = m_ds->addTask(QStringLiteral("parent"), p->id());
        Task *child = m_ds->addTask(QStringLiteral("child"), p->id());
        child->setParentId(parent->id());
        QVERIFY(m_ds->removeTask(parent->id()));   // shared cascade stamp
        QVERIFY(m_ds->removeProject(p->id()));
        QVERIFY(m_ds->restoreTask(parent->id()));
        QVERIFY(!child->isDeleted());
        QCOMPARE(parent->projectId(), m_inboxId);
        QCOMPARE(child->projectId(), m_inboxId);
    }

    void restore_intoLiveProject_keepsPlacement()
    {
        Project *p = m_ds->addProject(QStringLiteral("Live"));
        Section *s = m_ds->addSection(QStringLiteral("S"), p->id());
        Task *t = m_ds->addTask(QStringLiteral("t"), p->id());
        t->setSectionId(s->id());
        m_ds->removeTask(t->id());
        QVERIFY(m_ds->restoreTask(t->id()));
        QCOMPARE(t->projectId(), p->id());
        QCOMPARE(t->sectionId(), s->id());
    }

    // ── D12: label name index ignores tombstones ────────────────────────────

    void labelByName_afterRemove_returnsNull()
    {
        Label *l = m_ds->addLabel(QStringLiteral("Work"));
        QCOMPARE(m_ds->labelByName(QStringLiteral("work")), l);
        QVERIFY(m_ds->removeLabel(l->id()));
        QVERIFY(!m_ds->labelByName(QStringLiteral("Work")));
        // A new live label with the same name is found again.
        Label *fresh = m_ds->addLabel(QStringLiteral("Work"));
        QCOMPARE(m_ds->labelByName(QStringLiteral("Work")), fresh);
    }

    void labelByName_loadPrefersLiveOverTombstone_inEitherOrder()
    {
        const QJsonObject live{{QStringLiteral("id"), QStringLiteral("l-live")},
                               {QStringLiteral("name"), QStringLiteral("Home")}};
        const QJsonObject dead{{QStringLiteral("id"), QStringLiteral("l-dead")},
                               {QStringLiteral("name"), QStringLiteral("home")},
                               {QStringLiteral("is_deleted"), true}};
        for (const QJsonArray &labels : {QJsonArray{live, dead}, QJsonArray{dead, live}}) {
            DataStore ds;
            ds.loadFromJson(stateWith({}, {}, {}, labels));
            Label *found = ds.labelByName(QStringLiteral("HOME"));
            QVERIFY(found);
            QCOMPARE(found->id(), QStringLiteral("l-live"));
        }
        DataStore onlyDead;
        onlyDead.loadFromJson(stateWith({}, {}, {}, QJsonArray{dead}));
        QVERIFY(!onlyDead.labelByName(QStringLiteral("home")));
    }

    void labelByName_removeKeepsOtherLiveLabelWithSameName()
    {
        Label *a = m_ds->addLabel(QStringLiteral("dup"));
        Label *b = m_ds->addLabel(QStringLiteral("Dup"));   // index now → b
        QVERIFY(m_ds->removeLabel(b->id()));
        QCOMPARE(m_ds->labelByName(QStringLiteral("dup")), a);
    }
};

QTEST_GUILESS_MAIN(TestDataStoreTombstones)
#include "test_datastore_tombstones.moc"
