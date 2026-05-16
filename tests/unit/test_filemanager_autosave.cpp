// Tests for FileManager 1-second autosave debounce behavior.
//
// FileManager listens on DataStore::dataChanged, sets m_dirty, and starts
// (or restarts) a 1 s single-shot QTimer in save(). When the timer fires
// performSave() runs and emits saved(). saveImmediate() stops the timer
// and writes synchronously, bypassing the debounce.
//
// Coverage focus (NOT covered by test_filemanager.cpp / _backups.cpp):
//   * single mutation triggers exactly one debounced save after ~1 s
//   * five rapid mutations coalesce into ONE save (not five)
//   * a fresh mutation after the first save fires another save
//     (debounce timer restarts on each onDataChanged, including post-save)
//   * saveImmediate() flushes BEFORE the debounce timer fires
//   * mutations interleaved with saveImmediate captures both writes
//   * concurrent project + task + label changes inside the debounce window
//     produce a single coalesced save covering all of them
//   * first-launch with empty store + first mutation produces state.json
//   * race: explicit saveImmediate followed by a still-pending timer must
//     not produce a second redundant save (the timer was stopped)
//   * mutation arriving after a debounced save still schedules the next save
//
// Driving time: rely on QSignalSpy::wait() to advance Qt's event loop so
// the QTimer fires. Each slot caps its waits at ~1.3 s to keep total
// runtime well under 15 s.

#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QElapsedTimer>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/task.h"
#include "model/project.h"

class TestFileManagerAutosave : public QObject
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
    }

    // Read project count from the on-disk state.json — used to confirm a
    // particular set of mutations made it through to disk.
    int projectCountOnDisk() const
    {
        QFile f(stateFilePath());
        if (!f.open(QIODevice::ReadOnly))
            return -1;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isNull() || !doc.isObject())
            return -1;
        return doc.object().value(QStringLiteral("projects")).toArray().size();
    }

    // Block until either the spy receives N events or timeout elapses.
    // Used in lieu of QTest::qWait() so the wait can short-circuit when
    // the debounce timer fires before the upper bound.
    static bool waitForCount(QSignalSpy &spy, int target, int timeoutMs)
    {
        QElapsedTimer t;
        t.start();
        while (spy.count() < target && t.elapsed() < timeoutMs) {
            // 50 ms granularity is plenty for a 1 s debounce.
            spy.wait(50);
        }
        return spy.count() >= target;
    }

