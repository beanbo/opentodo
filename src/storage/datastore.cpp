#include "datastore.h"
#include "utils/datetime.h"
#include "sync/crdt_meta.h"
#include "sync/hlc.h"
#include "utils/textsearch.h"
#include "utils/uuid.h"

#include <QSettings>
#include <mutex>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>

DataStore::DataStore(QObject *parent)
    : QObject(parent)
{
}

// ── CRDT helpers ─────────────────────────────────────────────────────────────

QString DataStore::localDeviceId()
{
    // Cache the device ID so we don't hit QSettings on every CRDT stamp.
    // std::call_once guarantees this runs exactly once even under concurrent
    // access (e.g. sync thread stamping fields simultaneously).
    static QString s_cachedDeviceId;
    static std::once_flag s_initFlag;
    std::call_once(s_initFlag, []() {
        QSettings settings;
        QString id = settings.value(QStringLiteral("sync/device_id")).toString();
        if (id.isEmpty()) {
            id = UuidHelper::generate();
            settings.setValue(QStringLiteral("sync/device_id"), id);
        }
        s_cachedDeviceId = id;
    });
    return s_cachedDeviceId;
}

void DataStore::stampEntityField(const QString &entityId,
                                  QMap<QString, QJsonObject> &metaMap,
                                  const QString &fieldName)
{
    const QString devId = localDeviceId();
    const QString ts    = HybridLogicalClock::now(devId);

    // metaMap stores the NAKED per-entity field-stamp map — i.e. the value
    // that ends up under entity["_meta"] on disk. So write the stamp directly
    // into this map, NOT via CrdtMeta::setFieldMeta (which expects a full
    // entity object and would write meta["_meta"][field], producing a
    // double-wrap once injectMeta puts another "_meta" key on top).
    QJsonObject meta = metaMap.value(entityId);
    QJsonObject fm;
    fm[QStringLiteral("ts")]  = ts;
    fm[QStringLiteral("dev")] = devId;
    meta[fieldName] = fm;
    metaMap.insert(entityId, meta);
}

// static
QJsonObject DataStore::injectMeta(QJsonObject entityJson,
                                   const QMap<QString, QJsonObject> &metaMap,
                                   const QString &entityId)
{
    const QJsonObject meta = metaMap.value(entityId);
    if (!meta.isEmpty())
        entityJson[QStringLiteral("_meta")] = meta;
    return entityJson;
}

// static
QJsonObject DataStore::extractMeta(QJsonObject entityJson,
                                    QMap<QString, QJsonObject> &metaMap,
                                    const QString &entityId)
{
    if (entityJson.contains(QStringLiteral("_meta"))) {
        QJsonObject meta = entityJson[QStringLiteral("_meta")].toObject();

        // Backwards-compat: legacy state.json files (schema v1) stored the
        // per-entity meta as entity["_meta"]["_meta"][field] = {ts, dev}
        // — a double-wrap caused by an older bug in stampEntityField.
        // Detect and flatten: a real (single-wrap) field-stamp entry has
        // both "ts" and "dev" keys; a legacy outer wrapper holds exactly
        // one key called "_meta" whose value is itself an object. Use the
        // INNER object so downstream CrdtMeta lookups (which expect
        // entity["_meta"][field]) find the stamps.
        // The schema_version migration in migrateJson sets v1->v2 once;
        // this guard catches any per-entity blob that still looks legacy
        // even on a "current" v2 file (paranoid defence-in-depth).
        if (meta.size() == 1 && meta.contains(QStringLiteral("_meta"))) {
            const QJsonValue inner = meta.value(QStringLiteral("_meta"));
            if (inner.isObject())
                meta = inner.toObject();
        }

        metaMap.insert(entityId, meta);
        entityJson.remove(QStringLiteral("_meta"));
    }
    return entityJson;
}

// static
void DataStore::stampField(QJsonObject &entity, const QString &fieldName)
{
    const QString devId = localDeviceId();
    const QString ts    = HybridLogicalClock::now(devId);
    CrdtMeta::setFieldMeta(entity, fieldName, ts, devId);
}

// ── Modification notifications ────────────────────────────────────────────────

void DataStore::markTaskModified(const QString &id)
{
    if (!m_taskIndex.contains(id)) return;
    // Stamp updated_at to record that something changed on this device.
    stampEntityField(id, m_taskMeta, QStringLiteral("updated_at"));
    emit tasksChanged();
    emit dataChanged();
}

void DataStore::markProjectModified(const QString &id)
{
    if (!m_projectIndex.contains(id)) return;
    stampEntityField(id, m_projectMeta, QStringLiteral("updated_at"));
    emit projectsChanged();
    emit dataChanged();
}

void DataStore::markLabelModified(const QString &id)
{
    if (!m_labelIndex.contains(id)) return;
    stampEntityField(id, m_labelMeta, QStringLiteral("updated_at"));
    emit labelsChanged();
    emit dataChanged();
}

void DataStore::markSectionModified(const QString &id)
{
    if (!m_sectionIndex.contains(id)) return;
    stampEntityField(id, m_sectionMeta, QStringLiteral("updated_at"));
    emit sectionsChanged();
    emit dataChanged();
}

void DataStore::markTaskFieldsModified(const QString &id, const QStringList &fields)
{
    if (!m_taskIndex.contains(id)) return;
    for (const QString &f : fields)
        stampEntityField(id, m_taskMeta, f);
    stampEntityField(id, m_taskMeta, QStringLiteral("updated_at"));
    emit tasksChanged();
    emit dataChanged();
}

void DataStore::markProjectFieldsModified(const QString &id, const QStringList &fields)
{
    if (!m_projectIndex.contains(id)) return;
    for (const QString &f : fields)
        stampEntityField(id, m_projectMeta, f);
    stampEntityField(id, m_projectMeta, QStringLiteral("updated_at"));
    emit projectsChanged();
    emit dataChanged();
}

void DataStore::markLabelFieldsModified(const QString &id, const QStringList &fields)
{
    if (!m_labelIndex.contains(id)) return;
    for (const QString &f : fields)
        stampEntityField(id, m_labelMeta, f);
    stampEntityField(id, m_labelMeta, QStringLiteral("updated_at"));
    emit labelsChanged();
    emit dataChanged();
}

void DataStore::markFilterFieldsModified(const QString &id, const QStringList &fields)
{
    if (!m_filterIndex.contains(id)) return;
    for (const QString &f : fields)
        stampEntityField(id, m_filterMeta, f);
    stampEntityField(id, m_filterMeta, QStringLiteral("updated_at"));
    emit filtersChanged();
    emit dataChanged();
}

void DataStore::markReminderFieldsModified(const QString &id, const QStringList &fields)
{
    if (!m_reminderIndex.contains(id)) return;
    for (const QString &f : fields)
        stampEntityField(id, m_reminderMeta, f);
    stampEntityField(id, m_reminderMeta, QStringLiteral("updated_at"));
    emit remindersChanged();
    emit dataChanged();
}

