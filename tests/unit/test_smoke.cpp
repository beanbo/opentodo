#include <QCoreApplication>
#include <QtTest>

#include "model/task.h"
#include "storage/datastore.h"

class TestSmoke : public QObject
{
    Q_OBJECT
private slots:
    void core_lib_links_and_runs()
    {
        DataStore store;
        QVERIFY(store.allTasks().isEmpty());

        Task t;
        t.setContent(QStringLiteral("hello"));
        QCOMPARE(t.content(), QStringLiteral("hello"));
    }
};

QTEST_GUILESS_MAIN(TestSmoke)
#include "test_smoke.moc"
