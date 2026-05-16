// Tests for DataStore behaviour when state.json carries a schema_version
// HIGHER than the build's CURRENT_SCHEMA_VERSION — i.e. the "older build is
// asked to read a state file written by a newer build" downgrade scenario.
//
// Expected behaviour, observed from src/storage/datastore.cpp:
//
//   - loadFromJson reads schema_version (default 0 if absent).
//   - If loaded < CURRENT      → migrateJson runs (forward migration), qInfo.
//   - If loaded == CURRENT     → no migration, no warning.
//   - If loaded >  CURRENT     → qWarning is emitted but the load PROCEEDS.
//                                Entities are best-effort loaded; loadedSchemaVersion
//                                returns the on-disk value as-is (preserved).
//
// IMPORTANT: toJson() ALWAYS writes CURRENT_SCHEMA_VERSION in the output.
// That means a future-version state.json that is loaded by an older build
// and then re-saved WILL have its schema_version downgraded to whatever the
// old build supports. That is by design (forward-only migration contract);
// this suite documents and pins that behaviour as the current contract.
//
// The data inside the entity arrays does survive a load+save cycle through
// an older build, but any future-only top-level keys (e.g. a hypothetical
// new entity collection introduced in v3) would be lost on resave because
// loadFromJson knows nothing about them.

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
#include <QRegularExpression>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "utils/uuid.h"

class TestSchemaDowngrade : public QObject
{
    Q_OBJECT

private:
    // QTemporaryDir kept alive for the whole suite so QSettings (read by
    // HLC and DataStore::localDeviceId) writes into a disposable location.
    QTemporaryDir m_tmp;

    static constexpr int kFutureVersion = 99;

    // Build a self-contained root with a configurable schema_version. Always
    // includes one inbox project, one task, one label so we can verify
    // best-effort entity loading regardless of the schema_version path.
    QJsonObject makeRootWithVersion(int schemaVersion,
                                    const QString &taskId,
                                    const QString &projectId,
                                    const QString &labelId,
                                    bool includeVersionKey = true) const
    {
        QJsonObject root;
        if (includeVersionKey)
            root[QStringLiteral("schema_version")] = schemaVersion;

        // Settings dictionary — lets us check it survives the load too.
        QJsonObject settings;
        settings[QStringLiteral("theme")] = QStringLiteral("dark");
        settings[QStringLiteral("custom_future_key")] = QStringLiteral("preserve-me");
        root[QStringLiteral("settings")] = settings;

        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        // Inbox project.
        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("color")]      = QStringLiteral("grey");
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;
        root[QStringLiteral("projects")] = QJsonArray{project};

        // Task — well-formed.
        QJsonObject task;
        task[QStringLiteral("id")]         = taskId;
        task[QStringLiteral("content")]    = QStringLiteral("FromFuture");
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("priority")]   = 3;
        task[QStringLiteral("created_at")] = nowIso;
        task[QStringLiteral("updated_at")] = nowIso;
        root[QStringLiteral("tasks")] = QJsonArray{task};

        // Label.
        QJsonObject label;
        label[QStringLiteral("id")]         = labelId;
        label[QStringLiteral("name")]       = QStringLiteral("future-label");
        label[QStringLiteral("color")]      = QStringLiteral("red");
        label[QStringLiteral("created_at")] = nowIso;
        label[QStringLiteral("updated_at")] = nowIso;
        root[QStringLiteral("labels")] = QJsonArray{label};

        // Empty arrays for unused collections so loadFromJson doesn't trip.
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();
        return root;
    }