private slots:
    void initTestCase()
    {
        // Per-test org/app so the test-mode AppDataLocation does not collide
        // with sibling FileManager-based tests when run under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_FileMgrAutosave"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filemanager_autosave"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    void cleanup()
    {
        // Each slot starts from a clean disk so prior test residue can't
        // affect rotation, debounce, or first-launch assertions.
        wipeAll();
    }

    // ── 1) Single mutation → exactly one debounced save ─────────────────
    //
    // No save should fire at t=0 and no save should fire at t≈100 ms;
    // the saved() signal must only arrive after the 1 s debounce.

    void debounce_single_mutation_fires_once()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();          // load() calls createDefaultState() → saveImmediate()

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        ds.addProject(QStringLiteral("Single"));
        QVERIFY(fm.isDirty());

        // Right after the mutation: nothing should have fired yet.
        QCOMPARE(savedSpy.count(), 0);

        // Sub-debounce wait: still nothing.
        savedSpy.wait(200);
        QCOMPARE(savedSpy.count(), 0);

        // Wait for the timer to fire (debounce is 1000 ms).
        QVERIFY(waitForCount(savedSpy, 1, 1300));
        QCOMPARE(savedSpy.count(), 1);
        QVERIFY(!fm.isDirty());
    }

    // ── 2) Five mutations within the debounce window coalesce to ONE save ─
    //
    // The existing test_filemanager.cpp::debounce_five_rapid_mutations_single_write
    // uses 5×50ms = 250ms total and asserts ≤1 extra save. This stronger
    // version tightens that to == 1 and reads back disk to prove every
    // mutation made it into the single coalesced write.

    void debounce_five_mutations_coalesce_to_single_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        const int initialProjects = ds.allProjects().size();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        // 5 mutations, ~80 ms apart → ≈400 ms total, well within the 1 s window.
        // We process events between mutations so signal/slot wiring runs
        // (which restarts the debounce timer each time).
        for (int i = 0; i < 5; ++i) {
            ds.addProject(QStringLiteral("Coalesce%1").arg(i));
            QCoreApplication::processEvents();
            savedSpy.wait(80);
        }

        // Mid-window snapshot: no save should have fired yet because the
        // timer kept being restarted.
        QCOMPARE(savedSpy.count(), 0);

        // Now drain to the actual debounce + small slack.
        QVERIFY(waitForCount(savedSpy, 1, 1300));

        // Exactly one save covering every mutation.
        QCOMPARE(savedSpy.count(), 1);

        // And the on-disk file reflects all 5 added projects.
        QCOMPARE(projectCountOnDisk(), initialProjects + 5);
    }

    // ── 3) Mutation after first save → debounce restarts and saves again ─

    void debounce_mutation_after_save_fires_second_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        // First mutation + wait for first debounced save.
        ds.addProject(QStringLiteral("First"));
        QVERIFY(waitForCount(savedSpy, 1, 1300));
        QCOMPARE(savedSpy.count(), 1);
        QVERIFY(!fm.isDirty());

        // Brief pause AFTER the first save lands so the timer is fully
        // idle before we mutate again.
        savedSpy.wait(50);

        // Second mutation should restart the debounce and produce a 2nd save.
        ds.addProject(QStringLiteral("Second"));
        QVERIFY(fm.isDirty());

        QVERIFY(waitForCount(savedSpy, 2, 1300));
        QCOMPARE(savedSpy.count(), 2);
        QVERIFY(!fm.isDirty());
    }

    // ── 4) saveImmediate() flushes BEFORE the debounce timer fires ──────
    //
    // After a mutation, calling saveImmediate() must produce exactly one
    // saved() signal RIGHT NOW, and the timer must not produce a second
    // save afterwards (because saveImmediate stops it).

    void saveImmediate_flushes_before_debounce_fires()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        const int initialProjects = ds.allProjects().size();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        ds.addProject(QStringLiteral("Flushed"));
        QVERIFY(fm.isDirty());

        // Don't wait for debounce — flush now.
        fm.saveImmediate();
        QCOMPARE(savedSpy.count(), 1);
        QVERIFY(!fm.isDirty());

        // Disk reflects the flush instantly.
        QCOMPARE(projectCountOnDisk(), initialProjects + 1);

        // Wait long enough that the original 1 s debounce timer would have
        // fired — saveImmediate() should have stopped it, so no extra save.
        savedSpy.wait(1200);
        QCOMPARE(savedSpy.count(), 1);
    }

    // ── 5) Mutations interleaved with saveImmediate are not lost ────────
    //
    // Mutate → saveImmediate (writes generation A) → mutate again →
    // wait for debounce → second write must contain BOTH mutations.

    void mutation_during_inflight_save_is_captured_by_next_debounce()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        const int initialProjects = ds.allProjects().size();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        // First mutation + immediate flush — generation A on disk.
        ds.addProject(QStringLiteral("BeforeFlush"));
        fm.saveImmediate();
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(projectCountOnDisk(), initialProjects + 1);

        // Second mutation arrives "during" the flush window. In a single-
        // threaded Qt event loop this manifests as: another mutation right
        // after saveImmediate returns. The next debounced save must capture
        // it — no lost write.
        ds.addProject(QStringLiteral("AfterFlush"));
        QVERIFY(fm.isDirty());

        QVERIFY(waitForCount(savedSpy, 2, 1300));
        QCOMPARE(savedSpy.count(), 2);
        QVERIFY(!fm.isDirty());

        // Both projects on disk after the second debounced save.
        QCOMPARE(projectCountOnDisk(), initialProjects + 2);
    }

    // ── 6) Concurrent mutations across entity types coalesce into 1 save ─
    //
    // FileManager listens on DataStore::dataChanged (the aggregate signal).
    // Adding a project + a task + a label inside the debounce window must
    // produce a single coalesced save covering all three changes.

    void concurrent_entity_mutations_coalesce_to_single_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        const int initialProjects = ds.allProjects().size();
        const int initialTasks    = ds.allTasks().size();
        const int initialLabels   = ds.allLabels().size();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        Project *p = ds.addProject(QStringLiteral("Concurrent"));
        QCoreApplication::processEvents();
        savedSpy.wait(50);

        ds.addTask(QStringLiteral("Concurrent task"), p->id());
        QCoreApplication::processEvents();
        savedSpy.wait(50);

        ds.addLabel(QStringLiteral("concurrent-label"));
        QCoreApplication::processEvents();
        // Total elapsed so far ≈ 100 ms, far inside the 1 s debounce.
        QCOMPARE(savedSpy.count(), 0);

        // Wait for debounce → exactly one save.
        QVERIFY(waitForCount(savedSpy, 1, 1300));
        QCOMPARE(savedSpy.count(), 1);

        // All three mutations made it to disk in the single coalesced save.
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        QVERIFY(doc.isObject());
        const QJsonObject root = doc.object();
        QCOMPARE(root.value(QStringLiteral("projects")).toArray().size(),
                 initialProjects + 1);
        QCOMPARE(root.value(QStringLiteral("tasks")).toArray().size(),
                 initialTasks + 1);
        QCOMPARE(root.value(QStringLiteral("labels")).toArray().size(),
                 initialLabels + 1);
    }

    // ── 7) First-launch behavior: state.json gets created ──────────────
    //
    // wipeAll() in cleanup() removed all state files. The very first
    // load() must call createDefaultState() → saveImmediate(), so
    // state.json exists immediately even with no user mutation.
    // A subsequent mutation triggers another debounced save.

    void first_launch_creates_state_then_debounces_user_mutation()
    {
        // Sanity: the cleanup() before this slot wiped state.json.
        QVERIFY(!QFile::exists(stateFilePath()));

        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        // load() ⇒ createDefaultState() ⇒ saveImmediate()
        QVERIFY(QFile::exists(stateFilePath()));
        QVERIFY(!fm.isDirty());

        QSignalSpy savedSpy(&fm, &FileManager::saved);
        const int initialProjects = ds.allProjects().size();

        // First user mutation after a clean launch — debounced.
        ds.addProject(QStringLiteral("FirstUserProject"));
        QVERIFY(fm.isDirty());
        QCOMPARE(savedSpy.count(), 0);

        QVERIFY(waitForCount(savedSpy, 1, 1300));
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(projectCountOnDisk(), initialProjects + 1);
    }

    // ── 8) Race: saveImmediate cancels the pending debounced save ──────
    //
    // Schedule a debounced save, then call saveImmediate before the timer
    // fires. saveImmediate() must call stop() on the timer (it does — see
    // FileManager::saveImmediate). After flushing once, no second save
    // should land even if we wait past the original deadline.

    void saveImmediate_cancels_pending_debounce()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        ds.addProject(QStringLiteral("Pending"));
        QVERIFY(fm.isDirty());

        // Wait part-way into the debounce window so the timer is actively
        // pending (but hasn't fired yet).
        savedSpy.wait(300);
        QCOMPARE(savedSpy.count(), 0);

        // Flush now — the timer should be stopped.
        fm.saveImmediate();
        QCOMPARE(savedSpy.count(), 1);
        QVERIFY(!fm.isDirty());

        // Wait past where the original timer would have fired (≥1 s after
        // mutation). If it weren't stopped we'd see savedCount==2.
        savedSpy.wait(1100);
        QCOMPARE(savedSpy.count(), 1);
    }

    // ── 9) Settings mutation also triggers debounced autosave ──────────
    //
    // The existing test exercises addProject. setSetting takes a different
    // path through DataStore but should still emit dataChanged → trigger
    // the same debounce.

    void setting_mutation_triggers_debounced_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        ds.setSetting(QStringLiteral("test_autosave_marker"), QStringLiteral("on"));
        QVERIFY(fm.isDirty());
        QCOMPARE(savedSpy.count(), 0);

        QVERIFY(waitForCount(savedSpy, 1, 1300));
        QCOMPARE(savedSpy.count(), 1);

        // Read back: setting must be in state.json.
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        QVERIFY(doc.isObject());
        const QJsonObject settings = doc.object()
                                        .value(QStringLiteral("settings"))
                                        .toObject();
        QCOMPARE(settings.value(QStringLiteral("test_autosave_marker")).toString(),
                 QStringLiteral("on"));
    }

    // ── 10) Long-running coalescing: 10 mutations spaced 100 ms apart ──
    //
    // Each mutation restarts the debounce. After the last one, we must
    // wait ~1 s before the save finally fires. Total elapsed from the
    // first mutation to first save should be > 10 × 100 ms + ~1 s.

    void long_running_mutations_keep_extending_debounce()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        QSignalSpy savedSpy(&fm, &FileManager::saved);

        QElapsedTimer wallClock;
        wallClock.start();

        for (int i = 0; i < 10; ++i) {
            ds.addProject(QStringLiteral("Extend%1").arg(i));
            QCoreApplication::processEvents();
            savedSpy.wait(100);
        }

        // After ~1000 ms of mutations, no save yet — the timer keeps
        // getting restarted.
        QCOMPARE(savedSpy.count(), 0);

        // Now wait the debounce out.
        QVERIFY(waitForCount(savedSpy, 1, 1300));

        // Exactly one save covers all 10 mutations.
        QCOMPARE(savedSpy.count(), 1);

        // Sanity: total elapsed must be > 10 × 100 ms (≈1000 ms) plus a
        // chunk of the post-mutation debounce. We don't pin this hard
        // because CI clocks vary; just verify it's not absurdly fast.
        QVERIFY(wallClock.elapsed() >= 1000);
    }
};

QTEST_GUILESS_MAIN(TestFileManagerAutosave)
#include "test_filemanager_autosave.moc"
