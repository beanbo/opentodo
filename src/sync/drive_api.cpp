#include "drive_api.h"
#include "oauth_client.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcDriveApi, "opentodo.driveapi")

namespace DriveEndpoints {
static const QString API_BASE    = QStringLiteral("https://www.googleapis.com/drive/v3");
static const QString UPLOAD_BASE = QStringLiteral("https://www.googleapis.com/upload/drive/v3");
} // namespace DriveEndpoints

DriveApi::DriveApi(OAuthClient *auth, QObject *parent)
    : QObject(parent)
    , m_auth(auth)
{
}

bool DriveApi::uploadFile(const QString &remotePath, const QByteArray &data)
{
    if (!m_auth->isAuthenticated()) return false;
    ensureRootFolder();

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        parentId = findOrCreateFolder(folderName, m_rootFolderId);
        if (parentId.isEmpty()) return false;
    }

    const QString existingId = findFileId(fileName, parentId);

    if (!existingId.isEmpty()) {
        // Update existing file via PATCH
        QUrl url(DriveEndpoints::UPLOAD_BASE +
                 QStringLiteral("/files/%1?uploadType=media").arg(existingId));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        auto *reply = m_nam.sendCustomRequest(req, "PATCH", data);
        // B-2.33: store method+body for retry on 401.
        reply->setProperty("_method", QByteArray("PATCH"));
        reply->setProperty("_body", data);
        const auto result = waitForReply(reply);
        return !result.isEmpty();
    }

    // Create new file via multipart upload
    QJsonObject metadata;
    metadata[QStringLiteral("name")]    = fileName;
    metadata[QStringLiteral("parents")] = QJsonArray{parentId};

    const QByteArray boundary = "----OpenTodoBoundary";
    QByteArray body;
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
    body += QJsonDocument(metadata).toJson(QJsonDocument::Compact) + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/json\r\n\r\n";
    body += data + "\r\n";
    body += "--" + boundary + "--";

    QUrl url(DriveEndpoints::UPLOAD_BASE + QStringLiteral("/files?uploadType=multipart"));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("multipart/related; boundary=%1")
                  .arg(QString::fromUtf8(boundary)));

    auto *reply = m_nam.post(req, body);
    // B-2.33: store method+body for retry on 401.
    reply->setProperty("_method", QByteArray("POST"));
    reply->setProperty("_body", body);
    const auto result = waitForReply(reply);
    return !result.isEmpty();
}

bool DriveApi::downloadFile(const QString &remotePath, QByteArray &data,
                             bool notFoundIsOk)
{
    if (!m_auth->isAuthenticated()) return false;
    ensureRootFolder();

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        // B-2.13: if the containing folder doesn't exist yet, treat it as
        // "not found" rather than a hard error.
        parentId = findFileId(folderName, m_rootFolderId);
        if (parentId.isEmpty()) {
            if (notFoundIsOk) { data.clear(); return true; }
            return false;
        }
    }

    const QString fileId = findFileId(fileName, parentId);
    if (fileId.isEmpty()) {
        // B-2.13: 404 — distinguish from true network error.
        if (notFoundIsOk) { data.clear(); return true; }
        return false;
    }

    QUrl url(DriveEndpoints::API_BASE +
             QStringLiteral("/files/%1?alt=media").arg(fileId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = m_nam.get(req);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qCInfo(lcDriveApi) << "downloadFile network error:" << reply->error() << reply->errorString();
        reply->deleteLater();
        // This is a real network error — always return false regardless of notFoundIsOk.
        return false;
    }

    data = reply->readAll();
    reply->deleteLater();
    return true;
}

bool DriveApi::deleteFile(const QString &remotePath)
{
    if (!m_auth->isAuthenticated()) return false;
    ensureRootFolder();

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        parentId = findFileId(folderName, m_rootFolderId);
        if (parentId.isEmpty()) return false;
    }

    const QString fileId = findFileId(fileName, parentId);
    if (fileId.isEmpty()) return false;

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files/%1").arg(fileId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = m_nam.deleteResource(req);
    // B-2.33: store method for retry on 401.
    reply->setProperty("_method", QByteArray("DELETE"));
    waitForReply(reply);
    return true;
}

