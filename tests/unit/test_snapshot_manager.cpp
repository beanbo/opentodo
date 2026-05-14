// ============================================================================
// test_snapshot_manager.cpp
//
// Tests for SnapshotManager — the commit-log compactor invoked by SyncEngine
// once a configurable threshold of committed-since-snapshot operations is
// reached (default 100).
//
// SnapshotManager is tightly coupled to GDriveClient (its compaction work is
// "upload state.json + delete ancestral log/<uuid>.json files on Drive").
// GDriveClient methods are non-virtual, so a fake-Drive subclass is not
// possible without production-side changes (which this test suite is not
// allowed to make). Therefore the assertions here cover:
//
//   1. The pure threshold logic (`shouldCompact`, `setCompactionThreshold`,
//      `commitCount`) which is fully observable through the public API.
//   2. The contract of `createSnapshot`/`setHeadCommitId`/`setGDriveClient`:
//      with no client, with an unauthenticated client, idempotent calls,
//      and the no-commits path. These exercise the early-exit + signal-emit
//      branches without touching the network.
//
// What is NOT covered (by design):
//   - The Drive-side filesystem effects (state.json upload, log/<uuid>.json
//     deletion). Those require either a real authenticated Drive account or
//     production-side virtualization of GDriveClient. The orchestrator can
//     extend this file once such a hook exists.
//
// All tests use QTemporaryDir and isolated QSettings (see initTestCase) to
// avoid polluting real user data with the OAuth/token state that
// GDriveClient touches on construction.
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QString>

#include "storage/datastore.h"
#include "sync/snapshot.h"
#include "sync/gdrive.h"

class TestSnapshotManager : public QObject
{
    Q_OBJECT

private:
    // Captured before/after toggling test-mode so we can restore.
    bool m_prevTestMode = false;

private slots:
    void initTestCase()
    {
        // Isolate QStandardPaths and QSettings into the test sandbox so that
        // GDriveClient's QSettings access (gdrive/access_token, etc.) does
        // not read from / write to the real user profile.
        m_prevTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);

        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTests"));
        QCoreApplication::setApplicationName(QStringLiteral("SnapshotManagerTest"));

