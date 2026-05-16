// =============================================================================
// CRDT _meta single-wrap regression guard (formerly: double-wrap pin)
// =============================================================================
//
// HISTORY
// -------
//   This test was originally written as a PIN that asserted the BROKEN
//   double-wrap shape — i.e. the bug was deliberately frozen so that any
//   future fix would make the test fail loudly. The fix has now landed:
//   stampEntityField writes per-field stamps directly into m_*Meta (no extra
//   _meta layer), and a v1→v2 migration in DataStore::migrateJson (plus a
//   defensive flatten in extractMeta) silently upgrades user state.json
//   files written by older builds.
//
// PURPOSE NOW
// -----------
//   Guard the IDEAL single-level layout described in the doc-comment of
//   src/sync/crdt_meta.h:
//
//     entity["_meta"][fieldName] == { "ts": "<hlc>", "dev": "<id>" }
//
//   And explicitly assert that the broken layout
//
//     entity["_meta"]["_meta"][fieldName] == { "ts": "<hlc>", "dev": "<id>" }
//
//   does NOT come back.
//
//
// WHAT IS THE DOUBLE-WRAP
// -----------------------
//   Trace, for any entity (e.g. Task), starting from a mutation:
//
//   1. DataStore::stampEntityField(id, m_taskMeta, "content") is called
//      [src/storage/datastore.cpp:40-50]. It loads `meta = m_taskMeta[id]`
//      (a fresh QJsonObject the first time), then calls:
//
//          CrdtMeta::setFieldMeta(meta, "content", ts, dev);
//
//      CrdtMeta::setFieldMeta [src/sync/crdt_meta.cpp:51-62] writes:
//
//          meta["_meta"]["content"] = { ts, dev }
//
//      i.e. it inserts a literal "_meta" key INSIDE the QJsonObject it was
//      handed. So m_taskMeta[id] now equals  { "_meta": { "content": ... } }.
//
//   2. DataStore::toJson() [src/storage/datastore.cpp:1118-1120,1153] calls:
//
//          injectMeta(t->toJson(), m_taskMeta, t->id())
//
//      injectMeta [src/storage/datastore.cpp:53-61] does:
//
//          entityJson["_meta"] = m_taskMeta[id];
//
//      i.e. it wraps the entire `{ "_meta": { "content": ... } }` blob under
//      ANOTHER "_meta" key. Final on-disk shape:
//
//          entity["_meta"] = { "_meta": { "content": { ts, dev } } }
//
//   3. The inverse operation extractMeta [src/storage/datastore.cpp:64-73]
//      reads `entityJson["_meta"]` (the doubly-wrapped object) and stores
//      that whole thing back into `m_taskMeta[id]`. So the round-trip is
//      structurally symmetric — load, save, load, save all preserve the
//      same shape on disk.
//
//
// IS IT INTENTIONAL? — NO. IS IT A BUG?  — YES, A SILENT-DEGRADATION BUG.
// -----------------------------------------------------------------------
//   The CrdtMeta API was clearly designed to be called with the raw entity
//   JSON (look at getFieldMeta, hasMeta, getMeta — they all read ONE level
//   of "_meta"). The doc comment at src/sync/crdt_meta.h:11-25 shows the
//   single-level layout as the intended structure.
//
//   DataStore confused two layers: it kept its per-entity meta map as a
//   "naked" QJsonObject (expected to be the value of the "_meta" key on the
//   entity), but stamped fields by passing that map ENTRY through CrdtMeta
//   helpers that ALSO write a "_meta" key. There is no second "namespace
//   layer" being modelled — it's just the same key applied twice.
//
//   IMPACT on Merger [src/sync/merger.cpp]:
//
//   - SyncEngine::pull() at line 281 calls
//        m_merger->mergeStates(m_store->toJson(), remoteState).
//     Both inputs have the doubly-wrapped shape (local because toJson()
//     produces it; remote because the other peer also ran toJson() to
//     produce its state.json upload).
//
//   - Merger::mergeEntity calls CrdtMeta::hasMeta(local) [merger.cpp:139].
//     hasMeta returns true (because entity["_meta"] is a non-empty object
//     — it contains a single key, "_meta"). So the CRDT path is taken:
//     resolveWithCrdt.
//
//   - resolveWithCrdt iterates the union of *data* field names ("content",
//     "priority", etc.) skipping "_meta" via isInternalField [merger.cpp:
//     346-350]. For each data field it calls CrdtMeta::getFieldMeta(local,
//     field) which reads `local["_meta"][field]` — but that path doesn't
//     exist; the actual data is at `local["_meta"]["_meta"][field]`. So
//     getFieldMeta returns an empty object for EVERY scalar field.
//
//   - Both lFm and rFm being empty drives the `lFm.isEmpty() && rFm.isEmpty()`
//     branch [merger.cpp:251-253], which falls back to whole-entity LWW on
//     `updated_at`. Result: the per-field CRDT semantics — the whole
//     reason _meta exists — are SILENTLY DISABLED for live data.
//
//   - The OR-Set (labels) path [merger.cpp:205-244] is just as broken:
//     getOrSetElements reads `entity["_meta"][field]["elements"]`, again
//     one level too shallow, returns empty, falls back to whole-entity LWW.
//
//   - applyPatch [src/sync/syncengine.cpp:383-403] is partially insulated
//     because Task::toJson() (the "current" side of applyPatch) does NOT
//     emit _meta — only injectMeta does, and injectMeta is only called from
//     DataStore::toJson, not from per-task toJson. So applyPatch's own
//     "_meta" handling operates on whatever shape the patch uses (which
//     would also be the doubly-wrapped shape if the patch was produced by
//     DataStore::toJson at commit time). Either way the field-meta lookup
//     downstream in Merger looks at the wrong path.
//
//   - validateEntity [merger.cpp:52-83] does NOT reject the double-wrap.
//     It only iterates one level of `_meta` looking for `dev` strings <=64
//     chars. The single key it sees is "_meta", whose value is an object,
//     and there is no "dev" field at that level — so the per-field length
//     check is just no-op'd. The double-wrap escapes validation cleanly.
//
//   NET EFFECT: a sync between two peers that both have non-empty meta
//   maps degrades to "newer updated_at wins for the whole entity". Two
//   peers concurrently editing different fields no longer get to keep
//   their respective fields — one whole entity wins. The carefully built
//   per-field CRDT machinery is bypassed by a missing/duplicate key.
//
//   It is not a *crash* bug. It is a silent semantic regression: data is
//   not lost in the single-writer case, but multi-writer concurrent
//   field-level conflicts no longer merge correctly. Whichever peer pushed
//   last (highest updated_at on the entity) drops the other peer's
//   un-merged field edits.
//
//
// FIX COMPLEXITY ESTIMATE
// -----------------------
//   The root cause is a one-line confusion in DataStore::stampEntityField
// [src/storage/datastore.cpp:40-50]:
//
//     // Current (wrong):
//     QJsonObject meta = metaMap.value(entityId);            // naked blob
//     CrdtMeta::setFieldMeta(meta, fieldName, ts, devId);    // writes meta["_meta"][f]
//     metaMap.insert(entityId, meta);                        // stores doubly-keyed
//
//   The minimum-mechanical fix is two lines: stop wrapping. Treat the per-
//   entity map value as the LITERAL "_meta" object (matching the comment
//   in datastore.h:222 — "entityId → _meta blob"), and write fields
//   directly:
//
//     // Proposed (correct):
//     QJsonObject meta = metaMap.value(entityId);
//     QJsonObject fm; fm["ts"] = ts; fm["dev"] = devId;
//     meta[fieldName] = fm;
//     metaMap.insert(entityId, meta);
//
//   ...and matching reads/extracts. injectMeta and extractMeta would not
//   need to change.
//
//   HOWEVER: a forward-only state.json migration is REQUIRED, because
//   existing state.json files on user disks are doubly-wrapped. After the
//   fix, extractMeta would store m_taskMeta[id] = { "_meta": { ... } } and
//   then field reads via CrdtMeta::getFieldMeta would still go to the wrong
//   level. So the loadFromJson path needs to detect the legacy double-wrap
//   (e.g. metaMap value contains exactly one key, "_meta", whose value is an
//   object) and unwrap it once. The schema_version mechanism at
//   datastore.cpp:1100-1108 (currently a no-op pass-through to v1) is the
//   right place — bump CURRENT_SCHEMA_VERSION to 2 and add an unwrap pass.
//
//   Total: ~3 source files (datastore.cpp, possibly crdt_meta.cpp comment,
//   the migration step), small surface area, but state.json migration
//   means it cannot be a one-line patch. Plus: every existing test that
//   checks the double-wrap shape (test_section_model.cpp lines 399-484,
//   test_comment_extended.cpp, test_reminder_model.cpp, this pin file)
//   will need to be updated to match the corrected single-level shape.
//
//   The fact that production sync degrades to "whole-entity LWW" — exactly
//   what the doc-comment in crdt_meta.h:30-32 calls the LEGACY fallback —
//   means that field-level CRDT has been *off* for live data since the
//   double-wrap was introduced. Fixing it improves merge correctness for
//   multi-device users. Pre-fix users do not lose data; they just lose the
//   per-field-merge guarantee.
//
//
// VERIFIED: TASK DOES IT TOO
// --------------------------
//   The same `stampEntityField` + `injectMeta` flow is used for *all*
// entity types (datastore.cpp:1118-1153). This pin uses Task because the
// task suffers the same double-wrap as Section, even though the project's
// commentary so far has focused on Section/Comment/Reminder. This pin
// asserts the current (broken) shape so that any future fix makes this
// test FAIL — at which point it should be deleted (or inverted) along
// with the matching tests in the section/comment/reminder files.
//
// =============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>

