#include "synchttpserver.h"

#include "localdirremote.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSyncServer, "opentodo.syncserver")

#if !defined(__EMSCRIPTEN__)

#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

namespace {

// Per-connection parse state, owned by the readyRead lambda via shared_ptr.
struct Conn {
    QByteArray buf;
    bool headersParsed = false;
    bool dispatched = false;
    QString method;
    QString target;        // path only (query stripped)
    QString authToken;     // value after "Bearer "
    qint64 contentLength = 0;
    int bodyStart = 0;
};

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    default:  return "Internal Server Error";
    }
}

// Length-independent constant-time compare. `b` is the server's own token (the
// reference); `a` is the attacker-controlled candidate. The size check is folded
// into the accumulator rather than early-returning, so a length mismatch never
// short-circuits and the loop bound is tied to the (fixed-length) server token —
// the candidate's length is not leaked via response timing.
bool tokensEqual(const QString &a, const QString &b)
{
    const QByteArray x = a.toUtf8();
    const QByteArray y = b.toUtf8();   // server token → reference length
    quint8 diff = quint8((x.size() ^ y.size()) != 0);
    const int n = y.size();
    for (int i = 0; i < n; ++i) {
        const quint8 xi = (i < x.size()) ? quint8(x[i]) : quint8(0);
        diff |= xi ^ quint8(y[i]);
    }
    return diff == 0;
}

} // namespace

SyncHttpServer::SyncHttpServer(LocalDirRemote *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

bool SyncHttpServer::start(quint16 port, const QString &token, bool allowLan)
{
    stop();
    if (token.isEmpty()) {
        qCWarning(lcSyncServer) << "refusing to start without a token";
        return false;
    }
    m_token = token;
    m_server = new QTcpServer(this);
    // By default loopback ONLY: Tailscale Funnel proxies the public HTTPS
    // endpoint to this local port, keeping the server off the LAN entirely.
    // allowLan binds all interfaces (0.0.0.0) so LAN peers can sync over Wi-Fi
    // without Tailscale — the bearer token is still the only auth gate.
    const QHostAddress bindAddr = allowLan ? QHostAddress(QHostAddress::Any)
                                           : QHostAddress(QHostAddress::LocalHost);
    if (!m_server->listen(bindAddr, port)) {
        qCWarning(lcSyncServer) << "listen failed on" << port << ":"
                                << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    connect(m_server, &QTcpServer::newConnection, this, &SyncHttpServer::onNewConnection);
    qCInfo(lcSyncServer) << "listening on"
                         << (allowLan ? "0.0.0.0:" : "127.0.0.1:") << m_port;
    emit started(m_port);
    return true;
}

void SyncHttpServer::stop()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
        m_port = 0;
        emit stopped();
    }
}

bool SyncHttpServer::isRunning() const
{
    return m_server && m_server->isListening();
}

void SyncHttpServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);

        auto st = std::make_shared<Conn>();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, st]() {
            if (st->dispatched)
                return;
            st->buf += sock->readAll();

            if (!st->headersParsed) {
                const int idx = st->buf.indexOf("\r\n\r\n");
                if (idx < 0) {
                    if (st->buf.size() > MAX_HEADER_BYTES) {
                        st->dispatched = true;
                        respond(sock, 431, {}, "text/plain", QString(), QString());
                    }
                    return; // wait for the rest of the header block
                }

                const QByteArray head = st->buf.left(idx);
                st->bodyStart = idx + 4;
                const QList<QByteArray> lines = head.split('\n');
                if (lines.isEmpty()) {
                    st->dispatched = true;
                    respond(sock, 400, {}, "text/plain", QString(), QString());
                    return;
                }

                // Request line: METHOD SP TARGET SP HTTP/x.y
                const QList<QByteArray> rl =
                    lines.first().trimmed().split(' ');
                if (rl.size() < 2) {
                    st->dispatched = true;
                    respond(sock, 400, {}, "text/plain", QString(), QString());
                    return;
                }
                st->method = QString::fromLatin1(rl.at(0)).toUpper();
                QString tgt = QString::fromLatin1(rl.at(1));
                const int q = tgt.indexOf(QLatin1Char('?'));
                if (q >= 0)
                    tgt = tgt.left(q);
                st->target = tgt;

                // Headers we care about.
                for (int i = 1; i < lines.size(); ++i) {
                    const QByteArray ln = lines.at(i).trimmed();
                    const int colon = ln.indexOf(':');
                    if (colon < 0)
                        continue;
                    const QByteArray name = ln.left(colon).trimmed().toLower();
                    const QByteArray val = ln.mid(colon + 1).trimmed();
                    if (name == "authorization") {
                        const QByteArray prefix = "Bearer ";
                        if (val.startsWith(prefix))
                            st->authToken = QString::fromUtf8(val.mid(prefix.size()));
                    } else if (name == "content-length") {
                        st->contentLength = val.toLongLong();
                    }
                }
                st->headersParsed = true;

                if (st->contentLength < 0 || st->contentLength > MAX_BODY_BYTES) {
                    st->dispatched = true;
                    respond(sock, 413, {}, "text/plain", st->method, st->target);
                    return;
                }

                // Auth gate at header-parse time: reject an invalid/missing
                // token BEFORE buffering any body, so an unauthenticated caller
                // can't make the server absorb up to MAX_BODY_BYTES. The
                // Authorization header was already parsed above; /ping and every
                // other route still requires auth because route() runs only past
                // this point.
                if (!tokensEqual(st->authToken, m_token)) {
                    st->dispatched = true;
                    respond(sock, 401, {}, "text/plain", st->method, st->target);
                    return;
                }
            }

            // Wait until the full declared body has arrived.
            const qint64 have = st->buf.size() - st->bodyStart;
            if (have < st->contentLength)
                return;

            st->dispatched = true;

            const QByteArray body = st->buf.mid(st->bodyStart,
                                                static_cast<int>(st->contentLength));
            route(sock, st->method, st->target, body);
        });
    }
}

