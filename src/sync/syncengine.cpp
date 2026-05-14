#include "syncengine.h"
#include "gdrive.h"
#include "merger.h"
#include "snapshot.h"
#include "hlc.h"
#include "storage/datastore.h"
#include "utils/uuid.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

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

    setStatus(Syncing);

    // Acquire lock
    if (!m_gdrive->acquireLock()) {
        setLastError(QStringLiteral("Could not acquire sync lock. Another device may be syncing."));
        setStatus(Error);
        return;
    }

    // Upload each pending commit (CRDT operation log).
    for (auto *commit : m_pendingCommits) {
        const QJsonDocument doc(commit->toJson());
        const QString fileName = QStringLiteral("log/%1.json").arg(commit->uuid());

        if (!m_gdrive->uploadFile(fileName, doc.toJson())) {
            setLastError(QStringLiteral("Failed to upload commit %1").arg(commit->uuid()));
            m_gdrive->releaseLock();
            setStatus(Error);
            return;
        }
        // B-2.48: advance m_headCommitId only after a successful upload.
        m_headCommitId = commit->uuid();
        m_snapshot->setHeadCommitId(m_headCommitId);
        m_snapshot->incrementCommitCount();
    }

    // Update HEAD pointer (no-op if there are no commits, but cheap).
    if (!m_headCommitId.isEmpty()) {
        QJsonObject headObj;
        headObj[QStringLiteral("commit")]    = m_headCommitId;
        headObj[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QJsonDocument headDoc(headObj);
        m_gdrive->uploadFile(QStringLiteral("HEAD"), headDoc.toJson());
    }

    // Always upload the current state snapshot. This is the main payload
    // for the user — without it the previous early-return ("no pending
    // commits → exit") meant state.json never reached Drive even though
    // the user clicked Sync, so nothing was visible on Google Drive.
    const QJsonDocument stateDoc(m_store->toJson());
    if (!m_gdrive->uploadFile(QStringLiteral("state.json"), stateDoc.toJson())) {
        setLastError(QStringLiteral("Failed to upload state.json"));
        m_gdrive->releaseLock();
        setStatus(Error);
        return;
    }

    // X-2.10: create snapshot BEFORE releasing lock so peers can't race in.
    if (m_snapshot->shouldCompact()) {
        m_snapshot->createSnapshot();
    }

    // Clear pending commits
    for (auto *commit : m_pendingCommits) {
        const QString filePath = pendingCommitsPath() + QStringLiteral("/") + commit->uuid() + QStringLiteral(".json");
        QFile::remove(filePath);
    }
    qDeleteAll(m_pendingCommits);
    m_pendingCommits.clear();

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
    QByteArray headData;
    const bool headOk = m_gdrive->downloadFile(QStringLiteral("HEAD"), headData,
                                                /*notFoundIsOk=*/true);
    if (!headOk) {
        // Real network error — do not proceed.
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
    const QStringList logFiles = m_gdrive->listFiles(QStringLiteral("log"));

    // Parse all log commits and chain-sort them.
    QList<Commit *> remoteCommits;
    for (const QString &fileName : logFiles) {
        if (!fileName.endsWith(QStringLiteral(".json"))) continue;
        QByteArray commitData;
        if (!m_gdrive->downloadFile(QStringLiteral("log/") + fileName, commitData))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(commitData);
        if (!doc.isObject()) continue;
        auto *c = Commit::fromJson(doc.object(), this);
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

    // Apply commits that are newer than our current local head.
    QString lastApplied = m_headCommitId;
    for (auto *c : remoteCommits) {
        // Skip commits already applied locally.
        if (c->uuid() == m_headCommitId) continue;
        // Skip commits whose parent we haven't seen (gap in chain): still apply
        // them to be safe — full merge will reconcile.
        applyCommit(c);
        lastApplied = c->uuid();
    }

    // Download remote snapshot and merge / replace.
    QByteArray stateData;
    if (!m_gdrive->downloadFile(QStringLiteral("state.json"), stateData)) {
        setLastError(QStringLiteral("Failed to download remote state"));
        // Clean up temporary commit objects.
        qDeleteAll(remoteCommits);
        m_gdrive->releaseLock();
        setStatus(Error);
        return;
    }

    const QJsonDocument stateDoc = QJsonDocument::fromJson(stateData);
    const QJsonObject remoteState = stateDoc.object();

    // B-2.42: validate remote state before loading — an empty or structurally
    // invalid JSON must never clear user data.
    // (QJsonDocument::fromJson returns invalid doc on parse error → .object()
    //  yields an empty QJsonObject, so isEmpty() covers both.)
    if (remoteState.isEmpty() || !remoteState.contains(QStringLiteral("tasks"))) {
        qCWarning(lcSync) << "pull(): remote state.json is invalid or empty — aborting pull";
        qDeleteAll(remoteCommits);
        m_gdrive->releaseLock();
        setLastError(QStringLiteral("Remote state.json is invalid (missing 'tasks'). Pull aborted."));
        setStatus(Error);
        return;
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
                if (patched.contains(QStringLiteral("parent_id")))
                    task->setParentId(patched[QStringLiteral("parent_id")].toString());
                if (patched.contains(QStringLiteral("order")))
                    task->setOrder(patched[QStringLiteral("order")].toInt());
                if (patched.contains(QStringLiteral("day_order")))
                    task->setDayOrder(patched[QStringLiteral("day_order")].toInt());
                if (patched.contains(QStringLiteral("is_pinned")))
                    task->setIsPinned(patched[QStringLiteral("is_pinned")].toBool());
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
