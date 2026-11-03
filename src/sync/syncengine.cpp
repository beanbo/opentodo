#include "syncengine.h"
#include "gdrive.h"
#include "remotestore.h"
#include "merger.h"
#include "snapshot.h"
#include "hlc.h"
#include "storage/datastore.h"
#include "utils/uuid.h"
#include "services/telemetry.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
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
    // Default the active remote backend to the Drive client; main.cpp may swap
    // it to a self-hosted backend after construction via setRemote().
    m_remote = m_gdrive;
    // Wire the snapshot manager to the same active remote.
    m_snapshot->setRemoteStore(m_remote);

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

void SyncEngine::setRemote(RemoteStore *remote)
{
    // Fall back to the Drive client if a null backend is passed so the engine
    // is never left without a remote to talk to.
    m_remote = remote ? remote : static_cast<RemoteStore *>(m_gdrive);
    if (m_snapshot)
        m_snapshot->setRemoteStore(m_remote);
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

// ── S12: re-entrancy guard ───────────────────────────────────────────────────
// Every remote call spins a nested QEventLoop, so the sync timer, the
// after-action debounce, the sync icon or pull-to-refresh can call back into
// push()/pull() mid-sync. The nested run used to try to take the remote lock we
// already hold (→ false "another device is syncing" error, or two .lock files
// on Drive). Now a re-entrant request only marks a follow-up, and one sync()
// (pull + push, which covers either request) runs after the current op ends.
bool SyncEngine::beginSyncOp()
{
    if (m_busy) {
        m_resyncPending = true;
        return false;
    }
    m_busy = true;
    return true;
}

void SyncEngine::endSyncOp()
{
    m_busy = false;
    if (m_resyncPending) {
        m_resyncPending = false;
        // Deferred so the caller's stack unwinds first (no recursion).
        QTimer::singleShot(0, this, &SyncEngine::sync);
    }
}

void SyncEngine::push()
{
    if (!beginSyncOp()) return;
    doPush();
    endSyncOp();
}

void SyncEngine::pull()
{
    if (!beginSyncOp()) return;
    doPull();
    endSyncOp();
}

void SyncEngine::sync()
{
    if (!beginSyncOp()) return;
    doPull();
    if (m_status != Error) {
        doPush();
    }
    endSyncOp();
}

// Order-insensitive comparison of two entity arrays keyed by "id".
static bool sameEntityArrays(const QJsonArray &a, const QJsonArray &b)
{
    if (a.size() != b.size()) return false;
    QHash<QString, QJsonObject> byId;
    byId.reserve(a.size());
    for (const auto &v : a) {
        const QJsonObject o = v.toObject();
        byId.insert(o.value(QStringLiteral("id")).toString(), o);
    }
    if (byId.size() != a.size()) return false;   // duplicate ids
    QSet<QString> seen;
    for (const auto &v : b) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (seen.contains(id)) return false;
        seen.insert(id);
        const auto it = byId.constFind(id);
        if (it == byId.constEnd() || it.value() != o) return false;
    }
    return true;
}

// S2: true when loading `merged` would not change the store's content. The
// merger emits entities in id order (not the store's list order), so compare
// collections as id-keyed sets; also compare settings + their stamps.
static bool mergedEqualsLocal(const QJsonObject &local, const QJsonObject &merged)
{
    for (const QString &key : {QStringLiteral("settings"), QStringLiteral("settings_meta")}) {
        if (local.value(key).toObject() != merged.value(key).toObject())
            return false;
    }
    static const QStringList collections = {
        QStringLiteral("tasks"),   QStringLiteral("projects"),
        QStringLiteral("sections"), QStringLiteral("labels"),
        QStringLiteral("filters"), QStringLiteral("reminders"),
        QStringLiteral("comments"),
    };
    for (const QString &key : collections) {
        if (!sameEntityArrays(local.value(key).toArray(), merged.value(key).toArray()))
            return false;
    }
    return true;
}

