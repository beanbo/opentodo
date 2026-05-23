// Tests for FileManager rotation, atomic write, listBackups,
// restoreBackup, and import/export round-trip.
//
// These tests focus on behavior NOT covered by tests/unit/test_filemanager.cpp:
//   * 3-generation rolling backup chain (.bak / .bak.1 / .bak.2)
//   * listBackups() metadata
//   * restoreBackup() success + failure paths
//   * Atomic write semantics around save errors
//   * importData / exportData edge cases
//
// FileManager doesn't expose a constructor argument for the storage path;
// it derives it from QStandardPaths::AppDataLocation. As in
// test_filemanager.cpp, we toggle QStandardPaths::setTestModeEnabled(true)
// so the path falls inside the test sandbox, then wipe state.json plus all
// .bak* generations between tests so cases stay independent.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVariantList>
#include <QVariantMap>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/task.h"
#include "model/project.h"

class TestFileManagerBackups : public QObject
{
    Q_OBJECT

private:
    QString stateFilePath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo/state.json");
    }

    QString bak0Path() const { return stateFilePath() + QStringLiteral(".bak"); }
    QString bak1Path() const { return stateFilePath() + QStringLiteral(".bak.1"); }
    QString bak2Path() const { return stateFilePath() + QStringLiteral(".bak.2"); }

    void wipeAll() const
    {
        QFile::remove(stateFilePath());
        QFile::remove(bak0Path());
        QFile::remove(bak1Path());
        QFile::remove(bak2Path());
        // Q3.10: listBackups() also surfaces dated (.bak.YYYYMMDDhhmmss)
        // backups and the pre-vacuum safety snapshot. cleanup() must wipe
        // these too so the "no backups exist" assertion is meaningful —
        // otherwise dated backups left behind by prior test runs (e.g.
        // sibling slots, or the parallel test_filemanager_error_paths
        // process when ctest -j is in effect) leak into this slot's
        // listBackups() output.
        QFile::remove(stateFilePath() + QStringLiteral(".pre-vacuum.bak"));
        const QString dataDir = QStandardPaths::writableLocation(
                                    QStandardPaths::AppDataLocation)
                                + QStringLiteral("/OpenTodo");
        QDir dir(dataDir);
        if (dir.exists()) {
            const auto dated = dir.entryInfoList(
                QStringList{} << QStringLiteral("state.json.bak.*"),
                QDir::Files | QDir::NoSymLinks);
            for (const QFileInfo &fi : dated)
                QFile::remove(fi.absoluteFilePath());
        }
    }

    // Helper: read stored JSON file and return its top-level "projects" array
    // size. Used to fingerprint each save generation.
    int projectCountOnDisk(const QString &path) const
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return -1;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isNull() || !doc.isObject())
            return -1;
        return doc.object().value(QStringLiteral("projects")).toArray().size();
    }

    // Touch backup files to differing mtimes so listBackups() ordering is
    // observable. Sleeping briefly between saves is the minimum reliable way
    // because QFileInfo::lastModified resolution is filesystem-dependent on
    // Windows (often 1-2 seconds for FAT-derived; ms for NTFS).
    void shortPause() const { QTest::qWait(50); }

