#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class LocalDirRemote;

// ─────────────────────────────────────────────────────────────────────────────
// SyncHttpServer — exposes a LocalDirRemote blob store over HTTP so remote
// OpenTodo clients (the phone, a second PC) can sync to this machine. Speaks the
// exact protocol HttpRemote expects:
//
//   GET    /blob/<path>      download   200+body | 404
//   PUT    /blob/<path>      upload     204
//   DELETE /blob/<path>      delete     204
//   GET    /list/<folder>    list       200 + JSON ["a.json", ...]
//   POST   /lock             acquire    200 | 409
//   DELETE /lock             release    200
//   GET    /ping             health     200
//
// Hardening (it sits behind a PUBLIC Tailscale Funnel HTTPS endpoint):
//   • Binds to 127.0.0.1 ONLY — never the LAN; Funnel proxies to loopback.
//   • Every request must carry `Authorization: Bearer <token>` or it's 401.
//   • Paths are sanitized by LocalDirRemote::resolve() (no "..", no absolute).
//   • Header section and request body are size-capped (flood / OOM guards).
//   • One request per connection, then close (no keep-alive, no chunked input).
//
// HTTP/1.1 is parsed by hand because Qt's HttpServer module is not present in
// this Qt build. The surface is deliberately tiny: a fixed verb+prefix table,
// nothing reflective, no filesystem access outside the store root.
// ─────────────────────────────────────────────────────────────────────────────
class SyncHttpServer : public QObject
{
    Q_OBJECT
public:
    explicit SyncHttpServer(LocalDirRemote *store, QObject *parent = nullptr);

    // Bind to 127.0.0.1:<port> with the given bearer token. Returns false if the
    // port is taken or the platform has no listening sockets (WASM).
    bool start(quint16 port, const QString &token);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }

    // Caps. Body cap is a hair above SyncEngine's 50 MB state limit for headroom.
    static constexpr qint64 MAX_HEADER_BYTES = 16 * 1024;
    static constexpr qint64 MAX_BODY_BYTES   = 64LL * 1024 * 1024;

signals:
    void started(quint16 port);
    void stopped();
    // method, target, HTTP status — for logging / a future UI activity line.
    void requestServed(const QString &method, const QString &target, int status);

private slots:
    void onNewConnection();

private:
    void route(QTcpSocket *sock, const QString &method, const QString &target,
               const QByteArray &body);
    void respond(QTcpSocket *sock, int status, const QByteArray &body,
                 const char *contentType, const QString &method,
                 const QString &target);

    QTcpServer *m_server = nullptr;
    LocalDirRemote *m_store;
    QString m_token;
    quint16 m_port = 0;
};