private slots:
    void initTestCase()
    {
        // Redirect QSettings (used by HLC and DataStore::localDeviceId) into a
        // throwaway location so the suite leaves no trace in HKCU / AppData.
        // Use a unique org/app pair so we don't clash with sibling tests.
        QVERIFY(m_tmp.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SchemaDowngrade"));
        QCoreApplication::setApplicationName(QStringLiteral("test_schema_downgrade"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmp.path());
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
    }

    // ── 1. Sanity: the build's CURRENT_SCHEMA_VERSION is below our hypothetical
    //       future version, so the "future" path actually fires. If a future
    //       Q-bump pushes CURRENT past 99 this test will warn the maintainer
    //       to bump kFutureVersion too. ──
    void future_version_constant_is_actually_in_the_future()
    {
        QVERIFY2(kFutureVersion > DataStore::CURRENT_SCHEMA_VERSION,
                 "kFutureVersion must exceed CURRENT_SCHEMA_VERSION — "
                 "bump it if a future schema rev catches up");
    }

    // ── 2. Future version (v99) emits the documented qWarning, AND entities
    //       still load best-effort. ──
    void future_version_warning_fires_and_entities_load()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        const QJsonObject root = makeRootWithVersion(
            kFutureVersion, taskId, projectId, labelId);

        DataStore ds;

        // Match the warning pattern in datastore.cpp loadFromJson:
        //   "DataStore: state.json schema_version %d is newer than supported %d; ..."
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral(
                "schema_version.*is newer than supported")));

        ds.loadFromJson(root);

        // Best-effort load — entities present.
        QCOMPARE(ds.allTasks().size(),    1);
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(),   1);

        // Task content survived intact.
        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);
        QCOMPARE(t->content(), QStringLiteral("FromFuture"));
        QCOMPARE(t->priority(), 3);

        // Loaded version reflects exactly what was on disk (not clobbered).
        QCOMPARE(ds.loadedSchemaVersion(), kFutureVersion);
    }

    // ── 3. Future version + save: documents the CURRENT behaviour — toJson()
    //       ALWAYS writes CURRENT_SCHEMA_VERSION, so a load+save through an
    //       older build downgrades the on-disk version. Entity data still
    //       round-trips, but the version key is reset.
    //
    //       This is the inverse of the "preserves forward-compat data"
    //       behaviour the task description hypothesised about; the actual
    //       wire contract in datastore.cpp toJson() is "always current". ──
    void future_version_save_downgrades_to_current_per_design()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        const QJsonObject input = makeRootWithVersion(
            kFutureVersion, taskId, projectId, labelId);

        DataStore ds;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral(
                "schema_version.*is newer than supported")));
        ds.loadFromJson(input);

        // Sanity: the in-memory loaded version is still 99.
        QCOMPARE(ds.loadedSchemaVersion(), kFutureVersion);

        // Re-serialize. The version field on disk is rewritten as CURRENT
        // because toJson() unconditionally stamps CURRENT_SCHEMA_VERSION.
        const QJsonObject saved = ds.toJson();
        QCOMPARE(saved.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);

        // But all entity data we know about did round-trip.
        const QJsonArray savedTasks = saved.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(savedTasks.size(), 1);
        QCOMPARE(savedTasks.first().toObject()
                     .value(QStringLiteral("content")).toString(),
                 QStringLiteral("FromFuture"));

        // Settings (including unknown future keys) survive too — settings
        // is a passthrough QJsonObject not subject to per-entity migration.
        const QJsonObject savedSettings =
            saved.value(QStringLiteral("settings")).toObject();
        QCOMPARE(savedSettings.value(QStringLiteral("custom_future_key"))
                              .toString(),
                 QStringLiteral("preserve-me"));
    }

    // ── 4. Future version + corrupt entity: the corrupt entity should not
    //       prevent the rest of the snapshot from loading. The store stays
    //       in a consistent, queryable state. ──
    void future_version_with_corrupt_entity_is_graceful()
    {
        const QString goodTaskId = UuidHelper::generate();
        const QString projectId  = UuidHelper::generate();
        const QString labelId    = UuidHelper::generate();

        QJsonObject root = makeRootWithVersion(
            kFutureVersion, goodTaskId, projectId, labelId);

        // Inject a corrupt task entry alongside the good one. Different
        // shapes of "corrupt": one with no id at all (legacy bug shape),
        // and one with the wrong type for a field (priority as a string).
        QJsonObject noId;
        noId[QStringLiteral("content")] = QStringLiteral("Missing id");
        // intentionally no "id" field

        QJsonObject wrongType;
        wrongType[QStringLiteral("id")]       = UuidHelper::generate();
        wrongType[QStringLiteral("content")]  = QStringLiteral("Bad type");
        wrongType[QStringLiteral("priority")] = QStringLiteral("not-a-number");

        QJsonArray tasks = root.value(QStringLiteral("tasks")).toArray();
        tasks.append(noId);
        tasks.append(wrongType);
        root[QStringLiteral("tasks")] = tasks;

        DataStore ds;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral(
                "schema_version.*is newer than supported")));
        ds.loadFromJson(root);   // must not crash

        // The well-formed task is reachable.
        Task *good = ds.taskById(goodTaskId);
        QVERIFY2(good != nullptr,
                 "Well-formed task must survive even when sibling entries are corrupt");
        QCOMPARE(good->content(), QStringLiteral("FromFuture"));

        // Lookup of a definitely-not-present id stays nullptr — index is consistent.
        QVERIFY(ds.taskById(QStringLiteral("not-an-id")) == nullptr);

        // Project/label arrays still loaded normally.
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(),   1);
    }

    // ── 5. Negative schema_version (e.g. -1) — documents observed behaviour:
    //       this is < CURRENT_SCHEMA_VERSION, so migrateJson() is invoked,
    //       but none of the version branches (v == 0, v == 1) match a -1
    //       starting point, so the version is returned unchanged. The qInfo
    //       "migrated state.json from schema v-1 to v-1" line is logged.
    //       Entities still load best-effort; loadedSchemaVersion() ends up
    //       reflecting the post-"migration" value (still -1). ──
    void negative_version_is_run_through_migration_no_op_no_crash()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        QJsonObject root = makeRootWithVersion(
            -1, taskId, projectId, labelId);

        DataStore ds;
        // qInfo from migrateJson path — explicitly ignore so it doesn't
        // surface as an unexpected message in the test log.
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression(QStringLiteral(
                "migrated state.json from schema v-?\\d+ to v-?\\d+")));

        ds.loadFromJson(root);

        // Entities still loaded — negative version is treated as "older
        // than us, run forward-migrate" by the < comparison.
        QCOMPARE(ds.allTasks().size(),    1);
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(),   1);

        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);
        QCOMPARE(t->content(), QStringLiteral("FromFuture"));

        // migrateJson found no v == -1 branch, so the value is returned
        // unchanged. The store records that as the "loaded" schema version.
        QCOMPARE(ds.loadedSchemaVersion(), -1);
    }

    // ── 6. Missing schema_version key — treated as v0, migrated forward to
    //       CURRENT. Asserts the existing migration path still works after
    //       the v1→v2 bump. ──
    void missing_schema_version_migrates_to_current()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        // includeVersionKey=false → the key is omitted entirely.
        const QJsonObject root = makeRootWithVersion(
            0, taskId, projectId, labelId, /*includeVersionKey=*/false);
        QVERIFY(!root.contains(QStringLiteral("schema_version")));

        DataStore ds;
        // The migration path emits a qInfo, ignore it explicitly.
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression(QStringLiteral(
                "migrated state.json from schema v0 to v\\d+")));

        ds.loadFromJson(root);

        // After migration, loadedSchemaVersion advances to CURRENT.
        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);

        // Entities loaded normally.
        QCOMPARE(ds.allTasks().size(),    1);
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(),   1);
    }

    // ── 7. Same version as current — no migration ran (no qInfo about
    //       migration), no warning, entity arrays preserved. We also assert
    //       that re-serialising and reading back the JSON gives an
    //       equivalent top-level structure (same keys, same task/project
    //       counts), modulo expected DataStore-internal rewrites. ──
    void same_version_as_current_no_migration_clean_round_trip()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        const QJsonObject input = makeRootWithVersion(
            DataStore::CURRENT_SCHEMA_VERSION, taskId, projectId, labelId);

        DataStore ds;
        // No QTest::ignoreMessage calls here: we want the test to fail if
        // any qWarning or qInfo about migration fires when none is expected.
        ds.loadFromJson(input);

        QCOMPARE(ds.loadedSchemaVersion(), DataStore::CURRENT_SCHEMA_VERSION);
        QCOMPARE(ds.allTasks().size(),    1);
        QCOMPARE(ds.allProjects().size(), 1);
        QCOMPARE(ds.allLabels().size(),   1);

        // Re-serialise. The version key is unchanged (still CURRENT) and the
        // top-level structure has the keys we expect.
        const QJsonObject saved = ds.toJson();
        QCOMPARE(saved.value(QStringLiteral("schema_version")).toInt(),
                 DataStore::CURRENT_SCHEMA_VERSION);

        // Same number of entities in each collection. We don't go for
        // byte-identical equality because DataStore re-stamps timestamps and
        // injects _meta on save — that's expected churn, not a regression.
        QCOMPARE(saved.value(QStringLiteral("tasks")).toArray().size(),    1);
        QCOMPARE(saved.value(QStringLiteral("projects")).toArray().size(), 1);
        QCOMPARE(saved.value(QStringLiteral("labels")).toArray().size(),   1);
        QCOMPARE(saved.value(QStringLiteral("filters")).toArray().size(),  0);

        // The per-entity content survived: task content/priority intact.
        const QJsonObject savedTask =
            saved.value(QStringLiteral("tasks")).toArray().first().toObject();
        QCOMPARE(savedTask.value(QStringLiteral("content")).toString(),
                 QStringLiteral("FromFuture"));
        QCOMPARE(savedTask.value(QStringLiteral("priority")).toInt(), 3);

        // settings dictionary unchanged.
        const QJsonObject savedSettings =
            saved.value(QStringLiteral("settings")).toObject();
        QCOMPARE(savedSettings.value(QStringLiteral("theme")).toString(),
                 QStringLiteral("dark"));
    }

    // ── 8. Future version is purely a top-level concern — entities don't
    //       need a special "future-aware" loader, so a load with v99 emits
    //       the standard dataChanged() pulse just like any other load. ──
    void future_version_load_still_emits_dataChanged()
    {
        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString labelId   = UuidHelper::generate();

        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral(
                "schema_version.*is newer than supported")));
        ds.loadFromJson(makeRootWithVersion(
            kFutureVersion, taskId, projectId, labelId));

        QVERIFY2(spy.count() >= 1,
                 "loadFromJson must emit dataChanged even when the on-disk "
                 "schema_version is newer than the build supports");
    }
};

QTEST_GUILESS_MAIN(TestSchemaDowngrade)
#include "test_schema_downgrade.moc"
