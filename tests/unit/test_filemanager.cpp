#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/task.h"
#include "model/project.h"

class TestFileManager : public QObject
{
    Q_OBJECT

private:
    // Build a FileManager whose DataStore uses a temp dir for state.json.
    // We redirect QStandardPaths into that dir via QStandardPaths::setTestModeEnabled.
    QTemporaryDir *m_tmpDir = nullptr;

    // Returns the state.json path that FileManager will use when test mode
    // is enabled. On non-Android the path is AppDataLocation/OpenTodo/state.json.
    QString stateFilePath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo/state.json");
    }

    void removeStateFile() const
    {
        QFile::remove(stateFilePath());
    }

private slots:
    void initTestCase()
    {
        // Per-test org/app so the test-mode AppDataLocation does not collide
        // with sibling FileManager-based tests when run under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filemanager"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    void cleanup()
    {
        // Remove state.json between tests so tests don't bleed into each other
        removeStateFile();
    }

    // ── No file → default state ───────────────────────────────────────────────

    void load_no_file_creates_default_state_with_inbox()
    {
        removeStateFile();

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy spy(&fm, &FileManager::loaded);
        fm.load();

        QCOMPARE(spy.count(), 1);

        // Default state must contain an inbox project named «Входящие»
        Project *inbox = ds.inboxProject();
        QVERIFY(inbox != nullptr);
        QVERIFY(inbox->isInbox());
        QCOMPARE(inbox->name(), QStringLiteral("Входящие"));
    }

    void load_no_file_state_json_is_created()
    {
        removeStateFile();

        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        // state.json should now exist (createDefaultState calls saveImmediate)
        QVERIFY(QFile::exists(stateFilePath()));
    }

    // ── saveImmediate / load round-trip ───────────────────────────────────────

    void saveImmediate_writes_valid_json()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load(); // create default state first

        // Mutate
        ds.addProject(QStringLiteral("Personal"));

        QSignalSpy savedSpy(&fm, &FileManager::saved);
        fm.saveImmediate();
        QCOMPARE(savedSpy.count(), 1);

        // File must contain valid JSON
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(!doc.isNull());
        QVERIFY(doc.object().contains(QStringLiteral("tasks")));
        QVERIFY(doc.object().contains(QStringLiteral("projects")));
    }

    void save_and_load_round_trip()
    {
        // Store 1: create data and save
        {
            DataStore ds;
            FileManager fm(&ds);
            fm.load(); // creates inbox

            Project *p = ds.addProject(QStringLiteral("Work"));
            Task *t = ds.addTask(QStringLiteral("Write docs"), p->id());
            t->setPriority(3);
            t->setLabels({QStringLiteral("dev")});
            ds.addLabel(QStringLiteral("urgent"));
            ds.addFilter(QStringLiteral("Overdue"), QStringLiteral("overdue"));
            Q_UNUSED(t)

            fm.saveImmediate();
        }

        // Store 2: load from same file
        {
            DataStore ds2;
            FileManager fm2(&ds2);
            QSignalSpy loadSpy(&fm2, &FileManager::loaded);
            fm2.load();
            QCOMPARE(loadSpy.count(), 1);

            // Project present
            bool foundWork = false;
            for (auto *p : ds2.allProjects()) {
                if (p->name() == QStringLiteral("Work")) { foundWork = true; break; }
            }
            QVERIFY(foundWork);

            // Task present with priority and labels preserved
            bool foundTask = false;
            for (auto *t : ds2.allTasks()) {
                if (t->content() == QStringLiteral("Write docs")) {
                    QCOMPARE(t->priority(), 3);
                    QCOMPARE(t->labels(), QStringList{QStringLiteral("dev")});
                    foundTask = true;
                    break;
                }
            }
            QVERIFY(foundTask);

            // Label present
            QVERIFY(ds2.labelByName(QStringLiteral("urgent")) != nullptr);

            // Filter present
            bool foundFilter = false;
            for (auto *f : ds2.allFilters()) {
                if (f->name() == QStringLiteral("Overdue")) { foundFilter = true; break; }
            }
            QVERIFY(foundFilter);
        }
    }

    // ── Multi-entity full round-trip ──────────────────────────────────────────

    void round_trip_multiple_tasks_and_projects()
    {
        const int taskCount = 5;
        const int projectCount = 3; // includes inbox created by load()

        {
            DataStore ds;
            FileManager fm(&ds);
            fm.load();

            Project *p = ds.addProject(QStringLiteral("Proj A"));
            Project *p2 = ds.addProject(QStringLiteral("Proj B"));
            Q_UNUSED(p2)
            for (int i = 0; i < taskCount; ++i) {
                ds.addTask(QStringLiteral("Task %1").arg(i), p->id());
            }
            fm.saveImmediate();
        }

        DataStore ds2;
        FileManager fm2(&ds2);
        fm2.load();

        QCOMPARE(ds2.allTasks().size(), taskCount);
        QCOMPARE(ds2.allProjects().size(), projectCount);
    }

    // ── isDirty flag ──────────────────────────────────────────────────────────

    void isDirty_true_after_mutation_false_after_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.saveImmediate(); // flush default state, clear dirty

        QVERIFY(!fm.isDirty());

        ds.addProject(QStringLiteral("Mutation"));
        // onDataChanged is called synchronously via signal → dirty becomes true
        QVERIFY(fm.isDirty());

        fm.saveImmediate();
        QVERIFY(!fm.isDirty());
    }

    // ── Debounce: 5 rapid mutations produce only one file write ───────────────

    void debounce_five_rapid_mutations_single_write()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.saveImmediate(); // flush

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        // Perform 5 mutations in quick succession without yielding the event loop
        for (int i = 0; i < 5; ++i) {
            ds.addProject(QStringLiteral("P%1").arg(i));
            QTest::qWait(50); // 50 ms between each — well under 1 s debounce
        }

        // At this point the debounce timer hasn't fired yet
        // saved count should be 0 (or possibly 1 if a timer tick squeezed in)
        int savedBefore = savedSpy.count();

        // Wait for debounce to fire (1000 ms timer + margin)
        QTest::qWait(1500);

        int savedAfter = savedSpy.count();
        // Only one additional save should have happened (the debounced one)
        QVERIFY(savedAfter - savedBefore <= 1);
        // And at least one save must have fired total
        QVERIFY(savedAfter >= 1);
    }

    // ── exportData / importData round-trip ────────────────────────────────────

    void exportData_returns_valid_json_string()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Export Test"));

        QString exported = fm.exportData();
        QVERIFY(!exported.isEmpty());

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(exported.toUtf8(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.object().contains(QStringLiteral("projects")));
    }

    void importData_round_trip()
    {
        // Export from store 1
        DataStore ds1;
        FileManager fm1(&ds1);
        fm1.load();
        Project *p = ds1.addProject(QStringLiteral("Imported Project"));
        ds1.addTask(QStringLiteral("Imported Task"), p->id());
        QString exported = fm1.exportData();

        // Import into store 2
        DataStore ds2;
        FileManager fm2(&ds2);
        fm2.load();
        bool ok = fm2.importData(exported);
        QVERIFY(ok);

        bool foundProject = false;
        for (auto *pr : ds2.allProjects()) {
            if (pr->name() == QStringLiteral("Imported Project")) { foundProject = true; break; }
        }
        QVERIFY(foundProject);

        bool foundTask = false;
        for (auto *t : ds2.allTasks()) {
            if (t->content() == QStringLiteral("Imported Task")) { foundTask = true; break; }
        }
        QVERIFY(foundTask);
    }

    void importData_invalid_json_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        bool ok = fm.importData(QStringLiteral("{ not valid json !!!"));
        QVERIFY(!ok);
    }

    // ── loaded() signal emitted exactly once ──────────────────────────────────

    void loaded_signal_emitted_on_load()
    {
        removeStateFile();
        DataStore ds;
        FileManager fm(&ds);
        QSignalSpy spy(&fm, &FileManager::loaded);
        fm.load();
        QCOMPARE(spy.count(), 1);
    }

    // ── dataPath is not empty ─────────────────────────────────────────────────

    void dataPath_is_set()
    {
        DataStore ds;
        FileManager fm(&ds);
        QVERIFY(!fm.dataPath().isEmpty());
    }

    // Regression: changes made within the 1-second autosave debounce window
    // must reach disk if the app shuts down. The fix in main.cpp connects
    // QCoreApplication::aboutToQuit → fileManager->saveImmediate(). This
    // test exercises the same primitive directly.
    void saveImmediate_flushes_pending_debounced_change()
    {
        DataStore ds;
        FileManager fm(&ds);

        // Wait for initial load to settle.
        QSignalSpy loadedSpy(&fm, &FileManager::loaded);
        fm.load();
        QVERIFY(loadedSpy.wait(2000) || !loadedSpy.isEmpty());

        // Change a setting — this fires dataChanged → save() → starts the
        // 1 s debounce timer (NOT immediate write).
        ds.setSetting(QStringLiteral("show_inbox_count"), true);
        QVERIFY(fm.isDirty());

        // Force-flush as we'd do on aboutToQuit, BEFORE the debounce timer
        // fires. Without saveImmediate(), this change would be lost when
        // the FileManager is destroyed by the app shutdown.
        QSignalSpy savedSpy(&fm, &FileManager::saved);
        fm.saveImmediate();
        QCOMPARE(savedSpy.size(), 1);
        QVERIFY(!fm.isDirty());

        // Reload from disk into a brand-new DataStore and confirm the
        // setting survived.
        DataStore ds2;
        FileManager fm2(&ds2);
        QSignalSpy loadedSpy2(&fm2, &FileManager::loaded);
        fm2.load();
        QVERIFY(loadedSpy2.wait(2000) || !loadedSpy2.isEmpty());

        QCOMPARE(ds2.setting(QStringLiteral("show_inbox_count")).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(TestFileManager)
#include "test_filemanager.moc"
