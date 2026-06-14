#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QQmlEngine>

#include "oauth_client.h"
#include "drive_api.h"
#include "remotestore.h"

// GDriveClient is a backward-compatible facade over OAuthClient + DriveApi.
// SyncEngine and QML continue to use this class unchanged. It is also the Drive
// implementation of the RemoteStore contract (the 7 blob/lock ops below), so the
// sync engine can target it polymorphically alongside other backends.
class GDriveClient : public QObject, public RemoteStore
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the syncEngine.gdrive context property.")

    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY accountEmailChanged)

public:
    explicit GDriveClient(QObject *parent = nullptr);

    bool isAuthenticated() const override { return m_auth->isAuthenticated(); }
    QString accountEmail() const { return m_auth->accountEmail(); }

    Q_INVOKABLE void authenticate() { m_auth->authenticate(); }
    Q_INVOKABLE void signOut()      { m_auth->signOut(); }

    // Token accessors (kept for code paths that test/set tokens directly)
    QString accessToken() const           { return m_auth->accessToken(); }
    void setAccessToken(const QString &t);
    void setRefreshToken(const QString &t);

    // File operations (RemoteStore contract)
    bool uploadFile(const QString &remotePath, const QByteArray &data) override
        { return m_drive->uploadFile(remotePath, data); }
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false, qint64 maxBytes = 0) override
        { return m_drive->downloadFile(remotePath, data, notFoundIsOk, maxBytes); }
    bool deleteFile(const QString &remotePath) override
        { return m_drive->deleteFile(remotePath); }
    QStringList listFiles(const QString &folderPath) override
        { return m_drive->listFiles(folderPath); }

    // Public-share operations (used by ShareManager). Uploads to Drive root
    // and grants anonymous reader role. See DriveApi::uploadPublicFile.
    bool uploadPublicFile(const QString &name,
                          const QByteArray &data,
                          const QString &mimeType,
                          QString *outFileId,
                          QString *outWebViewLink)
        { return m_drive->uploadPublicFile(name, data, mimeType, outFileId, outWebViewLink); }

    bool deletePublicFileById(const QString &fileId)
        { return m_drive->deletePublicFileById(fileId); }

    // Folder operations
    bool createFolder(const QString &path)  { return m_drive->createFolder(path); }
    bool folderExists(const QString &path)  { return m_drive->folderExists(path); }
    bool ensureRootFolder()                 { return m_drive->ensureRootFolder(); }

    // Lock mechanism (RemoteStore contract; isLocked() is Drive-only extra)
    bool acquireLock() override  { return m_drive->acquireLock(); }
    bool releaseLock() override  { return m_drive->releaseLock(); }
    bool isLocked() const { return m_drive->isLocked(); }

    // Q2.19-E: test hook — exposes the underlying DriveApi so the test suite
    // can install a mock QNetworkAccessManager via DriveApi::setNetworkManagerForTesting.
    // Production code never calls this.
    DriveApi *driveApiForTesting() { return m_drive; }

signals:
    void authenticatedChanged();
    void accountEmailChanged();
    void authenticationSucceeded();
    void authenticationFailed(const QString &error);
    void uploadProgress(qint64 sent, qint64 total);
    void downloadProgress(qint64 received, qint64 total);

private:
    OAuthClient *m_auth;
    DriveApi    *m_drive;
};
