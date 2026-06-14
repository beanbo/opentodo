#pragma once

#include "remotestore.h"

#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// LocalDirRemote — a RemoteStore backed by a directory on the local filesystem.
//
// Two roles in the self-hosted topology:
//   1. The PC's OWN sync transport when the self-hosted backend is active — the
//      desktop app pushes/pulls against this directory directly (no network).
//   2. The canonical store that SyncHttpServer exposes over HTTP to remote
//      clients (the phone). Both touch the same files; the ".lock" blob is the
//      cross-actor coordination point, exactly like Drive's .lock.
//
// Blob paths are relative ("HEAD", "state.json", "log/<uuid>.json"); they map to
// files under the root dir. Path traversal ("..", absolute paths) is refused.
// Lock staleness (LOCK_TTL_SECONDS) mirrors the Drive backend so SyncEngine
// behaves identically regardless of transport.
// ─────────────────────────────────────────────────────────────────────────────
class LocalDirRemote : public RemoteStore
{
public:
    explicit LocalDirRemote(const QString &rootDir = QString());

    void setRootDir(const QString &rootDir);
    QString rootDir() const { return m_root; }

    bool isAuthenticated() const override;
    bool acquireLock() override;
    bool releaseLock() override;
    bool uploadFile(const QString &remotePath, const QByteArray &data) override;
    bool downloadFile(const QString &remotePath, QByteArray &data,
                      bool notFoundIsOk = false, qint64 maxBytes = 0) override;
    bool deleteFile(const QString &remotePath) override;
    QStringList listFiles(const QString &folderPath) override;

    // Match the Drive backend's 5-minute stale-lock reclaim window.
    static constexpr qint64 LOCK_TTL_SECONDS = 300;

    // Resolve a relative blob path to an absolute filesystem path under the root,
    // or an empty string if the path escapes the root (traversal / absolute).
    // Exposed so SyncHttpServer can reuse the exact same sanitization.
    QString resolve(const QString &remotePath) const;

private:
    QString m_root;
};