#include "model/task.h"
#include "model/project.h"
#include "storage/datastore.h"

class TestMetaDoubleWrapPin : public QObject
{
    Q_OBJECT

private:
    static void configureSettings()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_MetaDoubleWrapPin"));
        QCoreApplication::setApplicationName(QStringLiteral("test_meta_double_wrap_pin"));
    }

private slots:
    void initTestCase() { configureSettings(); }
    void cleanupTestCase() {}

    // ── Regression guard: Task entity must serialise with single-wrap _meta.
    void task_serialised_state_has_single_wrap_meta_key()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Pin Project"));
        QVERIFY(p != nullptr);
        Task *t = ds.addTask(QStringLiteral("Pin task"), p->id());
        QVERIFY(t != nullptr);

        const QJsonObject root = ds.toJson();
        QVERIFY(root.contains(QStringLiteral("tasks")));
        const QJsonArray tasksArr =
            root.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(tasksArr.size(), 1);

        const QJsonObject obj = tasksArr.at(0).toObject();
        QCOMPARE(obj.value(QStringLiteral("id")).toString(), t->id());

        // The single "_meta" must be present — DataStore::injectMeta wrote it.
        QVERIFY2(obj.contains(QStringLiteral("_meta")),
                 "addTask must inject a _meta blob into the serialised state");

        const QJsonObject meta =
            obj.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(!meta.isEmpty(),
                 "_meta must be a non-empty object");

        // REGRESSION GUARD: the _meta must NOT contain an inner "_meta"
        // key. If this fails the double-wrap bug has come back.
        QVERIFY2(!meta.contains(QStringLiteral("_meta")),
                 "REGRESSION: _meta wraps an inner _meta key — the legacy "
                 "double-wrap bug is back. CrdtMeta::getFieldMeta would "
                 "silently return empty for every scalar field, degrading "
                 "merge to whole-entity LWW.");

        // The map must contain at least the "content" field, because
        // DataStore::addTask explicitly stamps it (datastore.cpp:139-146).
        QVERIFY2(meta.contains(QStringLiteral("content")),
                 "_meta must include 'content' field stamped by addTask");

        const QJsonObject contentMeta =
            meta.value(QStringLiteral("content")).toObject();
        QVERIFY2(contentMeta.contains(QStringLiteral("ts")),
                 "field meta must carry an HLC 'ts' string");
        QVERIFY2(contentMeta.contains(QStringLiteral("dev")),
                 "field meta must carry a 'dev' deviceId string");
        QVERIFY(!contentMeta.value(QStringLiteral("ts")).toString().isEmpty());
        QVERIFY(!contentMeta.value(QStringLiteral("dev")).toString().isEmpty());

        // The IDEAL single-level layout: CrdtMeta::getFieldMeta(obj, "content")
        // reads obj["_meta"]["content"] and now returns the real stamp.
        const QJsonObject directLookup =
            obj.value(QStringLiteral("_meta")).toObject()
               .value(QStringLiteral("content")).toObject();
        QVERIFY2(!directLookup.isEmpty(),
                 "obj['_meta']['content'] must be non-empty — this is the "
                 "exact lookup Merger::resolveWithCrdt performs.");
        QCOMPARE(directLookup.value(QStringLiteral("ts")).toString(),
                 contentMeta.value(QStringLiteral("ts")).toString());
    }

    // ── Migration: legacy double-wrap state.json silently flattens on load.
    void legacy_double_wrap_state_is_migrated_on_load()
    {
        // Hand-craft a v1 root containing an entity with the old buggy
        // double-wrap shape. After loadFromJson + toJson round-trip the
        // shape must be flattened to the single-wrap form.
        const QString taskId    = QStringLiteral("legacy-task-uuid");
        const QString projectId = QStringLiteral("legacy-proj-uuid");
        const QString hlcTs     = QStringLiteral(
            "00000001923f1234:00000003:device-X");

        QJsonObject contentMeta;
        contentMeta[QStringLiteral("ts")]  = hlcTs;
        contentMeta[QStringLiteral("dev")] = QStringLiteral("device-X");

        QJsonObject innerFieldMap;
        innerFieldMap[QStringLiteral("content")] = contentMeta;

        QJsonObject doubleWrap;
        doubleWrap[QStringLiteral("_meta")] = innerFieldMap;

        QJsonObject task;
        task[QStringLiteral("id")]         = taskId;
        task[QStringLiteral("content")]    = QStringLiteral("Hi");
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        task[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        task[QStringLiteral("_meta")] = doubleWrap;

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        project[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

        QJsonObject root;
        root[QStringLiteral("schema_version")] = 1;        // legacy
        root[QStringLiteral("tasks")]    = QJsonArray{task};
        root[QStringLiteral("projects")] = QJsonArray{project};

        DataStore ds;
        ds.loadFromJson(root);

        // Migration ran — schema bumped to v2.
        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);

        // Re-serialise: per-entity meta is now flat single-wrap.
        const QJsonObject saved = ds.toJson();
        const QJsonObject taskOut =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        QVERIFY(taskOut.contains(QStringLiteral("_meta")));
        const QJsonObject metaOut =
            taskOut.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(!metaOut.contains(QStringLiteral("_meta")),
                 "Legacy double-wrap was not flattened by migration.");
        QVERIFY2(metaOut.contains(QStringLiteral("content")),
                 "Migrated meta must carry the inner 'content' field stamp.");
        QCOMPARE(metaOut.value(QStringLiteral("content")).toObject()
                       .value(QStringLiteral("ts")).toString(),
                 hlcTs);
    }
};

QTEST_GUILESS_MAIN(TestMetaDoubleWrapPin)
#include "test_meta_double_wrap_pin.moc"