QStringList DriveApi::listFiles(const QString &folderPath)
{
    QStringList result;
    if (!m_auth->isAuthenticated()) return result;
    ensureRootFolder();

    QString folderId = m_rootFolderId;
    if (!folderPath.isEmpty()) {
        folderId = findFileId(folderPath, m_rootFolderId);
        if (folderId.isEmpty()) return result;
    }

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"),
                       QStringLiteral("'%1' in parents and trashed = false").arg(folderId));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("files(id,name)"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = m_nam.get(req);
    const auto json = waitForReply(reply);

    const auto files = json[QStringLiteral("files")].toArray();
    for (const auto &f : files) {
        result.append(f.toObject()[QStringLiteral("name")].toString());
    }

    return result;
}

bool DriveApi::createFolder(const QString &path)
{
    return !findOrCreateFolder(path, m_rootFolderId).isEmpty();
}

bool DriveApi::folderExists(const QString &path)
{
    return !findFileId(path, m_rootFolderId).isEmpty();
}

bool DriveApi::ensureRootFolder()
{
    if (!m_rootFolderId.isEmpty()) return true;

    m_rootFolderId = findOrCreateFolder(QLatin1String(ROOT_FOLDER_NAME));
    return !m_rootFolderId.isEmpty();
}

bool DriveApi::acquireLock()
{
    if (!m_auth->isAuthenticated()) return false;
    ensureRootFolder();

    const QString lockId = findFileId(QLatin1String(LOCK_FILE_NAME), m_rootFolderId);
    if (!lockId.isEmpty()) {
        // Lock exists — check if stale (older than 5 minutes)
        QUrl url(DriveEndpoints::API_BASE +
                 QStringLiteral("/files/%1?fields=modifiedTime").arg(lockId));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

        auto *reply = m_nam.get(req);
        const auto json = waitForReply(reply);
        const auto modifiedTime = QDateTime::fromString(
            json[QStringLiteral("modifiedTime")].toString(), Qt::ISODate);

        if (modifiedTime.isValid() &&
            modifiedTime.secsTo(QDateTime::currentDateTimeUtc()) < 300) {
            return false; // Lock is fresh, someone else is syncing
        }
        // Stale lock — delete it
        deleteFile(QLatin1String(LOCK_FILE_NAME));
    }

    QJsonObject lockData;
    lockData[QStringLiteral("device_id")] =
        QSettings().value(QStringLiteral("sync/device_id")).toString();
    lockData[QStringLiteral("timestamp")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    return uploadFile(QLatin1String(LOCK_FILE_NAME), QJsonDocument(lockData).toJson());
}

bool DriveApi::releaseLock()
{
    return deleteFile(QLatin1String(LOCK_FILE_NAME));
}

bool DriveApi::isLocked() const
{
    // F-2.39: real implementation — read .lock file's modifiedTime.
    // We can't call const methods that mutate m_nam, so cast away const for the
    // network ops (all network state is in m_nam which is logically a cache).
    DriveApi *self = const_cast<DriveApi *>(this);

    const QString lockId = self->findFileId(QLatin1String(LOCK_FILE_NAME), m_rootFolderId);
    if (lockId.isEmpty()) return false;

    QUrl url(DriveEndpoints::API_BASE +
             QStringLiteral("/files/%1?fields=modifiedTime").arg(lockId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = self->m_nam.get(req);
    const auto json = self->waitForReply(reply);
    const auto modifiedTime = QDateTime::fromString(
        json[QStringLiteral("modifiedTime")].toString(), Qt::ISODate);

    if (!modifiedTime.isValid()) return false;
    // Lock is "live" if modified within the last 5 minutes.
    return modifiedTime.secsTo(QDateTime::currentDateTimeUtc()) < 300;
}

QString DriveApi::findFileId(const QString &name, const QString &parentId)
{
    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files"));
    QUrlQuery query;

    // S-2.40: escape single quotes in name to avoid Drive query injection.
    const QString escapedName = QString(name).replace(QLatin1Char('\''), QStringLiteral("\\'"));
    QString q = QStringLiteral("name = '%1' and trashed = false").arg(escapedName);
    if (!parentId.isEmpty())
        q += QStringLiteral(" and '%1' in parents").arg(parentId);
    query.addQueryItem(QStringLiteral("q"),      q);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("files(id)"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = m_nam.get(req);
    const auto json = waitForReply(reply);

    const auto files = json[QStringLiteral("files")].toArray();
    if (files.isEmpty()) return {};
    return files[0].toObject()[QStringLiteral("id")].toString();
}

QString DriveApi::findOrCreateFolder(const QString &name, const QString &parentId)
{
    const QString existingId = findFileId(name, parentId);
    if (!existingId.isEmpty()) return existingId;

    QJsonObject metadata;
    metadata[QStringLiteral("name")]     = name;
    metadata[QStringLiteral("mimeType")] =
        QStringLiteral("application/vnd.google-apps.folder");
    if (!parentId.isEmpty())
        metadata[QStringLiteral("parents")] = QJsonArray{parentId};

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files"));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QByteArray folderBody = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    auto *reply = m_nam.post(req, folderBody);
    // B-2.33: tag method for 401 retry.
    reply->setProperty("_method", QByteArray("POST"));
    reply->setProperty("_body", folderBody);
    const auto json = waitForReply(reply);

    return json[QStringLiteral("id")].toString();
}

QNetworkReply *DriveApi::makeRequest(const QString &url, const QByteArray &method,
                                     const QByteArray &data, const QString &contentType)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    return m_nam.sendCustomRequest(req, method, data);
}

QJsonObject DriveApi::waitForReply(QNetworkReply *reply)
{
    return waitForReplyWithRetry(reply, /*allowRetry=*/true);
}

QJsonObject DriveApi::waitForReplyWithRetry(QNetworkReply *reply, bool allowRetry)
{
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject result;
    if (reply->error() == QNetworkReply::NoError) {
        const auto data = reply->readAll();
        const auto doc  = QJsonDocument::fromJson(data);
        if (doc.isObject()) result = doc.object();
    } else if (reply->error() == QNetworkReply::AuthenticationRequiredError && allowRetry) {
        // B-2.33: refresh token and retry the SAME request once.
        qCInfo(lcDriveApi) << "401 received — refreshing access token and retrying";

        // Capture what we need before deleting the reply.
        // The request method and body are stored as custom properties by the
        // callers via QNetworkReply::setProperty("_method") / ("_body").
        const QNetworkRequest originalReq = reply->request();
        const QByteArray method   = reply->property("_method").toByteArray();
        const QByteArray sentBody = reply->property("_body").toByteArray();
        reply->deleteLater();

        if (!m_auth->refreshAccessToken()) {
            qCInfo(lcDriveApi) << "token refresh failed; giving up";
            return {};
        }

        // Rebuild the request with the fresh token.
        QNetworkRequest retryReq = originalReq;
        retryReq.setRawHeader("Authorization",
                              QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

        // Re-issue the request using the same method as the original.
        QNetworkReply *retryReply;
        if (method == "POST") {
            retryReply = m_nam.post(retryReq, sentBody);
        } else if (method == "PATCH") {
            retryReply = m_nam.sendCustomRequest(retryReq, "PATCH", sentBody);
        } else if (method == "DELETE") {
            retryReply = m_nam.deleteResource(retryReq);
        } else {
            // Default: GET (covers findFileId, listFiles, acquireLock metadata fetch).
            retryReply = m_nam.get(retryReq);
        }

        // Retry once more without further retry to avoid infinite loops.
        return waitForReplyWithRetry(retryReply, /*allowRetry=*/false);
    } else {
        qCInfo(lcDriveApi) << "network error:" << reply->error() << reply->errorString();
    }

    reply->deleteLater();
    return result;
}