// ── Inbound CRDT meta application (Q2.18-A) ──────────────────────────────────
//
// Per-field merge of an inbound _meta blob into a local entity meta map.
// Fields present in `incomingMeta` overwrite the local stamp; fields absent
// are left intact. No-op for unknown entity ids. The aggregate notification
// signals are emitted by the caller (see applyInbound*Meta dispatchers below)
// so the same helper drives every entity type.
namespace {
void mergeIncomingMeta(const QString &id,
                       QMap<QString, QJsonObject> &metaMap,
                       const QJsonObject &incomingMeta)
{
    if (incomingMeta.isEmpty()) return;
    QJsonObject meta = metaMap.value(id);
    for (auto it = incomingMeta.begin(); it != incomingMeta.end(); ++it)
        meta.insert(it.key(), it.value());
    metaMap.insert(id, meta);
}
} // namespace

void DataStore::applyInboundTaskMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_taskIndex.contains(id)) return;
    mergeIncomingMeta(id, m_taskMeta, incomingMeta);
    emit tasksChanged();
    emit dataChanged();
}

void DataStore::applyInboundProjectMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_projectIndex.contains(id)) return;
    mergeIncomingMeta(id, m_projectMeta, incomingMeta);
    emit projectsChanged();
    emit dataChanged();
}

void DataStore::applyInboundSectionMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_sectionIndex.contains(id)) return;
    mergeIncomingMeta(id, m_sectionMeta, incomingMeta);
    emit sectionsChanged();
    emit dataChanged();
}

void DataStore::applyInboundLabelMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_labelIndex.contains(id)) return;
    mergeIncomingMeta(id, m_labelMeta, incomingMeta);
    emit labelsChanged();
    emit dataChanged();
}

void DataStore::applyInboundFilterMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_filterIndex.contains(id)) return;
    mergeIncomingMeta(id, m_filterMeta, incomingMeta);
    emit filtersChanged();
    emit dataChanged();
}

void DataStore::applyInboundReminderMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_reminderIndex.contains(id)) return;
    mergeIncomingMeta(id, m_reminderMeta, incomingMeta);
    emit remindersChanged();
    emit dataChanged();
}

void DataStore::applyInboundCommentMeta(const QString &id, const QJsonObject &incomingMeta)
{
    if (!m_commentIndex.contains(id)) return;
    mergeIncomingMeta(id, m_commentMeta, incomingMeta);
    emit commentsChanged();
    emit dataChanged();
}

// ===================== TASKS =====================

