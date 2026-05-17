#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>

class OAuthClient;
class QNetworkReply;

class DriveApi : public QObject
{
    Q_OBJECT

public:
    explicit DriveApi(OAuthClient *auth, QObject *parent = nullptr);

    // Q2.19-E: test hook — inject a custom QNetworkAccessManager so the test
    // suite can intercept outbound traffic without making real HTTP calls.
    // Ownership is NOT transferred; the caller must outlive the DriveApi.
    // Pass nullptr to revert to the internal manager. Production code
    // continues to use the embedded m_nam.
    void setNetworkManagerForTesting(QNetworkAccessManager *nam);

    // File operations
    bool uploadFile(const QString &remotePath, const QByteArray &data);

    /**
     * @brief Upload a file as a publicly-readable Drive object (anyone with the
     *        link can VIEW it). Used by the ShareManager to publish a snapshot
     *        of a project / selection of tasks via a shareable URL.
     *
     * Implementation walks three Drive REST calls:
     *   1. POST /upload/drive/v3/files?uploadType=multipart — create file in
     *      Drive root.
     *   2. POST /drive/v3/files/<id>/permissions with
     *      {"role":"reader","type":"anyone"} — flip the visibility to public.
     *   3. GET /drive/v3/files/<id>?fields=webViewLink,id — fetch the URL.
     */
    bool uploadPublicFile(const QString &name,
                          const QByteArray &data,
                          const QString &mimeType,
                          QString *outFileId,
                          QString *outWebViewLink);

    /**
     * @brief Delete a Drive file by id. Used by ShareManager to revoke a
     *        previously-published share link.
     */
    bool deletePublicFileById(const QString &fileId);

    /**
     * @brief Download a remote file.
     *
     * @param remotePath  Path relative to root folder.
     * @param data        Receives the file contents on success.
     * @param notFoundIsOk  When true, a 404 (file does not exist yet) is not
     *                      treated as an error: returns true with empty data.
     *                      When false (default), 404 returns false.
     * @param maxBytes    Hard upper bound on the body size in bytes.
     *                    A response whose Content-Length declares (or whose
     *                    streamed bytesReceived() exceeds) this value is
     *                    aborted; downloadFile returns false. Pass 0 to opt
     *                    out of the cap (legacy behaviour). The caller is
     *                    expected to pass SyncEngine::MAX_STATE_SIZE or
     *                    MAX_COMMIT_SIZE depending on the payload.
     * @return true on success (or 404 when notFoundIsOk=true), false on real error.
     *
     * B-2.13: callers that need to distinguish "no remote yet" from "network
     *         failure" should pass notFoundIsOk=true.
     */
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false,
                      qint64 maxBytes = 0);
    bool deleteFile(const QString &remotePath);
    QStringList listFiles(const QString &folderPath);

    // Folder operations
    bool createFolder(const QString &path);
    bool folderExists(const QString &path);
    bool ensureRootFolder();

    // Lock mechanism
    bool acquireLock();
    bool releaseLock();
    bool isLocked() const;

private:
    QString findFileId(const QString &name, const QString &parentId = {});
    QString findOrCreateFolder(const QString &name, const QString &parentId = {});
    QNetworkReply *makeRequest(const QString &url, const QByteArray &method,
                               const QByteArray &data = {},
                               const QString &contentType = {});
    // Returns parsed JSON body; on 401 refreshes token and retries once (B-2.33).
    QJsonObject waitForReply(QNetworkReply *reply);
    // Internal helper: retry the request with a fresh token when allowRetry=true.
    QJsonObject waitForReplyWithRetry(QNetworkReply *reply, bool allowRetry);

    // Returns the active network manager — either the test-injected one or
    // the internal m_nam. All HTTP requests MUST go through this accessor.
    QNetworkAccessManager *nam();

    OAuthClient *m_auth;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkAccessManager> m_namOverride;  // test hook
    QString m_rootFolderId;

    static constexpr auto ROOT_FOLDER_NAME = "opentodo_data";
    static constexpr auto LOCK_FILE_NAME   = ".lock";
};
