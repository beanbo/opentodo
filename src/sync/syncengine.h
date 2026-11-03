#pragma once

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTimer>
#include <QQmlEngine>

#include "commit.h"

class DataStore;
class GDriveClient;
class RemoteStore;
class Merger;
class SnapshotManager;
class Telemetry;

class SyncEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SyncStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QDateTime lastSyncTime READ lastSyncTime NOTIFY lastSyncTimeChanged)
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)

public:
    // Reliability caps on remote payload sizes (used by pull() — see the
    // Content-Length and streaming guards in syncengine.cpp). A malicious
    // or runaway peer could otherwise push a multi-GB state.json that the
    // desktop client would happily buffer into memory.
    static constexpr qint64 MAX_STATE_SIZE  = 50LL * 1024 * 1024; // 50 MB
    static constexpr qint64 MAX_COMMIT_SIZE =  5LL * 1024 * 1024; //  5 MB

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

    // The active remote backend used for ALL push/pull/snapshot blob I/O.
    // Defaults to the Drive client (m_gdrive); call setRemote() to point the
    // engine at a self-hosted backend (LocalDirRemote / HttpRemote). Merge and
    // CRDT logic are unchanged — only the transport differs.
    RemoteStore *remote() const { return m_remote; }
    void setRemote(RemoteStore *remote);

    QList<Commit *> pendingCommits() const;
    void addPendingCommit(Commit *commit);

    void savePendingCommitToDisk(Commit *commit);
    void loadPendingCommitsFromDisk();

    // Q3.10: list every entityId referenced by an unsynced commit in the
    // local pending log. Used by DataStore::vacuum to skip tombstones whose
    // deletion hasn't yet propagated to peers — purging those would leave
    // peers with an orphan that can never resolve.
    Q_INVOKABLE QStringList pendingEntityIds() const;

    // Inject optional telemetry service. Wired in main.cpp; safe to leave
    // nullptr in tests — callers must null-check before use.
    void setTelemetry(Telemetry *telemetry);

    // True when HEAD references a commit not present in either the local or
    // last-seen remote log. push() refuses to operate in this state to avoid
    // chaining new commits off a phantom parent. Cleared on the next
    // successful pull() that resolves a consistent HEAD.
    bool isDanglingHead() const { return m_danglingHead; }

    // S12: true only while the engine itself is loading a merged remote state
    // into the DataStore. main.cpp's after-action scheduler ignores the
    // dataChanged emitted during that window (it is not a user edit) while
    // still remembering genuine edits made during a sync's network waits.
    bool isApplyingRemoteState() const { return m_applyingRemote; }

    // S12: true while a push/pull/sync is running (remote I/O spins nested
    // event loops, so a timer or UI tap can re-enter). A re-entrant request is
    // coalesced into one follow-up sync() after the current one finishes.
    bool isBusy() const { return m_busy; }

signals:
    void statusChanged();
    void lastErrorChanged();
    void lastSyncTimeChanged();
    void syncCompleted();
    void syncFailed(const QString &error);
    void commitCreated(Commit *commit);

    // Emitted when pull() drops a single commit whose JSON body could not be
    // parsed. The remaining commits still process; this surfaces the per-file
    // skip so QML / telemetry / user-visible toasts can react.
    void commitCorrupted(const QString &hash, const QString &reason);

    // Emitted when pull() detects HEAD references a commit absent from log/.
    // The engine enters a "dangling HEAD" state and will refuse push() until
    // a manual resync resolves the inconsistency.
    void danglingHeadDetected(const QString &headHash);

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

    // Bodies of push()/pull(); the public entry points add the S12
    // re-entrancy guard around them.
    void doPush();
    void doPull();
    bool beginSyncOp();
    void endSyncOp();

    // S2 + S4: advance the local HLC past the merged state's stamps, then load
    // `merged` into the store only if it differs from `local` (an unchanged
    // merge must not reset every ViewModel through DataStore::clear()).
    void applyMergedState(const QJsonObject &local, const QJsonObject &merged);

    DataStore *m_store;
    GDriveClient *m_gdrive;
    RemoteStore *m_remote = nullptr;   // active backend; defaults to m_gdrive
    Merger *m_merger;
    SnapshotManager *m_snapshot;
    Telemetry *m_telemetry = nullptr;

    SyncStatus m_status = Offline;
    QString m_lastError;
    QDateTime m_lastSyncTime;
    QString m_deviceId;
    QList<Commit *> m_pendingCommits;
    QString m_headCommitId;
    bool m_danglingHead = false;
    bool m_busy = false;            // S12
    bool m_resyncPending = false;   // S12
    bool m_applyingRemote = false;  // S12
    QTimer m_syncTimer;
};