Task *DataStore::addTask(const QString &content, const QString &projectId)
{
    auto *task = new Task(this);
    task->setContent(content);

    // B-1.3: null-check inboxProject() before calling id() on it.
    if (projectId.isEmpty()) {
        Project *inbox = inboxProject();
        if (inbox)
            task->setProjectId(inbox->id());
        // If no inbox exists yet, leave projectId empty — caller must fix up.
    } else {
        task->setProjectId(projectId);
    }

    // B-1.61: count only non-deleted tasks for a meaningful order value.
    int activeCount = 0;
    for (auto *t : m_tasks) {
        if (!t->isDeleted()) ++activeCount;
    }
    task->setOrder(activeCount);

    m_tasks.append(task);
    m_taskIndex.insert(task->id(), task);

    // Stamp initial scalar fields for CRDT tracking.
    const QString id = task->id();
    const QStringList initialFields = {
        QStringLiteral("content"), QStringLiteral("project_id"),
        QStringLiteral("order"),   QStringLiteral("priority"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_completed"), QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_taskMeta, f);

    emit tasksChanged();
    emit dataChanged();
    return task;
}

Task *DataStore::taskById(const QString &id) const
{
    return m_taskIndex.value(id, nullptr);
}

bool DataStore::removeTask(const QString &id)
{
    auto *task = m_taskIndex.value(id);
    if (!task) return false;

    const QDateTime now = QDateTime::currentDateTimeUtc();

    // iter-night-4: cascade tombstone the entire descendant subtree, not just
    // direct children. Previously only depth-1 was cascaded; grandchildren
    // (subtasks of subtasks) stayed live with parentId pointing at a now-
    // tombstoned parent. Their reminders kept firing OS-toasts for tasks the
    // user thought were deleted; their entries became orphans in any tree
    // walker that started from the original root. Walk the parentId graph
    // with a visited set so a pre-existing cycle (defensively possible —
    // applyCommit has a guard but legacy state.json files may carry one)
    // can't loop forever.
    QSet<QString> cascadeIds;
    cascadeIds.insert(id);
    QStringList frontier{id};
    while (!frontier.isEmpty()) {
        const QString parentId = frontier.takeFirst();
        for (auto *t : m_tasks) {
            if (!t || t->isDeleted()) continue;
            if (t->parentId() != parentId) continue;
            if (cascadeIds.contains(t->id())) continue; // cycle defence
            cascadeIds.insert(t->id());
            frontier.append(t->id());
        }
    }

    // Tombstone every task in the cascade set with the SAME `now` timestamp.
    // restoreTask relies on updatedAt == cascadeStamp equality to identify
    // descendants it should bring back together with the root, so they MUST
    // share one timestamp.
    for (const QString &tid : std::as_const(cascadeIds)) {
        Task *t = m_taskIndex.value(tid);
        if (!t || t->isDeleted()) continue;
        t->setIsDeleted(true);
        t->setUpdatedAt(now);
        stampEntityField(tid, m_taskMeta, QStringLiteral("is_deleted"));
        stampEntityField(tid, m_taskMeta, QStringLiteral("updated_at"));
    }

    // Tombstone reminders + comments for every task in the cascade set.
    // Single linear pass over each store rather than nesting (was O(N×M)
    // before, now O(N+M+M) once cascadeIds is built).
    for (auto *r : m_reminders) {
        if (!r || r->isDeleted()) continue;
        if (!cascadeIds.contains(r->taskId())) continue;
        const QString rid = r->id();
        r->setIsDeleted(true);
        r->setUpdatedAt(now);
        stampEntityField(rid, m_reminderMeta, QStringLiteral("is_deleted"));
        stampEntityField(rid, m_reminderMeta, QStringLiteral("updated_at"));
    }
    for (auto *c : m_comments) {
        if (!c || c->isDeleted()) continue;
        if (!cascadeIds.contains(c->taskId())) continue;
        const QString cid = c->id();
        c->setIsDeleted(true);
        c->setUpdatedAt(now);
        stampEntityField(cid, m_commentMeta, QStringLiteral("is_deleted"));
        stampEntityField(cid, m_commentMeta, QStringLiteral("updated_at"));
    }

    emit tasksChanged();
    emit remindersChanged();
    emit commentsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::restoreTask(const QString &id)
{
    auto *task = m_taskIndex.value(id);
    if (!task) return false;

    // Capture the cascade-tombstone timestamp BEFORE we mutate the task.
    // removeTask() set task->updatedAt() to the cascade `now` and copied that
    // same instant onto every reminder/comment it tombstoned. So we can
    // identify "was cascaded by this delete" by exact updatedAt equality.
    //
    // Backward-compat note: legacy state.json files written before this
    // restore-cascade existed may have updatedAt millisecond drift between
    // the task and its cascaded children (different `now` per setter call in
    // older builds). For those, the equality test fails and the children
    // stay tombstoned — which matches the historical behaviour, so no
    // regression. Only state.json files written by the current removeTask
    // (which uses a single shared `now`) get full cascade-restore.
    const QDateTime cascadeStamp = task->updatedAt();

    const QDateTime now = QDateTime::currentDateTimeUtc();

    task->setIsDeleted(false);
    task->setUpdatedAt(now);
    stampEntityField(id, m_taskMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_taskMeta, QStringLiteral("updated_at"));

    // Cascade-restore comments tombstoned by the same removeTask call.
    bool anyCommentRestored = false;
    for (auto *c : m_comments) {
        if (c && c->isDeleted() && c->taskId() == id
            && c->updatedAt() == cascadeStamp) {
            const QString cid = c->id();
            c->setIsDeleted(false);
            c->setUpdatedAt(now);
            stampEntityField(cid, m_commentMeta, QStringLiteral("is_deleted"));
            stampEntityField(cid, m_commentMeta, QStringLiteral("updated_at"));
            anyCommentRestored = true;
        }
    }

    // Cascade-restore reminders tombstoned by the same removeTask call.
    bool anyReminderRestored = false;
    for (auto *r : m_reminders) {
        if (r && r->isDeleted() && r->taskId() == id
            && r->updatedAt() == cascadeStamp) {
            const QString rid = r->id();
            r->setIsDeleted(false);
            r->setUpdatedAt(now);
            stampEntityField(rid, m_reminderMeta, QStringLiteral("is_deleted"));
            stampEntityField(rid, m_reminderMeta, QStringLiteral("updated_at"));
            anyReminderRestored = true;
        }
    }

    // iter-night-4: cascade-restore the entire descendant subtree (depth-N),
    // mirroring the now-recursive removeTask. We find every descendant by
    // walking parentId graph, but ONLY restore those whose updatedAt matches
    // cascadeStamp — that's how we identify "tombstoned by THIS removeTask
    // call" vs "tombstoned independently before/after". Legacy state.json
    // files written by the old depth-1 removeTask will have grandchildren
    // with different timestamps and stay tombstoned — matches old behaviour,
    // no regression.
    QSet<QString> cascadeIds;
    cascadeIds.insert(id);
    QStringList frontier{id};
    while (!frontier.isEmpty()) {
        const QString parentId = frontier.takeFirst();
        for (auto *t : m_tasks) {
            if (!t || !t->isDeleted()) continue;
            if (t->parentId() != parentId) continue;
            if (t->updatedAt() != cascadeStamp) continue;
            if (cascadeIds.contains(t->id())) continue; // cycle defence
            cascadeIds.insert(t->id());
            frontier.append(t->id());
        }
    }

    // Restore every descendant task (id itself already handled above).
    for (const QString &tid : std::as_const(cascadeIds)) {
        if (tid == id) continue;
        Task *t = m_taskIndex.value(tid);
        if (!t || !t->isDeleted()) continue;
        t->setIsDeleted(false);
        t->setUpdatedAt(now);
        stampEntityField(tid, m_taskMeta, QStringLiteral("is_deleted"));
        stampEntityField(tid, m_taskMeta, QStringLiteral("updated_at"));
    }

    // Restore descendant reminders / comments that match the cascade stamp.
    for (auto *r : m_reminders) {
        if (!r || !r->isDeleted()) continue;
        if (r->updatedAt() != cascadeStamp) continue;
        if (!cascadeIds.contains(r->taskId())) continue;
        if (r->taskId() == id) continue; // already handled at the top
        const QString rid = r->id();
        r->setIsDeleted(false);
        r->setUpdatedAt(now);
        stampEntityField(rid, m_reminderMeta, QStringLiteral("is_deleted"));
        stampEntityField(rid, m_reminderMeta, QStringLiteral("updated_at"));
        anyReminderRestored = true;
    }
    for (auto *c : m_comments) {
        if (!c || !c->isDeleted()) continue;
        if (c->updatedAt() != cascadeStamp) continue;
        if (!cascadeIds.contains(c->taskId())) continue;
        if (c->taskId() == id) continue;
        const QString cid = c->id();
        c->setIsDeleted(false);
        c->setUpdatedAt(now);
        stampEntityField(cid, m_commentMeta, QStringLiteral("is_deleted"));
        stampEntityField(cid, m_commentMeta, QStringLiteral("updated_at"));
        anyCommentRestored = true;
    }

    emit tasksChanged();
    if (anyCommentRestored) emit commentsChanged();
    if (anyReminderRestored) emit remindersChanged();
    emit dataChanged();
    return true;
}

bool DataStore::purgeTask(const QString &id)
{
    Task *task = m_taskIndex.value(id, nullptr);
    if (!task) return false;

    // Erase from list and index. Also drop the per-entity CRDT meta blob so
    // the purge does not leak a stale _meta entry into the next state.json.
    m_tasks.removeAll(task);
    m_taskIndex.remove(id);
    m_taskMeta.remove(id);

    // Hard-purge dependent reminders/comments too — they reference a task id
    // that no longer exists. Their soft-delete tombstones are already in the
    // sync log; further references aren't useful locally.
    for (int i = m_reminders.size() - 1; i >= 0; --i) {
        Reminder *r = m_reminders.at(i);
        if (r && r->taskId() == id) {
            m_reminderIndex.remove(r->id());
            m_reminderMeta.remove(r->id());
            m_reminders.removeAt(i);
            r->deleteLater();
        }
    }
    for (int i = m_comments.size() - 1; i >= 0; --i) {
        Comment *c = m_comments.at(i);
        if (c && c->taskId() == id) {
            m_commentIndex.remove(c->id());
            m_commentMeta.remove(c->id());
            m_comments.removeAt(i);
            c->deleteLater();
        }
    }

    task->deleteLater();

    emit tasksChanged();
    emit remindersChanged();
    emit commentsChanged();
    emit dataChanged();
    return true;
}

QList<Task *> DataStore::allTasksIncludingDeleted() const
{
    return m_tasks;
}

QList<Task *> DataStore::allTasks() const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (!t->isDeleted()) result.append(t);
    }
    return result;
}

QList<Task *> DataStore::tasksForProject(const QString &projectId) const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && t->projectId() == projectId && t->parentId().isEmpty())
            result.append(t);
    }
    return result;
}

QList<Task *> DataStore::tasksForSection(const QString &sectionId) const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && t->sectionId() == sectionId)
            result.append(t);
    }
    return result;
}

QList<Task *> DataStore::tasksForToday() const
{
    QList<Task *> result;
    const QDate today = QDate::currentDate();
    const QString todayStr = DateTimeHelper::toDateString(today);
    for (auto *t : m_tasks) {
        if (t->isDeleted() || t->isCompleted()) continue;

        // B-1.30: include tasks with dueDate == today OR dueDatetime whose
        // date component == today (datetime-only tasks have no dueDate set).
        if (t->dueDate() == todayStr) {
            result.append(t);
        } else if (t->dueDate().isEmpty() && !t->dueDatetime().isEmpty()) {
            const QDateTime dt = DateTimeHelper::fromIsoString(t->dueDatetime());
            if (dt.isValid() && dt.date() == today)
                result.append(t);
        }
    }
    return result;
}

QList<Task *> DataStore::overdueTasks() const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (t->isDeleted() || t->isCompleted()) continue;

        // B-1.28: also catch datetime-only tasks (dueDate empty, dueDatetime set).
        if (DateTimeHelper::isOverdue(t->dueDate())) {
            result.append(t);
        } else if (t->dueDate().isEmpty() && !t->dueDatetime().isEmpty()) {
            const QDateTime dt = DateTimeHelper::fromIsoString(t->dueDatetime());
            if (dt.isValid() && dt < QDateTime::currentDateTime())
                result.append(t);
        }
    }
    return result;
}