void SyncEngine::applyMergedState(const QJsonObject &local, const QJsonObject &merged)
{
    // S4: receiving remote state must advance the local HLC (the HLC receive
    // rule), otherwise a device whose wall clock lags a peer's keeps stamping
    // edits BELOW the stamps it just merged and its next edits silently lose.
    // HybridLogicalClock::update was only reached from the commit-log path,
    // which production never exercises.
    const QString maxTs = Merger::maxHlc(merged);
    const QString lastTs = HybridLogicalClock::loadLast();
    if (!maxTs.isEmpty() && HybridLogicalClock::compare(maxTs, lastTs) > 0)
        HybridLogicalClock::update(lastTs, maxTs, m_deviceId);

    // S2: loadFromJson() runs DataStore::clear() → aboutToReset, which resets
    // every list model (closing the editor, dropping typed text, resetting
    // scroll). Doing that on every ~5 s after-action push when nothing changed
    // made the app unusable; skip it when the merge is a no-op.
    if (mergedEqualsLocal(local, merged))
        return;

    m_applyingRemote = true;
    m_store->loadFromJson(merged);
    m_applyingRemote = false;
}

void SyncEngine::doPush()
{
    if (!m_remote->isAuthenticated()) {
        setStatus(Offline);
        return;
    }

    // Refuse to push while HEAD is dangling — a successful push would chain
    // new commits off a parent that no peer can resolve, silently corrupting
    // the commit chain. The caller must trigger a manual resync first.
    if (m_danglingHead) {
        const QString reason = QStringLiteral(
            "Отправка отклонена: локальный HEAD «%1» ссылается на коммит, "
            "отсутствующий в удалённом журнале. Выполните повторную "
            "синхронизацию вручную для восстановления.")
            .arg(m_headCommitId);
        qCWarning(lcSync) << reason;
        setLastError(reason);
        setStatus(Error);
        emit syncFailed(reason);
        return;
    }

    setStatus(Syncing);

    // Acquire lock
    if (!m_remote->acquireLock()) {
        setLastError(QStringLiteral("Не удалось получить блокировку синхронизации. Возможно, "
                                      "синхронизация выполняется на другом устройстве, "
                                      "либо отсутствует подключение к сети."));
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

        if (!m_remote->uploadFile(fileName, doc.toJson())) {
            setLastError(QStringLiteral("Не удалось отправить коммит %1").arg(commit->uuid()));
            // Drop only the commits actually delivered; leave the failing one
            // and any that follow it on disk so the next push picks up where
            // we stopped.
            purgeUploaded();
            m_remote->releaseLock();
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
    // Iter-sync-merge-fix: before overwriting the remote snapshot with our local
    // state, fold the CURRENT remote state.json back into local. push() uploads
    // the whole state.json (last-writer-wins on the snapshot), so without this a
    // device whose local snapshot lacks a peer's recently-pushed entity would
    // clobber that entity out of remote state.json — precisely how a task ends up
    // stranded on a single device. We hold the sync lock here, so this
    // download → merge → upload is atomic against other devices. notFoundIsOk=true
    // keeps the very first push (no remote state yet) working; a malformed or
    // empty remote body is ignored rather than allowed to delete local data.
    {
        QByteArray remoteStateData;
        const bool gotRemote = m_remote->downloadFile(
            QStringLiteral("state.json"), remoteStateData,
            /*notFoundIsOk=*/true, /*maxBytes=*/MAX_STATE_SIZE);
        if (!gotRemote) {
            // S6: the download FAILED (network error, 5xx/429, lookup error,
            // oversize body) — this is NOT "no remote state yet" (that returns
            // true with an empty body). Uploading our unmerged local state now
            // would overwrite every peer change in remote state.json, so abort
            // and let the next sync retry.
            setLastError(QStringLiteral(
                "Не удалось загрузить удалённый state.json перед отправкой — "
                "отправка отменена, чтобы не перезаписать изменения других "
                "устройств."));
            purgeUploaded();
            m_remote->releaseLock();
            setStatus(Error);
            return;
        }
        if (!remoteStateData.isEmpty()) {
            const QJsonObject remoteState =
                QJsonDocument::fromJson(remoteStateData).object();
            if (!remoteState.isEmpty()
                && remoteState.contains(QStringLiteral("tasks"))) {
                const QJsonObject local = m_store->toJson();
                applyMergedState(local, m_merger->mergeStates(local, remoteState));
            }
        }
    }

    const QJsonDocument stateDoc(m_store->toJson());
    if (!m_remote->uploadFile(QStringLiteral("state.json"), stateDoc.toJson())) {
        setLastError(QStringLiteral("Не удалось отправить state.json"));
        // Iter-15 regress-fix for iter-14: the upload loop already removed
        // every uploaded commit from m_pendingCommits and put them in
        // uploadedThisPass. If we return here without purging, the Commit*
        // objects leak (deleteLater never called) and their pending-log
        // files stay on disk — on next launch they reload as pending,
        // re-upload (idempotent), and re-increment commitCount, which can
        // trip shouldCompact prematurely. The commits were successfully
        // delivered to Drive, so dropping them is the right move.
        purgeUploaded();
        m_remote->releaseLock();
        setStatus(Error);
        return;
    }

    // Update HEAD pointer (no-op if there are no commits, but cheap).
    if (!m_headCommitId.isEmpty()) {
        QJsonObject headObj;
        headObj[QStringLiteral("commit")]    = m_headCommitId;
        headObj[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QJsonDocument headDoc(headObj);
        // iter-night-3 regress-fix (#58/#59 follow-up): the uploadFile return
        // value here was being ignored. If HEAD failed but state.json + log/
        // succeeded, peers would still see the OLD HEAD on Drive while their
        // local m_headCommitId == oldHead would short-circuit the pull
        // (state.json never re-downloaded). Worse, the subsequent
        // shouldCompact() / createSnapshot() walks ancestors of our NEW
        // m_headCommitId and deletes their log/<uuid>.json files — peers and
        // future pulls then see a dangling HEAD pointing at a now-purged
        // commit and refuse to push. Treat the HEAD upload as a hard
        // failure and abort BEFORE running compaction.
        if (!m_remote->uploadFile(QStringLiteral("HEAD"), headDoc.toJson())) {
            setLastError(QStringLiteral("Не удалось отправить указатель HEAD"));
            purgeUploaded();
            m_remote->releaseLock();
            setStatus(Error);
            return;
        }
    }

    // X-2.10: create snapshot BEFORE releasing lock so peers can't race in.
    // (Now gated on HEAD upload success — see comment above.)
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

    m_remote->releaseLock();

    m_lastSyncTime = QDateTime::currentDateTimeUtc();
    emit lastSyncTimeChanged();
    setStatus(Synced);
    emit syncCompleted();
}

void SyncEngine::doPull()
{
    if (!m_remote->isAuthenticated()) {
        setStatus(Offline);
        return;
    }

    setStatus(Syncing);

    // X-2.12: acquire lock for the pull so we don't race against a concurrent push.
    if (!m_remote->acquireLock()) {
        setLastError(QStringLiteral("Не удалось получить блокировку синхронизации при загрузке. "
                                      "Возможно, синхронизация выполняется на другом "
                                      "устройстве, либо отсутствует подключение к сети."));
        setStatus(Error);
        return;
    }

    // B-2.13: use notFoundIsOk=true so a missing HEAD (first sync) is not
    // treated as a network error. A real network failure still returns false.
    // The HEAD file is tiny (a JSON object with one commit hash + timestamp),
    // so cap it with the same MAX_STATE_SIZE bound used elsewhere — a
    // multi-MB HEAD response would already be a sign of remote corruption.
    QByteArray headData;
    const bool headOk = m_remote->downloadFile(QStringLiteral("HEAD"), headData,
                                                /*notFoundIsOk=*/true,
                                                /*maxBytes=*/MAX_STATE_SIZE);
    if (!headOk) {
        // Real network error (or oversize) — do not proceed.
        setLastError(QStringLiteral("Ошибка сети при загрузке HEAD. Проверьте подключение к интернету."));
        m_remote->releaseLock();
        setStatus(Error);
        return;
    }

    const QString remoteHead = headData.isEmpty()
        ? QString()
        : QJsonDocument::fromJson(headData).object()[QStringLiteral("commit")].toString();

    if (headData.isEmpty() || remoteHead == m_headCommitId) {
        // S5: no commits to replay — but that says nothing about state.json.
        // Production pushes whole-state snapshots without creating commits, so
        // remote HEAD is always absent and this branch is the NORMAL pull: it
        // previously returned Synced without reading state.json, which made
        // "Pull" / "reload from Drive" and the first half of sync() no-ops.
        // Download the snapshot (a missing one is fine — first sync) and merge.
        QByteArray snapData;
        if (!m_remote->downloadFile(QStringLiteral("state.json"), snapData,
                                    /*notFoundIsOk=*/true,
                                    /*maxBytes=*/MAX_STATE_SIZE)) {
            setLastError(QStringLiteral(
                "Не удалось загрузить удалённое состояние (ошибка сети или "
                "размер тела ответа превысил MAX_STATE_SIZE = %1 байт)").arg(MAX_STATE_SIZE));
            m_remote->releaseLock();
            setStatus(Error);
            return;
        }
        if (!snapData.isEmpty()) {
            const QJsonObject snapState = QJsonDocument::fromJson(snapData).object();
            // B-2.42: never let an empty / structurally invalid snapshot touch
            // local data.
            if (snapState.isEmpty() || !snapState.contains(QStringLiteral("tasks"))) {
                qCWarning(lcSync) << "pull(): remote state.json is invalid or empty — aborting pull";
                m_remote->releaseLock();
                setLastError(QStringLiteral("Удалённый state.json повреждён (отсутствует «tasks»). Загрузка прервана."));
                setStatus(Error);
                return;
            }
            const QJsonObject local = m_store->toJson();
            applyMergedState(local, m_merger->mergeStates(local, snapState));
        }
        m_remote->releaseLock();
        m_lastSyncTime = QDateTime::currentDateTimeUtc();
        emit lastSyncTimeChanged();
        setStatus(Synced);
        emit syncCompleted();
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

    const QStringList logFiles = m_remote->listFiles(QStringLiteral("log"));

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
    if (!m_remote->downloadFile(QStringLiteral("state.json"), stateData,
                                /*notFoundIsOk=*/false,
                                /*maxBytes=*/MAX_STATE_SIZE)) {
        setLastError(QStringLiteral(
            "Не удалось загрузить удалённое состояние (ошибка сети или "
            "размер тела ответа превысил MAX_STATE_SIZE = %1 байт)").arg(MAX_STATE_SIZE));
        m_remote->releaseLock();
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
        m_remote->releaseLock();
        setLastError(QStringLiteral("Удалённый state.json повреждён (отсутствует «tasks»). Загрузка прервана."));
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
            "Удалённый HEAD «%1» ссылается на коммит, отсутствующий в log/. "
            "Удалённое хранилище внутренне несогласованно — применение "
            "удалённого состояния отменено. Выполните повторную "
            "синхронизацию вручную для восстановления.")
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
            m_remote->releaseLock();
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
        if (!m_remote->downloadFile(QStringLiteral("log/") + fileName, commitData,
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
        // Commit application mutates the store directly — not a user edit.
        m_applyingRemote = true;
        for (auto *c : remoteCommits) {
            // Skip commits already applied locally.
            if (c->uuid() == m_headCommitId) continue;
            applyCommit(c);
            lastApplied = c->uuid();
        }
        m_applyingRemote = false;
    }

    // Merge remote snapshot with local state. ALWAYS merge — never blindly
    // overwrite local with the remote snapshot. The old fast-path assumed
    // "no pending commits ⇒ local holds nothing the remote lacks", but that
    // invariant breaks whenever local has an entity that never made it into the
    // remote snapshot:
    //   * a commit whose effect a peer's whole-state push() later clobbered out
    //     of remote state.json (its log/ entry since compacted away), or
    //   * data loaded from a backup / import / direct edit that produced no
    //     pending commit.
    // In those cases loadFromJson(remoteState) silently DELETED the local-only
    // entity (this is exactly how the "просмотр видео" task got stranded on one
    // device). mergeStates() unions both sides and resolves per field via the
    // HLC, with tombstones winning, so local-only non-deleted entities survive
    // while genuine remote deletes and newer remote fields still apply. Merging
    // is safe even when there are no pending commits (it is a no-op when local
    // already equals the last-synced remote).
    const QJsonObject localState = m_store->toJson();
    applyMergedState(localState, m_merger->mergeStates(localState, remoteState));

    qDeleteAll(remoteCommits);
    m_headCommitId = remoteHead;
    // The merge succeeded — clear any prior dangling state. A subsequent
    // successful pull resets the engine so push() can resume even if the
    // current pull had to bypass the chain.
    m_danglingHead = false;
    m_remote->releaseLock();
    m_lastSyncTime = QDateTime::currentDateTimeUtc();
    emit lastSyncTimeChanged();
    setStatus(Synced);
    emit syncCompleted();
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

    if (m_pendingCommits.isEmpty()) return;

    // Iter-night-22: m_pendingCommits was just populated in UUID-alphabetic
    // order (QDir::Name on random v4 UUID filenames). That has nothing to
    // do with commit creation order, so the previous "last() is HEAD"
    // assignment was wrong with probability (N-1)/N — pinned by the test
    // slot `pending_commits_load_in_UUID_alphabetic_not_creation_order` in
    // test_syncengine_lifecycle.cpp.
    //
    // Consequences pre-fix:
    //   * `createCommit` stamps `parent_commit = m_headCommitId` — so a
    //     new commit created AFTER a crash-restart chains under an
    //     arbitrary pending commit, NOT the actual chain tail.
    //   * `push()` iterates m_pendingCommits in alphabetic order →
    //     uploads chain-out-of-order. Each upload advances
    //     `m_headCommitId = uploaded.uuid()`, so the FINAL remote HEAD
    //     points at the alphabetically-last UUID, which may not be the
    //     genuine chain tail. Snapshot compaction's ancestor walk from
    //     HEAD then misses the branches uploaded earlier — they become
    //     orphan log files on Drive, unreachable from HEAD, and peers
    //     pulling later see a corrupt chain.
    //
    // The correct chain tail is the unique commit whose UUID is NOT
    // listed as anyone's `parent_commit`. Walk the chain from the root
    // (the commit whose `parent_commit` is empty OR points outside the
    // pending set — i.e. already-pushed to remote) and topologically
    // sort. Use `Commit::timestamp()` to break ties on the rare case of
    // multiple tails (a forked local chain — shouldn't happen but the
    // load path can't assume input is clean).
    QHash<QString, Commit*> byUuid;
    byUuid.reserve(m_pendingCommits.size());
    QSet<QString> referencedAsParent;
    for (Commit *c : m_pendingCommits) {
        if (!c) continue;
        byUuid.insert(c->uuid(), c);
    }
    for (Commit *c : m_pendingCommits) {
        if (!c) continue;
        const QString p = c->parentCommit();
        if (!p.isEmpty() && byUuid.contains(p))
            referencedAsParent.insert(p);
    }

    // Find tails — commits whose UUID is not referenced as anyone's parent.
    QList<Commit*> tails;
    for (Commit *c : m_pendingCommits) {
        if (!c) continue;
        if (!referencedAsParent.contains(c->uuid()))
            tails.append(c);
    }

    Commit *tail = nullptr;
    if (tails.size() == 1) {
        tail = tails.first();
    } else if (tails.size() > 1) {
        // Multiple tails — local chain forked (corrupt state). Pick by
        // latest timestamp and log a warning so the diagnostic surface
        // exists if a user reports drift.
        tail = tails.first();
        for (Commit *c : tails) {
            if (c->timestamp() > tail->timestamp()) tail = c;
        }
        qWarning("SyncEngine: pending log has %lld tails — forked local "
                 "chain. Using latest-timestamp tail %s as HEAD; sync may "
                 "drop sibling branches.",
                 static_cast<long long>(tails.size()),
                 qPrintable(tail->uuid()));
    }
    // If tails is empty (cycle) fall through — m_headCommitId stays at the
    // pre-load value (typically the remote HEAD from settings).

    // Reorder m_pendingCommits to follow the chain root → tail so push()
    // uploads in chain order. Defence-in-depth: each commit's
    // parent_commit is already stamped correctly, so any order succeeds
    // on the upload side; ordered upload makes the remote chain
    // inspectable at every step and makes a partial-upload failure
    // recover predictably.
    if (tail) {
        QList<Commit*> ordered;
        ordered.reserve(m_pendingCommits.size());
        // Walk root → tail by following parent_commit backwards from
        // the tail, then reverse.
        QList<Commit*> reverseChain;
        QSet<QString> visited;
        Commit *cur = tail;
        while (cur && !visited.contains(cur->uuid())) {
            visited.insert(cur->uuid());
            reverseChain.append(cur);
            const QString p = cur->parentCommit();
            if (p.isEmpty()) break;
            auto it = byUuid.constFind(p);
            if (it == byUuid.constEnd()) break;  // points outside pending set
            cur = it.value();
        }
        for (int i = reverseChain.size() - 1; i >= 0; --i)
            ordered.append(reverseChain.at(i));

        // Any commits not in the chain (orphan branches in corrupt state)
        // come after, in their pre-existing alphabetic order. They'll
        // upload last so the linear chain reaches Drive first.
        QSet<Commit*> inChain(ordered.begin(), ordered.end());
        for (Commit *c : m_pendingCommits) {
            if (c && !inChain.contains(c))
                ordered.append(c);
        }
        m_pendingCommits = ordered;
        m_headCommitId = tail->uuid();
    } else {
        // No tail found (cycle or empty after filter). Fall back to the
        // historical behaviour so we don't regress healthy chains; log.
        m_headCommitId = m_pendingCommits.last()->uuid();
        qWarning("SyncEngine: could not determine pending log chain tail; "
                 "falling back to alphabetic last() = %s",
                 qPrintable(m_headCommitId));
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

// Walk the candidate parent's ancestry: true if reparenting `taskId` under
// `candidate` would create a subtask cycle (self-parent, or an ancestry chain
// that loops back to taskId). MAX_DEPTH bounds the walk so a pre-existing remote
// cycle can't infinite-loop this guard.
static bool wouldCreateParentCycle(DataStore *store, const QString &taskId,
                                   const QString &candidate)
{
    if (candidate == taskId)
        return true;
    if (candidate.isEmpty())
        return false;
    constexpr int MAX_DEPTH = 1000;
    QString cursor = candidate;
    for (int i = 0; i < MAX_DEPTH; ++i) {
        Task *ancestor = store->taskById(cursor);
        if (!ancestor) return false;        // chain ends at a missing task
        const QString next = ancestor->parentId();
        if (next.isEmpty()) return false;   // reached a root
        if (next == taskId) return true;    // chain returns to the reparented task
        if (next == cursor) return false;   // self-loop ancestor; stop walking
        cursor = next;
    }
    return false;
}

// Project-tree variant of the cycle guard (mirrors the task one but walks the
// project parentId graph).
static bool wouldCreateProjectParentCycle(DataStore *store, const QString &projectId,
                                          const QString &candidate)
{
    if (candidate == projectId)
        return true;
    if (candidate.isEmpty())
        return false;
    constexpr int MAX_DEPTH = 1000;
    QString cursor = candidate;
    for (int i = 0; i < MAX_DEPTH; ++i) {
        Project *ancestor = store->projectById(cursor);
        if (!ancestor) return false;
        const QString next = ancestor->parentId();
        if (next.isEmpty()) return false;
        if (next == projectId) return true;
        if (next == cursor) return false;
        cursor = next;
    }
    return false;
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
                QJsonObject patched = applyPatch(task->toJson(), op.data);
                // Q3.03-D: reject a parent_id that would create a subtask cycle,
                // but apply every other field. Dropping the key from the patch
                // leaves the task's current parent_id untouched.
                if (patched.contains(QStringLiteral("parent_id"))
                    && wouldCreateParentCycle(
                           m_store, op.entityId,
                           patched[QStringLiteral("parent_id")].toString())) {
                    qCWarning(lcSync)
                        << "applyCommit: rejected parent_id cycle for task"
                        << op.entityId << "→"
                        << patched[QStringLiteral("parent_id")].toString();
                    patched.remove(QStringLiteral("parent_id"));
                }
                // iter-6: re-hydrate the LIVE object via setters so each field's
                // NOTIFY signal fires (QML bindings + DataStore's relational
                // bucket cache observe every change) rather than relying on the
                // later loadFromJson to paper over a stale field.
                task->applyJson(patched);
                // Q2.18-A: push inbound per-field _meta back so per-field LWW
                // survives — the merger already determined the right stamps;
                // trust them (don't re-stamp with the local HLC).
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
                // Iter-7: guard inbound project parent_id (cycle + Inbox-stays-
                // root). Without it a peer commit creating A→B→A would overflow
                // the JS stack in the recursive sidebar walker (Q3.19) on expand.
                if (patched.contains(QStringLiteral("parent_id"))) {
                    const QString candidate =
                        patched[QStringLiteral("parent_id")].toString();
                    if (wouldCreateProjectParentCycle(m_store, op.entityId, candidate)) {
                        qCWarning(lcSync)
                            << "applyCommit: rejected project parent_id cycle for"
                            << op.entityId << "→" << candidate;
                    } else if (project->isInbox() && !candidate.isEmpty()) {
                        // Inbox must stay root — sidebar / inbox filter mode
                        // assume it lives at the top level.
                        qCWarning(lcSync)
                            << "applyCommit: rejected parent_id for Inbox project"
                            << op.entityId;
                    } else {
                        project->setParentId(candidate);
                    }
                }
                // All other fields via setters (signals fire). parent_id handled
                // above; Project::applyJson deliberately skips it.
                project->applyJson(patched);
                // Q2.18-A: push inbound _meta back (per-field LWW survives).
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
                // applyPatch preserves the inbound _meta so per-field CRDT stamps
                // survive (else the merger downgrades to whole-object LWW).
                const QJsonObject patched = applyPatch(section->toJson(), op.data);
                section->applyJson(patched);
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
                const QJsonObject patched = applyPatch(label->toJson(), op.data);
                // Iter-night-40: route renames through DataStore::renameLabel so
                // m_labelNameIndex AND every task's `labels` array stay consistent
                // (Label::applyJson deliberately skips name).
                if (patched.contains(QStringLiteral("name")))
                    m_store->renameLabel(op.entityId,
                        patched[QStringLiteral("name")].toString());
                label->applyJson(patched);
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
                const QJsonObject patched = applyPatch(comment->toJson(), op.data);
                comment->applyJson(patched);
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
                const QJsonObject patched = applyPatch(reminder->toJson(), op.data);
                reminder->applyJson(patched);
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
                const QJsonObject patched = applyPatch(filter->toJson(), op.data);
                filter->applyJson(patched);
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
