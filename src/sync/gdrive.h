#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QQmlEngine>

#include "oauth_client.h"
#include "drive_api.h"

// GDriveClient is a backward-compatible facade over OAuthClient + DriveApi.
// SyncEngine and QML continue to use this class unchanged.
class GDriveClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the syncEngine.gdrive context property.")

    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY accountEmailChanged)

public:
    explicit GDriveClient(QObject *parent = nullptr);

    bool isAuthenticated() const { return m_auth->isAuthenticated(); }
    QString accountEmail() const { return m_auth->accountEmail(); }

    Q_INVOKABLE void authenticate() { m_auth->authenticate(); }
    Q_INVOKABLE void signOut()      { m_auth->signOut(); }

    // Token accessors (kept for code paths that test/set tokens directly)
    QString accessToken() const           { return m_auth->accessToken(); }
    void setAccessToken(const QString &t);
    void setRefreshToken(const QString &t);

    // File operations
    bool uploadFile(const QString &remotePath, const QByteArray &data)
        { return m_drive->uploadFile(remotePath, data); }
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false)
        { return m_drive->downloadFile(remotePath, data, notFoundIsOk); }
    bool deleteFile(const QString &remotePath)
        { return m_drive->deleteFile(remotePath); }
    QStringList listFiles(const QString &folderPath)
        { return m_drive->listFiles(folderPath); }

    // Folder operations
    bool createFolder(const QString &path)  { return m_drive->createFolder(path); }
    bool folderExists(const QString &path)  { return m_drive->folderExists(path); }
    bool ensureRootFolder()                 { return m_drive->ensureRootFolder(); }

    // Lock mechanism
    bool acquireLock()  { return m_drive->acquireLock(); }
    bool releaseLock()  { return m_drive->releaseLock(); }
    bool isLocked() const { return m_drive->isLocked(); }

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