QList<Task *> DataStore::completedTasks() const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && t->isCompleted())
            result.append(t);
    }
    return result;
}

QList<Task *> DataStore::subtasks(const QString &parentId) const
{
    QList<Task *> result;
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && t->parentId() == parentId)
            result.append(t);
    }
    return result;
}

QList<Task *> DataStore::searchTasks(const QString &query,
                                     bool accentSensitive) const
{
    QList<Task *> result;
    if (query.isEmpty()) return result;

    // Pre-normalize the needle once — hoists the NFKC/Mn/casefold work out of
    // the per-task loop. For 10k tasks this is the difference between O(N+M)
    // and O(N*M) normalization passes.
    const bool stripDiacritics = !accentSensitive;
    const QString normalizedQuery =
        TextSearch::normalize(query, stripDiacritics);
    if (normalizedQuery.isEmpty()) return result; // query was pure diacritics

    for (auto *t : m_tasks) {
        if (!t->isDeleted() &&
            (TextSearch::containsNormalized(t->content(), normalizedQuery,
                                            stripDiacritics) ||
             TextSearch::containsNormalized(t->description(), normalizedQuery,
                                            stripDiacritics)))
            result.append(t);
    }
    return result;
}

int DataStore::taskCount() const
{
    int count = 0;
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && !t->isCompleted()) ++count;
    }
    return count;
}

void DataStore::addExistingTask(Task *task)
{
    task->setParent(this);
    m_tasks.append(task);
    m_taskIndex.insert(task->id(), task);
}

// ===================== PROJECTS =====================

Project *DataStore::addProject(const QString &name)
{
    auto *project = new Project(this);
    project->setName(name);

    // B-1.61: use active (non-deleted) count for order.
    int activeCount = 0;
    for (auto *p : m_projects) {
        if (!p->isDeleted()) ++activeCount;
    }
    project->setOrder(activeCount);

    // B-1.55: enforce inbox uniqueness — if a project claims is_inbox but one
    // already exists, demote the new one.
    if (project->isInbox() && inboxProject() != nullptr)
        project->setIsInbox(false);

    m_projects.append(project);
    m_projectIndex.insert(project->id(), project);

    const QString id = project->id();
    const QStringList initialFields = {
        QStringLiteral("name"),   QStringLiteral("color"),
        QStringLiteral("order"),  QStringLiteral("is_favorite"),
        QStringLiteral("is_inbox"), QStringLiteral("is_archived"),
        QStringLiteral("view_style"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_projectMeta, f);

    emit projectsChanged();
    emit dataChanged();
    return project;
}

Project *DataStore::projectById(const QString &id) const
{
    return m_projectIndex.value(id, nullptr);
}

bool DataStore::removeProject(const QString &id)
{
    auto *project = m_projectIndex.value(id);
    if (!project || project->isInbox()) return false;

    const QDateTime now = QDateTime::currentDateTimeUtc();

    project->setIsDeleted(true);
    project->setUpdatedAt(now);
    stampEntityField(id, m_projectMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_projectMeta, QStringLiteral("updated_at"));

    // B-1.7: cascade — tombstone all sections belonging to this project.
    for (auto *s : m_sections) {
        if (!s->isDeleted() && s->projectId() == id) {
            const QString sid = s->id();
            s->setIsDeleted(true);
            s->setUpdatedAt(now);
            stampEntityField(sid, m_sectionMeta, QStringLiteral("is_deleted"));
            stampEntityField(sid, m_sectionMeta, QStringLiteral("updated_at"));
        }
    }

    // Cascade — tombstone all tasks (and their reminders/comments) in project.
    for (auto *t : m_tasks) {
        if (!t->isDeleted() && t->projectId() == id) {
            const QString tid = t->id();
            t->setIsDeleted(true);
            t->setUpdatedAt(now);
            stampEntityField(tid, m_taskMeta, QStringLiteral("is_deleted"));
            stampEntityField(tid, m_taskMeta, QStringLiteral("updated_at"));

            for (auto *r : m_reminders) {
                if (!r->isDeleted() && r->taskId() == tid) {
                    const QString rid = r->id();
                    r->setIsDeleted(true);
                    r->setUpdatedAt(now);
                    stampEntityField(rid, m_reminderMeta, QStringLiteral("is_deleted"));
                    stampEntityField(rid, m_reminderMeta, QStringLiteral("updated_at"));
                }
            }
            for (auto *c : m_comments) {
                if (!c->isDeleted() && c->taskId() == tid) {
                    const QString cid = c->id();
                    c->setIsDeleted(true);
                    c->setUpdatedAt(now);
                    stampEntityField(cid, m_commentMeta, QStringLiteral("is_deleted"));
                    stampEntityField(cid, m_commentMeta, QStringLiteral("updated_at"));
                }
            }
        }
    }

    // Cascade — tombstone project-level comments.
    for (auto *c : m_comments) {
        if (!c->isDeleted() && c->projectId() == id) {
            const QString cid = c->id();
            c->setIsDeleted(true);
            c->setUpdatedAt(now);
            stampEntityField(cid, m_commentMeta, QStringLiteral("is_deleted"));
            stampEntityField(cid, m_commentMeta, QStringLiteral("updated_at"));
        }
    }

    // ITER4 #1: cascade — tombstone child subprojects (and recursively their
    // children, tasks, sections, reminders, comments). Without this an orphan
    // subproject pointing at a tombstoned parent stays visible in the sidebar
    // and never reconciles away on sync.
    // Snapshot the child id list first because removeProject mutates m_projects.
    QStringList childIds;
    for (auto *child : m_projects) {
        if (!child->isDeleted() && child->parentId() == id)
            childIds << child->id();
    }
    for (const QString &childId : childIds)
        removeProject(childId);   // recursive — handles grandchildren too

    emit projectsChanged();
    emit sectionsChanged();
    emit tasksChanged();
    emit remindersChanged();
    emit commentsChanged();
    emit dataChanged();
    return true;
}

QList<Project *> DataStore::allProjects() const
{
    QList<Project *> result;
    for (auto *p : m_projects) {
        if (!p->isDeleted()) result.append(p);
    }
    return result;
}

Project *DataStore::inboxProject() const
{
    for (auto *p : m_projects) {
        if (p->isInbox() && !p->isDeleted()) return p;
    }
    return nullptr;
}

int DataStore::projectCount() const
{
    int count = 0;
    for (auto *p : m_projects) {
        if (!p->isDeleted()) ++count;
    }
    return count;
}

void DataStore::addExistingProject(Project *project)
{
    project->setParent(this);

    // B-1.55: enforce inbox uniqueness when injecting from sync.
    if (project->isInbox() && inboxProject() != nullptr)
        project->setIsInbox(false);

    m_projects.append(project);
    m_projectIndex.insert(project->id(), project);
}

// ===================== AUTO-ARCHIVE =====================

// Driven by the "auto_archive_old_projects_days" setting (default 0 = disabled).
// User-facing control: SpinBox in SettingsSectionGeneral.qml; trigger point:
// FileManager::load() reads the setting and calls this on every app start.
int DataStore::autoArchiveStaleProjects(int daysThreshold)
{
    if (daysThreshold <= 0) return 0;

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-daysThreshold);
    int archivedCount = 0;

    for (Project *p : m_projects) {
        if (!p) continue;
        if (p->isDeleted() || p->isArchived()) continue;
        if (p->isInbox()) continue;

        // Collect all non-deleted tasks for this project (top-level + subtasks).
        QList<const Task *> projectTasks;
        for (const Task *t : m_tasks) {
            if (!t->isDeleted() && t->projectId() == p->id())
                projectTasks.append(t);
        }

        // An empty project has no activity to measure — skip it so we don't
        // silently archive a project the user just created with no tasks yet.
        if (projectTasks.isEmpty()) continue;

        bool allCompleted = true;
        QDateTime mostRecent;
        for (const Task *t : projectTasks) {
            if (!t->isCompleted()) {
                allCompleted = false;
                break;
            }
            // Prefer completedAt when valid (most precise), fall back to updatedAt.
            QDateTime ts = t->completedAt();
            if (!ts.isValid())
                ts = t->updatedAt();
            if (!ts.isValid()) continue;
            if (!mostRecent.isValid() || ts > mostRecent)
                mostRecent = ts;
        }

        if (!allCompleted) continue;
        if (!mostRecent.isValid()) continue;
        if (mostRecent >= cutoff) continue;   // recent enough — keep

        // Stale: archive in-place (soft mutation, no cascade).
        const QString pid = p->id();
        p->setIsArchived(true);
        p->setUpdatedAt(QDateTime::currentDateTimeUtc());
        stampEntityField(pid, m_projectMeta, QStringLiteral("is_archived"));
        stampEntityField(pid, m_projectMeta, QStringLiteral("updated_at"));
        archivedCount++;

        qInfo("DataStore: auto-archived stale project '%s' (last activity: %s)",
              qPrintable(p->name()),
              qPrintable(mostRecent.toString(Qt::ISODate)));
    }

    if (archivedCount > 0) {
        emit projectsChanged();
        emit dataChanged();
    }
    return archivedCount;
}

