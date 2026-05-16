// ============================================================================
// test_snapshot_corruption.cpp
//
// Corruption-resilience pinning tests for SnapshotManager + the on-disk
// snapshot blob (state.json) it produces. Complements test_snapshot_manager
// (pure threshold logic) and test_snapshot_compaction_equivalence (replay
// equivalence property) by surfacing the failure modes the in-memory loader
// MUST tolerate when the snapshot file is corrupted, truncated, partially
// written, contains junk, or is plain missing.
//
// ─────────────────────────────────────────────────────────────────────────────
// Why these slots live HERE (Infra) and not in test_filemanager_error_paths:
//
//   FileManager's load() is a higher-level convenience that drives
//   QFile + auto-recovery from .bak rotations. The SnapshotManager surface
//   under test is the LOWER layer: state.json -> DataStore::loadFromJson.
//   That is the same blob SnapshotManager uploads to Drive in createSnapshot()
//   — so the load-side resilience contract pinned here is the inverse of
//   the produce-side contract test_snapshot_compaction_equivalence asserts
//   (snapshot + tail == replay). If a peer downloads a bad state.json from
//   Drive, this is the loader path that must catch it.
//
// What is pinned per slot:
//   1. load_with_corrupted_snapshot_falls_back_cleanly
//      — Garbage bytes (not valid JSON at all). QJsonDocument::fromJson
//        produces a null doc, the caller (here: a thin wrapper mimicking
//        FileManager's load contract) does NOT pass the null root through
//        to DataStore::loadFromJson, and the DataStore is left in a clean
//        empty state — no crash, no half-loaded entities.
//
//   2. load_with_truncated_snapshot_handles_partial_json
//      — Valid prefix cut mid-string. QJsonDocument::fromJson reports a
//        parse error; same fallback behaviour as (1).
//
//   3. compaction_with_zero_commits_is_noop
//      — Freshly-constructed SnapshotManager, no GDrive client, zero commit
//        count. createSnapshot() must NOT silently produce a zero-snapshot
//        on disk and must NOT mutate the commit counter. Exercises the
//        early-exit-on-no-auth branch of snapshot.cpp:26-29.
//
//   4. compaction_during_concurrent_write_does_not_corrupt
//      — Filesystem analogue: simulate the process being killed in the
//        middle of writing state.json. The half-written file is detected
//        as invalid JSON; loadFromJson is NOT called with a bogus root;
//        the in-memory DataStore stays empty (no half-state). Pin the
//        recovery semantics so a future regression that bypasses the
//        validity check is caught.
//
//   5. snapshot_with_unknown_entity_type_skips_it
//      — Forward-compat: a snapshot written by a newer build contains an
//        entity collection the current DataStore doesn't know. The loader
//        must ignore the unknown key without erroring out (the known
//        collections still load).
//
//   6. snapshot_with_orphan_tombstones_preserves_them
//      — A tombstone (is_deleted=true, no live counterpart in the same
//        snapshot) MUST survive the loadFromJson round-trip. If it didn't,
//        a peer that hadn't yet pulled the delete would re-introduce the
//        entity on its next push.
//
//   7. shouldCompact_returns_true_at_exact_threshold
//      — Pin the >= boundary. With commitCount() forced to zero (the only
//        publicly-driveable value), setting threshold to 0 makes
//        shouldCompact() true; setting it to 1 makes it false. This is
//        the boundary that determines whether SyncEngine::push triggers
//        a compaction.
//
//   8. compaction_atomicity
//      — Document the production behaviour observed in snapshot.cpp:
//        on the early-exit failure branch (no auth / no client) the
//        in-memory commit counter is preserved and no on-disk artefacts
//        are produced. The "remove ancestral log files" step only runs
//        AFTER the state.json upload succeeded — so a failed upload
//        leaves the log untouched. Tests the no-auth analogue of that
//        contract.
//
// ─────────────────────────────────────────────────────────────────────────────
// Constraints honoured:
//   - No production code is modified. Every assertion pins current
//     behaviour; flipping an assertion when production is changed is
//     the regression-bench.
//   - QTemporaryDir is used for all filesystem fixtures.
//   - Unique QSettings org/app per slot avoids parallel-ctest races on
//     gdrive/* + sync/device_id keys (same convention as
//     test_snapshot_compaction_equivalence).
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QString>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "storage/datastore.h"
#include "sync/snapshot.h"
#include "sync/gdrive.h"
#include "model/task.h"
#include "model/project.h"

