#include <QtTest>
#include <QSignalSpy>
#include "storage/datastore.h"
#include "model/project.h"

class TestDataStoreReset : public QObject { Q_OBJECT

    // Helper: creates a DataStore with an Inbox project so addTask("", "") works
    DataStore *makeStore()
    {
        auto *ds = new DataStore(this);
        auto *inbox = new Project(ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds->addExistingProject(inbox);
        return ds;
    }

private slots:
    void aboutToReset_emitted_before_clear() {
        QScopedPointer<DataStore> store(makeStore());
        store->addTask(QStringLiteral("foo"), QString());
        QSignalSpy spy(store.data(), &DataStore::aboutToReset);
        store->clear();
        QCOMPARE(spy.count(), 1);
        QVERIFY(store->allTasks().isEmpty());
    }

    void loadFromJson_resets_via_signal() {
        QScopedPointer<DataStore> store(makeStore());
        store->addTask(QStringLiteral("a"), QString());
        QSignalSpy spy(store.data(), &DataStore::aboutToReset);
        store->loadFromJson(QJsonObject{});  // empty doc resets
        QVERIFY(spy.count() >= 1);
    }
};
QTEST_GUILESS_MAIN(TestDataStoreReset)
#include "test_datastore_reset.moc"