// ===================== VACUUM (Q3.10) =====================

// Hard-purge tombstones whose updated_at is older than `daysToKeep` days.
// Convenience overload — does not consult the sync layer for pending commits;
// callers wiring this from main.cpp should prefer the 2-arg overload so
// unsynced tombstones survive.
int DataStore::vacuum(int daysToKeep)
{
    return vacuum(daysToKeep, QStringList{});
}

int DataStore::vacuum(int daysToKeep, const QStringList &pendingEntityIds)
{
    // Defensive: refuse a degenerate threshold. We never want to nuke
    // freshly-created tombstones that may still be in flight to peers.
    if (daysToKeep < 1) daysToKeep = 1;

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-daysToKeep);
    // O(1) membership check for entity ids referenced by pending commits.
    const QSet<QString> pending = QSet<QString>(pendingEntityIds.cbegin(),
                                                pendingEntityIds.cend());
    int removed = 0;

    // Generic walker — captures the pattern used for every entity list:
    //   1. iterate from the back so removeAt(i) doesn't invalidate i+1
    //   2. skip non-tombstones (is_deleted == false)
    //   3. skip tombstones younger than cutoff
    //   4. skip tombstones referenced by a pending commit (sync canary)
    //   5. erase from list, index, meta map; deleteLater the QObject
    auto walk = [&](auto &list, auto &index, QMap<QString, QJsonObject> &meta) {
        for (int i = list.size() - 1; i >= 0; --i) {
            auto *e = list.at(i);
            if (!e) continue;
            if (!e->isDeleted()) continue;
            const QDateTime ts = e->updatedAt();
            // Guard: if updated_at is invalid (e.g. malformed legacy entry)
            // err on the safe side and keep the entry.
            if (!ts.isValid()) continue;
            if (ts >= cutoff) continue;
            if (pending.contains(e->id())) continue;
            const QString id = e->id();
            index.remove(id);
            meta.remove(id);
            list.removeAt(i);
            e->deleteLater();
            ++removed;
        }
    };

    walk(m_tasks,     m_taskIndex,     m_taskMeta);
    walk(m_projects,  m_projectIndex,  m_projectMeta);
    walk(m_sections,  m_sectionIndex,  m_sectionMeta);
    walk(m_filters,   m_filterIndex,   m_filterMeta);
    walk(m_reminders, m_reminderIndex, m_reminderMeta);
    walk(m_comments,  m_commentIndex,  m_commentMeta);

    // Label has no updated_at field (the model is intentionally minimal —
    // id/name/color/order/isFavorite/isDeleted only), so we can't gate the
    // tombstone drop on age. Instead: drop tombstoned labels unconditionally
    // unless they're referenced by a pending commit. Labels churn so rarely
    // in practice that lack of a grace window is acceptable.
    for (int i = m_labels.size() - 1; i >= 0; --i) {
        auto *l = m_labels.at(i);
        if (!l) continue;
        if (!l->isDeleted()) continue;
        if (pending.contains(l->id())) continue;
        const QString id = l->id();
        m_labelIndex.remove(id);
        m_labelMeta.remove(id);
        m_labels.removeAt(i);
        l->deleteLater();
        ++removed;
    }

    // Rebuild the case-folded label-name index since walk() doesn't know
    // about it. Cheaper than tracking removals inline.
    m_labelNameIndex.clear();
    for (auto *l : m_labels) {
        if (l && !l->isDeleted())
            m_labelNameIndex.insert(l->name().toLower(), l);
    }

    if (removed > 0) {
        // Emit per-collection signals so any ViewModel listening on the
        // specific change re-fetches. dataChanged() at the end coalesces
        // FileManager autosave to ONE write.
        emit tasksChanged();
        emit projectsChanged();
        emit sectionsChanged();
        emit labelsChanged();
        emit filtersChanged();
        emit remindersChanged();
        emit commentsChanged();
        emit dataChanged();
    }
    qInfo("DataStore::vacuum: removed %d tombstoned entities (cutoff %s, pending %d)",
          removed, qPrintable(cutoff.toString(Qt::ISODate)), pending.size());
    return removed;
}

// ===================== SECTIONS =====================