private slots:
    void initTestCase()
    {
        // Per-test org/app so the test-mode AppDataLocation does not collide
        // with sibling FileManager-based tests when run under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_FileMgrBackups"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filemanager_backups"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    void cleanup()
    {
        wipeAll();
    }

    // --- Rotation ----------------------------------------------------------

    // 1st save: state.json is created by createDefaultState().
    // The implementation skips rotation when state.json doesn't already exist,
    // so no .bak files should appear after the first save.
    void rotation_first_save_creates_state_no_bak()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();              // calls createDefaultState() → saveImmediate()

        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(!QFile::exists(bak0Path()));
        QVERIFY(!QFile::exists(bak1Path()));
        QVERIFY(!QFile::exists(bak2Path()));
    }

    // 2nd save: state.json already exists, so it gets copied into .bak before
    // the new contents are written.
    void rotation_second_save_creates_bak()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Save2"));
        fm.saveImmediate();

        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(QFile::exists(bak0Path()));
        QVERIFY(!QFile::exists(bak1Path()));
        QVERIFY(!QFile::exists(bak2Path()));
    }

    // 3rd save: previous .bak rotates to .bak.1, fresh .bak written.
    void rotation_third_save_creates_bak1()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("A"));
        fm.saveImmediate();
        ds.addProject(QStringLiteral("B"));
        fm.saveImmediate();

        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(QFile::exists(bak0Path()));
        QVERIFY(QFile::exists(bak1Path()));
        QVERIFY(!QFile::exists(bak2Path()));
    }

    // 4th save: chain is fully populated.
    void rotation_fourth_save_creates_bak2()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("A"));
        fm.saveImmediate();
        ds.addProject(QStringLiteral("B"));
        fm.saveImmediate();
        ds.addProject(QStringLiteral("C"));
        fm.saveImmediate();

        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(QFile::exists(bak0Path()));
        QVERIFY(QFile::exists(bak1Path()));
        QVERIFY(QFile::exists(bak2Path()));
    }

    // 5th save: oldest backup (.bak.2) is overwritten by what used to be
    // .bak.1 — there is never a .bak.3.
    // Each save adds another project, so by counting projects we can trace
    // which generation each on-disk file represents.
    void rotation_fifth_save_caps_at_three_generations()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        const int initialProjects = ds.allProjects().size(); // includes Inbox

        // Save 2..5: each adds one new project.
        for (int i = 0; i < 4; ++i) {
            ds.addProject(QStringLiteral("P%1").arg(i));
            fm.saveImmediate();
        }

        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(QFile::exists(bak0Path()));
        QVERIFY(QFile::exists(bak1Path()));
        QVERIFY(QFile::exists(bak2Path()));

        // No phantom .bak.3.
        QVERIFY(!QFile::exists(stateFilePath() + QStringLiteral(".bak.3")));

        // After 5 saves, project counts on disk should be:
        //   state.json → initial + 4 (current state)
        //   .bak       → initial + 3 (snapshot before 5th save)
        //   .bak.1     → initial + 2
        //   .bak.2     → initial + 1
        // The +0 generation (just inbox, before first add) has aged out.
        QCOMPARE(projectCountOnDisk(stateFilePath()), initialProjects + 4);
        QCOMPARE(projectCountOnDisk(bak0Path()),      initialProjects + 3);
        QCOMPARE(projectCountOnDisk(bak1Path()),      initialProjects + 2);
        QCOMPARE(projectCountOnDisk(bak2Path()),      initialProjects + 1);
    }

    // --- listBackups -------------------------------------------------------

    void listBackups_empty_when_no_backups_exist()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();   // creates state.json but no .bak yet

        const QVariantList list = fm.listBackups();
        QCOMPARE(list.size(), 0);
    }

    // After 4 saves the chain is full; listBackups should surface the three
    // rolling generations (.bak / .bak.1 / .bak.2). Each entry must carry
    // valid path/name/size/mtime fields.
    //
    // Q3.10: listBackups() ALSO includes dated (.bak.YYYYMMDDhhmmss)
    // backups created on every save plus an optional pre-vacuum snapshot.
    // The three saves above land within a single wall-clock second so the
    // dedup logic in writeDatedBackup() collapses them into one dated file
    // — total entries are 3 (rolling) + at most 1 (dated) = up to 4. We
    // pin the lower bound and explicitly verify the rolling triplet is
    // present rather than constraining the exact size.
    void listBackups_after_four_saves_returns_three_entries()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("A"));
        fm.saveImmediate(); shortPause();
        ds.addProject(QStringLiteral("B"));
        fm.saveImmediate(); shortPause();
        ds.addProject(QStringLiteral("C"));
        fm.saveImmediate();

        const QVariantList list = fm.listBackups();
        QVERIFY2(list.size() >= 3,
                 qPrintable(QStringLiteral("expected ≥3 backups, got %1")
                                .arg(list.size())));

        // Build map name → entry for order-independent assertions, and verify
        // every required key is present and well-formed.
        QMap<QString, QVariantMap> byName;
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            QVERIFY(m.contains(QStringLiteral("name")));
            QVERIFY(m.contains(QStringLiteral("path")));
            QVERIFY(m.contains(QStringLiteral("size")));
            QVERIFY(m.contains(QStringLiteral("mtime")));

            const QString name = m.value(QStringLiteral("name")).toString();
            QVERIFY(!name.isEmpty());

            const QString path = m.value(QStringLiteral("path")).toString();
            QVERIFY(QFile::exists(path));

            const qint64 size = m.value(QStringLiteral("size")).toLongLong();
            QVERIFY(size > 0);

            const QDateTime mtime = m.value(QStringLiteral("mtime")).toDateTime();
            QVERIFY(mtime.isValid());

            byName.insert(name, m);
        }

        QVERIFY(byName.contains(QStringLiteral("state.json.bak")));
        QVERIFY(byName.contains(QStringLiteral("state.json.bak.1")));
        QVERIFY(byName.contains(QStringLiteral("state.json.bak.2")));
    }

    // --- restoreBackup -----------------------------------------------------

    // Save twice, mutate the in-memory store, restore from .bak, verify the
    // restored state matches the .bak generation rather than current state.
    void restoreBackup_round_trip_replaces_state()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Original"));
        fm.saveImmediate();      // generation A → state.json

        // 2nd save creates .bak holding generation A.
        ds.addProject(QStringLiteral("AfterFirst"));
        fm.saveImmediate();

        // Sanity: we expect .bak to be the generation-A snapshot (with
        // "Original" project but NOT "AfterFirst" yet).
        QVERIFY(QFile::exists(bak0Path()));

        // Now corrupt the in-memory state with another change before restore
        // so we can observe that restoreBackup actually rolls things back.
        ds.addProject(QStringLiteral("ShouldBeOverwritten"));
        // No save here — we still want to restore the .bak that holds gen A.

        const bool ok = fm.restoreBackup(bak0Path());
        QVERIFY(ok);

        // After restore, "Original" must be present but "AfterFirst" and
        // "ShouldBeOverwritten" must be absent.
        bool foundOriginal = false;
        bool foundAfterFirst = false;
        bool foundShouldBeOverwritten = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("Original"))           foundOriginal = true;
            if (p->name() == QStringLiteral("AfterFirst"))         foundAfterFirst = true;
            if (p->name() == QStringLiteral("ShouldBeOverwritten")) foundShouldBeOverwritten = true;
        }
        QVERIFY(foundOriginal);
        QVERIFY(!foundAfterFirst);
        QVERIFY(!foundShouldBeOverwritten);

        // Persisted state.json should now reflect the restored generation —
        // restoreBackup() calls saveImmediate() after applying.
        DataStore ds2;
        FileManager fm2(&ds2);
        fm2.load();
        bool foundOriginalOnDisk = false;
        for (auto *p : ds2.allProjects()) {
            if (p->name() == QStringLiteral("Original")) { foundOriginalOnDisk = true; break; }
        }
        QVERIFY(foundOriginalOnDisk);
    }

    void restoreBackup_invalid_path_returns_false_and_keeps_state()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Survivor"));
        fm.saveImmediate();

        // Read state.json bytes BEFORE the restore attempt.
        QFile before(stateFilePath());
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray bytesBefore = before.readAll();
        before.close();

        const bool ok = fm.restoreBackup(QStringLiteral("/this/path/does/not/exist.bak"));
        QVERIFY(!ok);

        // state.json must be untouched.
        QFile after(stateFilePath());
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QByteArray bytesAfter = after.readAll();
        after.close();
        QCOMPARE(bytesAfter, bytesBefore);

        // In-memory store must still hold the survivor project.
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("Survivor")) { found = true; break; }
        }
        QVERIFY(found);
    }

    void restoreBackup_corrupt_file_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Survivor"));
        fm.saveImmediate();

        // Hand-write a corrupt "backup" file in the data dir.
        const QString corruptPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                    + QStringLiteral("/OpenTodo/handcrafted-bad.bak");
        QFile bad(corruptPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ this is not json !!!");
        bad.close();

        const bool ok = fm.restoreBackup(corruptPath);
        QVERIFY(!ok);

        // In-memory store still has Survivor.
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("Survivor")) { found = true; break; }
        }
        QVERIFY(found);

        QFile::remove(corruptPath);
    }

    // --- Atomic write -------------------------------------------------------

    // QSaveFile guarantees state.json isn't replaced unless commit() succeeds.
    // We can't easily inject a failure into FileManager::performSave (it
    // doesn't expose a hook), so instead we exercise the
    // exportTo() variant which is structurally identical (same QSaveFile
    // pattern) and pass it a path inside a directory that doesn't exist.
    // Then we verify state.json is left intact.
    void atomic_write_failure_does_not_corrupt_state()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Pristine"));
        fm.saveImmediate();

        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytesBefore = f.readAll();
        f.close();

        // Try to export to a path whose parent directory doesn't exist —
        // QSaveFile.open() should fail and exportTo() must return false
        // without touching state.json.
        const QString badTarget = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                  + QStringLiteral("/OpenTodo/no-such-dir/export.json");
        const bool ok = fm.exportTo(badTarget);
        QVERIFY(!ok);

        // state.json byte-identical to what we saved.
        QFile f2(stateFilePath());
        QVERIFY(f2.open(QIODevice::ReadOnly));
        const QByteArray bytesAfter = f2.readAll();
        f2.close();
        QCOMPARE(bytesAfter, bytesBefore);
    }

    // --- importData ---------------------------------------------------------

    // importData with a valid JSON doc (built from a known-good in-memory
    // snapshot) populates DataStore correctly and persists immediately.
    void importData_valid_json_populates_store()
    {
        // Producer — build a known snapshot.
        DataStore producer;
        FileManager producerFm(&producer);
        producerFm.load();
        Project *p = producer.addProject(QStringLiteral("Imported P"));
        producer.addTask(QStringLiteral("Imported T"), p->id());
        producer.addLabel(QStringLiteral("imported-lbl"));
        const QString payload = producerFm.exportData();
        QVERIFY(!payload.isEmpty());

        // Wipe disk so the consumer starts from defaults.
        wipeAll();

        DataStore consumer;
        FileManager consumerFm(&consumer);
        consumerFm.load();   // gives consumer the default inbox

        const bool ok = consumerFm.importData(payload);
        QVERIFY(ok);

        bool foundProj = false;
        for (auto *pr : consumer.allProjects()) {
            if (pr->name() == QStringLiteral("Imported P")) { foundProj = true; break; }
        }
        QVERIFY(foundProj);

        bool foundTask = false;
        for (auto *t : consumer.allTasks()) {
            if (t->content() == QStringLiteral("Imported T")) { foundTask = true; break; }
        }
        QVERIFY(foundTask);

        QVERIFY(consumer.labelByName(QStringLiteral("imported-lbl")) != nullptr);

        // importData calls saveImmediate() so state.json should now hold the
        // imported snapshot.
        QVERIFY(QFile::exists(stateFilePath()));
    }

    void importData_malformed_json_returns_false_store_unchanged()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("BeforeImport"));
        const int beforeProjects = ds.allProjects().size();
        const int beforeTasks = ds.allTasks().size();

        const bool ok = fm.importData(QStringLiteral("not even close to json"));
        QVERIFY(!ok);

        // Counts unchanged.
        QCOMPARE(ds.allProjects().size(), beforeProjects);
        QCOMPARE(ds.allTasks().size(), beforeTasks);

        // BeforeImport project still present.
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("BeforeImport")) { found = true; break; }
        }
        QVERIFY(found);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Iter-night-15 pins. The pre-fix importData rejected ONLY parse errors;
    // every other "valid JSON, wrong shape" payload silently wiped the store
    // via `m_store->loadFromJson({}|array|foreign-object)` → `clear()`.
    // ─────────────────────────────────────────────────────────────────────────

    void importData_jsonArrayRoot_returns_false_store_unchanged()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));
        const int beforeProjects = ds.allProjects().size();

        // Valid JSON, but the root is an ARRAY (e.g. a package.json that's a
        // bare list, a Todoist labels export, an iCal CSV row dump). Pre-fix
        // `doc.object()` silently returned `{}` and the store was wiped.
        const bool ok = fm.importData(QStringLiteral(R"([1, 2, "three", {"id": "x"}])"));
        QVERIFY2(!ok,
                 "importData must reject a JSON-array root — pre-fix the "
                 "QJsonDocument::object() shortcut silently emptied the "
                 "store via clear() under loadFromJson({}).");
        QCOMPARE(ds.allProjects().size(), beforeProjects);
        bool sentinelStillThere = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("Sentinel")) {
                sentinelStillThere = true;
                break;
            }
        }
        QVERIFY(sentinelStillThere);
    }

    void importData_emptyObject_returns_false_store_unchanged()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));
        const int beforeProjects = ds.allProjects().size();

        // An empty JSON object passes parseError and isObject(), but has none
        // of the required top-level keys.
        const bool ok = fm.importData(QStringLiteral("{}"));
        QVERIFY2(!ok,
                 "importData must reject an empty object — pre-fix the "
                 "store was clear()'d into a default state.");
        QCOMPARE(ds.allProjects().size(), beforeProjects);
    }

    void importData_unrelatedObject_returns_false_store_unchanged()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));
        const int beforeProjects = ds.allProjects().size();

        // A perfectly valid foreign JSON object — e.g. a package.json. None
        // of its keys overlap with the required OpenTodo-export keys
        // (schema_version / projects / tasks / settings).
        const QString fakePackageJson = QStringLiteral(
            R"({"name":"foo","version":"1.0.0","dependencies":{"bar":"^2"}})");
        const bool ok = fm.importData(fakePackageJson);
        QVERIFY2(!ok,
                 "importData must reject foreign objects (package.json, "
                 "random API payloads) — pre-fix the user lost all data on "
                 "a single mis-click in the file picker.");
        QCOMPARE(ds.allProjects().size(), beforeProjects);
    }

    void importData_validButTinyObjectMissingSchema_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));

        // Has SOME keys, but none of the required OpenTodo top-level keys.
        const QString suspect = QStringLiteral(
            R"({"_meta":{"foo":"bar"},"comments":[]})");
        QVERIFY(!fm.importData(suspect));
    }

    void importData_minimalValidExport_succeeds()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        // Smallest payload that satisfies the new gate: any single key from
        // the required set is enough. `tasks: []` is a deliberate import that
        // wipes all tasks (legitimate user intent).
        const QString minimal = QStringLiteral(
            R"({"schema_version":1,"tasks":[],"projects":[],"settings":{}})");
        const bool ok = fm.importData(minimal);
        QVERIFY2(ok,
                 "Minimal valid OpenTodo-shaped export must still succeed; "
                 "the gate only filters obviously-foreign payloads.");
    }

    void importData_takesPreImportSafetySnapshot()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("PreImportProject"));
        fm.saveImmediate();

        // Wipe any pre-existing rolling-chain entries so we can attribute
        // every survivor below to this slot's saveImmediate(s).
        const QString bak0 = stateFilePath() + QStringLiteral(".bak");
        const QString bak1 = stateFilePath() + QStringLiteral(".bak.1");
        QFile::remove(bak0);
        QFile::remove(bak1);
        QVERIFY(!QFile::exists(bak0));
        QVERIFY(!QFile::exists(bak1));

        // A legitimate import that replaces all data.
        const QString replacement = QStringLiteral(
            R"({"schema_version":1,"projects":[{"id":"x","name":"Imported","is_inbox":true,"is_deleted":false}],"tasks":[],"settings":{}})");
        const bool ok = fm.importData(replacement);
        QVERIFY(ok);

        // Post-fix importData runs saveImmediate TWICE: once to snapshot
        // the pre-import state and once to write the imported state. The
        // pre-fix path ran only the post-import save, so .bak contained
        // pre-import data (copied off state.json before the write) but
        // .bak.1 stayed empty. Post-fix, both rolling slots hold the
        // pre-import snapshot, giving the user one extra slot of safety
        // against a hasty second mutation rotating the snapshot away.
        QVERIFY(QFile::exists(bak0));
        QVERIFY2(QFile::exists(bak1),
                 "Iter-night-15: importData must invoke saveImmediate "
                 "BEFORE the load (in addition to the existing one after) "
                 "so both .bak and .bak.1 carry the pre-import snapshot. "
                 "Mirror's restoreBackup's two-saveImmediate contract; "
                 "pre-fix the user had only the .bak slot and any rapid "
                 "subsequent mutation rotated it away.");
    }

    // --- exportData round-trip ---------------------------------------------

    // export → import via a fresh DataStore should yield identical entity
    // counts. This is a stronger guarantee than the existing
    // importData_round_trip in test_filemanager.cpp because it explicitly
    // counts every collection.
    void exportData_round_trip_preserves_entity_counts()
    {
        DataStore ds1;
        FileManager fm1(&ds1);
        fm1.load();

        Project *p1 = ds1.addProject(QStringLiteral("Alpha"));
        Project *p2 = ds1.addProject(QStringLiteral("Beta"));
        Q_UNUSED(p2)
        ds1.addTask(QStringLiteral("T1"), p1->id());
        ds1.addTask(QStringLiteral("T2"), p1->id());
        ds1.addTask(QStringLiteral("T3"), p1->id());
        ds1.addLabel(QStringLiteral("L1"));
        ds1.addLabel(QStringLiteral("L2"));
        ds1.addFilter(QStringLiteral("F1"), QStringLiteral("today"));
        ds1.addSection(QStringLiteral("S1"), p1->id());

        const int projectsBefore = ds1.allProjects().size();
        const int tasksBefore    = ds1.allTasks().size();
        const int labelsBefore   = ds1.allLabels().size();
        const int filtersBefore  = ds1.allFilters().size();

        const QString payload = fm1.exportData();
        QVERIFY(!payload.isEmpty());

        // Brand-new store; import from the snapshot.
        DataStore ds2;
        FileManager fm2(&ds2);
        // Don't call load() — importData replaces in-memory state outright,
        // and we want to assert counts match exactly without inbox bias from
        // createDefaultState().
        const bool ok = fm2.importData(payload);
        QVERIFY(ok);

        QCOMPARE(ds2.allProjects().size(), projectsBefore);
        QCOMPARE(ds2.allTasks().size(),    tasksBefore);
        QCOMPARE(ds2.allLabels().size(),   labelsBefore);
        QCOMPARE(ds2.allFilters().size(),  filtersBefore);
    }
};

QTEST_GUILESS_MAIN(TestFileManagerBackups)
#include "test_filemanager_backups.moc"
