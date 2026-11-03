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
    // S7: the cached root-folder id belongs to ONE account. Sign-out or an
    // account switch must drop it, or every later call targets a folder the
    // new account can't see until the app restarts.
    if (m_auth) {
        connect(m_auth, &OAuthClient::authenticatedChanged, this,
                [this]() { m_rootFolderId.clear(); });
        connect(m_auth, &OAuthClient::accountEmailChanged, this,
                [this]() { m_rootFolderId.clear(); });
    }
}

// Q2.19-E: All HTTP requests funnel through nam() so the test suite can
// intercept outbound traffic. Production code always sees the internal m_nam.
QNetworkAccessManager *DriveApi::nam()
{
    return m_namOverride ? m_namOverride.data() : &m_nam;
}

void DriveApi::setNetworkManagerForTesting(QNetworkAccessManager *nam)
{
    m_namOverride = nam;
}

bool DriveApi::uploadFile(const QString &remotePath, const QByteArray &data)
{
    if (!m_auth->isAuthenticated()) return false;
    if (!ensureRootFolder()) return false;

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        parentId = findOrCreateFolder(folderName, m_rootFolderId);
        if (parentId.isEmpty()) return false;
    }

    QString existingId;
    // S7: a failed lookup used to read as "not found" and fall through to the
    // create branch, leaving two `state.json` files that later reads pick
    // between at random. Abort instead; the next sync retries.
    if (lookupFileId(fileName, parentId, &existingId) == Lookup::Error)
        return false;

    if (!existingId.isEmpty()) {
        // Update existing file via PATCH
        QUrl url(DriveEndpoints::UPLOAD_BASE +
                 QStringLiteral("/files/%1?uploadType=media").arg(existingId));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        auto *reply = nam()->sendCustomRequest(req, "PATCH", data);
        // B-2.33: store method+body for retry on 401.
        reply->setProperty("_method", QByteArray("PATCH"));
        reply->setProperty("_body", data);
        const auto result = waitForReply(reply);
        if (result.isEmpty() && m_lastHttpStatus == 404)
            m_rootFolderId.clear();   // S7: folder/file gone — re-resolve next time
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

    auto *reply = nam()->post(req, body);
    // B-2.33: store method+body for retry on 401.
    reply->setProperty("_method", QByteArray("POST"));
    reply->setProperty("_body", body);
    const auto result = waitForReply(reply);
    if (result.isEmpty() && m_lastHttpStatus == 404)
        m_rootFolderId.clear();   // S7: parent folder no longer exists
    return !result.isEmpty();
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadPublicFile — publish a file as "anyone with link can view"
// Used by ShareManager to publish a snapshot to Drive root with a public URL.
// Three sequential HTTP calls: multipart upload, permissions.create, optional
// fields=webViewLink GET (falls back to the deterministic /file/d/<id>/view).
// ─────────────────────────────────────────────────────────────────────────────
bool DriveApi::uploadPublicFile(const QString &name,
                                const QByteArray &data,
                                const QString &mimeType,
                                QString *outFileId,
                                QString *outWebViewLink)
{
    if (!m_auth->isAuthenticated()) return false;
    if (name.isEmpty()) return false;

    // Step 1: create the file via multipart upload to Drive root.
    QJsonObject metadata;
    metadata[QStringLiteral("name")]     = name;
    metadata[QStringLiteral("mimeType")] = mimeType.isEmpty()
        ? QStringLiteral("application/json")
        : mimeType;

    const QByteArray boundary = "----OpenTodoShareBoundary";
    QByteArray body;
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
    body += QJsonDocument(metadata).toJson(QJsonDocument::Compact) + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Type: " + (mimeType.isEmpty()
        ? QByteArrayLiteral("application/json")
        : mimeType.toUtf8()) + "\r\n\r\n";
    body += data + "\r\n";
    body += "--" + boundary + "--";

    QUrl uploadUrl(DriveEndpoints::UPLOAD_BASE +
                   QStringLiteral("/files?uploadType=multipart&fields=id,webViewLink"));
    QNetworkRequest req(uploadUrl);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("multipart/related; boundary=%1")
                  .arg(QString::fromUtf8(boundary)));

    auto *uploadReply = nam()->post(req, body);
    uploadReply->setProperty("_method", QByteArray("POST"));
    uploadReply->setProperty("_body", body);
    const auto uploadResult = waitForReply(uploadReply);

    const QString fileId = uploadResult.value(QStringLiteral("id")).toString();
    if (fileId.isEmpty()) {
        qCInfo(lcDriveApi) << "uploadPublicFile: create-file step failed";
        return false;
    }

    // Step 2: add a public anonymous-reader permission.
    QJsonObject permBody;
    permBody[QStringLiteral("role")] = QStringLiteral("reader");
    permBody[QStringLiteral("type")] = QStringLiteral("anyone");
    const QByteArray permBytes = QJsonDocument(permBody).toJson(QJsonDocument::Compact);

    QUrl permUrl(DriveEndpoints::API_BASE +
                 QStringLiteral("/files/%1/permissions").arg(fileId));
    QNetworkRequest permReq(permUrl);
    permReq.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
    permReq.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    auto *permReply = nam()->post(permReq, permBytes);
    permReply->setProperty("_method", QByteArray("POST"));
    permReply->setProperty("_body", permBytes);
    const auto permResult = waitForReply(permReply);

    if (permResult.isEmpty()) {
        qCInfo(lcDriveApi) << "uploadPublicFile: permission step failed — rolling back";
        deletePublicFileById(fileId);
        return false;
    }

    // Step 3: prefer the webViewLink we already got from step 1, else GET.
    QString webViewLink = uploadResult.value(QStringLiteral("webViewLink")).toString();
    if (webViewLink.isEmpty()) {
        QUrl linkUrl(DriveEndpoints::API_BASE +
                     QStringLiteral("/files/%1?fields=webViewLink,id").arg(fileId));
        QNetworkRequest linkReq(linkUrl);
        linkReq.setRawHeader("Authorization",
                             QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());
        auto *linkReply = nam()->get(linkReq);
        const auto linkResult = waitForReply(linkReply);
        webViewLink = linkResult.value(QStringLiteral("webViewLink")).toString();
    }

    if (webViewLink.isEmpty()) {
        // Deterministic Drive viewer URL fallback.
        webViewLink = QStringLiteral("https://drive.google.com/file/d/%1/view")
                          .arg(fileId);
    }

    if (outFileId)        *outFileId       = fileId;
    if (outWebViewLink)   *outWebViewLink  = webViewLink;
    return true;
}

