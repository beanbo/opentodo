#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QPointer>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

// ============================================================================
// Regression test for "selectedTask is a raw QObject* that dangles after
// DataStore::clear()" (Q-Loop B Bug A, May 2026).
//
// Symptom: QML right-pane editor binds to TaskViewModel::selectedTask. After
// a sync pull / Todoist backup import / reset-all-data, DataStore::clear()
// runs qDeleteAll(m_tasks), but the m_selectedTask pointer kept its raw
// address. The QML null guard `if (taskViewModel.selectedTask) ...` therefore
// passed (non-null) and dereferenced freed memory.
//
// Production fix lives in two places:
//
//   1. taskviewmodel.h declares m_selectedTask as QPointer<QObject> (instead
//      of a raw QObject *). QPointer auto-nulls when the watched object is
//      destroyed, so even if the explicit clear path below is skipped the
//      property reads as nullptr after the Task is freed.
//
//   2. taskviewmodel.cpp's ctor connects DataStore::aboutToReset BEFORE the
//      list-model reset block. The slot clears m_selectedTask and emits
//      selectedTaskChanged() so QML re-evaluates its binding and tears down
//      the editor view BEFORE qDeleteAll runs.
//
// These tests pin both the explicit-clear-on-aboutToReset behaviour AND the
// QPointer auto-null safety net.
// ============================================================================

namespace {

DataStore *makeStoreWithInbox()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

} // anonymous namespace

