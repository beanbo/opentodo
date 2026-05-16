// Direct tests for DataStore _meta helpers + migration edge cases.
//
// Background: pre-Q2.10 the per-entity CRDT meta blob got serialized as
//
//     entity["_meta"]["_meta"][field] = {ts, dev}     // legacy, double-wrap
//
// because stampEntityField wrote via CrdtMeta::setFieldMeta into a map that
// already represented the inner _meta object — so injectMeta then put another
// "_meta" key on top of it.
//
// Q2.10-A fixed it to write the field-stamp directly into the per-entity
// meta map (single-wrap), and added:
//   1. extractMeta defensive flatten on load (catches per-entity blobs that
//      still look legacy even if migrateJson didn't fire).
//   2. migrateJson v1 -> v2 that flattens every collection's _meta during a
//      one-shot migration when schema_version < 2.
//
// CrdtMeta::getFieldMeta reads entity["_meta"][field], so a double-wrap
// silently breaks every per-field merge — extractMeta + migrateJson are the
// gate that prevents that.
//
// These tests cover the gaps that test_datastore_migration.cpp left:
//   - migration idempotence (double-wrap survives one round of load/save
//     without re-corrupting),
//   - already-flat (v2) state is left structurally intact,
//   - mixed double + single wrap inside one file gets unified,
//   - empty / missing / partially-double-wrap meta blobs do not crash and
//     do not corrupt unrelated keys,
//   - stampEntityField semantics (first stamp creates, second stamp advances,
//     two distinct fields coexist, tombstone field stamps correctly),
//   - extractMeta roundtrip via the merger code path (CrdtMeta::getFieldMeta
//     finds the stamp under entity["_meta"][field] after toJson()),
//   - schema version constants and a fresh save always carrying schema_version=2.
//
// Note: the helpers themselves are private. We exercise them end-to-end
// through loadFromJson / toJson, because that's the only path that exists
// in production — and it's also the only path that matters for correctness.

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDateTime>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "sync/hlc.h"
#include "sync/crdt_meta.h"
#include "utils/uuid.h"

