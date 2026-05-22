#include "syncengine.h"
#include "gdrive.h"
#include "merger.h"
#include "snapshot.h"
#include "hlc.h"
#include "storage/datastore.h"
#include "utils/uuid.h"
#include "services/telemetry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcSync, "opentodo.sync")

SyncEngine::SyncEngine(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_gdrive(new GDriveClient(this))
    , m_merger(new Merger(this))
    , m_snapshot(new SnapshotManager(store, this))
{
    // Wire snapshot manager to the same GDrive client.
    m_snapshot->setGDriveClient(m_gdrive);

    // Generate or load device ID
    QSettings settings;
    m_deviceId = settings.value(QStringLiteral("sync/device_id")).toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = UuidHelper::generate();
        settings.setValue(QStringLiteral("sync/device_id"), m_deviceId);
    }

    m_syncTimer.setSingleShot(false);
    connect(&m_syncTimer, &QTimer::timeout, this, &SyncEngine::onSyncTimerTimeout);

    loadPendingCommitsFromDisk();
}

SyncEngine::SyncStatus SyncEngine::status() const { return m_status; }
QString SyncEngine::lastError() const { return m_lastError; }
QDateTime SyncEngine::lastSyncTime() const { return m_lastSyncTime; }
QString SyncEngine::deviceId() const { return m_deviceId; }

void SyncEngine::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
    if (m_merger)
        m_merger->setTelemetry(telemetry);
}

Commit *SyncEngine::createCommit(const QList<SyncOperation> &operations)
{
    if (operations.isEmpty()) return nullptr;

    auto *commit = new Commit(this);
    commit->setDeviceId(m_deviceId);
    commit->setParentCommit(m_headCommitId);
    commit->setOperations(operations);
    // B-2.16/17: stamp HLC on the commit at creation time.
    commit->setHlc(HybridLogicalClock::now(m_deviceId));

    // B-2.48: do NOT advance m_headCommitId here; it is advanced only after
    // the commit is successfully uploaded in push().
    addPendingCommit(commit);
    savePendingCommitToDisk(commit);

    emit commitCreated(commit);
    return commit;
}

