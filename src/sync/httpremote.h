#pragma once

#include "remotestore.h"

#include <QByteArray>
#include <QString>

class QNetworkAccessManager;

// ─────────────────────────────────────────────────────────────────────────────
// HttpRemote — a RemoteStore that talks to a self-hosted OpenTodo sync server
// (SyncHttpServer) over HTTP(S). This is what a phone / second PC uses when the
// self-hosted backend is selected; it points at the Tailscale Funnel URL
// (e.g. https://my-pc.tail0000.ts.net) with a bearer token.
//
// Wire protocol (every request carries `Authorization: Bearer <token>`):
//   GET    /blob/<path>      download blob          200 + body | 404
//   PUT    /blob/<path>      upload blob (body)     200/204
//   DELETE /blob/<path>      delete blob            200/204 | 404 (ok)
//   GET    /list/<folder>    list names (JSON array)200 + ["a.json",...]
//   POST   /lock             acquire advisory lock  200 | 409 (held)
//   DELETE /lock             release advisory lock  200/204
//   GET    /ping             health/auth probe      200
//
// Requests block (QEventLoop) to match SyncEngine's synchronous push/pull flow,
// the same way DriveApi does.
// ─────────────────────────────────────────────────────────────────────────────
class HttpRemote : public RemoteStore
{
public:
    HttpRemote();
    ~HttpRemote() override;

    // baseUrl: scheme+host (no trailing slash), e.g. "https://my-pc.tail0000.ts.net".
    void configure(const QString &baseUrl, const QString &token);
    QString baseUrl() const { return m_baseUrl; }

    bool isAuthenticated() const override;     // url + token configured
    bool acquireLock() override;
    bool releaseLock() override;
    bool uploadFile(const QString &remotePath, const QByteArray &data) override;
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false, qint64 maxBytes = 0) override;
    bool deleteFile(const QString &remotePath) override;
    QStringList listFiles(const QString &folderPath) override;

    static constexpr int REQUEST_TIMEOUT_MS = 30000;

private:
    enum class Verb { Get, Put, Delete, Post };
    struct Reply {
        int statusCode = 0;     // HTTP status, 0 on transport failure
        QByteArray body;
        bool transportError = false;
    };
    // maxBytes>0 aborts the transfer if the response exceeds the cap.
    Reply send(Verb verb, const QString &endpoint,
               const QByteArray &body = {}, qint64 maxBytes = 0);

    QString m_baseUrl;
    QString m_token;
    QNetworkAccessManager *m_nam = nullptr;
};
