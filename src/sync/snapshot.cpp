#include "snapshot.h"
#include "commit.h"
#include "remotestore.h"
#include "storage/datastore.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>

Q_LOGGING_CATEGORY(lcSnapshot, "opentodo.snapshot")

SnapshotManager::SnapshotManager(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

bool SnapshotManager::shouldCompact() const
{
    return m_commitCount >= m_compactionThreshold;
}

void SnapshotManager::createSnapshot()
{
    if (!m_remote || !m_remote->isAuthenticated()) {
        emit snapshotFailed(QStringLiteral("Sync backend not connected"));
        return;
    }

    // Upload current state as the new snapshot (called before releaseLock — X-2.10).
    const QJsonDocument doc(m_store->toJson());
    if (!m_remote->uploadFile(QStringLiteral("state.json"), doc.toJson())) {
        emit snapshotFailed(QStringLiteral("Failed to upload snapshot"));
        return;
    }

    // B-2.9: only delete log files whose commit chain terminates at or before
    // m_headCommitId. We must NOT delete commits that a peer may not have
    // pulled yet (i.e. commits that point to a parent beyond our known head).
    //
    // Strategy: download all log files, build parent→uuid map, walk the
    // ancestor chain from m_headCommitId, and only delete those.
    const QStringList logFiles = m_remote->listFiles(QStringLiteral("log"));

    // Parse commits into a uuid→parentCommit map.
    QMap<QString, QString> parentOf; // uuid → parent_commit
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        QByteArray data;
        if (!m_remote->downloadFile(QStringLiteral("log/") + fileName, data)) continue;
        const QJsonDocument d = QJsonDocument::fromJson(data);
        if (!d.isObject()) continue;
        const QJsonObject obj = d.object();
        const QString uuid   = obj[QStringLiteral("uuid")].toString();
        const QString parent = obj[QStringLiteral("parent_commit")].toString();
        if (!uuid.isEmpty())
            parentOf[uuid] = parent;
    }

    // Walk ancestor chain from m_headCommitId, collecting UUIDs to delete.
    QSet<QString> toDelete;
    QString cur = m_headCommitId;
    int safety = logFiles.size() + 1; // prevent infinite loop on cycle
    while (!cur.isEmpty() && parentOf.contains(cur) && safety-- > 0) {
        toDelete.insert(cur);
        cur = parentOf[cur];
    }

    // Q3.04-C: two-phase delete with abort-on-failure.
    //
    // Old behaviour (the bug this guards against): the loop called
    // deleteFile() and IGNORED its return value, then resetCommitCount()
    // ran unconditionally at the end. If one file failed to delete
    // (transient 5xx, file locked by an antivirus on a local backend,
    // network blip mid-batch) the counter was reset anyway, and the next
    // push would NOT trigger a re-compaction even though stale log files
    // were still lying around the remote — confusing peers that walk the
    // log chain after pulling.
    //
    // New behaviour: stop at the first failure, do NOT reset the counter,
    // and emit snapshotFailed so the caller can retry (next push will see
    // shouldCompact() == true again and retry the compaction, retrying
    // the failed deletes). state.json has already been uploaded — that is
    // intentional, since the new snapshot is itself a valid baseline for
    // any peer; the only impact of leftover log files is that they will
    // be retried on the next compaction. Counter preservation is what
    // keeps that retry on the rails.
    QString firstFailedFile;
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        // fileName is "<uuid>.json"
        const QString uuid = fileName.left(fileName.length() - 5);
        if (!toDelete.contains(uuid)) continue;

        const QString remotePath = QStringLiteral("log/") + fileName;
        qCInfo(lcSnapshot) << "compacting log file:" << fileName;
        if (!m_remote->deleteFile(remotePath)) {
            firstFailedFile = fileName;
            qCWarning(lcSnapshot)
                << "compaction: deleteFile failed for" << remotePath
                << "— aborting; commit counter preserved so next push retries.";
            break;
        }
    }

    if (!firstFailedFile.isEmpty()) {
        // Partial-failure path: do NOT reset the counter; do NOT emit
        // snapshotCreated. state.json is already on the remote (peers can
        // still consume it as a baseline) but at least one log file we
        // intended to fold into that snapshot is still on the remote.
        // Preserving the counter forces the next push to retry compaction.
        emit snapshotFailed(
            QStringLiteral("Compaction aborted: could not delete log file '%1'. "
                           "Commit counter preserved; next push will retry.")
                .arg(firstFailedFile));
        return;
    }

    resetCommitCount();
    emit snapshotCreated();
}

int SnapshotManager::commitCount() const
{
    return m_commitCount;
}

void SnapshotManager::setRemoteStore(RemoteStore *remote)
{
    m_remote = remote;
}

void SnapshotManager::setHeadCommitId(const QString &headId)
{
    m_headCommitId = headId;
}

void SnapshotManager::setCompactionThreshold(int threshold)
{
    m_compactionThreshold = threshold;
}

void SnapshotManager::incrementCommitCount()
{
    ++m_commitCount;
}

void SnapshotManager::resetCommitCount()
{
    m_commitCount = 0;
}