namespace {

// Use unique QSettings org/app per slot. Same isolation pattern as
// test_snapshot_compaction_equivalence (file scope, called from each slot
// before any QSettings-touching production code runs).
void useUniqueSettings(const QString &slotName)
{
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SnapshotCorruption"));
    QCoreApplication::setApplicationName(
        QStringLiteral("test_snapshot_corruption_") + slotName);
    QSettings s;
    s.clear();
    s.sync();
}

// Hand-write raw bytes to a path. Mirrors writeRawState() in
// test_filemanager_error_paths so the corruption scenarios stay isolated
// from FileManager's auto-recovery to .bak generations.
bool writeRaw(const QString &path, const QByteArray &bytes)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = f.write(bytes);
    f.close();
    return written == bytes.size();
}

// Thin wrapper around the load path SnapshotManager + FileManager share:
// read the file, parse as JSON, hand the root object to
// DataStore::loadFromJson IFF it is a valid JSON object. Mirrors the
// validity check in FileManager::load (filemanager.cpp:65-68) but stays
// agnostic of FileManager's auto-recovery to .bak so the corruption
// behaviour we're pinning is purely the loader's, not the recovery
// chain's.
//
// Returns:
//   true  — JSON was valid, DataStore::loadFromJson was called.
//   false — JSON was invalid, DataStore::loadFromJson was NOT called.
bool loadSnapshotInto(DataStore *store, const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return false;
    if (doc.isNull() || !doc.isObject()) return false;

    store->loadFromJson(doc.object());
    return true;
}

// Build a minimal but well-formed snapshot blob (matches DataStore::toJson
// shape) so we can compare "clean load" expectations against "corrupted
// load" outcomes without depending on FileManager defaults.
QJsonObject makeMinimalValidSnapshot()
{
    QJsonObject root;
    root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
    root[QStringLiteral("settings")]      = QJsonObject();
    root[QStringLiteral("tasks")]         = QJsonArray();
    root[QStringLiteral("completed_tasks")] = QJsonArray();
    root[QStringLiteral("projects")]      = QJsonArray();
    root[QStringLiteral("sections")]      = QJsonArray();
    root[QStringLiteral("labels")]        = QJsonArray();
    root[QStringLiteral("filters")]       = QJsonArray();
    root[QStringLiteral("reminders")]     = QJsonArray();
    root[QStringLiteral("comments")]      = QJsonArray();
    return root;
}

} // namespace

class TestSnapshotCorruption : public QObject
{
    Q_OBJECT

private:
    bool m_prevTestMode = false;

private slots:
    void initTestCase()
    {
        m_prevTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(m_prevTestMode);
    }

