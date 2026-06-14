#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────────
// RemoteStore — the storage contract the sync engine speaks.
//
// The sync model is a commit-log over a state snapshot living in a *dumb* blob
// store: a `.lock` file, a `HEAD` pointer, `state.json`, and `log/<uuid>.json`
// commit files. Google Drive was historically the only such store; this
// interface lets SyncEngine / SnapshotManager target any backend that can store
// and retrieve named blobs and hold an advisory lock — e.g. a self-hosted HTTP
// server on the user's PC reached over a Tailscale Funnel — WITHOUT touching any
// merge / CRDT logic, which stays entirely client-side. The remote never merges.
//
// Pure interface (deliberately NOT a QObject): an implementer that also needs
// signals/properties multiply-inherits QObject itself and adds Q_OBJECT — see
// GDriveClient (gdrive.h), which is `public QObject, public RemoteStore`.
// ─────────────────────────────────────────────────────────────────────────────
class RemoteStore
{
public:
    virtual ~RemoteStore() = default;

    // True when the backend is configured / reachable enough to attempt a sync
    // (Drive: OAuth token present; self-hosted: server URL + token configured).
    virtual bool isAuthenticated() const = 0;

    // Advisory whole-store lock. acquireLock() returns false when another device
    // currently holds a fresh lock; releaseLock() drops it. Stale locks (older
    // than the backend's TTL) are reclaimed by acquireLock().
    virtual bool acquireLock() = 0;
    virtual bool releaseLock() = 0;

    // Blob ops over relative paths: "HEAD", "state.json", "log/<uuid>.json".
    virtual bool uploadFile(const QString &remotePath, const QByteArray &data) = 0;

    // notFoundIsOk: a missing blob yields true with empty `data` (e.g. first
    // sync, no HEAD yet). maxBytes: hard cap — the implementer MUST refuse a
    // payload exceeding it (a runaway/malicious peer must not OOM the client).
    virtual bool downloadFile(const QString &remotePath, QByteArray &data,
                              bool notFoundIsOk = false, qint64 maxBytes = 0) = 0;

    virtual bool deleteFile(const QString &remotePath) = 0;

    // Names (not full paths) of the blobs directly under `folderPath`, e.g.
    // listFiles("log") -> ["<uuid>.json", ...].
    virtual QStringList listFiles(const QString &folderPath) = 0;
};