void SyncEngine::push()
{
    if (!m_gdrive->isAuthenticated()) {
        setStatus(Offline);
        return;
    }

    // Refuse to push while HEAD is dangling — a successful push would chain
    // new commits off a parent that no peer can resolve, silently corrupting
    // the commit chain. The caller must trigger a manual resync first.
    if (m_danglingHead) {
        const QString reason = QStringLiteral(
            "Refusing to push: local HEAD '%1' references a commit not present "
            "in the remote log. Run a manual resync to recover.")
            .arg(m_headCommitId);
        qCWarning(lcSync) << reason;
        setLastError(reason);
        setStatus(Error);
        emit syncFailed(reason);
        return;
    }

    setStatus(Syncing);

    // Acquire lock
    if (!m_gdrive->acquireLock()) {
        setLastError(QStringLiteral("Could not acquire sync lock. Another device may be syncing."));
        setStatus(Error);
        return;
    }

    // Upload each pending commit (CRDT operation log).
    // Iter-13: track + drop each uploaded commit before moving on. The old
    // code only purged pending commits after the *entire* loop succeeded,
    // so a mid-cycle failure (network drop, 429, quota) left already-
    // uploaded commits stuck in m_pendingCommits + on disk. The next push
    // would re-upload them (idempotent by uuid) AND re-bump
    // incrementCommitCount, causing shouldCompact() to fire prematurely.
    QList<Commit *> uploadedThisPass;
    uploadedThisPass.reserve(m_pendingCommits.size());
    auto purgeUploaded = [this, &uploadedThisPass]() {
        for (auto *commit : uploadedThisPass) {
            const QString filePath = pendingCommitsPath()
                + QStringLiteral("/") + commit->uuid() + QStringLiteral(".json");
            QFile::remove(filePath);
            m_pendingCommits.removeOne(commit);
            commit->deleteLater();
        }
        uploadedThisPass.clear();
    };

    // Walk by index because we are mutating m_pendingCommits.
    while (!m_pendingCommits.isEmpty()) {
        auto *commit = m_pendingCommits.first();
        const QJsonDocument doc(commit->toJson());
        const QString fileName = QStringLiteral("log/%1.json").arg(commit->uuid());

        if (!m_gdrive->uploadFile(fileName, doc.toJson())) {
            setLastError(QStringLiteral("Failed to upload commit %1").arg(commit->uuid()));
            // Drop only the commits actually delivered; leave the failing one
            // and any that follow it on disk so the next push picks up where
            // we stopped.
            purgeUploaded();
            m_gdrive->releaseLock();
            setStatus(Error);
            return;
        }
        // B-2.48: advance m_headCommitId only after a successful upload.
        m_headCommitId = commit->uuid();
        m_snapshot->setHeadCommitId(m_headCommitId);
        m_snapshot->incrementCommitCount();
        uploadedThisPass.append(commit);
        // Detach from m_pendingCommits but don't delete yet — purgeUploaded
        // at the end (or on failure return) handles the actual delete +
        // pending-file removal so the cleanup path stays in one place.
        m_pendingCommits.removeOne(commit);
    }

    // Iter-14: upload state.json BEFORE HEAD. Previously HEAD landed first,
    // so a state.json failure (Drive quota / 429) left peers seeing a HEAD
    // that pointed to a commit not yet reflected in the snapshot. The
    // recovery path through log/ usually closes the gap, but if
    // SnapshotManager compacted log/ before the partial-failure peer
    // pulled, the snapshot referenced by HEAD would be permanently gone.
    // state.json first means a peer never sees a HEAD it can't justify
    // from the snapshot.
    const QJsonDocument stateDoc(m_store->toJson());
    if (!m_gdrive->uploadFile(QStringLiteral("state.json"), stateDoc.toJson())) {
        setLastError(QStringLiteral("Failed to upload state.json"));
        // Iter-15 regress-fix for iter-14: the upload loop already removed
        // every uploaded commit from m_pendingCommits and put them in
        // uploadedThisPass. If we return here without purging, the Commit*
        // objects leak (deleteLater never called) and their pending-log
        // files stay on disk — on next launch they reload as pending,
        // re-upload (idempotent), and re-increment commitCount, which can
        // trip shouldCompact prematurely. The commits were successfully
        // delivered to Drive, so dropping them is the right move.
        purgeUploaded();
        m_gdrive->releaseLock();
        setStatus(Error);
        return;
    }

    // Update HEAD pointer (no-op if there are no commits, but cheap).
    if (!m_headCommitId.isEmpty()) {
        QJsonObject headObj;
        headObj[QStringLiteral("commit")]    = m_headCommitId;
        headObj[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QJsonDocument headDoc(headObj);
        m_gdrive->uploadFile(QStringLiteral("HEAD"), headDoc.toJson());
    }

    // X-2.10: create snapshot BEFORE releasing lock so peers can't race in.
    if (m_snapshot->shouldCompact()) {
        m_snapshot->createSnapshot();
    }

    // Iter-13: purgeUploaded was already called incrementally during the
    // upload loop (and on the failure return path). At this point both
    // uploadedThisPass and m_pendingCommits should be empty — but call
    // purgeUploaded once more to handle any commit that slipped in via
    // addPendingCommit between the loop ending and HEAD/state.json upload
    // (the lock prevents peers but our own code may queue mid-push).
    purgeUploaded();

    m_gdrive->releaseLock();

    m_lastSyncTime = QDateTime::currentDateTimeUtc();
    emit lastSyncTimeChanged();
    setStatus(Synced);
    emit syncCompleted();
}

void SyncEngine::pull()
{
    if (!m_gdrive->isAuthenticated()) {
        setStatus(Offline);
        return;
    }

    setStatus(Syncing);

    // X-2.12: acquire lock for the pull so we don't race against a concurrent push.
    if (!m_gdrive->acquireLock()) {
        setLastError(QStringLiteral("Could not acquire sync lock during pull."));
        setStatus(Error);
        return;
    }

    // B-2.13: use notFoundIsOk=true so a missing HEAD (first sync) is not
    // treated as a network error. A real network failure still returns false.
    // The HEAD file is tiny (a JSON object with one commit hash + timestamp),
    // so cap it with the same MAX_STATE_SIZE bound used elsewhere — a
    // multi-MB HEAD response would already be a sign of remote corruption.
    QByteArray headData;
    const bool headOk = m_gdrive->downloadFile(QStringLiteral("HEAD"), headData,
                                                /*notFoundIsOk=*/true,
                                                /*maxBytes=*/MAX_STATE_SIZE);
    if (!headOk) {
        // Real network error (or oversize) — do not proceed.
        setLastError(QStringLiteral("Network error downloading HEAD. Check connectivity."));
        m_gdrive->releaseLock();
        setStatus(Error);
        return;
    }

    if (headData.isEmpty()) {
        // No remote data yet — first sync, nothing to pull.
        m_gdrive->releaseLock();
        setStatus(Synced);
        return;
    }

    const QJsonDocument headDoc = QJsonDocument::fromJson(headData);
    const QString remoteHead = headDoc.object()[QStringLiteral("commit")].toString();

    if (remoteHead == m_headCommitId) {
        // Already up to date
        m_gdrive->releaseLock();
        m_lastSyncTime = QDateTime::currentDateTimeUtc();
        emit lastSyncTimeChanged();
        setStatus(Synced);
        return;
    }

    // B-2.1: download and apply individual commits newer than our local HEAD,
    // then merge with the snapshot.
    //
    // Pipeline order (Q3.10 refresh):
    //   1. listFiles(log/) — the directory listing is the SOURCE OF TRUTH for
    //      dangling-HEAD detection. Whether a particular commit body was
    //      downloadable / parseable is unrelated — a malformed commit is
    //      already accounted for in the listing.
    //   2. Download state.json with MAX_STATE_SIZE cap + B-2.42 invariants.
    //      A failure here aborts the pull with the SPECIFIC error (size,
    //      malformed, missing 'tasks') rather than falling through to the
    //      coarser-grained dangling-HEAD message.
    //   3. Dangling-HEAD check, using the log-file listing built in step 1.
    //      When dangling is detected we still apply state.json IF that
    //      snapshot carries real content (state.json is the authoritative
    //      snapshot — the log is just for incremental updates). An empty
    //      snapshot under a dangling HEAD is suspicious enough to refuse,
    //      preserving local data.
    //   4. Download + parse the individual commits, skipping (and surfacing
    //      via commitCorrupted) any that fail. Apply them when safe.
    //   5. Apply state.json.

    const QStringList logFiles = m_gdrive->listFiles(QStringLiteral("log"));

    // Build the set of commit hashes that EXIST in the log directory (drop
    // the ".json" suffix). Used by the dangling-HEAD check below — based on
    // directory contents, NOT on which commit bodies happened to parse.
    QSet<QString> logFileHashes;
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        logFileHashes.insert(fileName.left(fileName.size() - 5));
    }

    // ── Download + validate state.json BEFORE the dangling-HEAD check ─────
    //
    // A failure here is the more actionable diagnosis: "your state.json is
    // malformed / too big" beats "your HEAD pointer is stale" when both are
    // true simultaneously. The size cap also acts as a DoS guard before any
    // further memory is allocated.
    QByteArray stateData;
    if (!m_gdrive->downloadFile(QStringLiteral("state.json"), stateData,
                                /*notFoundIsOk=*/false,
                                /*maxBytes=*/MAX_STATE_SIZE)) {
        setLastError(QStringLiteral(
            "Failed to download remote state (network error or body "
            "exceeded MAX_STATE_SIZE = %1 bytes)").arg(MAX_STATE_SIZE));
        m_gdrive->releaseLock();
        setStatus(Error);
        return;
    }

    const QJsonDocument stateDoc = QJsonDocument::fromJson(stateData);
    const QJsonObject remoteState = stateDoc.object();

    // B-2.42: validate remote state before loading — an empty or structurally
    // invalid JSON must never clear user data. (QJsonDocument::fromJson
    // returns an invalid doc on parse error → .object() yields an empty
    // QJsonObject, so isEmpty() covers both.)
    if (remoteState.isEmpty() || !remoteState.contains(QStringLiteral("tasks"))) {
        qCWarning(lcSync) << "pull(): remote state.json is invalid or empty — aborting pull";
        m_gdrive->releaseLock();
        setLastError(QStringLiteral("Remote state.json is invalid (missing 'tasks'). Pull aborted."));
        setStatus(Error);
        return;
    }

    // ── Dangling-HEAD detection (using the directory listing) ──────────────
    //
    // If HEAD references a commit absent from log/ AND we don't already have
    // it locally (m_headCommitId), the remote is internally inconsistent.
    // We discriminate by snapshot content:
    //
    //  - If state.json carries real entities (at least one of
    //    tasks/projects/sections/labels is non-empty), trust the snapshot —
    //    it represents the authoritative state. Flag for telemetry / signal
    //    but proceed with the merge.
    //  - If state.json is entirely empty, treat dangling HEAD as the more
    //    likely truth: a partial / failed remote write. Abort the pull,
    //    preserve local data, and require manual resync.
    const bool danglingHead = !remoteHead.isEmpty()
        && !logFileHashes.contains(remoteHead)
        && remoteHead != m_headCommitId;
    auto isStateEmpty = [&]() {
        const auto tasksArr      = remoteState.value(QStringLiteral("tasks"))     .toArray();
        const auto projectsArr   = remoteState.value(QStringLiteral("projects"))  .toArray();
        const auto sectionsArr   = remoteState.value(QStringLiteral("sections"))  .toArray();
        const auto labelsArr     = remoteState.value(QStringLiteral("labels"))    .toArray();
        const auto filtersArr    = remoteState.value(QStringLiteral("filters"))   .toArray();
        const auto remindersArr  = remoteState.value(QStringLiteral("reminders")) .toArray();
        return tasksArr.isEmpty() && projectsArr.isEmpty()
            && sectionsArr.isEmpty() && labelsArr.isEmpty()
            && filtersArr.isEmpty() && remindersArr.isEmpty();
    };

    if (danglingHead) {
        const QString reason = QStringLiteral(
            "Remote HEAD '%1' references a commit not present in log/. "
            "The remote is internally inconsistent — refusing to apply "
            "remote state. Manually resync to recover.")
            .arg(remoteHead);
        qCWarning(lcSync) << "SyncEngine: dangling HEAD detected -" << reason;
        if (m_telemetry) {
            QVariantMap props;
            props.insert(QStringLiteral("head"), remoteHead);
            m_telemetry->trackEvent(QStringLiteral("sync_dangling_head"), props);
        }
        emit danglingHeadDetected(remoteHead);

        if (isStateEmpty()) {
            // No snapshot rescue path — abort to preserve local data.
            m_danglingHead = true;
            m_gdrive->releaseLock();
            setLastError(reason);
            setStatus(Error);
            return;
        }
        // Snapshot carries real content — keep going. We still set the
        // flag for telemetry consumers but do NOT block the merge: state.json
        // wins when log and HEAD disagree with the snapshot. The flag is
        // cleared after the merge succeeds so future push() can resume.
    }

    // ── Parse all log commits and chain-sort them ─────────────────────────
    QList<Commit *> remoteCommits;
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        // Derive the on-disk hash from the filename (drop ".json"). Used as
        // the identifier in commitCorrupted() signals when the body fails
        // to parse — the listing already proved the file exists.
        const QString fileHash = fileName.left(fileName.size() - 5);

        QByteArray commitData;
        if (!m_gdrive->downloadFile(QStringLiteral("log/") + fileName, commitData,
                                    /*notFoundIsOk=*/false,
                                    /*maxBytes=*/MAX_COMMIT_SIZE)) {
            const QString reason = QStringLiteral(
                "download failed or body exceeded MAX_COMMIT_SIZE (%1 bytes)")
                .arg(MAX_COMMIT_SIZE);
            qCWarning(lcSync) << "SyncEngine: dropping corrupted commit"
                              << fileHash << reason;
            if (m_telemetry) {
                QVariantMap props;
                props.insert(QStringLiteral("hash"),   fileHash);
                props.insert(QStringLiteral("reason"), reason);
                m_telemetry->trackEvent(QStringLiteral("sync_corrupted_commit"), props);
            }
            emit commitCorrupted(fileHash, reason);
            continue;
        }
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(commitData, &parseErr);
        if (!doc.isObject()) {
            // Replace the previous silent `if (!doc.isObject()) continue;` —
            // a malformed commit must surface to qWarning + telemetry + signal
            // so operators can tell "data loss because remote was corrupt"
            // from "data loss because we have a bug here". Neighbours still
            // process normally.
            const QString reason = parseErr.error == QJsonParseError::NoError
                ? QStringLiteral("JSON root was not an object")
                : parseErr.errorString();
            qCWarning(lcSync) << "SyncEngine: dropping corrupted commit"
                              << fileHash << reason;
            if (m_telemetry) {
                QVariantMap props;
                props.insert(QStringLiteral("hash"),   fileHash);
                props.insert(QStringLiteral("reason"), reason);
                m_telemetry->trackEvent(QStringLiteral("sync_corrupted_commit"), props);
            }
            emit commitCorrupted(fileHash, reason);
            continue;
        }
        auto *c = Commit::fromJson(doc.object(), this);
        // Defence in depth: a parsed commit whose UUID doesn't match the
        // on-disk filename hash is treated as corrupted. The directory
        // listing's filename IS the canonical identifier; mismatched or
        // missing uuids inside the body indicate a malformed commit
        // structure (e.g. valid JSON object that lacks the schema we
        // expect, or that lies about its identity).
        if (c->uuid() != fileHash) {
            const QString reason = c->uuid().isEmpty()
                ? QStringLiteral("commit body has empty 'uuid' field")
                : QStringLiteral(
                      "commit body's uuid '%1' does not match filename '%2'")
                  .arg(c->uuid(), fileHash);
            qCWarning(lcSync) << "SyncEngine: dropping corrupted commit"
                              << fileHash << reason;
            if (m_telemetry) {
                QVariantMap props;
                props.insert(QStringLiteral("hash"),   fileHash);
                props.insert(QStringLiteral("reason"), reason);
                m_telemetry->trackEvent(QStringLiteral("sync_corrupted_commit"), props);
            }
            emit commitCorrupted(fileHash, reason);
            c->deleteLater();
            continue;
        }
        remoteCommits.append(c);
    }

    // Sort commits by parent chain: commits whose parentCommit == m_headCommitId
    // come first; fall back to timestamp ordering when chain is incomplete.
    // Simple topological sort: build parent→child map.
    {
        QMap<QString, Commit *> byUuid;
        for (auto *c : remoteCommits)
            byUuid.insert(c->uuid(), c);

        QList<Commit *> sorted;
        QSet<QString> visited;

        // Walk chain from remoteHead backwards to find linear order.
        QList<QString> chain;
        QString cur = remoteHead;
        while (byUuid.contains(cur) && !visited.contains(cur)) {
            chain.prepend(cur);
            visited.insert(cur);
            cur = byUuid[cur]->parentCommit();
        }
        for (const auto &id : chain)
            sorted.append(byUuid[id]);

        // Any commits not in chain (parallel branches): append by timestamp.
        for (auto *c : remoteCommits) {
            if (!visited.contains(c->uuid()))
                sorted.append(c);
        }
        remoteCommits = sorted;
    }

    // Apply commits that are newer than our current local head. Skip when we
    // detected a dangling HEAD above — the chain can't be trusted to point
    // at the right ancestor, so applying parallel commits could re-introduce
    // already-superseded state. The snapshot below carries the merged truth.
    if (!danglingHead) {
        QString lastApplied = m_headCommitId;
        for (auto *c : remoteCommits) {
            // Skip commits already applied locally.
            if (c->uuid() == m_headCommitId) continue;
            applyCommit(c);
            lastApplied = c->uuid();
        }
    }

    // Merge remote snapshot with local state (pending commits keep local changes).
    if (m_pendingCommits.isEmpty()) {
        m_store->loadFromJson(remoteState);
    } else {
        const QJsonObject merged = m_merger->mergeStates(m_store->toJson(), remoteState);
        m_store->loadFromJson(merged);
    }

    qDeleteAll(remoteCommits);
    m_headCommitId = remoteHead;
    // The merge succeeded — clear any prior dangling state. A subsequent
    // successful pull resets the engine so push() can resume even if the
    // current pull had to bypass the chain.
    m_danglingHead = false;
    m_gdrive->releaseLock();
    m_lastSyncTime = QDateTime::currentDateTimeUtc();
    emit lastSyncTimeChanged();
    setStatus(Synced);
    emit syncCompleted();
}