Section *DataStore::addSection(const QString &name, const QString &projectId)
{
    auto *section = new Section(this);
    section->setName(name);
    section->setProjectId(projectId);
    section->setOrder(sectionsForProject(projectId).size()); // already non-deleted

    m_sections.append(section);
    m_sectionIndex.insert(section->id(), section);

    // B-1.10: stamp initial CRDT fields, mirroring addTask/addProject/addLabel.
    const QString id = section->id();
    const QStringList initialFields = {
        QStringLiteral("name"), QStringLiteral("project_id"),
        QStringLiteral("order"), QStringLiteral("is_collapsed"),
        QStringLiteral("is_archived"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_sectionMeta, f);

    emit sectionsChanged();
    emit dataChanged();
    return section;
}

Section *DataStore::sectionById(const QString &id) const
{
    return m_sectionIndex.value(id, nullptr);
}

bool DataStore::removeSection(const QString &id)
{
    auto *section = m_sectionIndex.value(id);
    if (!section) return false;

    section->setIsDeleted(true);
    section->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // B-1.9: stamp CRDT fields so tombstone survives merge.
    stampEntityField(id, m_sectionMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_sectionMeta, QStringLiteral("updated_at"));

    emit sectionsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::renameSection(const QString &id, const QString &newName)
{
    auto *section = m_sectionIndex.value(id);
    if (!section || newName.trimmed().isEmpty()) return false;
    section->setName(newName.trimmed());
    stampEntityField(id, m_sectionMeta, QStringLiteral("name"));
    emit sectionsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::setSectionCollapsed(const QString &id, bool collapsed)
{
    auto *section = m_sectionIndex.value(id);
    if (!section) return false;
    if (section->isCollapsed() == collapsed) return true; // no-op, no stamp churn
    section->setIsCollapsed(collapsed);
    stampEntityField(id, m_sectionMeta, QStringLiteral("is_collapsed"));
    stampEntityField(id, m_sectionMeta, QStringLiteral("updated_at"));
    emit sectionsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::setSectionArchived(const QString &id, bool archived)
{
    auto *section = m_sectionIndex.value(id);
    if (!section) return false;
    if (section->isArchived() == archived) return true;
    section->setIsArchived(archived);
    stampEntityField(id, m_sectionMeta, QStringLiteral("is_archived"));
    stampEntityField(id, m_sectionMeta, QStringLiteral("updated_at"));
    emit sectionsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::setSectionOrder(const QString &id, int order)
{
    auto *section = m_sectionIndex.value(id);
    if (!section) return false;
    // Section ordering invariant: orders are non-negative — addSection assigns
    // them sequentially starting at 0 and every reorder helper swaps within
    // that range. A negative order survives JSON round-trip but breaks sort
    // assumptions in ProjectView (visible sections vanish below "(Без раздела)"
    // bucket which sorts at order=0 by convention). Reject early.
    if (order < 0) return false;
    if (section->order() == order) return true;
    section->setOrder(order);
    stampEntityField(id, m_sectionMeta, QStringLiteral("order"));
    stampEntityField(id, m_sectionMeta, QStringLiteral("updated_at"));
    emit sectionsChanged();
    emit dataChanged();
    return true;
}

QList<Section *> DataStore::sectionsForProject(const QString &projectId) const
{
    QList<Section *> result;
    for (auto *s : m_sections) {
        if (!s->isDeleted() && !s->isArchived() && s->projectId() == projectId)
            result.append(s);
    }
    return result;
}

void DataStore::addExistingSection(Section *section)
{
    section->setParent(this);
    m_sections.append(section);
    m_sectionIndex.insert(section->id(), section);
}

// ===================== LABELS =====================

Label *DataStore::addLabel(const QString &name)
{
    auto *label = new Label(this);
    label->setName(name);

    // B-1.61: use active (non-deleted) count for order.
    int activeCount = 0;
    for (auto *l : m_labels) {
        if (!l->isDeleted()) ++activeCount;
    }
    label->setOrder(activeCount);

    m_labels.append(label);
    m_labelIndex.insert(label->id(), label);
    m_labelNameIndex.insert(name.toLower(), label);

    const QString id = label->id();
    const QStringList initialFields = {
        QStringLiteral("name"), QStringLiteral("color"),
        QStringLiteral("order"), QStringLiteral("is_favorite"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_labelMeta, f);

    emit labelsChanged();
    emit dataChanged();
    return label;
}

Label *DataStore::labelById(const QString &id) const
{
    return m_labelIndex.value(id, nullptr);
}

Label *DataStore::labelByName(const QString &name) const
{
    return m_labelNameIndex.value(name.toLower(), nullptr);
}

bool DataStore::removeLabel(const QString &id)
{
    auto *label = m_labelIndex.value(id);
    if (!label) return false;

    label->setIsDeleted(true);
    stampEntityField(id, m_labelMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_labelMeta, QStringLiteral("updated_at"));

    emit labelsChanged();
    emit dataChanged();
    return true;
}

QList<Label *> DataStore::allLabels() const
{
    QList<Label *> result;
    for (auto *l : m_labels) {
        if (!l->isDeleted()) result.append(l);
    }
    return result;
}

void DataStore::addExistingLabel(Label *label)
{
    label->setParent(this);
    m_labels.append(label);
    m_labelIndex.insert(label->id(), label);
    m_labelNameIndex.insert(label->name().toLower(), label);
}

// ===================== FILTERS =====================

Filter *DataStore::addFilter(const QString &name, const QString &query)
{
    auto *filter = new Filter(this);
    filter->setName(name);
    filter->setQuery(query);

    // B-1.62: use active (non-deleted) count for order.
    int activeCount = 0;
    for (auto *f : m_filters) {
        if (!f->isDeleted()) ++activeCount;
    }
    filter->setOrder(activeCount);

    m_filters.append(filter);
    m_filterIndex.insert(filter->id(), filter);

    // B-1.10: stamp initial CRDT fields.
    const QString id = filter->id();
    const QStringList initialFields = {
        QStringLiteral("name"), QStringLiteral("query"),
        QStringLiteral("color"), QStringLiteral("order"),
        QStringLiteral("is_favorite"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
    for (const auto &f2 : initialFields)
        stampEntityField(id, m_filterMeta, f2);

    emit filtersChanged();
    emit dataChanged();
    return filter;
}

Filter *DataStore::filterById(const QString &id) const
{
    return m_filterIndex.value(id, nullptr);
}

bool DataStore::removeFilter(const QString &id)
{
    auto *filter = m_filterIndex.value(id);
    if (!filter) return false;

    filter->setIsDeleted(true);
    filter->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // B-1.9: stamp CRDT tombstone fields.
    stampEntityField(id, m_filterMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_filterMeta, QStringLiteral("updated_at"));

    emit filtersChanged();
    emit dataChanged();
    return true;
}

QList<Filter *> DataStore::allFilters() const
{
    QList<Filter *> result;
    for (auto *f : m_filters) {
        if (!f->isDeleted()) result.append(f);
    }
    return result;
}

void DataStore::addExistingFilter(Filter *filter)
{
    filter->setParent(this);
    m_filters.append(filter);
    m_filterIndex.insert(filter->id(), filter);
}

// ===================== REMINDERS =====================

Reminder *DataStore::addReminder(const QString &taskId, const QString &type)
{
    auto *reminder = new Reminder(this);
    reminder->setTaskId(taskId);
    reminder->setType(type);

    m_reminders.append(reminder);
    m_reminderIndex.insert(reminder->id(), reminder);

    // B-1.10: stamp initial CRDT fields.
    //
    // Iter-night-14: extended the list to include "due", "location", and
    // "recurrence_rule". These are user-mutable fields that the QML editor
    // (EditorRemindersSection.qml) sets immediately after addReminder via
    // setDueDatetime / setLocationCoords / setRepeatRule on the model
    // object. Without an initial stamp on the empty value, the post-setter
    // markReminderFieldsModified call has nothing to compare against on
    // peer-side merges; with this initial stamp + the post-setter stamp,
    // both Devices A and B carry per-field meta and Merger can use HLC to
    // converge instead of falling through to whole-entity LWW.
    const QString id = reminder->id();
    const QStringList initialFields = {
        QStringLiteral("task_id"), QStringLiteral("type"),
        QStringLiteral("due"),
        QStringLiteral("minute_offset"),
        QStringLiteral("location"),
        QStringLiteral("recurrence_rule"),
        QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_reminderMeta, f);

    emit remindersChanged();
    emit dataChanged();
    return reminder;
}

Reminder *DataStore::reminderById(const QString &id) const
{
    return m_reminderIndex.value(id, nullptr);
}

bool DataStore::removeReminder(const QString &id)
{
    auto *reminder = m_reminderIndex.value(id);
    if (!reminder) return false;

    reminder->setIsDeleted(true);
    reminder->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // B-1.9: stamp CRDT tombstone fields.
    stampEntityField(id, m_reminderMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_reminderMeta, QStringLiteral("updated_at"));

    emit remindersChanged();
    emit dataChanged();
    return true;
}

QList<Reminder *> DataStore::remindersForTask(const QString &taskId) const
{
    QList<Reminder *> result;
    for (auto *r : m_reminders) {
        if (!r->isDeleted() && r->taskId() == taskId)
            result.append(r);
    }
    return result;
}

QList<Reminder *> DataStore::allRemindersIncludingDeleted() const
{
    return m_reminders;
}

void DataStore::addExistingReminder(Reminder *reminder)
{
    reminder->setParent(this);
    m_reminders.append(reminder);
    m_reminderIndex.insert(reminder->id(), reminder);
}

// ===================== COMMENTS =====================

Comment *DataStore::addComment(const QString &content, const QString &taskId, const QString &projectId)
{
    // B-1.47: a comment must belong to exactly one parent.
    Q_ASSERT_X(taskId.isEmpty() || projectId.isEmpty(),
               "DataStore::addComment",
               "Comment cannot have both taskId and projectId set simultaneously.");
    if (!taskId.isEmpty() && !projectId.isEmpty()) {
        qWarning("DataStore::addComment: both taskId and projectId set — ignoring projectId.");
    }

    auto *comment = new Comment(this);
    comment->setContent(content);
    comment->setTaskId(taskId);
    // Only set projectId if taskId is not provided.
    if (taskId.isEmpty())
        comment->setProjectId(projectId);

    m_comments.append(comment);
    m_commentIndex.insert(comment->id(), comment);

    // B-1.10: stamp initial CRDT fields.
    const QString id = comment->id();
    const QStringList initialFields = {
        QStringLiteral("content"), QStringLiteral("task_id"),
        QStringLiteral("project_id"), QStringLiteral("posted_at"),
        QStringLiteral("updated_at"), QStringLiteral("is_deleted"),
    };
    for (const auto &f : initialFields)
        stampEntityField(id, m_commentMeta, f);

    emit commentsChanged();
    emit dataChanged();
    return comment;
}

Comment *DataStore::commentById(const QString &id) const
{
    return m_commentIndex.value(id, nullptr);
}

bool DataStore::removeComment(const QString &id)
{
    auto *comment = m_commentIndex.value(id);
    if (!comment) return false;

    comment->setIsDeleted(true);
    comment->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // B-1.9: stamp CRDT tombstone fields.
    stampEntityField(id, m_commentMeta, QStringLiteral("is_deleted"));
    stampEntityField(id, m_commentMeta, QStringLiteral("updated_at"));

    emit commentsChanged();
    emit dataChanged();
    return true;
}

bool DataStore::updateComment(const QString &id, const QString &content)
{
    auto *comment = m_commentIndex.value(id);
    if (!comment) return false;

    comment->setContent(content);
    comment->setUpdatedAt(QDateTime::currentDateTimeUtc());
    stampEntityField(id, m_commentMeta, QStringLiteral("content"));
    stampEntityField(id, m_commentMeta, QStringLiteral("updated_at"));

    emit commentsChanged();
    emit dataChanged();
    return true;
}

QList<Comment *> DataStore::commentsForTask(const QString &taskId) const
{
    QList<Comment *> result;
    for (auto *c : m_comments) {
        if (!c->isDeleted() && c->taskId() == taskId)
            result.append(c);
    }
    return result;
}

QList<Comment *> DataStore::commentsForProject(const QString &projectId) const
{
    QList<Comment *> result;
    for (auto *c : m_comments) {
        if (!c->isDeleted() && c->projectId() == projectId)
            result.append(c);
    }
    return result;
}

void DataStore::addExistingComment(Comment *comment)
{
    comment->setParent(this);
    m_comments.append(comment);
    m_commentIndex.insert(comment->id(), comment);
}

// ===================== SETTINGS =====================

QJsonObject DataStore::settings() const
{
    return m_settings;
}

void DataStore::setSettings(const QJsonObject &settings)
{
    m_settings = settings;
    emit settingsChanged();
    emit dataChanged();
}

QVariant DataStore::setting(const QString &key) const
{
    return m_settings.value(key).toVariant();
}

void DataStore::setSetting(const QString &key, const QVariant &value)
{
    m_settings[key] = QJsonValue::fromVariant(value);
    emit settingsChanged();
    emit dataChanged();
}

QString DataStore::exportSettings(const QString &dir)
{
    const QString outDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : dir;
    QDir().mkpath(outDir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString path  = outDir + QStringLiteral("/opentodo-settings-") + stamp + QStringLiteral(".json");

    QJsonObject root;
    root[QStringLiteral("format")]         = QStringLiteral("opentodo-settings-snapshot");
    root[QStringLiteral("format_version")] = 1;
    root[QStringLiteral("exported_at")]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root[QStringLiteral("settings")]       = m_settings;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return path;
}

bool DataStore::importSettings(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("opentodo-settings-snapshot"))
        return false;

    const QJsonObject newSettings = root.value(QStringLiteral("settings")).toObject();
    setSettings(newSettings);   // emits settingsChanged + dataChanged
    return true;
}

// ===================== SERIALIZATION =====================

// static
int DataStore::migrateJson(QJsonObject &root, int fromVersion)
{
    int v = fromVersion;
    // v0 → v1 — schema versioning was introduced at the same time as v1,
    // so files without a `schema_version` key just get the version bumped.
    // No structural changes to the JSON layout are required.
    if (v == 0) {
        // No field renames or reshaping needed — bumping only.
        v = 1;
    }
    // v1 → v2 — flatten the legacy double-wrap of per-entity CRDT meta.
    //   Old shape: entity["_meta"]["_meta"][field] = {ts, dev}
    //   New shape: entity["_meta"][field] = {ts, dev}
    // Affects every entity collection that DataStore::toJson serialises.
    // Idempotent: an already-flat _meta is left untouched.
    if (v == 1) {
        const QStringList collections = {
            QStringLiteral("tasks"),     QStringLiteral("projects"),
            QStringLiteral("sections"),  QStringLiteral("labels"),
            QStringLiteral("filters"),   QStringLiteral("reminders"),
            QStringLiteral("comments"),
        };
        for (const QString &coll : collections) {
            if (!root.contains(coll)) continue;
            QJsonArray arr = root.value(coll).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr.at(i).toObject();
                if (!obj.contains(QStringLiteral("_meta"))) continue;
                QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
                // Detect legacy double-wrap: a single key "_meta" whose
                // value is an object — flatten by replacing the outer
                // wrapper with the inner field-stamp map.
                if (meta.size() == 1 && meta.contains(QStringLiteral("_meta"))) {
                    const QJsonValue inner = meta.value(QStringLiteral("_meta"));
                    if (inner.isObject()) {
                        obj[QStringLiteral("_meta")] = inner.toObject();
                        arr.replace(i, obj);
                    }
                }
            }
            root[coll] = arr;
        }
        v = 2;
    }
    // Future: if (v == 2) { ...migrate to v3...; v = 3; }
    return v;
}

QJsonObject DataStore::toJson() const
{
    QJsonObject root;
    // F-1.60: always persist the current schema version.
    root[QStringLiteral("schema_version")] = CURRENT_SCHEMA_VERSION;
    root[QStringLiteral("settings")] = m_settings;

    QJsonArray tasksArr;
    for (auto *t : m_tasks)
        tasksArr.append(injectMeta(t->toJson(), m_taskMeta, t->id()));
    root[QStringLiteral("tasks")] = tasksArr;

    // completed_tasks are stored alongside tasks with is_completed flag.
    root[QStringLiteral("completed_tasks")] = QJsonArray();

    QJsonArray projectsArr;
    for (auto *p : m_projects)
        projectsArr.append(injectMeta(p->toJson(), m_projectMeta, p->id()));
    root[QStringLiteral("projects")] = projectsArr;

    QJsonArray sectionsArr;
    for (auto *s : m_sections)
        sectionsArr.append(injectMeta(s->toJson(), m_sectionMeta, s->id()));
    root[QStringLiteral("sections")] = sectionsArr;

    QJsonArray labelsArr;
    for (auto *l : m_labels)
        labelsArr.append(injectMeta(l->toJson(), m_labelMeta, l->id()));
    root[QStringLiteral("labels")] = labelsArr;

    QJsonArray filtersArr;
    for (auto *f : m_filters)
        filtersArr.append(injectMeta(f->toJson(), m_filterMeta, f->id()));
    root[QStringLiteral("filters")] = filtersArr;

    QJsonArray remindersArr;
    for (auto *r : m_reminders)
        remindersArr.append(injectMeta(r->toJson(), m_reminderMeta, r->id()));
    root[QStringLiteral("reminders")] = remindersArr;

    QJsonArray commentsArr;
    for (auto *c : m_comments)
        commentsArr.append(injectMeta(c->toJson(), m_commentMeta, c->id()));
    root[QStringLiteral("comments")] = commentsArr;

    return root;
}

void DataStore::loadFromJson(const QJsonObject &rootIn)
{
    QJsonObject root = rootIn;
    m_loadedSchemaVersion = root.value(QStringLiteral("schema_version")).toInt(0);

    if (m_loadedSchemaVersion < CURRENT_SCHEMA_VERSION) {
        const int finalVer = migrateJson(root, m_loadedSchemaVersion);
        qInfo("DataStore: migrated state.json from schema v%d to v%d",
              m_loadedSchemaVersion, finalVer);
        m_loadedSchemaVersion = finalVer;
    } else if (m_loadedSchemaVersion > CURRENT_SCHEMA_VERSION) {
        qWarning("DataStore: state.json schema_version %d is newer than supported %d; "
                 "loading anyway, fields may be lost",
                 m_loadedSchemaVersion, CURRENT_SCHEMA_VERSION);
    }

    clear();

    m_settings = root[QStringLiteral("settings")].toObject();

    // B-1.18: tombstones older than 30 days are pruned from the in-memory
    // index (they are kept in the JSON on disk until the next save, after
    // which they disappear naturally).  Recent tombstones are kept so that
    // sync can propagate deletions to peers that haven't yet synced.
    const QDateTime pruneThreshold = QDateTime::currentDateTimeUtc().addDays(-30);

    const auto tasksArr = root[QStringLiteral("tasks")].toArray();
    for (const auto &v : tasksArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_taskMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue; // drop stale tombstone
        }
        auto *t = Task::fromJson(obj, this);
        m_tasks.append(t);
        m_taskIndex.insert(t->id(), t);
    }

    const auto projectsArr = root[QStringLiteral("projects")].toArray();
    for (const auto &v : projectsArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_projectMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *p = Project::fromJson(obj, this);
        m_projects.append(p);
        m_projectIndex.insert(p->id(), p);
    }

    const auto sectionsArr = root[QStringLiteral("sections")].toArray();
    for (const auto &v : sectionsArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_sectionMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *s = Section::fromJson(obj, this);
        m_sections.append(s);
        m_sectionIndex.insert(s->id(), s);
    }

    const auto labelsArr = root[QStringLiteral("labels")].toArray();
    for (const auto &v : labelsArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_labelMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *l = Label::fromJson(obj, this);
        m_labels.append(l);
        m_labelIndex.insert(l->id(), l);
        m_labelNameIndex.insert(l->name().toLower(), l);
    }

    const auto filtersArr = root[QStringLiteral("filters")].toArray();
    for (const auto &v : filtersArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_filterMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *f = Filter::fromJson(obj, this);
        m_filters.append(f);
        m_filterIndex.insert(f->id(), f);
    }

    const auto remindersArr = root[QStringLiteral("reminders")].toArray();
    for (const auto &v : remindersArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_reminderMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *r = Reminder::fromJson(obj, this);
        m_reminders.append(r);
        m_reminderIndex.insert(r->id(), r);
    }

    const auto commentsArr = root[QStringLiteral("comments")].toArray();
    for (const auto &v : commentsArr) {
        QJsonObject obj = v.toObject();
        const QString id = obj[QStringLiteral("id")].toString();
        obj = extractMeta(obj, m_commentMeta, id);
        if (obj[QStringLiteral("is_deleted")].toBool()) {
            const QDateTime updatedAt = QDateTime::fromString(
                obj[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
            if (updatedAt.isValid() && updatedAt < pruneThreshold)
                continue;
        }
        auto *c = Comment::fromJson(obj, this);
        m_comments.append(c);
        m_commentIndex.insert(c->id(), c);
    }

    emit tasksChanged();
    emit projectsChanged();
    emit sectionsChanged();
    emit labelsChanged();
    emit filtersChanged();
    emit remindersChanged();
    emit commentsChanged();
    emit settingsChanged();
    // R-1.17: emit aggregate so FileManager / ViewModels listening on
    // dataChanged() see a consistent state after load.
    emit dataChanged();
}

void DataStore::clear()
{
    emit aboutToReset();

    qDeleteAll(m_tasks);
    m_tasks.clear();
    m_taskIndex.clear();
    m_taskMeta.clear();

    qDeleteAll(m_projects);
    m_projects.clear();
    m_projectIndex.clear();
    m_projectMeta.clear();

    qDeleteAll(m_sections);
    m_sections.clear();
    m_sectionIndex.clear();
    m_sectionMeta.clear();

    qDeleteAll(m_labels);
    m_labels.clear();
    m_labelIndex.clear();
    m_labelNameIndex.clear();
    m_labelMeta.clear();

    qDeleteAll(m_filters);
    m_filters.clear();
    m_filterIndex.clear();
    m_filterMeta.clear();

    qDeleteAll(m_reminders);
    m_reminders.clear();
    m_reminderIndex.clear();
    m_reminderMeta.clear();

    qDeleteAll(m_comments);
    m_comments.clear();
    m_commentIndex.clear();
    m_commentMeta.clear();

    m_settings = QJsonObject();
}
