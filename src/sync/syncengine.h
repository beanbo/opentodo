#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QTimer>
#include <QQmlEngine>

#include "commit.h"

class DataStore;
class GDriveClient;
class Merger;
class SnapshotManager;

class SyncEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SyncStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QDateTime lastSyncTime READ lastSyncTime NOTIFY lastSyncTimeChanged)
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)

public:
    enum SyncStatus {
        Synced,
        Syncing,
        Error,
        Offline
    };
    Q_ENUM(SyncStatus)

    explicit SyncEngine(DataStore *store, QObject *parent = nullptr);

    SyncStatus status() const;
    QString lastError() const;
    QDateTime lastSyncTime() const;
    QString deviceId() const;

    Q_INVOKABLE Commit *createCommit(const QList<SyncOperation> &operations);
    Q_INVOKABLE void push();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void sync();
    Q_INVOKABLE void setSyncInterval(int seconds);

    // Expose the underlying Drive client so QML (e.g. settings → "Connect")
    // can read `authenticated`/`accountEmail` and call `authenticate()`/`signOut()`
    // directly without going through SyncViewModel for every property.
    GDriveClient *gdriveClient() const { return m_gdrive; }

    QList<Commit *> pendingCommits() const;
    void addPendingCommit(Commit *commit);

    void savePendingCommitToDisk(Commit *commit);
    void loadPendingCommitsFromDisk();

signals:
    void statusChanged();
    void lastErrorChanged();
    void lastSyncTimeChanged();
    void syncCompleted();
    void syncFailed(const QString &error);
    void commitCreated(Commit *commit);

private slots:
    void onSyncTimerTimeout();

private:
    // Friend declaration for the Q2.18-A regression test that needs to
    // invoke applyCommit() directly without going through pull() (which
    // requires an authenticated GDriveClient). Production code never
    // calls applyCommit() externally — only this regression test does.
    friend class TestApplyCommitMetaPreservation;

    void setStatus(SyncStatus status);
    void setLastError(const QString &error);
    void applyCommit(Commit *commit);
    QString pendingCommitsPath() const;

    DataStore *m_store;
    GDriveClient *m_gdrive;
    Merger *m_merger;
    SnapshotManager *m_snapshot;

    SyncStatus m_status = Offline;
    QString m_lastError;
    QDateTime m_lastSyncTime;
    QString m_deviceId;
    QList<Commit *> m_pendingCommits;
    QString m_headCommitId;
    QTimer m_syncTimer;
};