void SyncEngine::sync()
{
    pull();
    if (m_status != Error) {
        push();
    }
}

void SyncEngine::setSyncInterval(int seconds)
{
    if (seconds <= 0) {
        m_syncTimer.stop();
    } else {
        // B-2.50: clamp to [1, 86400] seconds to prevent int overflow on
        // seconds * 1000 (max valid int is ~2.1M seconds before overflow).
        const int clamped = qBound(1, seconds, 86400);
        m_syncTimer.start(clamped * 1000);
    }
}

QList<Commit *> SyncEngine::pendingCommits() const
{
    return m_pendingCommits;
}

QStringList SyncEngine::pendingEntityIds() const
{
    // Walk every operation in every pending commit and collect entity ids.
    // Used by DataStore::vacuum to skip tombstones whose deletion hasn't
    // reached peers yet — purging those would orphan the deletion at the
    // remote, so we err on the side of keeping the tombstone alive.
    QStringList ids;
    for (const Commit *c : m_pendingCommits) {
        if (!c) continue;
        for (const SyncOperation &op : c->operations()) {
            if (!op.entityId.isEmpty())
                ids.append(op.entityId);
        }
    }
    return ids;
}

void SyncEngine::addPendingCommit(Commit *commit)
{
    commit->setParent(this);
    m_pendingCommits.append(commit);
}