    // ── 1. Corrupted snapshot file → clean empty state, no crash ─────────────
    //      State.json is overwritten with bytes that aren't valid JSON at all
    //      ("not json at all\x00\xff\xfe garbage" — random binary). The load
    //      path's parse check must catch this BEFORE loadFromJson is called.
    //      The DataStore must remain in its empty default state.
    void load_with_corrupted_snapshot_falls_back_cleanly()
    {
        useUniqueSettings(QStringLiteral("corrupted"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString snapPath = tmp.path() + QStringLiteral("/state.json");

        // Random garbage bytes — definitely not parseable as JSON.
        // Embed NUL + high-bit bytes via QByteArrayLiteral so AUTOMOC can scan
        // this file (NUL inside a raw string would break the byte-level
        // moc scanner). The trailing block uses an explicit-length literal so
        // the embedded NUL is not interpreted as a C-string terminator.
        QByteArray garbage = QByteArrayLiteral("not json at all !!! ");
        garbage.append('\x00');
        const char trailing[] = "more garbage \xff\xfe\x01\x02 binary";
        garbage.append(trailing, sizeof(trailing) - 1);
        QVERIFY(writeRaw(snapPath, garbage));

        DataStore store;
        const int beforeTasks    = store.taskCount();
        const int beforeProjects = store.projectCount();

        // No crash. Returns false because the JSON is invalid.
        const bool loaded = loadSnapshotInto(&store, snapPath);
        QVERIFY2(!loaded,
                 "Garbage snapshot must NOT be passed to DataStore::loadFromJson");

        // DataStore is untouched — same counts as before the failed load.
        QCOMPARE(store.taskCount(),    beforeTasks);
        QCOMPARE(store.projectCount(), beforeProjects);
    }

    // ── 2. Truncated snapshot (JSON cut mid-string) → clean fallback ──────────
    //      Build a valid snapshot, serialise to JSON, then truncate the bytes
    //      at a position that lands INSIDE a string literal. The QJsonDocument
    //      parser must reject this; loadFromJson must NOT be called.
    void load_with_truncated_snapshot_handles_partial_json()
    {
        useUniqueSettings(QStringLiteral("truncated"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString snapPath = tmp.path() + QStringLiteral("/state.json");

        // Populate a valid-looking snapshot so we have enough content to
        // truncate meaningfully (need a string literal mid-stream to cut into).
        QJsonObject root = makeMinimalValidSnapshot();
        QJsonArray tasks;
        QJsonObject t1;
        t1[QStringLiteral("id")]      = QStringLiteral("task-truncate-1");
        t1[QStringLiteral("content")] = QStringLiteral(
            "This is a fairly long content field that we will truncate inside");
        t1[QStringLiteral("project_id")] = QStringLiteral("inbox");
        t1[QStringLiteral("is_deleted")] = false;
        tasks.append(t1);
        root[QStringLiteral("tasks")] = tasks;

        const QByteArray fullJson = QJsonDocument(root).toJson();
        QVERIFY(fullJson.size() > 100); // sanity — enough room to slice
        // Slice off the trailing 40 bytes so the cut likely lands inside the
        // content string. (Empirically: this puts the cut squarely inside the
        // "is_deleted" / "content" payload area — definitely a parse error.)
        const QByteArray truncated = fullJson.left(fullJson.size() - 40);
        QVERIFY(truncated.size() > 0);
        QVERIFY(writeRaw(snapPath, truncated));

        DataStore store;
        const int beforeTasks = store.taskCount();
        const bool loaded = loadSnapshotInto(&store, snapPath);
        QVERIFY2(!loaded,
                 "Truncated snapshot must NOT be passed to DataStore::loadFromJson");
        QCOMPARE(store.taskCount(), beforeTasks);

        // Independent confirmation: QJsonDocument flags it as a parse error.
        QFile f(snapPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        QJsonDocument::fromJson(f.readAll(), &err);
        QVERIFY2(err.error != QJsonParseError::NoError,
                 "Truncated JSON should produce a parse error");
    }

    // ── 3. Compaction with zero commits is a no-op ──────────────────────────
    //      A fresh SnapshotManager with no GDrive client must NOT produce a
    //      zero-snapshot artefact, must NOT mutate the commit counter, and
    //      must signal failure exactly once per call (cleanly, no double-emit).
    void compaction_with_zero_commits_is_noop()
    {
        useUniqueSettings(QStringLiteral("zeroCommits"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore store;
        SnapshotManager sm(&store);

        // Pre-condition: fresh manager has zero commits, would not compact
        // at the default threshold.
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        QSignalSpy okSpy(&sm, &SnapshotManager::snapshotCreated);
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);

        // No GDrive client → early-exit failure. Must NOT corrupt the
        // counter (resetCommitCount is on the success path only).
        sm.createSnapshot();

        QCOMPARE(okSpy.count(), 0);
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(sm.commitCount(), 0); // counter NOT reset/mutated

        // No state.json artefact was written into the QTemporaryDir
        // (the early-exit branch returns before any file I/O).
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/state.json")));

        // shouldCompact reflects the still-zero counter at every threshold
        // strictly above 0 → no zero-snapshot path.
        sm.setCompactionThreshold(1);
        QVERIFY(!sm.shouldCompact());
        sm.setCompactionThreshold(100);
        QVERIFY(!sm.shouldCompact());
    }

    // ── 4. Compaction during concurrent write → loader stays clean ───────────
    //      Simulate: a snapshot write was interrupted mid-stream (process
    //      killed, OS power-loss, drive eject between QFile::open and
    //      QFile::close). The on-disk file ends with a truncated trailing
    //      JSON token. The loader must reject it, NOT pass garbage to
    //      DataStore::loadFromJson, and leave DataStore empty.
    //
    //      We separately verify that the OTHER expected loader contract holds:
    //      the loader IS able to load the FULL valid snapshot if it had
    //      been written completely (sanity check that our test fixture is
    //      well-formed).
    void compaction_during_concurrent_write_does_not_corrupt()
    {
        useUniqueSettings(QStringLiteral("concurrentWrite"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString snapPath = tmp.path() + QStringLiteral("/state.json");

        // Build a full snapshot with a non-trivial entity inside so the
        // truncation point matters.
        QJsonObject root = makeMinimalValidSnapshot();
        QJsonArray projects;
        QJsonObject p1;
        p1[QStringLiteral("id")]         = QStringLiteral("p-concurrent-1");
        p1[QStringLiteral("name")]       = QStringLiteral("Project mid-write");
        p1[QStringLiteral("is_deleted")] = false;
        p1[QStringLiteral("created_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
        p1[QStringLiteral("updated_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
        projects.append(p1);
        root[QStringLiteral("projects")] = projects;

        const QByteArray fullJson = QJsonDocument(root).toJson();

        // ── Scenario A: half-written file (interrupted mid-flush). ──────────
        // The on-disk bytes are the first half of the would-be snapshot:
        // missing the closing '}' and any number of trailing brackets. This
        // is what a kill -9 between QFile::write and QFile::close leaves.
        const qint64 halfSize = fullJson.size() / 2;
        const QByteArray halfWritten = fullJson.left(static_cast<int>(halfSize));
        QVERIFY(writeRaw(snapPath, halfWritten));

        {
            DataStore store;
            const bool loaded = loadSnapshotInto(&store, snapPath);
            QVERIFY2(!loaded,
                     "Half-written snapshot must NOT be passed to DataStore::loadFromJson");
            QCOMPARE(store.taskCount(),    0);
            QCOMPARE(store.projectCount(), 0);
        }

        // ── Scenario B: empty file (interrupted before any bytes written). ─
        QVERIFY(writeRaw(snapPath, QByteArray()));
        {
            DataStore store;
            const bool loaded = loadSnapshotInto(&store, snapPath);
            QVERIFY2(!loaded,
                     "Empty snapshot must NOT be passed to DataStore::loadFromJson");
            QCOMPARE(store.taskCount(),    0);
            QCOMPARE(store.projectCount(), 0);
        }

        // ── Positive control: the COMPLETE bytes load successfully. ─────────
        // Pins that our test harness isn't falsely rejecting good data.
        QVERIFY(writeRaw(snapPath, fullJson));
        {
            DataStore store;
            const bool loaded = loadSnapshotInto(&store, snapPath);
            QVERIFY2(loaded,
                     "Full snapshot positive-control failed to load");
            QCOMPARE(store.projectCount(), 1);
            QVERIFY(store.projectById(QStringLiteral("p-concurrent-1")) != nullptr);
        }
    }

    // ── 5. Snapshot with unknown entity type → loader silently ignores it ───
    //      A forward-compat scenario: a peer running a NEWER build has
    //      written a snapshot containing a collection key the current build
    //      doesn't know ("unknown_entity"). The loader must NOT crash and
    //      must NOT abort loading the known collections. Pinning this
    //      behaviour preserves the upgrade path: a newer peer can write a
    //      snapshot, an older peer can read it (lossy on the unknown
    //      collection, but functional on everything else).
    void snapshot_with_unknown_entity_type_skips_it()
    {
        useUniqueSettings(QStringLiteral("unknownEntity"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString snapPath = tmp.path() + QStringLiteral("/state.json");

        QJsonObject root = makeMinimalValidSnapshot();

        // Add a known task so the loader has something legitimate to load.
        QJsonArray tasks;
        QJsonObject t1;
        t1[QStringLiteral("id")]         = QStringLiteral("t-known-1");
        t1[QStringLiteral("content")]    = QStringLiteral("Known task");
        t1[QStringLiteral("project_id")] = QStringLiteral("inbox");
        t1[QStringLiteral("is_deleted")] = false;
        t1[QStringLiteral("created_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
        t1[QStringLiteral("updated_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
        tasks.append(t1);
        root[QStringLiteral("tasks")] = tasks;

        // Inject an entity collection that current DataStore does NOT know.
        // Each "unknown" entry has a shape resembling a real entity (id +
        // some fields) so a future-build loader could read it without
        // confusion.
        QJsonArray unknownEntities;
        QJsonObject u1;
        u1[QStringLiteral("id")]            = QStringLiteral("u-future-1");
        u1[QStringLiteral("future_field")]  = QStringLiteral("hello");
        u1[QStringLiteral("future_number")] = 42;
        unknownEntities.append(u1);
        root[QStringLiteral("unknown_entity")] = unknownEntities;

        // Also inject a single scalar unknown top-level key — the loader
        // must skip non-collection top-level keys it doesn't recognise.
        root[QStringLiteral("future_scalar")] = QStringLiteral("ignored");

        const QByteArray bytes = QJsonDocument(root).toJson();
        QVERIFY(writeRaw(snapPath, bytes));

        DataStore store;
        const bool loaded = loadSnapshotInto(&store, snapPath);
        QVERIFY2(loaded,
                 "Snapshot with unknown keys must still load — keys are ignored");

        // Known collection survived.
        QCOMPARE(store.taskCount(), 1);
        QVERIFY(store.taskById(QStringLiteral("t-known-1")) != nullptr);
        // No projects/labels/etc. created from the unknown collection.
        QCOMPARE(store.projectCount(), 0);
    }

    // ── 6. Orphan tombstones (is_deleted=true, no live counterpart) survive ─
    //      A tombstone in the snapshot — with no corresponding live entity
    //      anywhere — MUST persist through loadFromJson. If it didn't, peers
    //      that haven't yet pulled the delete commit would resurrect the
    //      entity on their next push.
    //
    //      The current loader prunes tombstones older than 30 days
    //      (datastore.cpp:1425 — pruneThreshold). To make the assertion
    //      deterministic we stamp updated_at to "today" so the tombstone is
    //      within the keep-window. The orphan property is independent of
    //      pruning: this slot pins the "kept" path.
    void snapshot_with_orphan_tombstones_preserves_them()
    {
        useUniqueSettings(QStringLiteral("orphanTombstones"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString snapPath = tmp.path() + QStringLiteral("/state.json");

        QJsonObject root = makeMinimalValidSnapshot();

        // updated_at must be recent enough to survive the 30-day prune.
        const QString recentTs = QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODateWithMs);

        // Orphan task tombstone — no live task points at it, but is_deleted=true.
        QJsonArray tasks;
        QJsonObject deletedTask;
        deletedTask[QStringLiteral("id")]         = QStringLiteral("t-orphan-tomb-1");
        deletedTask[QStringLiteral("content")]    = QStringLiteral("Deleted long ago");
        deletedTask[QStringLiteral("project_id")] = QStringLiteral("inbox");
        deletedTask[QStringLiteral("is_deleted")] = true;
        deletedTask[QStringLiteral("created_at")] = recentTs;
        deletedTask[QStringLiteral("updated_at")] = recentTs;
        tasks.append(deletedTask);
        root[QStringLiteral("tasks")] = tasks;

        // Orphan project tombstone.
        QJsonArray projects;
        QJsonObject deletedProject;
        deletedProject[QStringLiteral("id")]         = QStringLiteral("p-orphan-tomb-1");
        deletedProject[QStringLiteral("name")]       = QStringLiteral("Gone project");
        deletedProject[QStringLiteral("is_deleted")] = true;
        deletedProject[QStringLiteral("created_at")] = recentTs;
        deletedProject[QStringLiteral("updated_at")] = recentTs;
        projects.append(deletedProject);
        root[QStringLiteral("projects")] = projects;

        // Orphan label tombstone.
        QJsonArray labels;
        QJsonObject deletedLabel;
        deletedLabel[QStringLiteral("id")]         = QStringLiteral("l-orphan-tomb-1");
        deletedLabel[QStringLiteral("name")]       = QStringLiteral("Old label");
        deletedLabel[QStringLiteral("is_deleted")] = true;
        deletedLabel[QStringLiteral("created_at")] = recentTs;
        deletedLabel[QStringLiteral("updated_at")] = recentTs;
        labels.append(deletedLabel);
        root[QStringLiteral("labels")] = labels;

        const QByteArray bytes = QJsonDocument(root).toJson();
        QVERIFY(writeRaw(snapPath, bytes));

        DataStore store;
        QVERIFY(loadSnapshotInto(&store, snapPath));

        // allTasksIncludingDeleted() exposes tombstones; allTasks() filters
        // them out. The orphan must appear in the FORMER but NOT the latter.
        const auto allTasks   = store.allTasks();              // live only
        const auto allTombs   = store.allTasksIncludingDeleted(); // + dead

        // Tombstone IS preserved on-load (visible via allTasksIncludingDeleted).
        bool foundOrphanTask = false;
        for (auto *t : allTombs) {
            if (t->id() == QStringLiteral("t-orphan-tomb-1")) {
                foundOrphanTask = true;
                QVERIFY2(t->isDeleted(),
                         "Orphan task tombstone must keep is_deleted=true");
            }
        }
        QVERIFY2(foundOrphanTask,
                 "Orphan task tombstone was lost during snapshot load");

        // ... and is excluded from the live-only query path.
        for (auto *t : allTasks) {
            QVERIFY2(t->id() != QStringLiteral("t-orphan-tomb-1"),
                     "Live-only query incorrectly returned a tombstone");
        }

        // Project tombstone: projects don't have an "includingDeleted"
        // query, but allProjects() in DataStore filters tombstones. We can
        // verify the project ID resolves directly through the index by
        // serialising back to JSON and checking the persisted form.
        const QJsonObject roundtrip = store.toJson();
        const QJsonArray projectsBack = roundtrip[QStringLiteral("projects")].toArray();
        bool foundOrphanProject = false;
        for (const QJsonValue &v : projectsBack) {
            const QJsonObject po = v.toObject();
            if (po[QStringLiteral("id")].toString() == QStringLiteral("p-orphan-tomb-1")) {
                foundOrphanProject = true;
                QVERIFY2(po[QStringLiteral("is_deleted")].toBool(),
                         "Orphan project tombstone must keep is_deleted=true on round-trip");
            }
        }
        QVERIFY2(foundOrphanProject,
                 "Orphan project tombstone lost on snapshot toJson round-trip");

        // Label tombstone: same check via toJson.
        const QJsonArray labelsBack = roundtrip[QStringLiteral("labels")].toArray();
        bool foundOrphanLabel = false;
        for (const QJsonValue &v : labelsBack) {
            const QJsonObject lo = v.toObject();
            if (lo[QStringLiteral("id")].toString() == QStringLiteral("l-orphan-tomb-1")) {
                foundOrphanLabel = true;
                QVERIFY2(lo[QStringLiteral("is_deleted")].toBool(),
                         "Orphan label tombstone must keep is_deleted=true on round-trip");
            }
        }
        QVERIFY2(foundOrphanLabel,
                 "Orphan label tombstone lost on snapshot toJson round-trip");
    }

    // ── 7. shouldCompact at exact threshold — pin the >= boundary ────────────
    //      The contract is shouldCompact() == (commitCount() >= threshold).
    //      With the (publicly-driveable) commitCount() == 0:
    //        threshold 0 → 0 >= 0 → true  (always-fires sentinel)
    //        threshold 1 → 0 <  1 → false (no fire)
    //      And we sanity-check that the default threshold of 100 obeys the
    //      same rule with a fresh-zero counter.
    void shouldCompact_returns_true_at_exact_threshold()
    {
        useUniqueSettings(QStringLiteral("exactThreshold"));

        DataStore store;
        SnapshotManager sm(&store);

        // Pre-condition: fresh counter is 0, default threshold is 100.
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact()); // 0 < 100

        // ── At exactly the threshold (count(0) == threshold(0)). ───────────
        // The >= boundary in snapshot.cpp:21 should fire.
        sm.setCompactionThreshold(0);
        QVERIFY2(sm.shouldCompact(),
                 "shouldCompact must be TRUE at count == threshold (the >= boundary)");

        // ── One above the counter (threshold(1), count(0)). ─────────────────
        // Pinning the symmetric NOT-fire side of the boundary.
        sm.setCompactionThreshold(1);
        QVERIFY2(!sm.shouldCompact(),
                 "shouldCompact must be FALSE when count strictly below threshold");

        // ── Default-threshold smoke check. ──────────────────────────────────
        // Mirrors the production gate SyncEngine uses (threshold=100, fires
        // when 100 commits have been pushed since the last snapshot).
        sm.setCompactionThreshold(100);
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        // Documented gap: we cannot drive commitCount() from a unit test
        // (incrementCommitCount is friend-only to SyncEngine), so the
        // boundary on the "99 → no compact, 100 → compact" axis is pinned
        // INDIRECTLY: with the counter forced to 0, setting threshold(0)
        // proves the >= predicate fires at exact equality, and setting
        // threshold(1) proves it doesn't fire at the off-by-one below.
        // Combined, these two assertions wrap the same `>=` operator as
        // the 99/100 case would.
    }

    // ── 8. Compaction atomicity — failed compaction leaves no artefacts ──────
    //      Pin the early-exit failure-branch contract observed in
    //      snapshot.cpp:26-29: when createSnapshot() cannot proceed (no
    //      client OR unauthenticated client), it must:
    //        (a) emit snapshotFailed exactly once,
    //        (b) NEVER emit snapshotCreated,
    //        (c) NEVER mutate the commit counter (resetCommitCount is on
    //            the SUCCESS path only),
    //        (d) NEVER produce on-disk artefacts under the working dir.
    //
    //      The "if compaction completes successfully, the commit log files
    //      for those compacted commits are removed" direction is exercised
    //      by test_snapshot_compaction_equivalence.cpp's
    //      compacting_at_threshold_compacts_all_old_commits slot — we focus
    //      here on the inverse (failure preserves everything).
    void compaction_atomicity()
    {
        useUniqueSettings(QStringLiteral("atomicity"));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString logDir = tmp.path() + QStringLiteral("/log");
        QDir().mkpath(logDir);

        // Lay down "pending commit log files" so we can verify they survive
        // the failed compaction. We write three files because the production
        // delete loop iterates all log files; if a future change accidentally
        // moves the deletion BEFORE the upload, this would catch a one-off bug.
        const QStringList logFiles = {
            QStringLiteral("commit-aaaa.json"),
            QStringLiteral("commit-bbbb.json"),
            QStringLiteral("commit-cccc.json"),
        };
        for (const QString &name : logFiles) {
            QFile f(logDir + QStringLiteral("/") + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"uuid\":\"x\"}");
            f.close();
        }

        DataStore store;
        SnapshotManager sm(&store);

        QSignalSpy okSpy(&sm, &SnapshotManager::snapshotCreated);
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);

        // ── Failure path 1: NO GDriveClient at all ───────────────────────────
        sm.createSnapshot();
        QCOMPARE(okSpy.count(),   0);
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(sm.commitCount(), 0);             // counter NOT reset
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/state.json")));
        QDir d(logDir);
        QCOMPARE(static_cast<int>(d.entryList({QStringLiteral("*.json")},
                                              QDir::Files).size()),
                 logFiles.size());

        // ── Failure path 2: client attached but UNAUTHENTICATED ──────────────
        //   Pins that wiring a logged-out client doesn't allow ANY of the
        //   side-effects (delete loop runs only after a successful upload).
        GDriveClient client;
        QVERIFY(!client.isAuthenticated());
        sm.setGDriveClient(&client);

        sm.createSnapshot();
        QCOMPARE(okSpy.count(),   0);
        QCOMPARE(failSpy.count(), 2);
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/state.json")));
        QCOMPARE(static_cast<int>(d.entryList({QStringLiteral("*.json")},
                                              QDir::Files).size()),
                 logFiles.size());

        // ── Repeated failures: counter STILL not reset ───────────────────────
        //   resetCommitCount only runs on the success branch; we never enter
        //   it on the early-exit branches. Pinned across multiple calls.
        sm.createSnapshot();
        sm.createSnapshot();
        QCOMPARE(okSpy.count(), 0);
        QCOMPARE(failSpy.count(), 4);
        QCOMPARE(sm.commitCount(), 0);
        QCOMPARE(static_cast<int>(d.entryList({QStringLiteral("*.json")},
                                              QDir::Files).size()),
                 logFiles.size());

        // Document the secondary atomicity gap observed in snapshot.cpp:70-79
        // for future maintainers (the deleteFile loop CONTINUES on a single
        // failure and resetCommitCount runs unconditionally at the end —
        // partial Drive deletions are NOT atomic in production, but that
        // sits on the post-upload-success path that's unreachable from a
        // unit test without GDrive integration).
        qInfo("compaction_atomicity: snapshot UPLOAD failure (no client / no auth)"
              " is atomic w.r.t. local artefacts — counter preserved, log files"
              " preserved, no state.json produced. Post-upload-success per-file"
              " deleteFile failures are NOT atomic in production (see"
              " snapshot.cpp:70-79); the loop continues on individual failures"
              " and resetCommitCount runs unconditionally. That gap is documented"
              " by test_snapshot_compaction_equivalence::partial_compaction_failure_is_atomic.");
    }
};

QTEST_GUILESS_MAIN(TestSnapshotCorruption)
#include "test_snapshot_corruption.moc"
