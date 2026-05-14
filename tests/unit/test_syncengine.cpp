// Regression tests for SyncEngine push/pull behaviour. Focus on the bugs
// that hid the entire Drive-sync feature: push() exited early when the
// pending-commits queue was empty (and the queue was always empty because
// createCommit() was never wired up), so state.json never reached Drive
// and users never saw any of their data on Google Drive.
//
// We can't talk to real Drive in unit tests, so the assertions are about
// the SyncEngine contract: when not authenticated, push() must transition
// to Offline (no longer silently no-op). When authenticated, the upload
// path runs unconditionally.
#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "sync/syncengine.h"
#include "model/task.h"

class TestSyncEngine : public QObject
{
    Q_OBJECT

private slots:
    // Without authentication, push() must reach the Offline state. Before
    // the fix, push() short-circuited on `pendingCommits.isEmpty()` and
    // never told the user anything was wrong, so the entire Drive sync
    // feature looked broken.
    void push_with_no_auth_reaches_offline()
    {
        DataStore store;
        SyncEngine engine(&store);
        engine.push();
        QCOMPARE(engine.status(), SyncEngine::Offline);
    }

    // sync() = pull() + push(). Same offline contract.
    void sync_with_no_auth_does_not_crash()
    {
        DataStore store;
        SyncEngine engine(&store);

        engine.sync();
        // Status either Offline or Error; never Synced (we have no auth).
        QVERIFY(engine.status() == SyncEngine::Offline
             || engine.status() == SyncEngine::Error);
    }

    // createCommit must produce a Commit object with the operations we
    // give it (verifies the API we now plan to wire up to dataChanged).
    void create_commit_returns_object_with_operations()
    {
        DataStore store;
        SyncEngine engine(&store);

        SyncOperation op;
        op.type     = QStringLiteral("task_add");
        op.entityId = QStringLiteral("test-id");

        Commit *c = engine.createCommit({op});
        QVERIFY(c != nullptr);
        QVERIFY(!c->uuid().isEmpty());
        QCOMPARE(c->operations().size(), 1);
        QCOMPARE(c->operations()[0].type, QStringLiteral("task_add"));
    }

    // Empty operations list → returns nullptr (no-op contract).
    void create_commit_empty_ops_returns_null()
    {
        DataStore store;
        SyncEngine engine(&store);
        QVERIFY(engine.createCommit({}) == nullptr);
    }

    // Device id must always be set (so commits aren't anonymous).
    void device_id_is_set()
    {
        DataStore store;
        SyncEngine e(&store);
        QVERIFY(!e.deviceId().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSyncEngine)
#include "test_syncengine.moc"