void SyncEngine::savePendingCommitToDisk(Commit *commit)
{
    QDir dir(pendingCommitsPath());
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));

    const QString filePath = pendingCommitsPath() + QStringLiteral("/") + commit->uuid() + QStringLiteral(".json");
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        const QJsonDocument doc(commit->toJson());
        file.write(doc.toJson());
    }
}

void SyncEngine::loadPendingCommitsFromDisk()
{
    QDir dir(pendingCommitsPath());
    if (!dir.exists()) return;

    const auto entries = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const auto &entry : entries) {
        QFile file(dir.filePath(entry));
        if (!file.open(QIODevice::ReadOnly)) continue;

        const QByteArray data = file.readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) continue;

        auto *commit = Commit::fromJson(doc.object(), this);
        m_pendingCommits.append(commit);
    }

    if (!m_pendingCommits.isEmpty()) {
        m_headCommitId = m_pendingCommits.last()->uuid();
    }
}

void SyncEngine::onSyncTimerTimeout()
{
    sync();
}

void SyncEngine::setStatus(SyncStatus status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

void SyncEngine::setLastError(const QString &error)
{
    m_lastError = error;
    emit lastErrorChanged();
}

// Helper: apply an update patch (op.data) on top of the current entity JSON,
// preserving _meta from the patch so per-field HLC stamps survive.
// Returns the patched JSON object.
static QJsonObject applyPatch(const QJsonObject &currentJson, const QJsonObject &patch)
{
    QJsonObject result = currentJson;
    // B-2.6/7: preserve per-field _meta from patch where present.
    QJsonObject resultMeta = currentJson.value(QStringLiteral("_meta")).toObject();
    const QJsonObject patchMeta = patch.value(QStringLiteral("_meta")).toObject();

    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == QStringLiteral("_meta")) continue; // handled below
        result[it.key()] = it.value();
    }

    // Merge _meta: patch meta fields overwrite local meta fields (remote wins
    // on the fields it explicitly updated).
    for (auto it = patchMeta.begin(); it != patchMeta.end(); ++it)
        resultMeta[it.key()] = it.value();
    if (!resultMeta.isEmpty())
        result[QStringLiteral("_meta")] = resultMeta;

    return result;
}

