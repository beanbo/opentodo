// Extended tests for Comment model + DataStore comment operations.
//
// Companions test_models.cpp::Comment_* and test_datastore.cpp::Comments_*.
// Focus areas covered here that the existing suite does not exercise:
//   - JSON round-trip of every persisted field (id/text/taskId/projectId/
//     postedAt/updatedAt/isDeleted/attachment)
//   - setContent timestamp behaviour (does it bump updatedAt?)
//   - empty-content acceptance
//   - 1 MiB content round-trip
//   - addComment to non-existent taskId (orphan acceptance)
//   - updateComment timestamp + return-value semantics
//   - removeComment soft-delete excludes from queries
//   - commentsForTask ordering relative to insertion order
//   - commentsForTask(invalidId) returns empty
//   - cascade behaviour when parent task is soft-deleted (tombstone)
//   - cascade behaviour when parent task is hard-purged (erasure)
//   - CRDT _meta blob present after addComment / updateComment
//   - tombstone preserved through full DataStore JSON round-trip
//
// IMPORTANT: avoid raw string literals — moc choked on R"(...)" in the past.

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "model/comment.h"
#include "model/task.h"
#include "model/project.h"
#include "storage/datastore.h"
#include "utils/uuid.h"

class TestCommentExtended : public QObject
{
    Q_OBJECT

private:
    // Helper: DataStore pre-loaded with one Inbox project so addTask works.
    DataStore *makeStore()
    {
        auto *ds = new DataStore(this);
        auto *inbox = new Project(ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds->addExistingProject(inbox);
        return ds;
    }

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    // ── Comment entity ────────────────────────────────────────────────────────

    void default_ctor_initialises_uuid_and_timestamps()
    {
        Comment c;
        QVERIFY(!c.id().isEmpty());
        QVERIFY(UuidHelper::isValid(c.id()));
        QVERIFY(c.postedAt().isValid());
        QVERIFY(c.updatedAt().isValid());
        // postedAt and updatedAt initialised to the same instant.
        QCOMPARE(c.postedAt(), c.updatedAt());
        QVERIFY(c.content().isEmpty());
        QVERIFY(c.taskId().isEmpty());
        QVERIFY(c.projectId().isEmpty());
        QVERIFY(!c.isDeleted());
    }

    void json_roundtrip_preserves_every_field()
    {
        Comment orig;
        orig.setContent(QStringLiteral("Discuss in next standup"));
        orig.setTaskId(QStringLiteral("task-abc"));
        orig.setProjectId(QStringLiteral("proj-xyz"));

        const QDateTime posted = QDateTime::fromString(
            QStringLiteral("2026-01-15T09:30:00.123Z"), Qt::ISODateWithMs);
        const QDateTime updated = QDateTime::fromString(
            QStringLiteral("2026-01-15T11:45:30.456Z"), Qt::ISODateWithMs);
        orig.setPostedAt(posted);
        orig.setUpdatedAt(updated);

        AttachmentObject att;
        att.fileName = QStringLiteral("notes.pdf");
        att.fileType = QStringLiteral("application/pdf");
        att.fileUrl = QStringLiteral("https://example.com/notes.pdf");
        att.fileSize = 2048;
        orig.setAttachment(att);

        const QJsonObject json = orig.toJson();

        QScopedPointer<Comment> r(Comment::fromJson(json));
        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->content(), QStringLiteral("Discuss in next standup"));
        QCOMPARE(r->taskId(), QStringLiteral("task-abc"));
        QCOMPARE(r->projectId(), QStringLiteral("proj-xyz"));
        QCOMPARE(r->postedAt(), posted);
        QCOMPARE(r->updatedAt(), updated);
        QCOMPARE(r->attachment().fileName, QStringLiteral("notes.pdf"));
        QCOMPARE(r->attachment().fileType, QStringLiteral("application/pdf"));
        QCOMPARE(r->attachment().fileUrl, QStringLiteral("https://example.com/notes.pdf"));
        QCOMPARE(r->attachment().fileSize, 2048);
        QVERIFY(!r->isDeleted());
    }

