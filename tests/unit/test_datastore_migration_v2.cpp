// ---------------------------------------------------------------------------
// test_datastore_migration_v2.cpp
//
// Quality round Q3.07: behavioural pinning for DataStore::loadFromJson edge
// cases that the existing test_datastore_migration suite doesn't reach.
//
// What's already covered in test_datastore_migration.cpp:
//   * legacy-version (no schema_version key) migrates to current
//   * current/future schema_version handling
//   * empty root clears existing state
//   * malformed JSON rejected at parse time (by the caller, not DataStore)
//   * task without id does not crash
//   * round-trip preserves entity counts and fields
//   * CRDT _meta blob preserved
//   * recent tombstones preserved (stale ones pruned)
//   * settings (nested) preserved
//   * HLC counter persists across settings reads
//   * clear() drops meta — no stale leakage
//   * loadFromJson emits dataChanged
//   * toJson always writes CURRENT_SCHEMA_VERSION
//
// What's already covered in test_schema_downgrade.cpp:
//   * future schema_version: warning + best-effort load + downgrade on save
//   * negative schema_version no-op migration
//   * corrupt entity in future-version snapshot
//
// This v2 suite pins NINE additional corners that none of the above touch:
//
//   1. load_state_json_missing_top_level_keys_treats_as_empty_collections —
//      "tasks": null, OR no "tasks" key at all → empty list, no error.
//   2. load_state_json_with_extra_unknown_top_level_keys_ignores_them —
//      future-feature top-level keys are silently dropped on resave.
//   3. load_task_with_missing_required_id_keeps_empty_id —
//      pin that fromJson does NOT assign a fallback UUID; the task is loaded
//      with an empty id (which then collides in the index if there are >1).
//   4. load_task_with_invalid_uuid_format_does_not_crash —
//      "id": "not-a-uuid" is accepted as a plain string; pin the contract.
//   5. load_task_with_parent_id_that_doesnt_exist_keeps_field —
//      orphan parent_id reference is preserved (no validation pass).
//   6. load_label_id_referenced_by_task_but_not_in_labels_list_keeps_reference —
//      orphan label string preserved on the task; allLabels() unaffected.
//   7. load_settings_with_unknown_key_preserves_in_dict —
//      forward-compat for QJsonObject pass-through of settings.
//   8. load_with_null_byte_in_string_field_does_not_crash —
//      NUL byte in content survives Task::fromJson and toJson round-trip.
//   9. load_with_extremely_long_string_field_does_not_oom —
//      content = 1 MB string survives the load (not chunked, not truncated).
//
// Constraints:
//   * QTEST_GUILESS_MAIN (no GUI; pure-infra helper).
//   * Unique QSettings org/app to keep parallel ctest runs isolated.
//   * NUL bytes constructed at runtime via QChar(QChar::Null), never embedded
//     as literal \x00 in the source — AUTOMOC refuses files with NUL bytes.
//   * No production-code changes; assertions pin current behaviour.
//   * Suite is registered as Infra layer in CMakeLists.txt.

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
#include "model/filter.h"
#include "utils/uuid.h"

class TestDataStoreMigrationV2 : public QObject
{
    Q_OBJECT

private:
    // QTemporaryDir kept alive for the whole suite so QSettings (read by
    // HLC and DataStore::localDeviceId) writes into a disposable location.
    // Unique org/app keeps the slot under `ctest -j` isolated from siblings.
    QTemporaryDir m_tmp;

    // Per-slot helper: a fresh org/app string so each slot's QSettings calls
    // land in their own namespace. The slot name is appended to avoid bleed
    // between slots that both stamp a CRDT field (and therefore both touch
    // the cached device id / HLC counter through QSettings).
    void setIsolatedQSettings(const char *slotName) const
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_DSMigrationV2_%1").arg(QLatin1String(slotName)));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_datastore_migration_v2_%1").arg(QLatin1String(slotName)));
    }

    // Build a string with an embedded NUL byte at runtime. The source file
    // itself stays NUL-free (AUTOMOC requirement).
    static QString stringWithNul(const QString &left, const QString &right)
    {
        QString s = left;
        s.append(QChar(QChar::Null));
        s.append(right);
        return s;
    }