void SyncHttpServer::route(QTcpSocket *sock, const QString &method,
                           const QString &target, const QByteArray &body)
{
    auto json = [](const QStringList &names) {
        QJsonArray arr;
        for (const QString &n : names)
            arr.append(n);
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    };

    // /ping — health + auth probe.
    if (target == QLatin1String("/ping")) {
        respond(sock, method == QLatin1String("GET") ? 200 : 405,
                QByteArrayLiteral("ok"), "text/plain", method, target);
        return;
    }

    // /lock — advisory whole-store lock.
    if (target == QLatin1String("/lock")) {
        if (method == QLatin1String("POST")) {
            const bool ok = m_store && m_store->acquireLock();
            respond(sock, ok ? 200 : 409, {}, "text/plain", method, target);
        } else if (method == QLatin1String("DELETE")) {
            const bool ok = m_store && m_store->releaseLock();
            respond(sock, ok ? 200 : 500, {}, "text/plain", method, target);
        } else {
            respond(sock, 405, {}, "text/plain", method, target);
        }
        return;
    }

    // /blob/<path>
    if (target.startsWith(QLatin1String("/blob/"))) {
        const QString path =
            QUrl::fromPercentEncoding(target.mid(6).toUtf8());
        // Reject up front anything LocalDirRemote would refuse (traversal).
        if (!m_store || m_store->resolve(path).isEmpty()) {
            respond(sock, 400, {}, "text/plain", method, target);
            return;
        }
        if (method == QLatin1String("GET")) {
            // Existence check first so we can return a clean 404 (downloadFile's
            // bool can't distinguish missing from empty-file).
            if (!QFileInfo::exists(m_store->resolve(path))) {
                respond(sock, 404, {}, "text/plain", method, target);
                return;
            }
            QByteArray data;
            if (!m_store->downloadFile(path, data, /*notFoundIsOk=*/false,
                                       MAX_BODY_BYTES)) {
                // Reachable only on oversize / read error at this point.
                respond(sock, 413, {}, "text/plain", method, target);
                return;
            }
            respond(sock, 200, data, "application/octet-stream", method, target);
        } else if (method == QLatin1String("PUT")) {
            const bool ok = m_store->uploadFile(path, body);
            respond(sock, ok ? 204 : 500, {}, "text/plain", method, target);
        } else if (method == QLatin1String("DELETE")) {
            const bool ok = m_store->deleteFile(path);
            respond(sock, ok ? 204 : 500, {}, "text/plain", method, target);
        } else {
            respond(sock, 405, {}, "text/plain", method, target);
        }
        return;
    }

    // /list/<folder>
    if (target.startsWith(QLatin1String("/list/"))) {
        if (method != QLatin1String("GET")) {
            respond(sock, 405, {}, "text/plain", method, target);
            return;
        }
        const QString folder =
            QUrl::fromPercentEncoding(target.mid(6).toUtf8());
        if (!m_store || m_store->resolve(folder).isEmpty()) {
            respond(sock, 400, {}, "text/plain", method, target);
            return;
        }
        respond(sock, 200, json(m_store->listFiles(folder)),
                "application/json", method, target);
        return;
    }

    respond(sock, 404, {}, "text/plain", method, target);
}

void SyncHttpServer::respond(QTcpSocket *sock, int status, const QByteArray &body,
                             const char *contentType, const QString &method,
                             const QString &target)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    if (status != 204) {
        resp += "Content-Type: ";
        resp += contentType;
        resp += "\r\n";
    }
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    if (status != 204)
        resp += body;

    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();

    if (!method.isEmpty())
        emit requestServed(method, target, status);
}

#else // __EMSCRIPTEN__ — no listening sockets on WebAssembly; server is a no-op.

SyncHttpServer::SyncHttpServer(LocalDirRemote *store, QObject *parent)
    : QObject(parent), m_store(store) {}
bool SyncHttpServer::start(quint16, const QString &) {
    qCWarning(lcSyncServer) << "sync server unavailable on this platform (WASM)";
    return false;
}
void SyncHttpServer::stop() {}
bool SyncHttpServer::isRunning() const { return false; }
void SyncHttpServer::onNewConnection() {}
void SyncHttpServer::route(QTcpSocket *, const QString &, const QString &, const QByteArray &) {}
void SyncHttpServer::respond(QTcpSocket *, int, const QByteArray &, const char *,
                             const QString &, const QString &) {}

#endif