    // setContent(...) on the Comment model itself does NOT auto-bump
    // updatedAt — only DataStore::updateComment does that. Document this
    // behaviour so future readers don't assume otherwise.
    void setContent_does_not_auto_bump_updatedAt_on_model()
    {
        Comment c;
        const QDateTime original = c.updatedAt();
        QTest::qWait(15);
        c.setContent(QStringLiteral("Edited"));
        // updatedAt unchanged — Comment is a passive data carrier.
        QCOMPARE(c.updatedAt(), original);

        // But explicit setUpdatedAt of course works.
        const QDateTime later = original.addSecs(60);
        c.setUpdatedAt(later);
        QCOMPARE(c.updatedAt(), later);
    }

    // Document that empty content is permitted by the model and survives
    // a JSON round-trip — there is no validation guard.
    void empty_content_is_accepted()
    {
        Comment c;
        c.setContent(QString());
        QVERIFY(c.content().isEmpty());

        const QJsonObject json = c.toJson();
        QVERIFY(json.contains(QStringLiteral("content")));
        QCOMPARE(json[QStringLiteral("content")].toString(), QString());

        QScopedPointer<Comment> r(Comment::fromJson(json));
        QVERIFY(r->content().isEmpty());
    }

    // 1 MiB content survives JSON round-trip without truncation or
    // re-encoding losses (UTF-16 → UTF-8 → UTF-16 inside QJsonValue).
    void long_content_1MB_round_trip()
    {
        const int oneMiB = 1024 * 1024;
        const QString big(oneMiB, QChar(QLatin1Char('x')));
        QCOMPARE(big.size(), oneMiB);

        Comment orig;
        orig.setContent(big);
        const QJsonObject json = orig.toJson();

        // Force through a full serialisation cycle to exercise the JSON encoder,
        // not just the in-memory QJsonValue copy.
        const QByteArray encoded = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QVERIFY(encoded.size() >= oneMiB);
        const QJsonObject parsed = QJsonDocument::fromJson(encoded).object();

        QScopedPointer<Comment> r(Comment::fromJson(parsed));
        QCOMPARE(r->content().size(), oneMiB);
        QCOMPARE(r->content(), big);
    }

    // ── DataStore comment ops ─────────────────────────────────────────────────

    void addComment_appears_in_commentsForTask()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Project *p = ds->addProject(QStringLiteral("Work"));
        Task *t = ds->addTask(QStringLiteral("Ship feature"), p->id());

        Comment *c = ds->addComment(QStringLiteral("Looks ready"), t->id(), QString());
        QVERIFY(c != nullptr);
        QCOMPARE(c->taskId(), t->id());
        QVERIFY(c->projectId().isEmpty());

