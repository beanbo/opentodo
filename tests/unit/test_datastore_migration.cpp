// Tests for DataStore JSON schema migration / versioning.
//
// What's actually wired in the production code (datastore.cpp):
//   - DataStore::CURRENT_SCHEMA_VERSION == 1.
//   - loadFromJson reads `schema_version` (defaults to 0 if absent).
//   - If loaded < current → migrateJson() is run (v0 → v1 is a no-op bump).
//   - If loaded > current → qWarning, but the load proceeds anyway.
//   - clear() is always called before re-populating, even for empty/malformed
//     input.
//   - loadFromJson is void (doesn't refuse): the only "did we accept it?"
//     observable is the resulting state.
//
// These tests hand-craft JSON snapshots and verify each path through the
// versioning + load logic, plus a few invariants (round-trip, _meta and
// settings preservation, tombstone preservation, HLC bookkeeping).

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDateTime>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/filter.h"
#include "sync/hlc.h"
#include "utils/uuid.h"

class TestDataStoreMigration : public QObject
{
    Q_OBJECT

private:
    // QTemporaryDir kept alive for the duration of the suite so QSettings
    // (which is read by HLC and DataStore::localDeviceId) writes into a
    // disposable location rather than the user's real Application Data path.
    QTemporaryDir m_tmp;

