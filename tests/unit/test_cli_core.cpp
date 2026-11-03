// clicore task resolution (global bug-fix pass, F4 / P15).
//
// The human `opentodo list` output prints only the first 8 characters of each
// id, but show / done / comment / delete used to require the FULL id, and
// they happily acted on soft-deleted (Trash) tasks. Pins:
//   * unique id prefixes (>= kMinIdPrefix chars) resolve to the task;
//   * ambiguous / too-short prefixes are rejected with a clear error;
//   * tombstoned tasks are "not found" for every command.

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

#include "cli/clicore.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

namespace {

DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

QJsonObject request(const QString &cmd, const QString &id)
{
    QJsonObject req;
    req[QStringLiteral("cmd")] = cmd;
    req[QStringLiteral("id")] = id;
    return req;
}

} // namespace

class TestCliCore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_CliCore"));
        QCoreApplication::setApplicationName(QStringLiteral("test_cli_core"));
    }

    void resolveTask_exactAndPrefix()
    {
        DataStore *store = makeStore();
        Task *t = store->addTask(QStringLiteral("Write docs"), store->inboxProject()->id());
        QString err;
        QCOMPARE(clicore::resolveTask(store, t->id(), &err), t);
        QCOMPARE(clicore::resolveTask(store, t->id().left(8), &err), t);
        QCOMPARE(clicore::resolveTask(store, t->id().left(8).toUpper(), &err), t);
        // Too short to be a safe prefix.
        QVERIFY(!clicore::resolveTask(store, t->id().left(2), &err));
        QCOMPARE(err, QStringLiteral("task not found"));
        QVERIFY(!clicore::resolveTask(store, QStringLiteral("zzzzzzzz"), &err));
        delete store;
    }

    void resolveTask_ambiguousPrefixRejected()
    {
        // Two tasks with ids sharing an 8-char prefix (loaded from JSON so the
        // ids are deterministic).
        auto taskJson = [](const QString &id, const QString &content) {
            QJsonObject o;
            o[QStringLiteral("id")] = id;
            o[QStringLiteral("content")] = content;
            o[QStringLiteral("project_id")] = QStringLiteral("p-inbox");
            return o;
        };
        QJsonObject inbox;
        inbox[QStringLiteral("id")] = QStringLiteral("p-inbox");
        inbox[QStringLiteral("name")] = QStringLiteral("Inbox");
        inbox[QStringLiteral("is_inbox")] = true;
        QJsonObject root;
        root[QStringLiteral("projects")] = QJsonArray{inbox};
        root[QStringLiteral("tasks")] = QJsonArray{
            taskJson(QStringLiteral("abcd1234-0000-4000-8000-000000000001"), QStringLiteral("one")),
            taskJson(QStringLiteral("abcd1234-1111-4000-8000-000000000002"), QStringLiteral("two")),
        };
        DataStore store;
        store.loadFromJson(root);
        QVERIFY(store.taskById(QStringLiteral("abcd1234-0000-4000-8000-000000000001")));

        QString err;
        QVERIFY(!clicore::resolveTask(&store, QStringLiteral("abcd1234"), &err));
        QVERIFY2(err.contains(QStringLiteral("ambiguous")), qPrintable(err));
        // A longer prefix disambiguates.
        Task *two = clicore::resolveTask(&store, QStringLiteral("abcd1234-1"), &err);
        QVERIFY(two);
        QCOMPARE(two->content(), QStringLiteral("two"));
    }

    void deletedTask_isNotFoundForEveryCommand()
    {
        DataStore *store = makeStore();
        Task *t = store->addTask(QStringLiteral("Gone"), store->inboxProject()->id());
        const QString id = t->id();
        QVERIFY(store->removeTask(id));
        QVERIFY(store->taskById(id) && store->taskById(id)->isDeleted());

        for (const QString &cmd : {QStringLiteral("show"), QStringLiteral("done"),
                                   QStringLiteral("delete")}) {
            const QJsonObject reply = clicore::handleRequest(store, request(cmd, id));
            QVERIFY2(!reply.value(QStringLiteral("ok")).toBool(), qPrintable(cmd));
            QCOMPARE(reply.value(QStringLiteral("error")).toString(), QStringLiteral("task not found"));
        }
        QJsonObject comment = request(QStringLiteral("comment"), id);
        comment[QStringLiteral("text")] = QStringLiteral("hello");
        QVERIFY(!clicore::handleRequest(store, comment).value(QStringLiteral("ok")).toBool());
        QVERIFY(clicore::addCommentOffline(store, id, QStringLiteral("x")).isEmpty());
        QString err;
        QVERIFY(!clicore::completeTaskOffline(store, id, &err));
        QVERIFY(!store->taskById(id)->isCompleted());
        delete store;
    }

    void done_withShortId_completesFullId()
    {
        DataStore *store = makeStore();
        Task *t = store->addTask(QStringLiteral("Ship it"), store->inboxProject()->id());
        const QJsonObject reply =
            clicore::handleRequest(store, request(QStringLiteral("done"), t->id().left(8)));
        QVERIFY2(reply.value(QStringLiteral("ok")).toBool(),
                 qPrintable(reply.value(QStringLiteral("error")).toString()));
        QCOMPARE(reply.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("id")).toString(), t->id());
        QVERIFY(t->isCompleted());

        const QJsonObject show =
            clicore::handleRequest(store, request(QStringLiteral("show"), t->id().left(8)));
        QVERIFY(show.value(QStringLiteral("ok")).toBool());
        delete store;
    }
};

QTEST_GUILESS_MAIN(TestCliCore)
#include "test_cli_core.moc"