        const QList<Comment *> list = ds->commentsForTask(t->id());
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first(), c);
    }

    // addComment with a taskId that does not exist in the store: accepted —
    // DataStore does NOT validate parent existence. Document the orphan
    // acceptance so callers know they own validation.
    void addComment_with_nonexistent_taskId_is_accepted_as_orphan()
    {
        QScopedPointer<DataStore> ds(makeStore());
        const QString ghostTaskId = QStringLiteral("does-not-exist-123");
        QVERIFY(ds->taskById(ghostTaskId) == nullptr);

        Comment *c = ds->addComment(QStringLiteral("Orphan"), ghostTaskId, QString());
        QVERIFY(c != nullptr);
        QCOMPARE(c->taskId(), ghostTaskId);

        // commentsForTask still returns it because the lookup is by id only.
        QCOMPARE(ds->commentsForTask(ghostTaskId).size(), 1);
    }

    void updateComment_changes_content_and_bumps_updatedAt()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c = ds->addComment(QStringLiteral("first draft"), t->id(), QString());
        const QDateTime before = c->updatedAt();
        QTest::qWait(15);

        const bool ok = ds->updateComment(c->id(), QStringLiteral("revised"));
        QVERIFY(ok);
        QCOMPARE(c->content(), QStringLiteral("revised"));
        QVERIFY(c->updatedAt() > before);
    }

    void updateComment_returns_false_for_unknown_id()
    {
        QScopedPointer<DataStore> ds(makeStore());
        const bool ok = ds->updateComment(QStringLiteral("nope"), QStringLiteral("x"));
        QVERIFY(!ok);
    }

    void removeComment_soft_deletes_and_excludes_from_queries()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c = ds->addComment(QStringLiteral("temp"), t->id(), QString());

        QCOMPARE(ds->commentsForTask(t->id()).size(), 1);
        QVERIFY(!c->isDeleted());

        const bool ok = ds->removeComment(c->id());
        QVERIFY(ok);
        QVERIFY(c->isDeleted());

        // Excluded from query results, but commentById still returns the
        // tombstone (so sync layer can serialise it).
        QCOMPARE(ds->commentsForTask(t->id()).size(), 0);
        QVERIFY(ds->commentById(c->id()) != nullptr);
    }

    void removeComment_returns_false_for_unknown_id()
    {
        QScopedPointer<DataStore> ds(makeStore());
        const bool ok = ds->removeComment(QStringLiteral("nope"));
        QVERIFY(!ok);
    }

    // commentsForTask returns in insertion order (m_comments append order).
    // Document this — there is no explicit sort by postedAt.
    void commentsForTask_returns_insertion_order()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());

        Comment *c1 = ds->addComment(QStringLiteral("first"), t->id(), QString());
        Comment *c2 = ds->addComment(QStringLiteral("second"), t->id(), QString());
        Comment *c3 = ds->addComment(QStringLiteral("third"), t->id(), QString());

        // Make c1's postedAt later than the others — the query should NOT
        // re-sort, it returns insertion order regardless.
        c1->setPostedAt(QDateTime::currentDateTimeUtc().addDays(1));

        const QList<Comment *> list = ds->commentsForTask(t->id());
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0), c1);
        QCOMPARE(list.at(1), c2);
        QCOMPARE(list.at(2), c3);
    }

    void commentsForTask_invalid_id_returns_empty()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        ds->addComment(QStringLiteral("hi"), t->id(), QString());

        QCOMPARE(ds->commentsForTask(QStringLiteral("nonexistent-task")).size(), 0);
        QCOMPARE(ds->commentsForTask(QString()).size(), 0);
    }

    void commentsForProject_filter_works_independently_of_task_comments()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Project *p = ds->addProject(QStringLiteral("Work"));
        Task *t = ds->addTask(QStringLiteral("X"), p->id());

        ds->addComment(QStringLiteral("on task"), t->id(), QString());
        ds->addComment(QStringLiteral("on project"), QString(), p->id());
        ds->addComment(QStringLiteral("also on project"), QString(), p->id());

        QCOMPARE(ds->commentsForTask(t->id()).size(), 1);
        QCOMPARE(ds->commentsForProject(p->id()).size(), 2);

        // Project-level query never sees task-only comments and vice versa.
        for (Comment *c : ds->commentsForTask(t->id())) {
            QVERIFY(c->projectId().isEmpty());
        }
        for (Comment *c : ds->commentsForProject(p->id())) {
            QVERIFY(c->taskId().isEmpty());
        }
    }

    // ── Orphan handling ──────────────────────────────────────────────────────

    // When a parent task is soft-deleted, DataStore::removeTask cascades and
    // tombstones the comment. So commentsForTask(deletedTaskId) returns empty
    // (filter excludes is_deleted), but commentById still resolves to the
    // tombstoned object.
    void task_soft_delete_cascades_tombstone_to_comments()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c1 = ds->addComment(QStringLiteral("c1"), t->id(), QString());
        Comment *c2 = ds->addComment(QStringLiteral("c2"), t->id(), QString());

        const QString tid = t->id();
        const QString c1id = c1->id();
        const QString c2id = c2->id();

        QVERIFY(ds->removeTask(tid));

        // commentsForTask filters by !isDeleted → returns nothing.
        QCOMPARE(ds->commentsForTask(tid).size(), 0);

        // But the comment objects still exist and are now tombstones.
        Comment *r1 = ds->commentById(c1id);
        Comment *r2 = ds->commentById(c2id);
        QVERIFY(r1 != nullptr);
        QVERIFY(r2 != nullptr);
        QVERIFY(r1->isDeleted());
        QVERIFY(r2->isDeleted());
    }

    // When a parent task is purged ("delete permanently"), its comments are
    // TOMBSTONED, not erased: purgeTask used to erase them, and the next sync's
    // union merge brought erased entities back from the remote copy. The
    // tombstone keeps the deletion winning; the comment is invisible to every
    // query.
    void task_purge_cascades_tombstone_to_comments()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c = ds->addComment(QStringLiteral("c"), t->id(), QString());
        const QString tid = t->id();
        const QString cid = c->id();

        QVERIFY(ds->purgeTask(tid));
        QVERIFY(ds->taskById(tid) == nullptr);
        QVERIFY(ds->commentById(cid) != nullptr);
        QVERIFY(ds->commentById(cid)->isDeleted());
        QCOMPARE(ds->commentsForTask(tid).size(), 0);
    }

    // ── CRDT sync metadata ───────────────────────────────────────────────────

    // After addComment, the per-entity _meta blob should be populated and
    // visible via the full DataStore::toJson serialisation.
    void addComment_stamps_meta_visible_in_full_toJson()
    {
        // SHAPE: single-level. entry["_meta"][fieldName] == { ts, dev }.
        // (The earlier double-wrap entry["_meta"]["_meta"][fieldName] was a
        //  bug — fixed alongside a v1->v2 schema migration in
        //  DataStore::migrateJson that flattens any legacy state.json.)
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c = ds->addComment(QStringLiteral("hello"), t->id(), QString());

        const QJsonObject root = ds->toJson();
        QVERIFY(root.contains(QStringLiteral("comments")));
        const QJsonArray arr = root[QStringLiteral("comments")].toArray();
        QCOMPARE(arr.size(), 1);

        const QJsonObject entry = arr.first().toObject();
        QCOMPARE(entry[QStringLiteral("id")].toString(), c->id());
        QVERIFY(entry.contains(QStringLiteral("_meta")));
        const QJsonObject meta = entry[QStringLiteral("_meta")].toObject();
        QVERIFY2(!meta.contains(QStringLiteral("_meta")),
                 "_meta must not contain a nested '_meta' key (legacy "
                 "double-wrap bug must not return)");
        // The set of fields stamped in addComment (datastore.cpp ~line 943-950).
        QVERIFY(meta.contains(QStringLiteral("content")));
        QVERIFY(meta.contains(QStringLiteral("task_id")));
        QVERIFY(meta.contains(QStringLiteral("posted_at")));
        QVERIFY(meta.contains(QStringLiteral("updated_at")));
        QVERIFY(meta.contains(QStringLiteral("is_deleted")));
    }

    // updateComment should re-stamp content + updated_at, advancing the HLC
    // ts on those fields beyond the initial stamp.
    void updateComment_advances_meta_timestamp_on_content_field()
    {
        // Single-level "_meta"[field] shape — see addComment_stamps_meta note.
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("X"), QString());
        Comment *c = ds->addComment(QStringLiteral("v1"), t->id(), QString());

        const QJsonObject before = ds->toJson()[QStringLiteral("comments")].toArray()
                                       .first().toObject()[QStringLiteral("_meta")].toObject();
        const QString tsBefore = before[QStringLiteral("content")].toObject()
                                     [QStringLiteral("ts")].toString();
        QVERIFY(!tsBefore.isEmpty());

        QTest::qWait(5);
        QVERIFY(ds->updateComment(c->id(), QStringLiteral("v2")));

        const QJsonObject after = ds->toJson()[QStringLiteral("comments")].toArray()
                                      .first().toObject()[QStringLiteral("_meta")].toObject();
        const QString tsAfter = after[QStringLiteral("content")].toObject()
                                    [QStringLiteral("ts")].toString();

        QVERIFY(!tsAfter.isEmpty());
        QVERIFY(tsAfter > tsBefore);   // HLC ts is lexicographically ordered
    }

    // After removeComment the tombstone (is_deleted=true) survives a full
    // toJson → loadFromJson round trip into a fresh store. This is what the
    // sync layer relies on to propagate deletes.
    void tombstone_survives_full_datastore_round_trip()
    {
        QScopedPointer<DataStore> ds1(makeStore());
        Task *t = ds1->addTask(QStringLiteral("X"), QString());
        Comment *c = ds1->addComment(QStringLiteral("doomed"), t->id(), QString());
        const QString cid = c->id();
        QVERIFY(ds1->removeComment(cid));

        const QJsonObject json = ds1->toJson();

        QScopedPointer<DataStore> ds2(new DataStore);
        ds2->loadFromJson(json);

        Comment *survivor = ds2->commentById(cid);
        QVERIFY(survivor != nullptr);
        QVERIFY(survivor->isDeleted());
        // Excluded from the live query in the new store too.
        QCOMPARE(ds2->commentsForTask(t->id()).size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCommentExtended)
#include "test_comment_extended.moc"
