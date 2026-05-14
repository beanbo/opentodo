#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>

class OAuthClient;
class QNetworkReply;

class DriveApi : public QObject
{
    Q_OBJECT

public:
    explicit DriveApi(OAuthClient *auth, QObject *parent = nullptr);

    // File operations
    bool uploadFile(const QString &remotePath, const QByteArray &data);

    /**
     * @brief Download a remote file.
     *
     * @param remotePath  Path relative to root folder.
     * @param data        Receives the file contents on success.
     * @param notFoundIsOk  When true, a 404 (file does not exist yet) is not
     *                      treated as an error: returns true with empty data.
     *                      When false (default), 404 returns false.
     * @return true on success (or 404 when notFoundIsOk=true), false on real error.
     *
     * B-2.13: callers that need to distinguish "no remote yet" from "network
     *         failure" should pass notFoundIsOk=true.
     */
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false);
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

    OAuthClient *m_auth;
    QNetworkAccessManager m_nam;
    QString m_rootFolderId;

    static constexpr auto ROOT_FOLDER_NAME = "opentodo_data";
    static constexpr auto LOCK_FILE_NAME   = ".lock";
};
