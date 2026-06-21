#include "httpremote.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
// Percent-encode a remote path segment so it round-trips through the server's
// QUrl::fromPercentEncoding, while keeping '/' as the path separator. Without
// this a space / '?' / '#' / '%' in a path would corrupt the request URL: the
// server splits the request target on '?' and decodes with fromPercentEncoding.
// Current callers pass UUID/log filenames (safe), but encoding keeps the wire
// protocol robust against any future path content.
QString encPath(const QString &p)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(p, "/"));
}
} // namespace

HttpRemote::HttpRemote()
    : m_nam(new QNetworkAccessManager)
{
}

HttpRemote::~HttpRemote()
{
    delete m_nam;
}

void HttpRemote::configure(const QString &baseUrl, const QString &token)
{
    // Normalise: drop any trailing slash so endpoint concatenation is clean.
    QString u = baseUrl.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    m_baseUrl = u;
    m_token = token.trimmed();
}

bool HttpRemote::isAuthenticated() const
{
    return !m_baseUrl.isEmpty() && !m_token.isEmpty();
}

HttpRemote::Reply HttpRemote::send(Verb verb, const QString &endpoint,
                                   const QByteArray &body, qint64 maxBytes)
{
    Reply out;
    if (m_baseUrl.isEmpty()) {
        out.transportError = true;
        return out;
    }

    QNetworkRequest req{QUrl(m_baseUrl + endpoint)};
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (verb == Verb::Put)
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/octet-stream"));

    QNetworkReply *reply = nullptr;
    switch (verb) {
    case Verb::Get:    reply = m_nam->get(req); break;
    case Verb::Put:    reply = m_nam->put(req, body); break;
    case Verb::Delete: reply = m_nam->deleteResource(req); break;
    case Verb::Post:   reply = m_nam->post(req, body); break;
    }
    if (!reply) {
        out.transportError = true;
        return out;
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Hard timeout so a hung Funnel never blocks the sync thread forever.
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
    });
    timer.start(REQUEST_TIMEOUT_MS);

    // OOM guard: abort as soon as the declared or received size exceeds the cap.
    if (maxBytes > 0) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [reply, maxBytes](qint64 received, qint64 total) {
            if ((total > 0 && total > maxBytes) || received > maxBytes)
                reply->abort();
        });
    }

    loop.exec();
    timer.stop();

    out.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    out.body = reply->readAll();
    reply->deleteLater();

    // A status code means the server answered (even 4xx/5xx); treat only a
    // genuine transport failure (no HTTP status) as a transport error.
    if (timedOut || (out.statusCode == 0 && netErr != QNetworkReply::NoError))
        out.transportError = true;
    if (maxBytes > 0 && out.body.size() > maxBytes)
        out.transportError = true;
    return out;
}

bool HttpRemote::acquireLock()
{
    if (!isAuthenticated())
        return false;
    const Reply r = send(Verb::Post, QStringLiteral("/lock"));
    return !r.transportError && r.statusCode == 200;   // 409 => held by a peer
}

bool HttpRemote::releaseLock()
{
    if (!isAuthenticated())
        return false;
    const Reply r = send(Verb::Delete, QStringLiteral("/lock"));
    return !r.transportError && (r.statusCode == 200 || r.statusCode == 204);
}

bool HttpRemote::uploadFile(const QString &remotePath, const QByteArray &data)
{
    if (!isAuthenticated())
        return false;
    const Reply r = send(Verb::Put, QStringLiteral("/blob/") + encPath(remotePath), data);
    return !r.transportError
           && (r.statusCode == 200 || r.statusCode == 201 || r.statusCode == 204);
}

bool HttpRemote::downloadFile(const QString &remotePath, QByteArray &data,
                              bool notFoundIsOk, qint64 maxBytes)
{
    data.clear();
    if (!isAuthenticated())
        return false;
    const Reply r = send(Verb::Get, QStringLiteral("/blob/") + encPath(remotePath), {}, maxBytes);
    if (r.transportError)
        return false;
    if (r.statusCode == 404)
        return notFoundIsOk;
    if (r.statusCode != 200)
        return false;
    data = r.body;
    return true;
}

bool HttpRemote::deleteFile(const QString &remotePath)
{
    if (!isAuthenticated())
        return false;
    const Reply r = send(Verb::Delete, QStringLiteral("/blob/") + encPath(remotePath));
    if (r.transportError)
        return false;
    return r.statusCode == 200 || r.statusCode == 204 || r.statusCode == 404;
}

QStringList HttpRemote::listFiles(const QString &folderPath)
{
    if (!isAuthenticated())
        return {};
    const Reply r = send(Verb::Get, QStringLiteral("/list/") + encPath(folderPath));
    if (r.transportError || r.statusCode != 200)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(r.body);
    if (!doc.isArray())
        return {};
    QStringList names;
    const QJsonArray arr = doc.array();
    names.reserve(arr.size());
    for (const QJsonValue &v : arr)
        names.append(v.toString());
    return names;
}
