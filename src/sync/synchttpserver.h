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
//   • Binds to 127.0.0.1 by default — never the LAN; Funnel proxies to loopback.
//     (Opt-in LAN binding via allowLan=true widens to 0.0.0.0 for Wi-Fi sync;
//      the bearer token stays the auth gate either way.)
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

    // Bind <port> with the given bearer token. Default binds 127.0.0.1 ONLY
    // (Funnel proxies to loopback). Pass allowLan=true to bind all interfaces
    // (0.0.0.0) so LAN peers reach it over Wi-Fi without Tailscale — still
    // gated by the bearer token. Returns false if the port is taken or the
    // platform has no listening sockets (WASM).
    bool start(quint16 port, const QString &token, bool allowLan = false);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }

    // Caps. Body cap is a hair above SyncEngine's 50 MB state limit for headroom.
    static constexpr qint64 MAX_HEADER_BYTES = 16 * 1024;
    static constexpr qint64 MAX_BODY_BYTES   = 64LL * 1024 * 1024;

    // S14: slow-client / connection-flood guards. A connection must finish its
    // header block within HEADER_TIMEOUT_MS and be completely served within
    // TOTAL_TIMEOUT_MS (a 50 MB state over a slow phone link fits comfortably),
    // and at most MAX_CONNECTIONS may be open at once — new ones beyond that
    // are dropped before any parsing. All of this applies BEFORE the bearer
    // token check, which is the point: unauthenticated peers behind the public
    // Funnel could otherwise hold sockets open forever (slowloris).
    static constexpr int DEFAULT_HEADER_TIMEOUT_MS = 10 * 1000;
    static constexpr int DEFAULT_TOTAL_TIMEOUT_MS  = 60 * 1000;
    static constexpr int DEFAULT_MAX_CONNECTIONS   = 32;
    // Override the guards (tests use short values). Applies to new connections.
    void setConnectionLimits(int headerTimeoutMs, int totalTimeoutMs, int maxConnections);
    int activeConnections() const { return m_activeConnections; }

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
    int m_headerTimeoutMs = DEFAULT_HEADER_TIMEOUT_MS;
    int m_totalTimeoutMs  = DEFAULT_TOTAL_TIMEOUT_MS;
    int m_maxConnections  = DEFAULT_MAX_CONNECTIONS;
    int m_activeConnections = 0;
};
