#include "app/clibridge.h"

#include "cli/clicore.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/comment.h"
#include "viewmodels/taskviewmodel.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

namespace {
const char *const kServerName = "OpenTodoCli";
}

CliBridge::CliBridge(DataStore *store, TaskViewModel *taskVM, QObject *parent)
    : QObject(parent), m_store(store), m_taskVM(taskVM), m_server(new QLocalServer(this))
{
    // A previous crash can leave a stale socket file that blocks listen() on
    // Unix; removeServer clears it. (No-op / harmless on Windows named pipes.)
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    // Same-user only — the CLI runs as the same user as the desktop app.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(QString::fromLatin1(kServerName))) {
        qWarning("CliBridge: could not listen on '%s': %s", kServerName,
                 qUtf8Printable(m_server->errorString()));
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &CliBridge::onNewConnection);
}

bool CliBridge::isListening() const
{
    return m_server && m_server->isListening();
}

void CliBridge::onNewConnection()
{
    while (QLocalSocket *sock = m_server->nextPendingConnection()) {
        connect(sock, &QLocalSocket::disconnected, sock, &QLocalSocket::deleteLater);
        auto buf = std::make_shared<QByteArray>();
        connect(sock, &QLocalSocket::readyRead, this, [this, sock, buf]() {
            buf->append(sock->readAll());
            const int nl = buf->indexOf('\n');
            if (nl < 0)
                return; // wait for the rest of the request line
            const QByteArray line = buf->left(nl);
            QByteArray reply = handleLine(line);
            reply.append('\n');
            sock->write(reply);
            sock->flush();
            sock->disconnectFromServer();
        });
    }
}

QByteArray CliBridge::handleLine(const QByteArray &line) const
{
    auto reply = [](const QJsonObject &o) {
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    };
    auto fail = [&](const QString &msg) {
        QJsonObject o;
        o[QStringLiteral("ok")] = false;
        o[QStringLiteral("error")] = msg;
        return reply(o);
    };

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("bad request"));
    const QJsonObject req = doc.object();
    const QString cmd = req.value(QStringLiteral("cmd")).toString();

    // ── Writes: route through the ViewModel so the real mutation path runs ──
    // done / comment / delete accept an id or a unique id prefix (as printed
    // by `opentodo list`) and refuse tasks already in the Trash — same
    // resolution as the offline path.
    if (cmd == QLatin1String("done")) {
        QString error;
        Task *t = clicore::resolveTask(m_store, req.value(QStringLiteral("id")).toString(), &error);
        if (!t)
            return fail(error);
        const QString id = t->id();
        const bool wasRecurring = t->dueIsRecurring() && !t->dueString().isEmpty();
        if (!m_taskVM->completeTask(id))
            return fail(QStringLiteral("could not complete task"));
        const QString note = req.value(QStringLiteral("note")).toString();
        if (!note.trimmed().isEmpty())
            m_store->addComment(note, id, t->projectId(), QStringLiteral("claude"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        // completeTask advances recurring tasks instead of completing them.
        d[QStringLiteral("mode")] = wasRecurring ? QStringLiteral("advanced")
                                                 : QStringLiteral("completed");
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("data")] = d;
        return reply(o);
    }

    if (cmd == QLatin1String("add")) {
        Project *p = clicore::resolveProject(m_store,
                                             req.value(QStringLiteral("repo")).toString(),
                                             req.value(QStringLiteral("project")).toString(),
                                             nullptr);
        // No project resolved → Inbox, explicitly. Passing an empty id let
        // TaskViewModel fall back to m_currentProjectId — whatever project the
        // user last had open in the UI — unlike the offline path.
        if (!p)
            p = m_store->inboxProject();
        const QString id = m_taskVM->addTask(req.value(QStringLiteral("content")).toString(),
                                             p ? p->id() : QString(),
                                             req.value(QStringLiteral("priority")).toInt(4),
                                             req.value(QStringLiteral("due")).toString(),
                                             {});
        if (id.isEmpty())
            return fail(QStringLiteral("failed to add task"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("data")] = d;
        return reply(o);
    }

    if (cmd == QLatin1String("comment")) {
        QString error;
        Task *t = clicore::resolveTask(m_store, req.value(QStringLiteral("id")).toString(), &error);
        if (!t)
            return fail(error);
        const QString id = t->id();
        Comment *c = m_store->addComment(req.value(QStringLiteral("text")).toString(),
                                         id, t->projectId(), QStringLiteral("claude"));
        if (!c)
            return fail(QStringLiteral("failed to add comment"));
        QJsonObject d;
        d[QStringLiteral("id")] = c->id();
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("data")] = d;
        return reply(o);
    }

    if (cmd == QLatin1String("delete")) {
        QString error;
        Task *t = clicore::resolveTask(m_store, req.value(QStringLiteral("id")).toString(), &error);
        if (!t)
            return fail(error);
        const QString id = t->id();
        if (!m_taskVM->deleteTask(id)) // routes through VM → undo entry + signals
            return fail(QStringLiteral("failed to delete task"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        d[QStringLiteral("deleted")] = true;
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("data")] = d;
        return reply(o);
    }

    // ── Reads: identical JSON to the offline path ──────────────────────────
    return reply(clicore::handleRequest(m_store, req));
}