class TestDataStoreMetaHelpers : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;

    // Build a per-field stamp object {ts, dev}.
    static QJsonObject makeStamp(const QString &ts, const QString &dev)
    {
        QJsonObject s;
        s[QStringLiteral("ts")]  = ts;
        s[QStringLiteral("dev")] = dev;
        return s;
    }

    // Build a minimal task entity with given id, content, project_id, and
    // optional pre-built _meta blob attached as-is (no flatten / wrap done
    // for the caller — the test specifies the exact shape).
    static QJsonObject makeTaskEntity(const QString &id,
                                      const QString &content,
                                      const QString &projectId,
                                      const QJsonObject &metaOrEmpty,
                                      bool attachMetaKey,
                                      bool isDeleted = false)
    {
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);
        QJsonObject t;
        t[QStringLiteral("id")]         = id;
        t[QStringLiteral("content")]    = content;
        t[QStringLiteral("project_id")] = projectId;
        t[QStringLiteral("priority")]   = 1;
        t[QStringLiteral("created_at")] = nowIso;
        t[QStringLiteral("updated_at")] = nowIso;
        t[QStringLiteral("is_deleted")] = isDeleted;
        if (attachMetaKey)
            t[QStringLiteral("_meta")]  = metaOrEmpty;
        return t;
    }

    // Build a minimal Inbox project so loaded tasks have a valid parent.
    static QJsonObject makeInbox(const QString &projectId)
    {
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);
        QJsonObject p;
        p[QStringLiteral("id")]         = projectId;
        p[QStringLiteral("name")]       = QStringLiteral("Inbox");
        p[QStringLiteral("is_inbox")]   = true;
        p[QStringLiteral("color")]      = QStringLiteral("grey");
        p[QStringLiteral("created_at")] = nowIso;
        p[QStringLiteral("updated_at")] = nowIso;
        return p;
    }

    // Build a root with a single task. `version` is written verbatim into
    // schema_version. `taskMeta` is the _meta blob attached to the task as-is.
    QJsonObject makeRootWithTask(int version,
                                 const QString &taskId,
                                 const QString &projectId,
                                 const QJsonObject &taskMeta,
                                 bool attachMetaKey,
                                 bool taskIsDeleted = false) const
    {
        QJsonObject root;
        root[QStringLiteral("schema_version")] = version;
        root[QStringLiteral("settings")]       = QJsonObject{};

        root[QStringLiteral("projects")] = QJsonArray{ makeInbox(projectId) };
        root[QStringLiteral("tasks")]    = QJsonArray{
            makeTaskEntity(taskId, QStringLiteral("hello"), projectId,
                           taskMeta, attachMetaKey, taskIsDeleted)
        };
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("labels")]    = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();
        return root;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_DSMetaHelpers"));
        QCoreApplication::setApplicationName(QStringLiteral("test_datastore_meta_helpers"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmp.path());
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
    }

    // ── Schema constant ─────────────────────────────────────────────────────

    // Q2.10 bumped CURRENT_SCHEMA_VERSION from 1 to 2 to drive the per-entity
    // _meta double-wrap flatten on load. If somebody bumps this further,
    // they must also extend migrateJson — this test pins the contract.
    void schema_version_is_2()
    {
        QCOMPARE(DataStore::CURRENT_SCHEMA_VERSION, 2);
    }

    // toJson always stamps the current version, regardless of in-memory state.
    void toJson_stamps_current_schema_version()
    {
        DataStore ds;
        const QJsonObject saved = ds.toJson();
        QCOMPARE(saved.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);
        QCOMPARE(saved.value(QStringLiteral("schema_version")).toInt(), 2);
    }

    // ── migrateJson: v1 → v2 flatten ─────────────────────────────────────────

    // Idempotence: a legacy v1 file with a double-wrap _meta is loaded once;
    // resulting in-memory state is saved (schema_version becomes 2 because
    // toJson always writes CURRENT). Re-loading that saved blob must NOT
    // re-corrupt anything — the meta stays single-wrap and the field stamp
    // is reachable through CrdtMeta.
    void migration_v1_double_wrap_is_flattened_and_idempotent()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        // Construct the legacy double-wrap shape:
        //   _meta : { _meta : { content : {ts, dev} } }
        QJsonObject inner;
        inner[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923f1234:00000003:device-A"),
                      QStringLiteral("device-A"));
        QJsonObject doubleWrap;
        doubleWrap[QStringLiteral("_meta")] = inner;

        const QJsonObject root = makeRootWithTask(
            /*version=*/1, taskId, projId, doubleWrap, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);
        // Migration ran (1 -> 2).
        QCOMPARE(ds.loadedSchemaVersion(), 2);
        QCOMPARE(ds.allTasks().size(), 1);

        const QJsonObject saved1   = ds.toJson();
        const QJsonObject taskObj1 =
            saved1.value(QStringLiteral("tasks")).toArray().first().toObject();
        QVERIFY2(taskObj1.contains(QStringLiteral("_meta")),
                 "Task should still carry _meta after migrate+save");

        // After flatten, the outer _meta should hold the field-stamp keys
        // directly, NOT a single nested "_meta" key wrapping them.
        const QJsonObject meta1 = taskObj1.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(!meta1.isEmpty(), "Flattened meta must be non-empty");
        QVERIFY2(meta1.contains(QStringLiteral("content")),
                 "After flatten 'content' stamp lives at meta[content]");
        QVERIFY2(!(meta1.size() == 1 && meta1.contains(QStringLiteral("_meta"))),
                 "Outer wrapper must be gone after migration");

        // The stamp itself is reachable through the merger's code path.
        const QJsonObject contentMeta = CrdtMeta::getFieldMeta(
            taskObj1, QStringLiteral("content"));
        QCOMPARE(contentMeta.value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-A"));
        QCOMPARE(contentMeta.value(QStringLiteral("ts")).toString(),
                 QStringLiteral("00000001923f1234:00000003:device-A"));

        // Round 2: load the saved-after-migration blob and re-save. Result
        // must be structurally identical for the _meta block (idempotence).
        DataStore ds2;
        ds2.loadFromJson(saved1);
        QCOMPARE(ds2.loadedSchemaVersion(), 2);

        const QJsonObject saved2   = ds2.toJson();
        const QJsonObject taskObj2 =
            saved2.value(QStringLiteral("tasks")).toArray().first().toObject();
        QCOMPARE(taskObj2.value(QStringLiteral("_meta")).toObject(), meta1);
    }

    // Already-flat v2 state: load → save. The _meta block must be returned
    // verbatim (no spurious wrapping or stripping). schema_version stays 2.
    void migration_v2_already_flat_is_unchanged()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        QJsonObject flat;
        flat[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923f0000:00000001:device-A"),
                      QStringLiteral("device-A"));
        flat[QStringLiteral("priority")] =
            makeStamp(QStringLiteral("00000001923f0001:00000002:device-A"),
                      QStringLiteral("device-A"));

        const QJsonObject root = makeRootWithTask(
            /*version=*/2, taskId, projId, flat, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);
        // Already at current — no migration would have run, but loaded version
        // is still recorded.
        QCOMPARE(ds.loadedSchemaVersion(), 2);

        const QJsonObject saved   = ds.toJson();
        const QJsonObject taskObj =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        QVERIFY(taskObj.contains(QStringLiteral("_meta")));

        const QJsonObject meta = taskObj.value(QStringLiteral("_meta")).toObject();
        QCOMPARE(meta, flat);   // bit-for-bit (modulo key ordering, which
                                // QJsonObject normalises on equality).
    }

    // Mixed file: one task carries the legacy double-wrap, another carries
    // the new flat shape. After migration both should be flat.
    void migration_mixed_double_and_single_wrap_unified()
    {
        const QString projId  = UuidHelper::generate();
        const QString taskAId = UuidHelper::generate();
        const QString taskBId = UuidHelper::generate();

        // Task A: double-wrap.
        QJsonObject innerA;
        innerA[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923fAAAA:00000003:device-A"),
                      QStringLiteral("device-A"));
        QJsonObject doubleA;
        doubleA[QStringLiteral("_meta")] = innerA;

        // Task B: already-flat.
        QJsonObject flatB;
        flatB[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923fBBBB:00000004:device-B"),
                      QStringLiteral("device-B"));

        QJsonObject root;
        root[QStringLiteral("schema_version")] = 1;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("projects")]       = QJsonArray{ makeInbox(projId) };
        root[QStringLiteral("tasks")] = QJsonArray{
            makeTaskEntity(taskAId, QStringLiteral("A"), projId, doubleA, true),
            makeTaskEntity(taskBId, QStringLiteral("B"), projId, flatB,   true),
        };
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("labels")]    = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);
        QCOMPARE(ds.loadedSchemaVersion(), 2);
        QCOMPARE(ds.allTasks().size(), 2);

        const QJsonObject saved = ds.toJson();
        const QJsonArray tasks  = saved.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(tasks.size(), 2);

        for (const QJsonValue &tv : tasks) {
            const QJsonObject obj  = tv.toObject();
            const QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
            QVERIFY2(meta.contains(QStringLiteral("content")),
                     "After migration every task must have flat content stamp");

            const QJsonObject contentMeta = CrdtMeta::getFieldMeta(
                obj, QStringLiteral("content"));
            QVERIFY(!contentMeta.isEmpty());
            QVERIFY(contentMeta.contains(QStringLiteral("ts")));
            QVERIFY(contentMeta.contains(QStringLiteral("dev")));

            // Verify the right device id ended up on each task.
            const QString id  = obj.value(QStringLiteral("id")).toString();
            const QString dev = contentMeta.value(QStringLiteral("dev")).toString();
            if (id == taskAId)
                QCOMPARE(dev, QStringLiteral("device-A"));
            else if (id == taskBId)
                QCOMPARE(dev, QStringLiteral("device-B"));
            else
                QFAIL("Unexpected task id in saved output");
        }
    }

    // _meta : {} is a valid (empty) entity meta — no field-stamps known.
    // Loading must not crash, and saving back should drop the empty _meta
    // (DataStore::injectMeta only attaches non-empty meta blobs).
    void migration_empty_entity_meta_does_not_crash()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        const QJsonObject root = makeRootWithTask(
            /*version=*/1, taskId, projId, QJsonObject{}, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);    // must not crash on empty meta
        QCOMPARE(ds.allTasks().size(), 1);

        const QJsonObject saved   = ds.toJson();
        const QJsonObject taskObj =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        // injectMeta skips empty meta, so the saved task must not carry
        // a stray empty _meta key.
        QVERIFY2(!taskObj.contains(QStringLiteral("_meta")),
                 "Empty entity meta must not survive a save round-trip");
    }

    // Entity with no _meta key at all (the common case for legacy data
    // written before CRDT meta existed). Must load cleanly.
    void migration_missing_entity_meta_does_not_crash()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        // attach=false → no _meta key in the entity at all.
        const QJsonObject root = makeRootWithTask(
            /*version=*/1, taskId, projId, QJsonObject{}, /*attach=*/false);

        DataStore ds;
        ds.loadFromJson(root);   // must not crash
        QCOMPARE(ds.allTasks().size(), 1);
        QCOMPARE(ds.taskById(taskId)->content(), QStringLiteral("hello"));

        const QJsonObject saved   = ds.toJson();
        const QJsonObject taskObj =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        QVERIFY(!taskObj.contains(QStringLiteral("_meta")));
    }

    // Partial double-wrap is the intentionally-tricky case for the migration:
    //   _meta : { _meta : {...inner...}, otherField : {ts, dev} }
    // i.e. legacy double-wrap with an extra sibling field. The flatten
    // condition in migrateJson is `meta.size() == 1 && meta.contains("_meta")`
    // — so this entity should NOT be flattened (the size-1 guard fails).
    // The non-flatten path must still preserve `otherField` and not corrupt
    // it. CrdtMeta::getFieldMeta("otherField") must continue to work.
    //
    // This protects against a naive flatten that would unconditionally
    // promote inner._meta and lose the sibling field.
    void migration_partial_double_wrap_preserves_sibling_field()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        QJsonObject inner;
        inner[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923f1111:00000001:device-A"),
                      QStringLiteral("device-A"));
        QJsonObject partial;
        partial[QStringLiteral("_meta")]    = inner;
        partial[QStringLiteral("priority")] =
            makeStamp(QStringLiteral("00000001923f2222:00000002:device-A"),
                      QStringLiteral("device-A"));

        const QJsonObject root = makeRootWithTask(
            /*version=*/1, taskId, projId, partial, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);
        QCOMPARE(ds.allTasks().size(), 1);

        const QJsonObject saved   = ds.toJson();
        const QJsonObject taskObj =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        QVERIFY(taskObj.contains(QStringLiteral("_meta")));

        const QJsonObject meta = taskObj.value(QStringLiteral("_meta")).toObject();
        // The sibling 'priority' stamp was preserved verbatim in the meta map.
        QVERIFY2(meta.contains(QStringLiteral("priority")),
                 "Sibling field next to inner _meta must be preserved");

        const QJsonObject prioMeta = CrdtMeta::getFieldMeta(
            taskObj, QStringLiteral("priority"));
        QCOMPARE(prioMeta.value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-A"));
        QCOMPARE(prioMeta.value(QStringLiteral("ts")).toString(),
                 QStringLiteral("00000001923f2222:00000002:device-A"));
    }

    // ── stampEntityField (private — exercised via markTaskModified) ──────────

    // First mark on a fresh entity must populate _meta with a single
    // updated_at stamp (markTaskModified stamps that one field).
    void stamp_first_call_populates_meta()
    {
        DataStore ds;
        Project *inbox = new Project(&ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds.addExistingProject(inbox);

        Task *t = ds.addTask(QStringLiteral("fresh"), inbox->id());
        QVERIFY(t != nullptr);

        // Pre-mark: nothing is in m_taskMeta. After markTaskModified, the
        // serialised entity must carry a single updated_at stamp.
        ds.markTaskModified(t->id());

        const QJsonObject saved = ds.toJson();
        const QJsonArray tasks  = saved.value(QStringLiteral("tasks")).toArray();
        QJsonObject taskObj;
        for (const QJsonValue &v : tasks) {
            if (v.toObject().value(QStringLiteral("id")).toString() == t->id()) {
                taskObj = v.toObject();
                break;
            }
        }
        QVERIFY(!taskObj.isEmpty());
        QVERIFY2(taskObj.contains(QStringLiteral("_meta")),
                 "First stamp must produce a non-empty _meta block");

        const QJsonObject meta = taskObj.value(QStringLiteral("_meta")).toObject();
        QVERIFY(meta.contains(QStringLiteral("updated_at")));

        // The stamp shape is {ts, dev} — single-wrap.
        const QJsonObject ua = meta.value(QStringLiteral("updated_at")).toObject();
        QVERIFY(ua.contains(QStringLiteral("ts")));
        QVERIFY(ua.contains(QStringLiteral("dev")));
        QVERIFY(!ua.value(QStringLiteral("ts")).toString().isEmpty());
        QVERIFY(!ua.value(QStringLiteral("dev")).toString().isEmpty());

        // No outer "_meta" wrapping — the post-Q2.10-fix shape.
        QVERIFY2(!(meta.size() == 1 && meta.contains(QStringLiteral("_meta"))),
                 "stampEntityField must produce single-wrap, not double-wrap");
    }

    // Second stamp on the SAME field replaces the previous {ts} with a newer
    // one. HLC is monotonically advancing, so ts2 > ts1.
    void stamp_second_call_advances_timestamp()
    {
        DataStore ds;
        Project *inbox = new Project(&ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds.addExistingProject(inbox);

        Task *t = ds.addTask(QStringLiteral("twice"), inbox->id());
        ds.markTaskModified(t->id());

        const QJsonObject saved1 = ds.toJson();
        QJsonObject obj1;
        for (const QJsonValue &v : saved1.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == t->id()) {
                obj1 = v.toObject();
                break;
            }
        }
        const QString ts1 =
            obj1.value(QStringLiteral("_meta")).toObject()
                .value(QStringLiteral("updated_at")).toObject()
                .value(QStringLiteral("ts")).toString();
        QVERIFY(!ts1.isEmpty());

        ds.markTaskModified(t->id());   // bump again

        const QJsonObject saved2 = ds.toJson();
        QJsonObject obj2;
        for (const QJsonValue &v : saved2.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == t->id()) {
                obj2 = v.toObject();
                break;
            }
        }
        const QString ts2 =
            obj2.value(QStringLiteral("_meta")).toObject()
                .value(QStringLiteral("updated_at")).toObject()
                .value(QStringLiteral("ts")).toString();
        QVERIFY(!ts2.isEmpty());

        QVERIFY2(HybridLogicalClock::compare(ts2, ts1) > 0,
                 "Re-stamping the same field must produce a strictly newer ts");
    }

    // Two distinct fields must coexist in the same entity meta map — they
    // do not stomp each other.
    void stamp_distinct_fields_coexist()
    {
        DataStore ds;
        Project *inbox = new Project(&ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds.addExistingProject(inbox);

        Task *t = ds.addTask(QStringLiteral("two-fields"), inbox->id());

        // markTaskModified stamps "updated_at". Stage a second different
        // field via the public stampField path on a synthesised JSON, then
        // load that JSON to get both stamps into m_taskMeta — but that's
        // awkward. Easier: we drive it through markTaskModified for the
        // updated_at stamp, then mutate the model so a save reflects both
        // a different field timestamp via direct loadFromJson reseed.
        //
        // Cleanest: build a hand-written state with two stamps in _meta and
        // round-trip it. That exercises extractMeta + injectMeta together,
        // which is the production path for "two distinct fields coexist".
        const QString taskId = t->id();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject metaBlob;
        metaBlob[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923f3333:00000001:device-A"),
                      QStringLiteral("device-A"));
        metaBlob[QStringLiteral("priority")] =
            makeStamp(QStringLiteral("00000001923f4444:00000002:device-A"),
                      QStringLiteral("device-A"));

        QJsonObject root;
        root[QStringLiteral("schema_version")] = 2;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("projects")]       = QJsonArray{ makeInbox(inbox->id()) };
        QJsonObject taskObj;
        taskObj[QStringLiteral("id")]         = taskId;
        taskObj[QStringLiteral("content")]    = QStringLiteral("two-fields");
        taskObj[QStringLiteral("project_id")] = inbox->id();
        taskObj[QStringLiteral("priority")]   = 2;
        taskObj[QStringLiteral("created_at")] = nowIso;
        taskObj[QStringLiteral("updated_at")] = nowIso;
        taskObj[QStringLiteral("_meta")]      = metaBlob;
        root[QStringLiteral("tasks")]    = QJsonArray{ taskObj };
        root[QStringLiteral("sections")] = QJsonArray();
        root[QStringLiteral("labels")]   = QJsonArray();
        root[QStringLiteral("filters")]  = QJsonArray();
        root[QStringLiteral("reminders")]= QJsonArray();
        root[QStringLiteral("comments")] = QJsonArray();

        DataStore ds2;
        ds2.loadFromJson(root);

        const QJsonObject saved = ds2.toJson();
        QJsonObject obj;
        for (const QJsonValue &v : saved.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == taskId) {
                obj = v.toObject();
                break;
            }
        }
        const QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
        QVERIFY(meta.contains(QStringLiteral("content")));
        QVERIFY(meta.contains(QStringLiteral("priority")));

        QCOMPARE(meta.value(QStringLiteral("content")).toObject()
                     .value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-A"));
        QCOMPARE(meta.value(QStringLiteral("priority")).toObject()
                     .value(QStringLiteral("ts")).toString(),
                 QStringLiteral("00000001923f4444:00000002:device-A"));
    }

    // Stamp on the tombstone field is_deleted: removeTask() soft-deletes,
    // which sets is_deleted and stamps updated_at. The is_deleted flag and
    // the stamp on it (or on updated_at, depending on implementation) must
    // both survive the save round-trip — otherwise sync would lose the
    // tombstone signal.
    void stamp_tombstone_field_survives_save()
    {
        DataStore ds;
        Project *inbox = new Project(&ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds.addExistingProject(inbox);

        Task *t = ds.addTask(QStringLiteral("doomed"), inbox->id());
        const QString taskId = t->id();

        QVERIFY(ds.removeTask(taskId));   // soft-delete + stamps

        // Tombstone is still reachable.
        Task *dead = ds.taskById(taskId);
        QVERIFY(dead != nullptr);
        QVERIFY(dead->isDeleted());

        // After save, the entity is in the JSON with is_deleted=true and a
        // _meta block carrying at least one stamp.
        const QJsonObject saved = ds.toJson();
        QJsonObject obj;
        for (const QJsonValue &v : saved.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == taskId) {
                obj = v.toObject();
                break;
            }
        }
        QVERIFY(!obj.isEmpty());
        QVERIFY(obj.value(QStringLiteral("is_deleted")).toBool());
        QVERIFY2(obj.contains(QStringLiteral("_meta")),
                 "Tombstoned task must keep its CRDT meta so peers can merge it");
        const QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
        QVERIFY(!meta.isEmpty());

        // Re-load and confirm the tombstone + meta survive a full round trip.
        DataStore ds2;
        ds2.loadFromJson(saved);
        Task *dead2 = ds2.taskById(taskId);
        QVERIFY(dead2 != nullptr);
        QVERIFY(dead2->isDeleted());
    }

    // ── extractMeta roundtrip via merger code path ───────────────────────────

    // After a hand-written single-wrap state is loaded, then re-serialised,
    // CrdtMeta::getFieldMeta (the merger's read path) must find the stamp
    // exactly where it was written. This is the contract that justifies the
    // private extractMeta helper existing at all.
    void extract_meta_roundtrip_visible_to_merger()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        QJsonObject metaBlob;
        metaBlob[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923fCAFE:00000005:device-X"),
                      QStringLiteral("device-X"));

        const QJsonObject root = makeRootWithTask(
            /*version=*/2, taskId, projId, metaBlob, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);

        const QJsonObject saved = ds.toJson();
        QJsonObject obj;
        for (const QJsonValue &v : saved.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == taskId) {
                obj = v.toObject();
                break;
            }
        }
        QVERIFY(!obj.isEmpty());

        // The stamp must be reachable via the production read API.
        const QJsonObject contentMeta = CrdtMeta::getFieldMeta(
            obj, QStringLiteral("content"));
        QVERIFY2(!contentMeta.isEmpty(),
                 "extractMeta+injectMeta must keep the stamp visible to CrdtMeta::getFieldMeta");
        QCOMPARE(contentMeta.value(QStringLiteral("ts")).toString(),
                 QStringLiteral("00000001923fCAFE:00000005:device-X"));
        QCOMPARE(contentMeta.value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-X"));
    }

    // extractMeta also has a defensive flatten path for blobs that look
    // legacy even on a v2 file (paranoid defence-in-depth — see comment in
    // datastore.cpp). Verify it: a v2 file that nonetheless contains a
    // double-wrap entity must come back single-wrap after a round-trip.
    void extract_meta_defensive_flatten_on_v2_file()
    {
        const QString taskId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        QJsonObject inner;
        inner[QStringLiteral("content")] =
            makeStamp(QStringLiteral("00000001923fDEAD:00000006:device-Y"),
                      QStringLiteral("device-Y"));
        QJsonObject doubleWrap;
        doubleWrap[QStringLiteral("_meta")] = inner;

        // schema_version=2 → migrateJson will NOT run. The defensive flatten
        // in extractMeta is the only thing that can fix this entity.
        const QJsonObject root = makeRootWithTask(
            /*version=*/2, taskId, projId, doubleWrap, /*attach=*/true);

        DataStore ds;
        ds.loadFromJson(root);
        QCOMPARE(ds.loadedSchemaVersion(), 2);

        const QJsonObject saved = ds.toJson();
        QJsonObject obj;
        for (const QJsonValue &v : saved.value(QStringLiteral("tasks")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == taskId) {
                obj = v.toObject();
                break;
            }
        }
        QVERIFY(!obj.isEmpty());

        const QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(meta.contains(QStringLiteral("content")),
                 "Defensive flatten must surface 'content' even on a v2 file");
        QVERIFY2(!(meta.size() == 1 && meta.contains(QStringLiteral("_meta"))),
                 "Defensive flatten must remove the outer _meta wrapper");

        const QJsonObject contentMeta = CrdtMeta::getFieldMeta(
            obj, QStringLiteral("content"));
        QCOMPARE(contentMeta.value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-Y"));
    }
};

QTEST_GUILESS_MAIN(TestDataStoreMetaHelpers)
#include "test_datastore_meta_helpers.moc"