        // Wipe any leftover Drive credentials that might be lurking in the
        // test-mode QSettings location (defensive — should already be empty).
        QSettings settings;
        settings.remove(QStringLiteral("gdrive"));
        settings.sync();
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.remove(QStringLiteral("gdrive"));
        settings.sync();
        QStandardPaths::setTestModeEnabled(m_prevTestMode);
    }

    // ── 1. shouldCompact returns false when commit count < threshold ───────

    void shouldCompact_false_when_count_below_threshold()
    {
        DataStore store;
        SnapshotManager sm(&store);

        // Default threshold is 100; a fresh manager has commitCount() == 0.
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        // Lower the threshold but keep it strictly above the count.
        sm.setCompactionThreshold(5);
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        // Threshold of 1 with zero commits also stays below.
        sm.setCompactionThreshold(1);
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());
    }

    // ── 2. shouldCompact returns true when count >= threshold ───────────────

    void shouldCompact_true_when_count_at_or_above_threshold()
    {
        DataStore store;
        SnapshotManager sm(&store);

        // Threshold of 0 means: shouldCompact == (count >= 0), always true.
        sm.setCompactionThreshold(0);
        QVERIFY(sm.shouldCompact());

        // Confirm the >= boundary holds by raising the threshold one notch
        // above the count and re-checking the boundary at exactly count.
        sm.setCompactionThreshold(1);
        QVERIFY(!sm.shouldCompact()); // count(0) < threshold(1)

        // Negative thresholds (defensive — production never sets these) still
        // satisfy the >= contract.
        sm.setCompactionThreshold(-1);
        QVERIFY(sm.shouldCompact());
    }

    // ── 3. createSnapshot with no GDrive client → snapshotFailed ────────────
    //      This is the "remove log/<commitId>.json files for already-folded
    //      commits" path's no-op branch. We can't observe Drive-side deletion
    //      without a real account, so we assert the contract: when there is
    //      no client (or it is unauthenticated), the manager must not pretend
    //      to have compacted — it emits snapshotFailed.

    void createSnapshot_without_gdrive_emits_failed()
    {
        DataStore store;
        SnapshotManager sm(&store);

        QSignalSpy okSpy(&sm, &SnapshotManager::snapshotCreated);
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);

        sm.createSnapshot();

        QCOMPARE(okSpy.count(), 0);
        QCOMPARE(failSpy.count(), 1);

        const QList<QVariant> args = failSpy.takeFirst();
        QCOMPARE(args.size(), 1);
        QVERIFY(args.at(0).toString().contains(QStringLiteral("Google Drive"),
                                               Qt::CaseInsensitive));
    }

    // ── 4. createSnapshot with unauthenticated GDrive → snapshotFailed ──────
    //      Confirms that wiring a client but leaving it logged-out still
    //      hits the early-exit branch. This stands in for "compact updates
    //      HEAD to reflect new baseline" — without auth, HEAD must NOT be
    //      touched (otherwise we'd corrupt the remote on a logged-out push).

    void createSnapshot_with_unauthenticated_client_emits_failed()
    {
        DataStore store;
        SnapshotManager sm(&store);

        GDriveClient client;
        QVERIFY(!client.isAuthenticated());
        sm.setGDriveClient(&client);

        QSignalSpy okSpy(&sm, &SnapshotManager::snapshotCreated);
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);

        sm.createSnapshot();

        QCOMPARE(okSpy.count(), 0);
        QCOMPARE(failSpy.count(), 1);
    }

    // ── 5. createSnapshot is idempotent under the failure branch ────────────
    //      Calling compact twice with the same (no-auth) state must produce
    //      the same observable result both times — exactly one failure
    //      signal per call, never a stray success.

    void createSnapshot_idempotent_under_failure_branch()
    {
        DataStore store;
        SnapshotManager sm(&store);

        QSignalSpy okSpy(&sm, &SnapshotManager::snapshotCreated);
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);

        sm.createSnapshot();
        sm.createSnapshot();
        sm.createSnapshot();

        QCOMPARE(okSpy.count(), 0);
        QCOMPARE(failSpy.count(), 3);
    }

    // ── 6. createSnapshot with no commits — no-op behavior ─────────────────
    //      A freshly-constructed manager has commitCount() == 0. Even after
    //      the failed createSnapshot the count must remain 0 (resetCommitCount
    //      is only reached on the success branch — the failure branch must
    //      not corrupt the counter).

    void createSnapshot_with_no_commits_leaves_count_at_zero()
    {
        DataStore store;
        SnapshotManager sm(&store);

        QCOMPARE(sm.commitCount(), 0);
        sm.createSnapshot(); // fails (no client)
        QCOMPARE(sm.commitCount(), 0);

        // Threshold-driven shouldCompact still reflects the (unchanged) count.
        sm.setCompactionThreshold(1);
        QVERIFY(!sm.shouldCompact());
    }

    // ── 7. setHeadCommitId is accepted and does not crash with empty/UUID ──
    //      The friend-only counter path can't be inspected from a unit test,
    //      but this asserts the public setter accepts the values that
    //      SyncEngine pushes into it (HLC-ordered commit UUIDs).

    void setHeadCommitId_accepts_uuid_and_empty_string()
    {
        DataStore store;
        SnapshotManager sm(&store);

        sm.setHeadCommitId(QString());                          // initial (no head)
        sm.setHeadCommitId(QStringLiteral("a1b2c3d4-e5f6-7890-abcd-ef1234567890"));
        sm.setHeadCommitId(QStringLiteral("0000000000000-DEV1-A")); // HLC-style

        // Replace with a different head — overwrites without state corruption.
        sm.setHeadCommitId(QStringLiteral("0000000000001-DEV2-B"));

        // No public getter — assert via the createSnapshot contract: the
        // manager must still be usable after multiple setHeadCommitId calls.
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);
        sm.createSnapshot();
        QCOMPARE(failSpy.count(), 1);
    }

    // ── 8. Multi-device HLC ordering — head can advance with monotonic IDs ──
    //      Simulates the case where two devices have written commits and the
    //      local SyncEngine has chosen the highest-HLC head as its baseline.
    //      The manager must accept the sequence without losing the previous
    //      counter state (which was reset only on the snapshot's success
    //      branch — never on setHeadCommitId).

    void setHeadCommitId_preserves_count_across_updates()
    {
        DataStore store;
        SnapshotManager sm(&store);

        // Lower threshold so shouldCompact reflects the count via boundary.
        sm.setCompactionThreshold(1);
        QCOMPARE(sm.commitCount(), 0);

        // Multi-device HLC sequence: dev1 then dev2 then back to dev1.
        // (HLC strings sort lexicographically in OpenTodo's encoding.)
        sm.setHeadCommitId(QStringLiteral("17000000000-DEV1-A"));
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        sm.setHeadCommitId(QStringLiteral("17000000001-DEV2-B"));
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        sm.setHeadCommitId(QStringLiteral("17000000002-DEV1-C"));
        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());

        // Counter is unchanged by head updates — only SyncEngine's commit-loop
        // mutates it (via the friend incrementCommitCount() path).
    }

    // ── 9. setGDriveClient — late binding works, can be re-wired ────────────
    //      Production sets the client once in SyncEngine's ctor, but the
    //      setter must tolerate being called multiple times (e.g. on
    //      sign-out → sign-in cycles).

    void setGDriveClient_can_be_called_multiple_times()
    {
        DataStore store;
        SnapshotManager sm(&store);

        GDriveClient c1;
        GDriveClient c2;

        sm.setGDriveClient(&c1);
        sm.setGDriveClient(&c2);
        sm.setGDriveClient(nullptr); // sign-out path

        // After clearing, createSnapshot must take the "no client" branch.
        QSignalSpy failSpy(&sm, &SnapshotManager::snapshotFailed);
        sm.createSnapshot();
        QCOMPARE(failSpy.count(), 1);

        // Re-wire and confirm the unauthenticated branch is still hit
        // (state.json upload is NOT attempted without auth — this stands in
        // for the "edge case: state.json doesn't exist yet" check, since
        // without auth no remote file is created or modified).
        sm.setGDriveClient(&c1);
        QVERIFY(!c1.isAuthenticated());
        sm.createSnapshot();
        QCOMPARE(failSpy.count(), 2);
    }

    // ── 10. Threshold roundtrip across mutations ────────────────────────────
    //      Ensures that flipping the threshold up and down is observable
    //      through shouldCompact() with no hidden caching.

    void setCompactionThreshold_round_trip()
    {
        DataStore store;
        SnapshotManager sm(&store);

        sm.setCompactionThreshold(0);   // always-true sentinel
        QVERIFY(sm.shouldCompact());

        sm.setCompactionThreshold(100); // factory default
        QVERIFY(!sm.shouldCompact());

        sm.setCompactionThreshold(0);
        QVERIFY(sm.shouldCompact());

        sm.setCompactionThreshold(50);
        QVERIFY(!sm.shouldCompact());
    }

    // ── 11. Fresh manager has expected initial state ───────────────────────
    //      Sanity check on the ctor: count=0, shouldCompact=false (default
    //      threshold 100 > 0). Catches accidental field-initializer changes.

    void freshly_constructed_manager_has_zero_count_and_does_not_compact()
    {
        DataStore store;
        SnapshotManager sm(&store);

        QCOMPARE(sm.commitCount(), 0);
        QVERIFY(!sm.shouldCompact());
    }
};

QTEST_GUILESS_MAIN(TestSnapshotManager)
#include "test_snapshot_manager.moc"