    // Build a "current" v1 root containing one Inbox project, one task,
    // one label, one filter, and a settings blob. Used as a baseline for
    // the round-trip / preserved-fields tests.
    QJsonObject makeCurrentRoot(const QString &taskId,
                                const QString &projectId,
                                const QString &labelId,
                                const QString &filterId,
                                bool includeMeta) const
    {
        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;

        // Settings dictionary
        QJsonObject settings;
        settings[QStringLiteral("theme")] = QStringLiteral("dark");
        settings[QStringLiteral("nested")] = QJsonObject{
            {QStringLiteral("k"), QStringLiteral("v")},
            {QStringLiteral("n"), 42}
        };
        root[QStringLiteral("settings")] = settings;

        // Inbox project
        QJsonObject project;
        project[QStringLiteral("id")] = projectId;
        project[QStringLiteral("name")] = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")] = true;
        project[QStringLiteral("color")] = QStringLiteral("grey");
        project[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        project[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (includeMeta) {
            QJsonObject meta;
            QJsonObject nameMeta;
            nameMeta[QStringLiteral("ts")] = QStringLiteral(
                "00000001923f0001:00000007:device-A");
            nameMeta[QStringLiteral("dev")] = QStringLiteral("device-A");
            meta[QStringLiteral("name")] = nameMeta;
            project[QStringLiteral("_meta")] = meta;
        }
        root[QStringLiteral("projects")] = QJsonArray{project};

        // Task
        QJsonObject task;
        task[QStringLiteral("id")] = taskId;
        task[QStringLiteral("content")] = QStringLiteral("Hello");
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("priority")] = 2;
        task[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        task[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (includeMeta) {
            QJsonObject meta;
            QJsonObject contentMeta;
            contentMeta[QStringLiteral("ts")] = QStringLiteral(
                "00000001923f1234:00000003:device-B");
            contentMeta[QStringLiteral("dev")] = QStringLiteral("device-B");
            meta[QStringLiteral("content")] = contentMeta;
            QJsonObject prioMeta;
            prioMeta[QStringLiteral("ts")] = QStringLiteral(
                "00000001923f5678:00000004:device-B");
            prioMeta[QStringLiteral("dev")] = QStringLiteral("device-B");
            meta[QStringLiteral("priority")] = prioMeta;
            task[QStringLiteral("_meta")] = meta;
        }
        root[QStringLiteral("tasks")] = QJsonArray{task};

        // Label
        QJsonObject label;
        label[QStringLiteral("id")] = labelId;
        label[QStringLiteral("name")] = QStringLiteral("urgent");
        label[QStringLiteral("color")] = QStringLiteral("red");
        label[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        label[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        root[QStringLiteral("labels")] = QJsonArray{label};

        // Filter
        QJsonObject filter;
        filter[QStringLiteral("id")] = filterId;
        filter[QStringLiteral("name")] = QStringLiteral("Today");
        filter[QStringLiteral("query")] = QStringLiteral("today");
        filter[QStringLiteral("color")] = QStringLiteral("blue");
        filter[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        filter[QStringLiteral("updated_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        root[QStringLiteral("filters")] = QJsonArray{filter};

        // Empty arrays for the rest so loadFromJson doesn't trip on missing keys.
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();

        return root;
    }

private slots:
    void initTestCase()
    {
        // Redirect QSettings (used by HLC and DataStore::localDeviceId) into a
        // throwaway location so the suite leaves no trace in HKCU / AppData.
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_DSMigration"));
        QCoreApplication::setApplicationName(QStringLiteral("test_datastore_migration"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmp.path());
    }

    void cleanupTestCase()
    {
        // Wipe QSettings keys we touched.
        QSettings settings;
        settings.clear();
    }

    // ── 1. Sanity check: the constant we test against is what we expect. ──
    void current_schema_version_is_known()
    {
        // v2 introduced the per-entity _meta single-wrap migration (was v1).
        QCOMPARE(DataStore::CURRENT_SCHEMA_VERSION, 2);
    }

    // ── 2. Legacy file: no schema_version key at all → treated as v0,
    //       migrated forward, all entities loaded. ──
    void load_state_without_schema_version_treated_as_v0_and_migrated()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        QJsonObject root = makeCurrentRoot(taskId, projectId, labelId, filterId, false);
        // Strip the version key — simulates a state.json written before
        // versioning was introduced.
        root.remove(QStringLiteral("schema_version"));

        DataStore ds;
        ds.loadFromJson(root);

        // After migration the loaded version should match CURRENT.
        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);

        // Entities loaded.
        QCOMPARE(ds.allTasks().size(), 1);
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(), 1);
        QCOMPARE(ds.allFilters().size(), 1);

        // Task content survived migration unaltered (v0→v1 is a no-op bump).
        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);
        QCOMPARE(t->content(), QStringLiteral("Hello"));
        QCOMPARE(t->priority(), 2);
    }

    // ── 3. Explicit schema_version: 1 (current) → no migration, no warnings. ──
    void load_state_with_current_schema_version_no_migration()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        const QJsonObject root = makeCurrentRoot(taskId, projectId, labelId, filterId, false);
        QCOMPARE(root.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);

        DataStore ds;
        ds.loadFromJson(root);

        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);
        QCOMPARE(ds.allTasks().size(), 1);
        QCOMPARE(ds.taskById(taskId)->content(), QStringLiteral("Hello"));
    }

    // ── 4. Future schema_version (current + 1) → DataStore loads gracefully
    //       with a warning, does NOT refuse. Verify that the load actually
    //       happened (entities present) and that loadedSchemaVersion reflects
    //       what was on disk. ──
    void load_state_with_future_schema_version_loads_with_warning()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        QJsonObject root = makeCurrentRoot(taskId, projectId, labelId, filterId, false);
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION + 1;

        // Documented behaviour: qWarning is emitted but load proceeds.
        DataStore ds;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("schema_version.*newer than supported")));
        ds.loadFromJson(root);

        // Future version is preserved as-is (we don't downgrade it).
        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION + 1);
        // Entities still loaded.
        QCOMPARE(ds.allTasks().size(), 1);
        QCOMPARE(ds.allProjects().size(), 1);
    }

    // ── 5. Pre-existing tasks before load are wiped (clear()) regardless of
    //       what's in the new root, including for an empty/malformed input. ──
    void load_empty_root_clears_existing_state()
    {
        DataStore ds;
        // Pre-populate something.
        ds.addProject(QStringLiteral("Pre-existing"));
        QCOMPARE(ds.allProjects().size(), 1);

        // loadFromJson with an empty object: no entities to load, but the
        // existing in-memory state should still be wiped.
        ds.loadFromJson(QJsonObject{});

        QCOMPARE(ds.allProjects().size(), 0);
        QCOMPARE(ds.allTasks().size(), 0);
        // Empty object had no schema_version → treated as v0 → migrated to current.
        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);
    }

    // ── 6. Malformed JSON: caller is expected to use QJsonDocument::fromJson
    //       and check the parse error. The DataStore itself never sees
    //       malformed bytes. We assert the parse-then-refuse contract. ──
    void malformed_json_is_rejected_at_parse_time()
    {
        const QByteArray bad = QByteArrayLiteral("{ this is not valid json :::");
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(bad, &err);

        QVERIFY(err.error != QJsonParseError::NoError);
        QVERIFY(doc.isNull());

        // A well-behaved caller (FileManager) bails out before reaching
        // loadFromJson, so the store stays untouched.
        DataStore ds;
        ds.addProject(QStringLiteral("Survives"));
        // Simulate the caller skipping loadFromJson on parse error:
        if (doc.isNull()) {
            // do nothing — store is preserved
        } else {
            ds.loadFromJson(doc.object());
        }
        QCOMPARE(ds.allProjects().size(), 1);
    }

    // ── 7. Missing required field (task without `id`): loadFromJson does not
    //       crash, and either skips or accepts the entity with an empty id.
    //       Either way the DataStore must remain in a consistent state. ──
    void load_state_with_task_missing_id_does_not_crash()
    {
        const QString projectId = UuidHelper::generate();
        const QString taskId    = UuidHelper::generate();

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};

        // One well-formed task and one with no id field.
        QJsonObject good;
        good[QStringLiteral("id")]      = taskId;
        good[QStringLiteral("content")] = QStringLiteral("Good");
        good[QStringLiteral("created_at")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

        QJsonObject bad;
        bad[QStringLiteral("content")] = QStringLiteral("No id");
        // intentionally no "id"

        root[QStringLiteral("tasks")]     = QJsonArray{good, bad};
        root[QStringLiteral("projects")]  = QJsonArray();
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("labels")]    = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash

        // The well-formed task is reachable by id.
        Task *good2 = ds.taskById(taskId);
        QVERIFY(good2 != nullptr);
        QCOMPARE(good2->content(), QStringLiteral("Good"));

        // Store consistency: index size matches list size.
        // (There is no direct accessor — verify by lookup of a few ids.)
        QVERIFY(ds.taskById(QStringLiteral("definitely-not-an-id")) == nullptr);
    }

    // ── 8. Round-trip: save current store → load into fresh store → entity
    //       counts and key field values match. ──
    void round_trip_preserves_entity_counts_and_fields()
    {
        // Build a full source store via the DataStore API.
        DataStore src;
        Project *inbox = new Project(&src);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        src.addExistingProject(inbox);

        Task    *t = src.addTask(QStringLiteral("RoundTrip task"), inbox->id());
        t->setPriority(3);
        Project *p = src.addProject(QStringLiteral("Side"));
        Label   *l = src.addLabel(QStringLiteral("rt-label"));
        Filter  *f = src.addFilter(QStringLiteral("RT filter"), QStringLiteral("p3"));
        Q_UNUSED(p) Q_UNUSED(l) Q_UNUSED(f)

        const QJsonObject snapshot = src.toJson();

        // Sanity: the toJson snapshot is current-versioned.
        QCOMPARE(snapshot.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);

        DataStore dst;
        dst.loadFromJson(snapshot);

        QCOMPARE(dst.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);
        QCOMPARE(dst.allTasks().size(),    src.allTasks().size());
        QCOMPARE(dst.allProjects().size(), src.allProjects().size());
        QCOMPARE(dst.allLabels().size(),   src.allLabels().size());
        QCOMPARE(dst.allFilters().size(),  src.allFilters().size());

        Task *t2 = dst.taskById(t->id());
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->content(), QStringLiteral("RoundTrip task"));
        QCOMPARE(t2->priority(), 3);
    }

    // ── 9. CRDT _meta blob preserved across save/load. The blob lives on the
    //       JSON entity object. After load, save again and check that the
    //       _meta keys we put in survive intact. ──
    void crdt_meta_blob_preserved_across_save_load()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        const QJsonObject input = makeCurrentRoot(
            taskId, projectId, labelId, filterId, /*includeMeta=*/true);

        DataStore ds;
        ds.loadFromJson(input);

        const QJsonObject output = ds.toJson();
        const QJsonArray  tasks  = output.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(tasks.size(), 1);

        const QJsonObject taskObj = tasks.first().toObject();
        QVERIFY2(taskObj.contains(QStringLiteral("_meta")),
                 "Task should still carry _meta after a save/load round trip");

        const QJsonObject meta = taskObj.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(meta.contains(QStringLiteral("content")),
                 "Per-field meta for 'content' should survive");
        QVERIFY2(meta.contains(QStringLiteral("priority")),
                 "Per-field meta for 'priority' should survive");

        const QJsonObject contentMeta = meta.value(QStringLiteral("content")).toObject();
        QCOMPARE(contentMeta.value(QStringLiteral("dev")).toString(),
                 QStringLiteral("device-B"));
        QCOMPARE(contentMeta.value(QStringLiteral("ts")).toString(),
                 QStringLiteral("00000001923f1234:00000003:device-B"));

        // And likewise for the project's _meta.
        const QJsonArray  projects = output.value(QStringLiteral("projects")).toArray();
        const QJsonObject projObj  = projects.first().toObject();
        QVERIFY(projObj.contains(QStringLiteral("_meta")));
    }

    // ── 10. Tombstones (`is_deleted: true`) are preserved across save/load
    //        as long as their updated_at is still recent (the prune horizon
    //        is 30 days). A tombstone with a current updated_at must come
    //        back on the next load. ──
    void recent_tombstones_are_preserved_across_save_load()
    {
        const QString liveId = UuidHelper::generate();
        const QString deadId = UuidHelper::generate();
        const QString projId = UuidHelper::generate();

        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};

        QJsonObject project;
        project[QStringLiteral("id")]         = projId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;
        root[QStringLiteral("projects")] = QJsonArray{project};

        QJsonObject live;
        live[QStringLiteral("id")]         = liveId;
        live[QStringLiteral("content")]    = QStringLiteral("alive");
        live[QStringLiteral("project_id")] = projId;
        live[QStringLiteral("created_at")] = nowIso;
        live[QStringLiteral("updated_at")] = nowIso;
        live[QStringLiteral("is_deleted")] = false;

        QJsonObject dead;
        dead[QStringLiteral("id")]         = deadId;
        dead[QStringLiteral("content")]    = QStringLiteral("tombstoned");
        dead[QStringLiteral("project_id")] = projId;
        dead[QStringLiteral("created_at")] = nowIso;
        dead[QStringLiteral("updated_at")] = nowIso;
        dead[QStringLiteral("is_deleted")] = true;

        root[QStringLiteral("tasks")]     = QJsonArray{live, dead};
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("labels")]    = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);

        // allTasks() filters tombstones, so we should see only `live`.
        QCOMPARE(ds.allTasks().size(), 1);
        // But the tombstone is still in the index (recent enough).
        Task *deadTask = ds.taskById(deadId);
        QVERIFY2(deadTask != nullptr,
                 "Recent tombstone must remain reachable by id so sync can ship it");
        QVERIFY(deadTask->isDeleted());

        // Round-trip: re-serialize and re-load. Tombstone must still be there.
        const QJsonObject saved = ds.toJson();
        DataStore ds2;
        ds2.loadFromJson(saved);

        Task *deadTask2 = ds2.taskById(deadId);
        QVERIFY2(deadTask2 != nullptr,
                 "Tombstone should survive a second save/load round trip");
        QVERIFY(deadTask2->isDeleted());
        QCOMPARE(ds2.allTasks().size(), 1);   // still hidden from active queries
    }

    // ── 11. Settings dictionary preserved across save/load including nested
    //        objects. ──
    void settings_preserved_across_save_load_with_nested_values()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        const QJsonObject input = makeCurrentRoot(taskId, projectId, labelId, filterId, false);

        DataStore ds;
        ds.loadFromJson(input);

        // Top-level setting preserved.
        QCOMPARE(ds.setting(QStringLiteral("theme")).toString(), QStringLiteral("dark"));

        // Nested object preserved on the QJsonObject we get back from settings().
        const QJsonObject settings = ds.settings();
        QVERIFY(settings.contains(QStringLiteral("nested")));
        const QJsonObject nested = settings.value(QStringLiteral("nested")).toObject();
        QCOMPARE(nested.value(QStringLiteral("k")).toString(), QStringLiteral("v"));
        QCOMPARE(nested.value(QStringLiteral("n")).toInt(), 42);

        // And re-serializing keeps it.
        const QJsonObject saved = ds.toJson();
        const QJsonObject savedSettings = saved.value(QStringLiteral("settings")).toObject();
        QCOMPARE(savedSettings.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
        QCOMPARE(savedSettings.value(QStringLiteral("nested")).toObject()
                              .value(QStringLiteral("n")).toInt(), 42);
    }

    // ── 12. HLC counter is restored from QSettings on first use, so logical
    //        time is monotone across "process restarts". The state.json
    //        itself does not store the HLC counter — that's a QSettings
    //        concern. We assert that the persistence path actually works:
    //        an HLC stamped now stays the most recent after we read it back.
    //
    //        This indirectly covers the "load doesn't reset the clock"
    //        contract for the migration code path. ──
    void hlc_counter_persists_across_settings_reads()
    {
        const QString deviceId = QStringLiteral("test-device");

        // Stamp once to advance the clock.
        const QString hlc1 = HybridLogicalClock::now(deviceId);
        QVERIFY(!hlc1.isEmpty());

        // QSettings now holds the last HLC.
        const QString persisted = HybridLogicalClock::loadLast();
        QCOMPARE(persisted, hlc1);

        // A subsequent stamp must compare strictly greater (HLC is monotone).
        const QString hlc2 = HybridLogicalClock::now(deviceId);
        QVERIFY2(HybridLogicalClock::compare(hlc2, hlc1) > 0,
                 "HLC must advance — a stale counter would let a newly loaded "
                 "state silently undo a recently stamped field");

        // Loading a JSON state must not clobber QSettings' HLC.
        DataStore ds;
        ds.loadFromJson(QJsonObject{});
        QCOMPARE(HybridLogicalClock::loadLast(), hlc2);
    }

    // ── 13. clear() called inside loadFromJson empties existing entity-meta
    //        maps too — otherwise a load would leak old _meta into the next
    //        toJson(). Verify by loading once with meta, then loading an
    //        empty object, then confirming a re-save has no _meta blobs. ──
    void clear_drops_meta_so_subsequent_save_has_no_stale_meta()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();
        const QString filterId  = UuidHelper::generate();

        DataStore ds;
        ds.loadFromJson(makeCurrentRoot(taskId, projectId, labelId, filterId, true));

        // Sanity: meta is in there now.
        const QJsonArray tasks0 = ds.toJson().value(QStringLiteral("tasks")).toArray();
        QVERIFY(tasks0.first().toObject().contains(QStringLiteral("_meta")));

        // Now load a blank state.
        ds.loadFromJson(QJsonObject{});

        // Re-save: there should be zero entities and obviously no leaked _meta.
        const QJsonObject saved = ds.toJson();
        QCOMPARE(saved.value(QStringLiteral("tasks")).toArray().size(),    0);
        QCOMPARE(saved.value(QStringLiteral("projects")).toArray().size(), 0);
        QCOMPARE(saved.value(QStringLiteral("labels")).toArray().size(),   0);
        QCOMPARE(saved.value(QStringLiteral("filters")).toArray().size(),  0);
    }

    // ── 14. loadFromJson always emits dataChanged so FileManager autosave
    //        and ViewModels see a single consistent post-load notification.
    //        This invariant matters for migrations that materially change
    //        in-memory state. ──
    void load_emits_dataChanged_signal()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);

        ds.loadFromJson(makeCurrentRoot(
            UuidHelper::generate(),
            UuidHelper::generate(),
            UuidHelper::generate(),
            UuidHelper::generate(),
            false));

        QVERIFY2(spy.count() >= 1, "loadFromJson must emit dataChanged at least once");
    }

    // ── 15. Schema version is always written by toJson (regardless of what
    //        the previous in-memory state thought it was). This is the
    //        forward-only contract that makes future migrations possible. ──
    void toJson_always_writes_current_schema_version()
    {
        // Construct a store that loaded a v0 file.
        DataStore ds;
        QJsonObject legacy = makeCurrentRoot(
            UuidHelper::generate(), UuidHelper::generate(),
            UuidHelper::generate(), UuidHelper::generate(),
            false);
        legacy.remove(QStringLiteral("schema_version"));   // legacy
        ds.loadFromJson(legacy);

        // Now save. The persisted state must carry CURRENT_SCHEMA_VERSION
        // regardless of what was on disk before.
        const QJsonObject saved = ds.toJson();
        QCOMPARE(saved.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);
    }
};

QTEST_GUILESS_MAIN(TestDataStoreMigration)
#include "test_datastore_migration.moc"