void SyncEngine::applyCommit(Commit *commit)
{
    // B-2.16/17: advance the local HLC using the commit's HLC field directly.
    const QString commitHlc = commit->hlc();
    if (!commitHlc.isEmpty()) {
        const QString localHlc = HybridLogicalClock::loadLast();
        HybridLogicalClock::update(localHlc, commitHlc, m_deviceId);
        // HLC::update persists via QSettings internally; return value not needed.
    }

    for (const auto &op : commit->operations()) {
        const QString &type = op.type;

        // ── Tasks ─────────────────────────────────────────────────────────────
        if (type == QStringLiteral("task_add")) {
            if (!m_store->taskById(op.entityId)) {
                auto *task = Task::fromJson(op.data, m_store);
                m_store->addExistingTask(task);
            }
        } else if (type == QStringLiteral("task_update")) {
            // F-2.2, B-2.6/7: generic patch — handles all fields + preserves _meta.
            auto *task = m_store->taskById(op.entityId);
            if (task) {
                const QJsonObject patched = applyPatch(task->toJson(), op.data);
                // Re-hydrate the live object from the patched JSON.
                // Task::fromJsonInto does not exist yet; use the available
                // pattern of creating a temporary and copying fields over.
                // NOTE TO STORAGE AGENT: addTaskFromJson(patched) or
                // Task::applyJson(task, patched) would be cleaner; use the
                // existing setters for now.
                if (patched.contains(QStringLiteral("content")))
                    task->setContent(patched[QStringLiteral("content")].toString());
                if (patched.contains(QStringLiteral("description")))
                    task->setDescription(patched[QStringLiteral("description")].toString());
                if (patched.contains(QStringLiteral("priority")))
                    task->setPriority(patched[QStringLiteral("priority")].toInt());
                if (patched.contains(QStringLiteral("is_completed")))
                    task->setIsCompleted(patched[QStringLiteral("is_completed")].toBool());
                if (patched.contains(QStringLiteral("is_deleted")))
                    task->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                if (patched.contains(QStringLiteral("project_id")))
                    task->setProjectId(patched[QStringLiteral("project_id")].toString());
                if (patched.contains(QStringLiteral("section_id")))
                    task->setSectionId(patched[QStringLiteral("section_id")].toString());
                if (patched.contains(QStringLiteral("parent_id"))) {
                    // Q3.03-D: cycle guard. An inbound commit that sets
                    // parent_id must not create a subtask cycle (e.g.
                    // A→B→C followed by C→A → A reachable from C's chain).
                    // Walk the candidate parent's ancestry — if it leads
                    // back to the task being updated, REJECT the
                    // parent_id update but apply every other field in
                    // the patch. MAX_DEPTH bounds the walk so a
                    // pre-existing remote cycle cannot infinite-loop
                    // this guard either.
                    const QString candidate =
                        patched[QStringLiteral("parent_id")].toString();
                    constexpr int MAX_DEPTH = 1000;
                    bool wouldCycle = false;
                    if (candidate == op.entityId) {
                        // Self-parent — trivially a 1-cycle.
                        wouldCycle = true;
                    } else if (!candidate.isEmpty()) {
                        QString cursor = candidate;
                        for (int i = 0; i < MAX_DEPTH; ++i) {
                            Task *ancestor = m_store->taskById(cursor);
                            if (!ancestor) break;                  // chain ends at a missing task — no cycle.
                            const QString next = ancestor->parentId();
                            if (next.isEmpty()) break;             // reached root — no cycle.
                            if (next == op.entityId) {             // chain returns to the task being reparented.
                                wouldCycle = true;
                                break;
                            }
                            if (next == cursor) break;             // self-loop on an ancestor; not our cycle, but stop walking.
                            cursor = next;
                        }
                    }
                    if (wouldCycle) {
                        qCWarning(lcSync)
                            << "applyCommit: rejected parent_id cycle for task"
                            << op.entityId << "→" << candidate
                            << "(ancestry chain would loop back).";
                    } else {
                        task->setParentId(candidate);
                    }
                }
                if (patched.contains(QStringLiteral("order")))
                    task->setOrder(patched[QStringLiteral("order")].toInt());
                if (patched.contains(QStringLiteral("day_order")))
                    task->setDayOrder(patched[QStringLiteral("day_order")].toInt());
                if (patched.contains(QStringLiteral("is_pinned")))
                    task->setIsPinned(patched[QStringLiteral("is_pinned")].toBool());
                // iter-6 fix: previously this dispatch covered ~11 fields and
                // silently skipped the rest. The follow-up loadFromJson(merged)
                // in pull() papered over it, but any QML binding consumed by a
                // taskChanged emit between applyCommit and loadFromJson saw a
                // stale field. Now the live Task object actually mirrors the
                // patched JSON for every key Task::toJson emits.
                if (patched.contains(QStringLiteral("due")))
                    task->setDue(DueObject::fromJson(
                        patched[QStringLiteral("due")].toObject()));
                if (patched.contains(QStringLiteral("deadline"))) {
                    // Task::toJson wraps deadline as {"date": ...}; Task::fromJson
                    // pulls .date from the wrapper. Mirror both shapes here so a
                    // raw-string future-emit doesn't quietly break.
                    const QJsonValue dv = patched[QStringLiteral("deadline")];
                    if (dv.isObject())
                        task->setDeadline(dv.toObject().value(QStringLiteral("date")).toString());
                    else
                        task->setDeadline(dv.toString());
                }
                if (patched.contains(QStringLiteral("labels"))) {
                    QStringList labels;
                    for (const auto &v : patched[QStringLiteral("labels")].toArray())
                        labels.append(v.toString());
                    task->setLabels(labels);
                }
                if (patched.contains(QStringLiteral("completed_at"))) {
                    const QString iso = patched[QStringLiteral("completed_at")].toString();
                    task->setCompletedAt(iso.isEmpty()
                        ? QDateTime()
                        : QDateTime::fromString(iso, Qt::ISODateWithMs));
                }
                if (patched.contains(QStringLiteral("color")))
                    task->setColor(patched[QStringLiteral("color")].toString());
                if (patched.contains(QStringLiteral("duration")))
                    task->setDuration(DurationObject::fromJson(
                        patched[QStringLiteral("duration")].toObject()));
                if (patched.contains(QStringLiteral("estimate_minutes")))
                    task->setEstimateMinutes(patched[QStringLiteral("estimate_minutes")].toInt());
                // Q2.18-A FIX: push the inbound per-field _meta back into the
                // DataStore's m_taskMeta map. Previously we dropped it on the
                // floor and then called markTaskModified, which re-stamped
                // updated_at with the LOCAL HLC — silently degrading per-field
                // LWW to whole-object LWW for inbound updates. The merger has
                // already determined the right stamps; trust them.
                m_store->applyInboundTaskMeta(task->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("task_delete")) {
            m_store->removeTask(op.entityId);

        // ── Projects ──────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("project_add")) {
            if (!m_store->projectById(op.entityId)) {
                auto *project = Project::fromJson(op.data, m_store);
                m_store->addExistingProject(project);
            }
        } else if (type == QStringLiteral("project_update")) {
            // F-2.3, B-2.6/7: generic patch.
            auto *project = m_store->projectById(op.entityId);
            if (project) {
                const QJsonObject patched = applyPatch(project->toJson(), op.data);
                if (patched.contains(QStringLiteral("name")))
                    project->setName(patched[QStringLiteral("name")].toString());
                if (patched.contains(QStringLiteral("color")))
                    project->setColor(patched[QStringLiteral("color")].toString());
                if (patched.contains(QStringLiteral("view_style")))
                    project->setViewStyle(patched[QStringLiteral("view_style")].toString());
                if (patched.contains(QStringLiteral("is_favorite")))
                    project->setIsFavorite(patched[QStringLiteral("is_favorite")].toBool());
                if (patched.contains(QStringLiteral("is_archived")))
                    project->setIsArchived(patched[QStringLiteral("is_archived")].toBool());
                if (patched.contains(QStringLiteral("is_deleted")))
                    project->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                if (patched.contains(QStringLiteral("order")))
                    project->setOrder(patched[QStringLiteral("order")].toInt());
                // iter-6 fix: previously this dispatch only covered the 7 fields
                // above. The follow-up loadFromJson(merged) covered most callers,
                // but parent_id, description, notes, icon, weekly_goal,
                // linked_repo_path never reached the live Project object between
                // applyCommit and loadFromJson — QML bindings emitted with stale
                // values, and iter-5 added parent_id stamping in addProject which
                // expects the inverse path to actually apply on inbound.
                if (patched.contains(QStringLiteral("parent_id"))) {
                    const QString candidate =
                        patched[QStringLiteral("parent_id")].toString();
                    // Iter-7: cycle + Inbox guard for inbound project parent_id.
                    // ProjectViewModel::updateProject has the equivalent local
                    // guard, and task_update has the equivalent inbound guard
                    // (syncengine.cpp ~657). Without a guard here a peer can
                    // ship a commit that creates A→B→A — the recursive
                    // sidebar walker in SidebarContent.qml (Q3.19) would
                    // overflow the JS stack on expand, freezing the UI.
                    constexpr int MAX_DEPTH = 1000;
                    bool wouldCycle = (candidate == op.entityId); // self-parent
                    if (!wouldCycle && !candidate.isEmpty()) {
                        QString cursor = candidate;
                        for (int i = 0; i < MAX_DEPTH; ++i) {
                            Project *ancestor = m_store->projectById(cursor);
                            if (!ancestor) break;
                            const QString next = ancestor->parentId();
                            if (next.isEmpty()) break;
                            if (next == op.entityId) { wouldCycle = true; break; }
                            if (next == cursor) break; // pre-existing self-loop; stop
                            cursor = next;
                        }
                    }
                    if (wouldCycle) {
                        qCWarning(lcSync)
                            << "applyCommit: rejected project parent_id cycle for"
                            << op.entityId << "→" << candidate;
                    } else if (project->isInbox() && !candidate.isEmpty()) {
                        // Inbox must stay root — sidebar / inbox filter mode
                        // assume it lives at the top level. Mirrors the local
                        // guard in ProjectViewModel::updateProject.
                        qCWarning(lcSync)
                            << "applyCommit: rejected parent_id for Inbox project"
                            << op.entityId;
                    } else {
                        project->setParentId(candidate);
                    }
                }
                if (patched.contains(QStringLiteral("description")))
                    project->setDescription(patched[QStringLiteral("description")].toString());
                if (patched.contains(QStringLiteral("notes")))
                    project->setNotes(patched[QStringLiteral("notes")].toString());
                if (patched.contains(QStringLiteral("icon")))
                    project->setIcon(patched[QStringLiteral("icon")].toString());
                if (patched.contains(QStringLiteral("weekly_goal")))
                    project->setWeeklyGoal(patched[QStringLiteral("weekly_goal")].toInt());
                if (patched.contains(QStringLiteral("linked_repo_path")))
                    project->setLinkedRepoPath(patched[QStringLiteral("linked_repo_path")].toString());
                // Q2.18-A FIX: push inbound _meta back (see task_update for rationale).
                m_store->applyInboundProjectMeta(project->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("project_delete")) {
            m_store->removeProject(op.entityId);

        // ── Sections ──────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("section_add")) {
            if (!m_store->sectionById(op.entityId)) {
                auto *section = Section::fromJson(op.data, m_store);
                m_store->addExistingSection(section);
            }
        } else if (type == QStringLiteral("section_update")) {
            // F-2.4: previously missing.
            auto *section = m_store->sectionById(op.entityId);
            if (section) {
                // Q2.18-A FIX: route through applyPatch so the inbound _meta
                // is preserved and propagated back into m_sectionMeta. Without
                // this the merger sees zero CRDT stamps for fields the commit
                // touched and silently downgrades to whole-object LWW.
                const QJsonObject patched = applyPatch(section->toJson(), op.data);
                if (patched.contains(QStringLiteral("name")))
                    section->setName(patched[QStringLiteral("name")].toString());
                if (patched.contains(QStringLiteral("order")))
                    section->setOrder(patched[QStringLiteral("order")].toInt());
                if (patched.contains(QStringLiteral("is_deleted")))
                    section->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                // Q3.27: is_collapsed + is_archived were missing from the
                // dispatch — inbound commits that toggled either field were
                // silently dropped. Both are listed in DataStore's per-field
                // meta keys (datastore.cpp:971), so they participate in CRDT
                // merging at the snapshot layer; without the apply path
                // they never reach the in-memory Section.
                if (patched.contains(QStringLiteral("is_collapsed")))
                    section->setIsCollapsed(patched[QStringLiteral("is_collapsed")].toBool());
                if (patched.contains(QStringLiteral("is_archived")))
                    section->setIsArchived(patched[QStringLiteral("is_archived")].toBool());
                m_store->applyInboundSectionMeta(section->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("section_delete")) {
            m_store->removeSection(op.entityId);

        // ── Labels ────────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("label_add")) {
            if (!m_store->labelById(op.entityId)) {
                auto *label = Label::fromJson(op.data, m_store);
                m_store->addExistingLabel(label);
            }
        } else if (type == QStringLiteral("label_update")) {
            // F-2.4: previously missing.
            auto *label = m_store->labelById(op.entityId);
            if (label) {
                // Q2.18-A FIX: applyPatch + push inbound _meta back.
                const QJsonObject patched = applyPatch(label->toJson(), op.data);
                if (patched.contains(QStringLiteral("name")))
                    label->setName(patched[QStringLiteral("name")].toString());
                if (patched.contains(QStringLiteral("color")))
                    label->setColor(patched[QStringLiteral("color")].toString());
                if (patched.contains(QStringLiteral("order")))
                    label->setOrder(patched[QStringLiteral("order")].toInt());
                if (patched.contains(QStringLiteral("is_deleted")))
                    label->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                // Iter-15: is_favorite was in Label::toJson but not applied
                // on inbound — peer's ★ toggle was invisible in the sidebar
                // Favorites section until the next full loadFromJson cycle.
                if (patched.contains(QStringLiteral("is_favorite")))
                    label->setIsFavorite(patched[QStringLiteral("is_favorite")].toBool());
                m_store->applyInboundLabelMeta(label->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("label_delete")) {
            m_store->removeLabel(op.entityId);

        // ── Comments ──────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("comment_add")) {
            if (!m_store->commentById(op.entityId)) {
                auto *comment = Comment::fromJson(op.data, m_store);
                m_store->addExistingComment(comment);
            }
        } else if (type == QStringLiteral("comment_update")) {
            // F-2.4: previously missing.
            auto *comment = m_store->commentById(op.entityId);
            if (comment) {
                // Q2.18-A FIX: applyPatch + push inbound _meta back.
                const QJsonObject patched = applyPatch(comment->toJson(), op.data);
                if (patched.contains(QStringLiteral("content")))
                    comment->setContent(patched[QStringLiteral("content")].toString());
                if (patched.contains(QStringLiteral("is_deleted")))
                    comment->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                m_store->applyInboundCommentMeta(comment->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("comment_delete")) {
            m_store->removeComment(op.entityId);

        // ── Reminders ─────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("reminder_add")) {
            if (!m_store->reminderById(op.entityId)) {
                auto *reminder = Reminder::fromJson(op.data, m_store);
                m_store->addExistingReminder(reminder);
            }
        } else if (type == QStringLiteral("reminder_update")) {
            // F-2.4: previously missing.
            auto *reminder = m_store->reminderById(op.entityId);
            if (reminder) {
                // Q2.18-A FIX: applyPatch + push inbound _meta back.
                const QJsonObject patched = applyPatch(reminder->toJson(), op.data);
                if (patched.contains(QStringLiteral("due"))) {
                    // "due" is stored as a nested JSON object matching ReminderDue.
                    const QJsonValue dueVal = patched[QStringLiteral("due")];
                    if (dueVal.isObject())
                        reminder->setDue(ReminderDue::fromJson(dueVal.toObject()));
                    else if (dueVal.isString())
                        reminder->setDueDatetime(dueVal.toString());
                }
                if (patched.contains(QStringLiteral("type")))
                    reminder->setType(patched[QStringLiteral("type")].toString());
                if (patched.contains(QStringLiteral("is_deleted")))
                    reminder->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                // Iter-15: Reminder::toJson also writes minute_offset (for
                // relative reminders), location (for geofenced ones), and
                // recurrence_rule. All three were dropped on inbound — peer
                // edits to "remind me 15 min before" / geofence coords /
                // RRULE silently revert until the next loadFromJson cycle.
                // ReminderScheduler uses these inputs directly, so the gap
                // can also misfire OS toasts at the old offset.
                if (patched.contains(QStringLiteral("minute_offset")))
                    reminder->setMinuteOffset(patched[QStringLiteral("minute_offset")].toInt());
                if (patched.contains(QStringLiteral("location")))
                    reminder->setLocation(LocationObject::fromJson(
                        patched[QStringLiteral("location")].toObject()));
                if (patched.contains(QStringLiteral("recurrence_rule")))
                    reminder->setRecurrenceRule(patched[QStringLiteral("recurrence_rule")].toString());
                m_store->applyInboundReminderMeta(reminder->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("reminder_delete")) {
            m_store->removeReminder(op.entityId);

        // ── Filters ───────────────────────────────────────────────────────────
        } else if (type == QStringLiteral("filter_add")) {
            // F-2.4: previously missing entirely.
            if (!m_store->filterById(op.entityId)) {
                auto *filter = Filter::fromJson(op.data, m_store);
                m_store->addExistingFilter(filter);
            }
        } else if (type == QStringLiteral("filter_update")) {
            auto *filter = m_store->filterById(op.entityId);
            if (filter) {
                // Q2.18-A FIX: applyPatch + push inbound _meta back.
                const QJsonObject patched = applyPatch(filter->toJson(), op.data);
                if (patched.contains(QStringLiteral("name")))
                    filter->setName(patched[QStringLiteral("name")].toString());
                if (patched.contains(QStringLiteral("query")))
                    filter->setQuery(patched[QStringLiteral("query")].toString());
                if (patched.contains(QStringLiteral("color")))
                    filter->setColor(patched[QStringLiteral("color")].toString());
                if (patched.contains(QStringLiteral("order")))
                    filter->setOrder(patched[QStringLiteral("order")].toInt());
                if (patched.contains(QStringLiteral("is_deleted")))
                    filter->setIsDeleted(patched[QStringLiteral("is_deleted")].toBool());
                // Iter-15: same as label — is_favorite was in toJson but
                // unhandled on inbound, so a peer's ★ on a filter wasn't
                // visible until the next loadFromJson cycle.
                if (patched.contains(QStringLiteral("is_favorite")))
                    filter->setIsFavorite(patched[QStringLiteral("is_favorite")].toBool());
                m_store->applyInboundFilterMeta(filter->id(),
                    patched.value(QStringLiteral("_meta")).toObject());
            }
        } else if (type == QStringLiteral("filter_delete")) {
            m_store->removeFilter(op.entityId);

        } else {
            qCWarning(lcSync) << "applyCommit: unknown operation type:" << type;
        }
    }
}

QString SyncEngine::pendingCommitsPath() const
{
#ifdef Q_OS_ANDROID
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/local_log");
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/OpenTodo/local_log");
#endif
}