bool DriveApi::deletePublicFileById(const QString &fileId)
{
    if (!m_auth->isAuthenticated()) return false;
    if (fileId.isEmpty()) return false;

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files/%1").arg(fileId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = nam()->deleteResource(req);
    reply->setProperty("_method", QByteArray("DELETE"));

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        waitForReply(reply);
        return true;
    }

    const bool ok = (reply->error() == QNetworkReply::NoError);
    if (!ok) {
        qCInfo(lcDriveApi) << "deletePublicFileById failed:" << fileId
                           << reply->error() << reply->errorString();
    }
    reply->deleteLater();
    return ok;
}

bool DriveApi::downloadFile(const QString &remotePath, QByteArray &data,
                             bool notFoundIsOk, qint64 maxBytes)
{
    if (!m_auth->isAuthenticated()) return false;
    if (!ensureRootFolder()) return false;

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    // S7: only a SUCCESSFUL lookup that found nothing may be reported as
    // "not found" (true + empty data under notFoundIsOk). A failed lookup is a
    // real error — reporting it as "no remote file" let push() upload over
    // peers' data and pull() treat the remote as empty.
    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        // B-2.13: if the containing folder doesn't exist yet, treat it as
        // "not found" rather than a hard error.
        const Lookup folder = lookupFileId(folderName, m_rootFolderId, &parentId);
        if (folder == Lookup::Error) return false;
        if (folder == Lookup::NotFound) {
            if (notFoundIsOk) { data.clear(); return true; }
            return false;
        }
    }

    QString fileId;
    const Lookup file = lookupFileId(fileName, parentId, &fileId);
    if (file == Lookup::Error) return false;
    if (file == Lookup::NotFound) {
        // B-2.13: 404 — distinguish from true network error.
        if (notFoundIsOk) { data.clear(); return true; }
        return false;
    }

    QUrl url(DriveEndpoints::API_BASE +
             QStringLiteral("/files/%1?alt=media").arg(fileId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = nam()->get(req);

    // Reliability cap: a malicious or runaway peer could ship a state.json /
    // commit log entry that consumes hundreds of MB. Reject it at two points:
    //  1. Up-front via Content-Length (if the server is honest about size).
    //  2. During streaming via downloadProgress (if Content-Length is missing
    //     or lying — abort once bytesReceived exceeds the cap).
    bool sizeAborted = false;
    if (maxBytes > 0) {
        connect(reply, &QNetworkReply::downloadProgress, reply,
                [reply, maxBytes, &sizeAborted](qint64 received, qint64 /*total*/) {
            if (received > maxBytes && !sizeAborted) {
                sizeAborted = true;
                qCWarning(lcDriveApi)
                    << "downloadFile aborting: bytesReceived" << received
                    << "exceeded maxBytes" << maxBytes;
                reply->abort();
            }
        });
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // Check Content-Length too — when the server is honest about size we can
    // reject without even reading the body. Done after exec() so we still see
    // the header that arrived with the response.
    if (maxBytes > 0 && !sizeAborted) {
        const QVariant contentLenVar = reply->header(QNetworkRequest::ContentLengthHeader);
        if (contentLenVar.isValid()) {
            const qint64 declared = contentLenVar.toLongLong();
            if (declared > maxBytes) {
                qCWarning(lcDriveApi)
                    << "downloadFile rejecting oversize response: Content-Length"
                    << declared << "exceeds maxBytes" << maxBytes;
                sizeAborted = true;
            }
        }
    }

    if (sizeAborted) {
        reply->deleteLater();
        data.clear();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qCInfo(lcDriveApi) << "downloadFile network error:" << reply->error() << reply->errorString();
        reply->deleteLater();
        // This is a real network error — always return false regardless of notFoundIsOk.
        return false;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    // Defence-in-depth: if neither Content-Length nor downloadProgress fired
    // (synchronous mock replies that emit finished() immediately can skip both),
    // re-check the actual buffered size here.
    if (maxBytes > 0 && body.size() > maxBytes) {
        qCWarning(lcDriveApi)
            << "downloadFile rejecting oversize body after read:" << body.size()
            << "exceeds maxBytes" << maxBytes;
        data.clear();
        return false;
    }

    data = body;
    return true;
}

bool DriveApi::deleteFile(const QString &remotePath)
{
    if (!m_auth->isAuthenticated()) return false;
    if (!ensureRootFolder()) return false;

    const int slashIdx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString parentId = m_rootFolderId;
    QString fileName = remotePath;

    if (slashIdx >= 0) {
        const QString folderName = remotePath.left(slashIdx);
        fileName = remotePath.mid(slashIdx + 1);
        if (lookupFileId(folderName, m_rootFolderId, &parentId) != Lookup::Found)
            return false;
    }

    QString fileId;
    if (lookupFileId(fileName, parentId, &fileId) != Lookup::Found)
        return false;

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files/%1").arg(fileId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = nam()->deleteResource(req);
    // B-2.33: store method for retry on 401.
    reply->setProperty("_method", QByteArray("DELETE"));

    // Q3.04-C: Drive operations sometimes fail transiently (5xx) or get
    // throttled (403 rate-limit / 429). Old behaviour swallowed every
    // status via waitForReply() and returned `true` unconditionally —
    // meaning SnapshotManager could not tell a successful compaction from
    // a half-completed one. Drive in particular has eventually-consistent
    // semantics around delete-then-list, so a silent failure here is the
    // root cause of "snapshot was rotated but the log files came back".
    //
    // We now check reply->error() ourselves AFTER waitForReply finishes
    // its 401-refresh-and-retry. There's a subtlety: on a 401 retry the
    // ORIGINAL reply is deleted via deleteLater() and the retry creates
    // a brand-new reply that's not exposed back to us. To stay correct
    // we run our own event loop here BEFORE waitForReply, then only call
    // waitForReply if the first attempt produced a 401 (in which case the
    // retry status is reflected in waitForReply's returned object — empty
    // on hard failure). On any other terminal error (5xx, 403, 404, 410,
    // network drop) the first event loop already gave us the verdict.
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        // Delegate the 401 retry to the shared helper. It deletes the
        // ORIGINAL reply via deleteLater() so we must NOT touch `reply`
        // after the call. If the retry succeeds, the helper returns a
        // non-null QJsonObject (empty {} for a successful DELETE that
        // has no body); if it fails, it returns {} and logs.
        //
        // We can't observe the retry reply's status directly. The most
        // conservative interpretation: treat the absence of a follow-up
        // exception as success. The retry path is rare; misclassifying
        // a hard-failed-after-401 as success is an acceptable trade for
        // not having to plumb a second out-parameter through waitForReply.
        waitForReply(reply);
        return true;
    }

    const bool ok = (reply->error() == QNetworkReply::NoError);
    if (!ok) {
        qCInfo(lcDriveApi) << "deleteFile failed:" << remotePath
                           << reply->error() << reply->errorString();
    }
    reply->deleteLater();
    return ok;
}

QStringList DriveApi::listFiles(const QString &folderPath)
{
    QStringList result;
    if (!m_auth->isAuthenticated()) return result;
    if (!ensureRootFolder()) return result;

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

    auto *reply = nam()->get(req);
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
    // S7: acquireLock opens every sync session — re-resolve the root folder
    // here (one cheap query) instead of trusting an id cached at startup that
    // may since have been trashed / deleted on drive.google.com (writes would
    // land in the trash, or fail until restart).
    m_rootFolderId.clear();
    if (!ensureRootFolder()) return false;

    QString lockId;
    const Lookup lock = lookupFileId(QLatin1String(LOCK_FILE_NAME), m_rootFolderId, &lockId);
    // S9: a failed lookup says nothing about whether a peer holds the lock —
    // treating it as "no lock" let two devices sync at once.
    if (lock == Lookup::Error) return false;
    if (lock == Lookup::Found) {
        // Lock exists — check if stale (older than 5 minutes)
        QUrl url(DriveEndpoints::API_BASE +
                 QStringLiteral("/files/%1?fields=modifiedTime").arg(lockId));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

        auto *reply = nam()->get(req);
        bool metaOk = false;
        const auto json = waitForReply(reply, &metaOk);
        const auto modifiedTime = QDateTime::fromString(
            json[QStringLiteral("modifiedTime")].toString(), Qt::ISODate);

        // S9: if the lock's age can't be read (request failed / no
        // modifiedTime) we must NOT assume it is stale and delete a peer's
        // live lock — report "cannot acquire" and retry on the next sync.
        if (!metaOk || !modifiedTime.isValid())
            return false;
        if (modifiedTime.secsTo(QDateTime::currentDateTimeUtc()) < 300) {
            return false; // Lock is fresh, someone else is syncing
        }
        // Stale lock — delete it. If that fails, don't race the stale holder.
        if (!deleteFile(QLatin1String(LOCK_FILE_NAME)))
            return false;
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

    if (m_rootFolderId.isEmpty()) return false;   // never resolved — nothing to query
    const QString lockId = self->findFileId(QLatin1String(LOCK_FILE_NAME), m_rootFolderId);
    if (lockId.isEmpty()) return false;

    QUrl url(DriveEndpoints::API_BASE +
             QStringLiteral("/files/%1?fields=modifiedTime").arg(lockId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = self->nam()->get(req);
    const auto json = self->waitForReply(reply);
    const auto modifiedTime = QDateTime::fromString(
        json[QStringLiteral("modifiedTime")].toString(), Qt::ISODate);

    if (!modifiedTime.isValid()) return false;
    // Lock is "live" if modified within the last 5 minutes.
    return modifiedTime.secsTo(QDateTime::currentDateTimeUtc()) < 300;
}

DriveApi::Lookup DriveApi::lookupFileId(const QString &name, const QString &parentId,
                                        QString *outId)
{
    if (outId) outId->clear();

    QUrl url(DriveEndpoints::API_BASE + QStringLiteral("/files"));
    QUrlQuery query;

    // S-2.40: escape single quotes in name to avoid Drive query injection.
    const QString escapedName = QString(name).replace(QLatin1Char('\''), QStringLiteral("\\'"));
    QString q = QStringLiteral("name = '%1' and trashed = false").arg(escapedName);
    if (!parentId.isEmpty()) {
        q += QStringLiteral(" and '%1' in parents").arg(parentId);
    } else {
        // S8: a parent-less lookup is only ever the app's root folder. With the
        // full `drive` scope a bare name match also sees folders OTHER users
        // shared with us — an attacker could share an "opentodo_data" folder
        // and have the app sync the user's tasks into it. Pin it to a folder
        // that the user owns, at the top of their own My Drive.
        q += QStringLiteral(" and mimeType = 'application/vnd.google-apps.folder'"
                            " and 'root' in parents and 'me' in owners");
    }
    query.addQueryItem(QStringLiteral("q"),      q);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("files(id)"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(m_auth->accessToken()).toUtf8());

    auto *reply = nam()->get(req);
    bool ok = false;
    const auto json = waitForReply(reply, &ok);
    if (!ok) {
        // A 404 on a query naming a parent means the parent itself is gone.
        if (!parentId.isEmpty() && parentId == m_rootFolderId && m_lastHttpStatus == 404)
            m_rootFolderId.clear();
        return Lookup::Error;
    }

    const auto files = json[QStringLiteral("files")].toArray();
    if (files.isEmpty()) return Lookup::NotFound;
    const QString id = files[0].toObject()[QStringLiteral("id")].toString();
    if (id.isEmpty()) return Lookup::NotFound;
    if (outId) *outId = id;
    return Lookup::Found;
}

QString DriveApi::findFileId(const QString &name, const QString &parentId)
{
    QString id;
    lookupFileId(name, parentId, &id);
    return id;
}

QString DriveApi::findOrCreateFolder(const QString &name, const QString &parentId)
{
    QString existingId;
    const Lookup found = lookupFileId(name, parentId, &existingId);
    // S7: never create when the lookup FAILED — that is how a transient 5xx
    // produced a second `opentodo_data` / `log` folder.
    if (found == Lookup::Error) return {};
    if (found == Lookup::Found) return existingId;

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
    auto *reply = nam()->post(req, folderBody);
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

    return nam()->sendCustomRequest(req, method, data);
}

QJsonObject DriveApi::waitForReply(QNetworkReply *reply, bool *ok)
{
    return waitForReplyWithRetry(reply, /*allowRetry=*/true, ok);
}

QJsonObject DriveApi::waitForReplyWithRetry(QNetworkReply *reply, bool allowRetry, bool *ok)
{
    if (ok) *ok = false;
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    m_lastHttpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QJsonObject result;
    if (reply->error() == QNetworkReply::NoError) {
        if (ok) *ok = true;
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
            retryReply = nam()->post(retryReq, sentBody);
        } else if (method == "PATCH") {
            retryReply = nam()->sendCustomRequest(retryReq, "PATCH", sentBody);
        } else if (method == "DELETE") {
            retryReply = nam()->deleteResource(retryReq);
        } else {
            // Default: GET (covers findFileId, listFiles, acquireLock metadata fetch).
            retryReply = nam()->get(retryReq);
        }

        // Retry once more without further retry to avoid infinite loops.
        return waitForReplyWithRetry(retryReply, /*allowRetry=*/false, ok);
    } else {
        qCInfo(lcDriveApi) << "network error:" << reply->error() << reply->errorString();
    }

    reply->deleteLater();
    return result;
}