private slots:
    void initTestCase()
    {
        // Redirect QSettings into a throwaway location so the suite leaves
        // no trace in HKCU / AppData. The actual org/app is overridden per
        // slot via setIsolatedQSettings to keep parallel runs deconflicted.
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmp.path());
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
        QStandardPaths::setTestModeEnabled(false);
    }

    // =====================================================================
    // Slot 1 — missing top-level keys (tasks/projects/etc) treated as empty
    //
    // QJsonObject::operator[] returns an undefined QJsonValue for absent
    // keys; .toArray() on it returns an empty QJsonArray. Same path covers
    // an explicit "tasks": null (toArray of a null QJsonValue is also
    // empty). loadFromJson must therefore treat both as "no entities" and
    // NOT crash on the for-each loop.
    // =====================================================================
    void load_state_json_missing_top_level_keys_treats_as_empty_collections()
    {
        setIsolatedQSettings("missing_top_level");

        // Variant A: no entity arrays at all, just schema_version + settings.
        QJsonObject rootNoArrays;
        rootNoArrays[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        rootNoArrays[QStringLiteral("settings")]       = QJsonObject{};
        // intentionally omit tasks/projects/sections/labels/filters/reminders/comments

        DataStore dsA;
        dsA.loadFromJson(rootNoArrays);

        QCOMPARE(dsA.allTasks().size(),     0);
        QCOMPARE(dsA.allProjects().size(),  0);
        QCOMPARE(dsA.allLabels().size(),    0);
        QCOMPARE(dsA.allFilters().size(),   0);

        // Variant B: explicit "tasks": null. QJsonValue::toArray() on a null
        // value returns an empty array, so this should behave identically.
        QJsonObject rootNullTasks;
        rootNullTasks[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        rootNullTasks[QStringLiteral("settings")]       = QJsonObject{};
        rootNullTasks[QStringLiteral("tasks")]          = QJsonValue::Null;
        rootNullTasks[QStringLiteral("projects")]       = QJsonValue::Null;
        rootNullTasks[QStringLiteral("sections")]       = QJsonValue::Null;
        rootNullTasks[QStringLiteral("labels")]         = QJsonValue::Null;
        rootNullTasks[QStringLiteral("filters")]        = QJsonValue::Null;
        rootNullTasks[QStringLiteral("reminders")]      = QJsonValue::Null;
        rootNullTasks[QStringLiteral("comments")]       = QJsonValue::Null;

        DataStore dsB;
        dsB.loadFromJson(rootNullTasks);

        QCOMPARE(dsB.allTasks().size(),    0);
        QCOMPARE(dsB.allProjects().size(), 0);
        QCOMPARE(dsB.allLabels().size(),   0);
        QCOMPARE(dsB.allFilters().size(),  0);

        // Variant C: completely empty object (no schema_version, no settings,
        // no arrays). Documented elsewhere as "treated as v0, migrated to
        // current"; we just verify it doesn't crash here.
        DataStore dsC;
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression(QStringLiteral(
                "migrated state.json from schema v0 to v\\d+")));
        dsC.loadFromJson(QJsonObject{});
        QCOMPARE(dsC.allTasks().size(),    0);
        QCOMPARE(dsC.allProjects().size(), 0);

        qInfo("Missing top-level entity keys are treated as empty collections "
              "(no fallback error). Forward-compat for partial snapshots.");
    }

    // =====================================================================
    // Slot 2 — extra unknown top-level keys are ignored on load AND on save
    //
    // Forward-compat probe: a future build might add a new top-level
    // collection (e.g. "automations") or a sidecar metadata blob. An older
    // build must not crash on the unknown key — but it WILL be dropped on
    // resave because toJson() only emits known keys. Pin this contract.
    // =====================================================================
    void load_state_json_with_extra_unknown_top_level_keys_ignores_them()
    {
        setIsolatedQSettings("extra_unknown_keys");

        const QString taskId    = UuidHelper::generate();
        const QString projectId = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        QJsonObject task;
        task[QStringLiteral("id")]         = taskId;
        task[QStringLiteral("content")]    = QStringLiteral("KnownGood");
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("created_at")] = nowIso;
        task[QStringLiteral("updated_at")] = nowIso;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{task};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        // Inject several unknown top-level keys at different value shapes.
        root[QStringLiteral("future_feature_xyz")] = QJsonObject{
            {QStringLiteral("k"), QStringLiteral("v")},
            {QStringLiteral("n"), 42}
        };
        root[QStringLiteral("automations")] = QJsonArray{
            QJsonObject{{QStringLiteral("rule"), QStringLiteral("auto-archive")}}
        };
        root[QStringLiteral("some_scalar")] = QStringLiteral("hello");
        root[QStringLiteral("some_number")] = 12345;

        DataStore ds;
        ds.loadFromJson(root);

        // Known data still loaded.
        QCOMPARE(ds.allTasks().size(),    1);
        QCOMPARE(ds.allProjects().size(), 1);
        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);
        QCOMPARE(t->content(), QStringLiteral("KnownGood"));

        // Resave. Unknown keys are silently dropped — toJson only writes
        // the keys it knows about.
        const QJsonObject saved = ds.toJson();
        QVERIFY2(!saved.contains(QStringLiteral("future_feature_xyz")),
                 "Unknown top-level keys are not preserved on resave — "
                 "an older build effectively strips them.");
        QVERIFY2(!saved.contains(QStringLiteral("automations")),
                 "Unknown top-level arrays are dropped on resave.");
        QVERIFY(!saved.contains(QStringLiteral("some_scalar")));
        QVERIFY(!saved.contains(QStringLiteral("some_number")));

        // Sanity: known keys present.
        QVERIFY(saved.contains(QStringLiteral("schema_version")));
        QVERIFY(saved.contains(QStringLiteral("tasks")));
        QVERIFY(saved.contains(QStringLiteral("projects")));

        qInfo("Unknown top-level keys are accepted on load but silently "
              "DROPPED on resave (toJson emits known keys only). "
              "DOCUMENTED FORWARD-COMPAT GAP: an older build round-tripping a "
              "newer state.json loses future-only data.");
    }

    // =====================================================================
    // Slot 3 — task with missing required id keeps an empty id
    //
    // Task::fromJson does not validate. It calls json["id"].toString() which
    // returns an empty QString for an absent key. The resulting Task has
    // m_id == "" and is appended to m_tasks + m_taskIndex[""] = task.
    // If multiple id-less tasks are loaded, they all collide on the same
    // index slot — the last one wins, but they all stay in the list.
    // =====================================================================
    void load_task_with_missing_required_id_keeps_empty_id()
    {
        setIsolatedQSettings("missing_id");

        const QString projectId = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        // Two tasks without "id". Without id collision, both should end up
        // in the list — but they will both collide on m_taskIndex[""].
        QJsonObject noId1;
        noId1[QStringLiteral("content")]    = QStringLiteral("First no-id");
        noId1[QStringLiteral("project_id")] = projectId;
        noId1[QStringLiteral("created_at")] = nowIso;
        noId1[QStringLiteral("updated_at")] = nowIso;

        QJsonObject noId2;
        noId2[QStringLiteral("content")]    = QStringLiteral("Second no-id");
        noId2[QStringLiteral("project_id")] = projectId;
        noId2[QStringLiteral("created_at")] = nowIso;
        noId2[QStringLiteral("updated_at")] = nowIso;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{noId1, noId2};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash

        // Both id-less tasks are in the list (no skip / no UUID assignment).
        // allTasks() filters tombstones but not empty ids — both should be
        // visible if the loader appended them.
        const QList<Task *> tasks = ds.allTasks();
        QCOMPARE(tasks.size(), 2);

        // Both have empty id.
        for (const Task *t : tasks) {
            QVERIFY2(t->id().isEmpty(),
                     "Task::fromJson does NOT assign a fallback UUID — empty id is kept.");
        }

        // taskById("") returns ONE of the two (the second insert wins the
        // index slot). We just assert non-null + that it points to one of
        // them.
        Task *byEmpty = ds.taskById(QString());
        QVERIFY2(byEmpty != nullptr,
                 "Empty-id lookup returns the last-inserted entry — pin the "
                 "current m_taskIndex collision behaviour.");
        const QString contentByIdx = byEmpty->content();
        QVERIFY(contentByIdx == QStringLiteral("First no-id")
             || contentByIdx == QStringLiteral("Second no-id"));

        qInfo("DOCUMENTED GAP: Task::fromJson does not validate id presence. "
              "Missing id is loaded as empty string and collides in m_taskIndex. "
              "If a future fix adds UUID assignment, this assertion will flip.");
    }

    // =====================================================================
    // Slot 4 — task with invalid UUID format does not crash
    //
    // The id is treated as an opaque string. There is no UUID-format check
    // anywhere on the load path. Pin: a task with "id": "not-a-uuid" is
    // accepted, reachable by that literal string, and round-trips intact.
    // =====================================================================
    void load_task_with_invalid_uuid_format_does_not_crash()
    {
        setIsolatedQSettings("invalid_uuid");

        const QString projectId = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        // Several "wrong shape" ids — none should be rejected.
        const QString badId1 = QStringLiteral("not-a-uuid");
        const QString badId2 = QStringLiteral("12345");
        const QString badId3 = QStringLiteral("{}{}{}");
        const QString badId4 = QStringLiteral(" leading-space");

        auto makeTask = [&](const QString &id, const QString &content) {
            QJsonObject task;
            task[QStringLiteral("id")]         = id;
            task[QStringLiteral("content")]    = content;
            task[QStringLiteral("project_id")] = projectId;
            task[QStringLiteral("created_at")] = nowIso;
            task[QStringLiteral("updated_at")] = nowIso;
            return task;
        };

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{
            makeTask(badId1, QStringLiteral("not-a-uuid task")),
            makeTask(badId2, QStringLiteral("numeric id task")),
            makeTask(badId3, QStringLiteral("braces task")),
            makeTask(badId4, QStringLiteral("leading-space task")),
        };
        root[QStringLiteral("projects")]  = QJsonArray{project};
        root[QStringLiteral("sections")]  = QJsonArray();
        root[QStringLiteral("labels")]    = QJsonArray();
        root[QStringLiteral("filters")]   = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")]  = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash, must not assert

        // All four tasks were loaded — no UUID validation.
        QCOMPARE(ds.allTasks().size(), 4);

        // Lookup by the exact (bad) id string works.
        QVERIFY(ds.taskById(badId1) != nullptr);
        QCOMPARE(ds.taskById(badId1)->content(),
                 QStringLiteral("not-a-uuid task"));
        QVERIFY(ds.taskById(badId2) != nullptr);
        QVERIFY(ds.taskById(badId3) != nullptr);
        QVERIFY(ds.taskById(badId4) != nullptr);

        // Round-trip: ids survive save/load unchanged (no canonicalisation).
        const QJsonObject saved = ds.toJson();
        DataStore ds2;
        ds2.loadFromJson(saved);
        QVERIFY(ds2.taskById(badId1) != nullptr);
        QVERIFY(ds2.taskById(badId4) != nullptr);

        qInfo("DOCUMENTED CONTRACT: ids are opaque strings end-to-end. "
              "UuidHelper::isValid() exists but is NOT called on the load path. "
              "Non-UUID ids survive save/load and remain looked-up by literal.");
    }

    // =====================================================================
    // Slot 5 — task with parent_id that doesn't exist keeps the field
    //
    // DataStore::loadFromJson does not validate cross-entity references.
    // A task whose parent_id points at a non-existent task id is accepted
    // as-is. Querying subtasks(orphanedParentId) on the orphan parent
    // would return nothing; the orphan child's parentId field is preserved
    // and round-trips on resave.
    // =====================================================================
    void load_task_with_parent_id_that_doesnt_exist_keeps_field()
    {
        setIsolatedQSettings("orphan_parent_id");

        const QString projectId = UuidHelper::generate();
        const QString orphanChildId = UuidHelper::generate();
        const QString ghostParentId = UuidHelper::generate();   // intentionally absent
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        // Child task references a ghost parent that does not exist in the tasks array.
        QJsonObject child;
        child[QStringLiteral("id")]         = orphanChildId;
        child[QStringLiteral("content")]    = QStringLiteral("Orphan child");
        child[QStringLiteral("project_id")] = projectId;
        child[QStringLiteral("parent_id")]  = ghostParentId;
        child[QStringLiteral("created_at")] = nowIso;
        child[QStringLiteral("updated_at")] = nowIso;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{child};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash

        // Orphan child loaded.
        Task *c = ds.taskById(orphanChildId);
        QVERIFY(c != nullptr);

        // parent_id is preserved verbatim — no nulling, no reparenting.
        QCOMPARE(c->parentId(), ghostParentId);

        // Ghost parent is genuinely absent.
        QVERIFY(ds.taskById(ghostParentId) == nullptr);

        // subtasks(ghostParentId) returns the orphan child anyway — the
        // query just filters by parentId field equality, no validation.
        const QList<Task *> children = ds.subtasks(ghostParentId);
        QCOMPARE(children.size(), 1);
        QCOMPARE(children.first()->id(), orphanChildId);

        // Round-trip: parent_id survives save+load unchanged.
        const QJsonObject saved = ds.toJson();
        DataStore ds2;
        ds2.loadFromJson(saved);
        Task *c2 = ds2.taskById(orphanChildId);
        QVERIFY(c2 != nullptr);
        QCOMPARE(c2->parentId(), ghostParentId);

        qInfo("DOCUMENTED CONTRACT: cross-entity refs (parent_id) are NOT "
              "validated at load. Orphan child references survive intact. "
              "Reconciliation is left to the UI / sync merger, which may or "
              "may not exist depending on the call site.");
    }

    // =====================================================================
    // Slot 6 — label_id referenced by task but not in labels list keeps reference
    //
    // task.labels is a QStringList of label names (NOT label ids — by the
    // Todoist-shape JSON contract). DataStore stores labels separately in
    // m_labels and indexes by both id and lowercased name. A task carrying
    // a label string that isn't in the labels collection still loads and
    // preserves that string on the task; allLabels() doesn't see it.
    // =====================================================================
    void load_label_id_referenced_by_task_but_not_in_labels_list_keeps_reference()
    {
        setIsolatedQSettings("orphan_label_ref");

        const QString projectId = UuidHelper::generate();
        const QString taskId    = UuidHelper::generate();
        const QString knownLabelId = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        // Known label "real".
        QJsonObject label;
        label[QStringLiteral("id")]         = knownLabelId;
        label[QStringLiteral("name")]       = QStringLiteral("real");
        label[QStringLiteral("color")]      = QStringLiteral("red");
        label[QStringLiteral("created_at")] = nowIso;
        label[QStringLiteral("updated_at")] = nowIso;

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        // Task references both a known and an unknown label string.
        QJsonObject task;
        task[QStringLiteral("id")]         = taskId;
        task[QStringLiteral("content")]    = QStringLiteral("With labels");
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("created_at")] = nowIso;
        task[QStringLiteral("updated_at")] = nowIso;
        task[QStringLiteral("labels")] = QJsonArray{
            QStringLiteral("real"),
            QStringLiteral("ghost-label"),
        };

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{task};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray{label};
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);

        // Both labels survive on the task.
        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);
        const QStringList loaded = t->labels();
        QCOMPARE(loaded.size(), 2);
        QVERIFY(loaded.contains(QStringLiteral("real")));
        QVERIFY(loaded.contains(QStringLiteral("ghost-label")));

        // allLabels() returns only the entries in m_labels — the orphan
        // string on the task does NOT magically materialise a Label entity.
        QCOMPARE(ds.allLabels().size(), 1);
        QCOMPARE(ds.allLabels().first()->name(), QStringLiteral("real"));

        // labelByName("ghost-label") returns nullptr — no auto-creation.
        QVERIFY2(ds.labelByName(QStringLiteral("ghost-label")) == nullptr,
                 "Orphan label string on a task does not auto-create a Label.");

        // Round-trip preserves the orphan reference on the task.
        const QJsonObject saved = ds.toJson();
        DataStore ds2;
        ds2.loadFromJson(saved);
        Task *t2 = ds2.taskById(taskId);
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->labels().size(), 2);
        QVERIFY(t2->labels().contains(QStringLiteral("ghost-label")));

        qInfo("DOCUMENTED CONTRACT: task.labels carries label NAMES (strings), "
              "not label ids. Orphan references survive load and round-trip. "
              "labelByName() on an orphan returns nullptr — no auto-creation.");
    }

    // =====================================================================
    // Slot 7 — settings with unknown key preserves in dict
    //
    // DataStore::m_settings is a passthrough QJsonObject. Whatever keys
    // come in on load go out on save. This is by design — settings is
    // an open-ended bag of QML-side preferences with no schema.
    // =====================================================================
    void load_settings_with_unknown_key_preserves_in_dict()
    {
        setIsolatedQSettings("settings_unknown_key");

        QJsonObject settings;
        // Known-ish key (theme is referenced by SettingsViewModel).
        settings[QStringLiteral("theme")] = QStringLiteral("dark");
        // Several plausibly-unknown keys with various value shapes.
        settings[QStringLiteral("future_pref_string")] = QStringLiteral("preserve");
        settings[QStringLiteral("future_pref_int")]    = 42;
        settings[QStringLiteral("future_pref_bool")]   = true;
        settings[QStringLiteral("future_pref_array")]  = QJsonArray{
            QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")
        };
        settings[QStringLiteral("future_pref_nested")] = QJsonObject{
            {QStringLiteral("inner_k"), QStringLiteral("inner_v")},
            {QStringLiteral("inner_n"), 7}
        };
        settings[QStringLiteral("future_pref_null")]   = QJsonValue::Null;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = settings;
        root[QStringLiteral("tasks")]          = QJsonArray();
        root[QStringLiteral("projects")]       = QJsonArray();
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);

        // Settings dict survives intact.
        const QJsonObject loaded = ds.settings();
        QCOMPARE(loaded.value(QStringLiteral("theme")).toString(),
                 QStringLiteral("dark"));
        QCOMPARE(loaded.value(QStringLiteral("future_pref_string")).toString(),
                 QStringLiteral("preserve"));
        QCOMPARE(loaded.value(QStringLiteral("future_pref_int")).toInt(), 42);
        QCOMPARE(loaded.value(QStringLiteral("future_pref_bool")).toBool(), true);
        QCOMPARE(loaded.value(QStringLiteral("future_pref_array")).toArray().size(), 3);
        QCOMPARE(loaded.value(QStringLiteral("future_pref_nested"))
                       .toObject().value(QStringLiteral("inner_n")).toInt(), 7);
        // null values stay as null (not coerced to string or removed).
        QVERIFY(loaded.contains(QStringLiteral("future_pref_null")));
        QVERIFY(loaded.value(QStringLiteral("future_pref_null")).isNull());

        // Resave: same keys still there.
        const QJsonObject saved = ds.toJson();
        const QJsonObject savedSettings = saved.value(QStringLiteral("settings"))
                                              .toObject();
        QCOMPARE(savedSettings.value(QStringLiteral("future_pref_string")).toString(),
                 QStringLiteral("preserve"));
        QCOMPARE(savedSettings.value(QStringLiteral("future_pref_nested"))
                              .toObject().value(QStringLiteral("inner_k")).toString(),
                 QStringLiteral("inner_v"));

        qInfo("Settings dict is a passthrough — any key/value shape is "
              "preserved on load AND on save. This is the forward-compat "
              "contract for QML-side preferences.");
    }

    // =====================================================================
    // Slot 8 — null byte in string field does not crash
    //
    // QString stores NUL freely. QJsonValue::toString preserves it across
    // parse/serialise. Task::fromJson and toJson pass through unmodified.
    // We construct the NUL at runtime to keep the source file NUL-free
    // (AUTOMOC refuses files with embedded 0x00 bytes).
    // =====================================================================
    void load_with_null_byte_in_string_field_does_not_crash()
    {
        setIsolatedQSettings("null_byte");

        const QString projectId = UuidHelper::generate();
        const QString taskId    = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        // "test<NUL>evil" — built at runtime so the source stays clean.
        const QString evilContent = stringWithNul(
            QStringLiteral("test"), QStringLiteral("evil"));
        QCOMPARE(evilContent.size(), 9);
        QCOMPARE(evilContent.at(4), QChar(QChar::Null));

        // Also a NUL in the description, and in the label name.
        const QString evilDesc = stringWithNul(
            QStringLiteral("desc"), QStringLiteral("inject"));

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        QJsonObject task;
        task[QStringLiteral("id")]          = taskId;
        task[QStringLiteral("content")]     = evilContent;
        task[QStringLiteral("description")] = evilDesc;
        task[QStringLiteral("project_id")]  = projectId;
        task[QStringLiteral("created_at")]  = nowIso;
        task[QStringLiteral("updated_at")]  = nowIso;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{task};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash

        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);

        // NUL byte survived end-to-end.
        QCOMPARE(t->content().size(), 9);
        QCOMPARE(t->content().at(4), QChar(QChar::Null));
        QCOMPARE(t->content(), evilContent);

        // "desc" (4) + NUL (1) + "inject" (6) = 11 chars.
        QCOMPARE(t->description().size(), 11);
        QCOMPARE(t->description().at(4), QChar(QChar::Null));

        // Round-trip via QJsonDocument bytes (the FileManager wire path).
        // QJsonDocument::toJson encodes NUL as the six-byte escape "\u0000",
        // and QJsonDocument::fromJson decodes it back to U+0000. Verify.
        const QJsonObject saved = ds.toJson();
        const QByteArray bytes = QJsonDocument(saved).toJson(QJsonDocument::Compact);
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        QCOMPARE(err.error, QJsonParseError::NoError);

        DataStore ds2;
        ds2.loadFromJson(doc.object());
        Task *t2 = ds2.taskById(taskId);
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->content().size(), 9);
        QCOMPARE(t2->content().at(4), QChar(QChar::Null));
        QCOMPARE(t2->content(), evilContent);

        qInfo("NUL bytes in string fields survive Task::fromJson, toJson, and "
              "QJsonDocument byte round-trip (encoded as \\u0000). Pin contract.");
    }

    // =====================================================================
    // Slot 9 — extremely long string field does not OOM
    //
    // 1 MB single string in task content. QJsonValue stores it whole;
    // Task::m_content is QString = QChar buffer. The load should complete
    // in well under a second on any reasonable build and not exhaust
    // memory. Verify the length round-trips and a sampling of characters
    // is intact.
    // =====================================================================
    void load_with_extremely_long_string_field_does_not_oom()
    {
        setIsolatedQSettings("long_string");

        const QString projectId = UuidHelper::generate();
        const QString taskId    = UuidHelper::generate();
        const QString nowIso = QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODateWithMs);

        // 1 MB = 1,048,576 chars. Build a 1 MB string of a repeating
        // 64-char pattern so we can verify content survived intact by
        // sampling random offsets.
        const QString chunk = QStringLiteral(
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!?");
        QCOMPARE(chunk.size(), 64);
        QString hugeContent;
        const int targetSize = 1024 * 1024;   // 1 MB worth of QChar
        hugeContent.reserve(targetSize);
        while (hugeContent.size() < targetSize)
            hugeContent.append(chunk);
        hugeContent.truncate(targetSize);
        QCOMPARE(hugeContent.size(), targetSize);

        QJsonObject project;
        project[QStringLiteral("id")]         = projectId;
        project[QStringLiteral("name")]       = QStringLiteral("Inbox");
        project[QStringLiteral("is_inbox")]   = true;
        project[QStringLiteral("created_at")] = nowIso;
        project[QStringLiteral("updated_at")] = nowIso;

        QJsonObject task;
        task[QStringLiteral("id")]         = taskId;
        task[QStringLiteral("content")]    = hugeContent;
        task[QStringLiteral("project_id")] = projectId;
        task[QStringLiteral("created_at")] = nowIso;
        task[QStringLiteral("updated_at")] = nowIso;

        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")]       = QJsonObject{};
        root[QStringLiteral("tasks")]          = QJsonArray{task};
        root[QStringLiteral("projects")]       = QJsonArray{project};
        root[QStringLiteral("sections")]       = QJsonArray();
        root[QStringLiteral("labels")]         = QJsonArray();
        root[QStringLiteral("filters")]        = QJsonArray();
        root[QStringLiteral("reminders")]      = QJsonArray();
        root[QStringLiteral("comments")]       = QJsonArray();

        // Time the load. Even on slow CI VMs 1 MB should be well under 5s.
        QElapsedTimer timer;
        timer.start();

        DataStore ds;
        ds.loadFromJson(root);   // must not crash, must not OOM

        const qint64 elapsedMs = timer.elapsed();
        QVERIFY2(elapsedMs < 5000,
                 qPrintable(QStringLiteral("1 MB content load took %1 ms "
                                            "(>5s budget) — investigate").arg(elapsedMs)));

        Task *t = ds.taskById(taskId);
        QVERIFY(t != nullptr);

        // Size is exact.
        QCOMPARE(t->content().size(), targetSize);

        // Sample three offsets to confirm no truncation / corruption.
        QCOMPARE(t->content().at(0),               QChar('0'));
        QCOMPARE(t->content().at(63),              QChar('?'));
        QCOMPARE(t->content().at(targetSize - 1),  QChar('?'));

        // Save+reload also fits in budget.
        timer.restart();
        const QJsonObject saved = ds.toJson();
        DataStore ds2;
        ds2.loadFromJson(saved);
        const qint64 rtMs = timer.elapsed();
        QVERIFY2(rtMs < 5000,
                 qPrintable(QStringLiteral("1 MB content round-trip took %1 ms "
                                            "(>5s budget)").arg(rtMs)));

        Task *t2 = ds2.taskById(taskId);
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->content().size(), targetSize);

        qInfo("1 MB content survived load (%lldms) + round-trip (%lldms). "
              "DOCUMENTED GAP: no upper bound enforced anywhere on string "
              "field size — a malicious sync peer could push GB-class payloads.",
              static_cast<long long>(elapsedMs),
              static_cast<long long>(rtMs));
    }
};

QTEST_GUILESS_MAIN(TestDataStoreMigrationV2)
#include "test_datastore_migration_v2.moc"
