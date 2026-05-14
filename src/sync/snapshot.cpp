#include "snapshot.h"
#include "commit.h"
#include "gdrive.h"
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
    if (!m_gdrive || !m_gdrive->isAuthenticated()) {
        emit snapshotFailed(QStringLiteral("Google Drive not connected"));
        return;
    }

    // Upload current state as the new snapshot (called before releaseLock — X-2.10).
    const QJsonDocument doc(m_store->toJson());
    if (!m_gdrive->uploadFile(QStringLiteral("state.json"), doc.toJson())) {
        emit snapshotFailed(QStringLiteral("Failed to upload snapshot"));
        return;
    }

    // B-2.9: only delete log files whose commit chain terminates at or before
    // m_headCommitId. We must NOT delete commits that a peer may not have
    // pulled yet (i.e. commits that point to a parent beyond our known head).
    //
    // Strategy: download all log files, build parent→uuid map, walk the
    // ancestor chain from m_headCommitId, and only delete those.
    const QStringList logFiles = m_gdrive->listFiles(QStringLiteral("log"));

    // Parse commits into a uuid→parentCommit map.
    QMap<QString, QString> parentOf; // uuid → parent_commit
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        QByteArray data;
        if (!m_gdrive->downloadFile(QStringLiteral("log/") + fileName, data)) continue;
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

    // Delete only identified ancestral commit files.
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        // fileName is "<uuid>.json"
        const QString uuid = fileName.left(fileName.length() - 5);
        if (toDelete.contains(uuid)) {
            qCInfo(lcSnapshot) << "compacting log file:" << fileName;
            m_gdrive->deleteFile(QStringLiteral("log/") + fileName);
        }
    }

    resetCommitCount();
    emit snapshotCreated();
}

int SnapshotManager::commitCount() const
{
    return m_commitCount;
}

void SnapshotManager::setGDriveClient(GDriveClient *client)
{
    m_gdrive = client;
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