class TestSelectedTaskUaf : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique per-test QSettings scope so this test doesn't trample the
        // user's real device id / sort-reversed flag, and doesn't collide
        // with sibling tests under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SelTaskUaf"));
        QCoreApplication::setApplicationName(QStringLiteral("test_selectedtask_uaf"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // ------------------------------------------------------------------
    // 1. selectedTask is cleared and the change signal fires when
    //    DataStore::clear() is called directly.
    // ------------------------------------------------------------------
    void clearedSynchronously_onDirectClear()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Будет удалён"), inboxId);
        QVERIFY(task != nullptr);

        vm.setSelectedTask(task);
        QCOMPARE(vm.selectedTask(), task);

        QSignalSpy spy(&vm, &TaskViewModel::selectedTaskChanged);
        QVERIFY(spy.isValid());

        // Direct nuke — same code path FileManager triggers when the user
        // hits "сбросить все данные".
        store->clear();

        // Property must read as nullptr (either via the explicit
        // aboutToReset clear OR via QPointer auto-null after qDeleteAll).
        QCOMPARE(vm.selectedTask(), static_cast<QObject *>(nullptr));

        // And the binding must have been notified at least once so QML
        // re-evaluates the right-pane editor before its Task is gone.
        QVERIFY(spy.count() >= 1);

        delete store;
    }

    // ------------------------------------------------------------------
    // 2. Same invariant through the loadFromJson() entry point — this is
    //    the path actually exercised by sync pull and backup restore.
    // ------------------------------------------------------------------
    void clearedSynchronously_onLoadFromJson()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Будет перезагружен"), inboxId);
        QVERIFY(task != nullptr);

        vm.setSelectedTask(task);
        QCOMPARE(vm.selectedTask(), task);

        // Empty document — every existing task is freed by clear() inside
        // loadFromJson before the new entities are constructed. Mirrors
        // SyncEngine::pull when the remote state is empty.
        QJsonObject empty;
        empty.insert(QStringLiteral("schema_version"),
                     DataStore::CURRENT_SCHEMA_VERSION);

        QSignalSpy spy(&vm, &TaskViewModel::selectedTaskChanged);
        QVERIFY(spy.isValid());

        store->loadFromJson(empty);

        // Property must read nullptr.
        QCOMPARE(vm.selectedTask(), static_cast<QObject *>(nullptr));

        // selectedTaskChanged must have fired at least once.
        QVERIFY(spy.count() >= 1);

        delete store;
    }

    // ------------------------------------------------------------------
    // 3. Pin the QPointer<QObject> safety net: even if the explicit
    //    aboutToReset clear were ever removed (e.g. by a careless future
    //    refactor), reading the property after the watched Task is freed
    //    must still return nullptr instead of a dangling pointer.
    //
    //    We can't observe that scenario by disconnecting the slot (it's
    //    a lambda), so we exercise the contract directly: deleteLater()
    //    the watched object and verify the property goes null after the
    //    event loop drains.
    // ------------------------------------------------------------------
    void qpointerSemanticsHoldOnExternalDestruction()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        // Use a free-standing QObject (Task is harder to destroy outside the
        // DataStore lifecycle; the property is typed QObject* so this is a
        // valid stand-in for the dangling-pointer scenario).
        auto *external = new QObject;
        vm.setSelectedTask(external);
        QCOMPARE(vm.selectedTask(), external);

        // Force-delete the watched object out from under the ViewModel.
        // QPointer's destruction hook nulls the watcher synchronously.
        delete external;

        // Without QPointer this read would return the freed pointer
        // (would crash on deref, but the read itself returns junk).
        // With QPointer it must be nullptr.
        QCOMPARE(vm.selectedTask(), static_cast<QObject *>(nullptr));

        delete store;
    }

    // ------------------------------------------------------------------
    // 4. Simulated sync pull path — DataStore::loadFromJson against a
    //    DIFFERENT remote state (not just an empty document). This is
    //    closer to the real "sync pulled and the remote has different
    //    tasks" scenario that triggered the original crash report.
    // ------------------------------------------------------------------
    void clearedThroughSimulatedSyncPull()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        const QString inboxId = store->inboxProject()->id();
        Task *localOnly = store->addTask(QStringLiteral("Только локально"), inboxId);
        QVERIFY(localOnly != nullptr);

        vm.setSelectedTask(localOnly);
        QCOMPARE(vm.selectedTask(), localOnly);

        // Build a remote snapshot that does NOT contain localOnly.
        // SyncEngine's pull path effectively does:
        //     dataStore->loadFromJson(remoteState);
        // so we drive that directly.
        DataStore *remote = makeStoreWithInbox();
        Task *remoteTask = remote->addTask(QStringLiteral("Удалённая задача"),
                                            remote->inboxProject()->id());
        Q_UNUSED(remoteTask);
        const QJsonObject remoteJson = remote->toJson();
        delete remote;

        QSignalSpy spy(&vm, &TaskViewModel::selectedTaskChanged);
        QVERIFY(spy.isValid());

        store->loadFromJson(remoteJson);

        // The Task object that selectedTask used to point at is now gone.
        QCOMPARE(vm.selectedTask(), static_cast<QObject *>(nullptr));
        QVERIFY(spy.count() >= 1);

        delete store;
    }

    // ------------------------------------------------------------------
    // 5. Setting selectedTask=nullptr explicitly is a no-op for the
    //    change signal when the property is already null. Guards against
    //    a future refactor accidentally emitting a redundant change.
    // ------------------------------------------------------------------
    void noSpuriousSignalWhenAlreadyNull()
    {
        DataStore *store = makeStoreWithInbox();
        TaskViewModel vm(store);

        QCOMPARE(vm.selectedTask(), static_cast<QObject *>(nullptr));

        QSignalSpy spy(&vm, &TaskViewModel::selectedTaskChanged);
        QVERIFY(spy.isValid());

        // Re-set to null — setSelectedTask short-circuits via != check.
        vm.setSelectedTask(nullptr);
        QCOMPARE(spy.count(), 0);

        // Now clearing the store also has nothing to change, but the
        // aboutToReset slot ALSO short-circuits (its body is guarded by
        // `if (m_selectedTask)`), so no signal should fire.
        store->clear();
        QCOMPARE(spy.count(), 0);

        delete store;
    }
};

QTEST_MAIN(TestSelectedTaskUaf)
#include "test_selectedtask_uaf.moc"
