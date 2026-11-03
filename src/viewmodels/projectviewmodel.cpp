#include "projectviewmodel.h"
#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "services/telemetry.h"

#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <algorithm>

ProjectViewModel::ProjectViewModel(DataStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    connect(m_store, &DataStore::projectsChanged, this, &ProjectViewModel::onProjectsChanged);
    connect(m_store, &DataStore::tasksChanged, this, &ProjectViewModel::onTasksChanged);
    connect(m_store, &DataStore::aboutToReset, this, [this]() {
        beginResetModel();
        m_projects.clear();
        endResetModel();
    });

    // Debounce the weekly-goal scan: a burst of task edits coalesces into one
    // pass after a short idle gap instead of re-scanning on every change.
    m_goalCheckTimer = new QTimer(this);
    m_goalCheckTimer->setSingleShot(true);
    m_goalCheckTimer->setInterval(60);
    connect(m_goalCheckTimer, &QTimer::timeout, this, &ProjectViewModel::checkWeeklyGoals);

    refresh();
}

void ProjectViewModel::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
}

int ProjectViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_projects.size();
}

QVariant ProjectViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_projects.size())
        return {};

    const Project *p = m_projects.at(index.row());

    switch (role) {
    case IdRole: return p->id();
    case NameRole: return p->name();
    case ColorRole: return p->color();
    case ParentIdRole: return p->parentId();
    case OrderRole: return p->order();
    case IsFavoriteRole: return p->isFavorite();
    case IsInboxRole: return p->isInbox();
    case IsArchivedRole: return p->isArchived();
    case ViewStyleRole: return p->viewStyle();
    case TaskCountRole: return taskCountForProject(p->id());
    case ProjectObjectRole: return QVariant::fromValue(const_cast<Project *>(p));
    }

    return {};
}

QHash<int, QByteArray> ProjectViewModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {ColorRole, "color"},
        {ParentIdRole, "parentId"},
        {OrderRole, "order"},
        {IsFavoriteRole, "isFavorite"},
        {IsInboxRole, "isInbox"},
        {IsArchivedRole, "isArchived"},
        {ViewStyleRole, "viewStyle"},
        {TaskCountRole, "taskCount"},
        {ProjectObjectRole, "projectObject"},
    };
}

int ProjectViewModel::count() const { return m_projects.size(); }

bool ProjectViewModel::showArchived() const { return m_showArchived; }
void ProjectViewModel::setShowArchived(bool show) {
    if (m_showArchived != show) {
        m_showArchived = show;
        emit showArchivedChanged();
        refresh();
    }
}

QString ProjectViewModel::addProject(const QString &name, const QString &color, const QString &parentId)
{
    Project *p = m_store->addProject(name);
    if (!p) return {};
    p->setColor(color);
    // parent_id isn't in addProject's initialFields — without a per-field
    // CRDT stamp here, a concurrent peer reparenting wins by empty-meta
    // fallback and the nested project's parent is silently lost after sync.
    QStringList fields;
    if (!parentId.isEmpty()) {
        // Iter-14: validate parentId before we attach. iter-13 added the
        // stamp but skipped the guards that the local updateProject path and
        // the inbound applyCommit path already enforce: Inbox must stay at
        // the root, and orphan / tombstoned parents are silently dropped.
        const Project *parent = m_store->projectById(parentId);
        if (parent && !parent->isDeleted() && !parent->isInbox()) {
            p->setParentId(parentId);
            fields << QStringLiteral("parent_id");
        } else if (parent && parent->isInbox()) {
            qWarning("ProjectViewModel::addProject: rejected Inbox as parent "
                     "for new project '%s'", qUtf8Printable(name));
        }
    }
    // color is already in initialFields but stamping again advances HLC.
    fields << QStringLiteral("color");
    m_store->markProjectFieldsModified(p->id(), fields);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_add"), {});

    emit projectAdded(p->id());
    return p->id();
}

bool ProjectViewModel::updateProject(const QString &id, const QString &field, const QVariant &value)
{
    Project *p = m_store->projectById(id);
    if (!p) return false;

    // Inbox immutability guard. DataStore::removeProject already short-circuits
    // on isInbox(), but the update path was asymmetric — name and parentId
    // could be silently overwritten via QML or callers calling updateProject
    // directly, which would either rename the Inbox or reparent it under an
    // arbitrary project. Both break the long-standing UX contract that "Inbox
    // is the always-present, always-named, always-top-level root". Color,
    // icon, isFavorite and order remain legitimately user-customisable on
    // the Inbox row, so the guard is intentionally narrow.
    if (p->isInbox()) {
        if (field == QStringLiteral("name")) {
            qWarning("ProjectViewModel::updateProject: Inbox cannot be renamed "
                     "(attempted new name '%s')", qUtf8Printable(value.toString()));
            return false;
        }
        if (field == QStringLiteral("parentId")) {
            qWarning("ProjectViewModel::updateProject: Inbox cannot be reparented "
                     "(attempted parentId '%s')", qUtf8Printable(value.toString()));
            return false;
        }
    }

    // The JSON (snake_case) name of the field actually mutated, for the
    // per-field CRDT stamp below. Keep in sync with each branch.
    QString jsonField;

    if (field == QStringLiteral("name"))                    { p->setName(value.toString());            jsonField = QStringLiteral("name"); }
    else if (field == QStringLiteral("color"))              { p->setColor(value.toString());           jsonField = QStringLiteral("color"); }
    else if (field == QStringLiteral("description"))        { p->setDescription(value.toString());     jsonField = QStringLiteral("description"); }
    else if (field == QStringLiteral("notes"))              { p->setNotes(value.toString());           jsonField = QStringLiteral("notes"); }
    else if (field == QStringLiteral("viewStyle"))          { p->setViewStyle(value.toString());       jsonField = QStringLiteral("view_style"); }
    else if (field == QStringLiteral("isFavorite"))         { p->setIsFavorite(value.toBool());        jsonField = QStringLiteral("is_favorite"); }
    else if (field == QStringLiteral("isArchived"))         { p->setIsArchived(value.toBool());        jsonField = QStringLiteral("is_archived"); }
    else if (field == QStringLiteral("order"))              { p->setOrder(value.toInt());              jsonField = QStringLiteral("order"); }
    else if (field == QStringLiteral("icon"))               { p->setIcon(value.toString());            jsonField = QStringLiteral("icon"); }
    else if (field == QStringLiteral("weeklyGoal"))         { p->setWeeklyGoal(value.toInt());         jsonField = QStringLiteral("weekly_goal"); }
    else if (field == QStringLiteral("linkedRepoPath"))     { p->setLinkedRepoPath(value.toString());  jsonField = QStringLiteral("linked_repo_path"); }
    else if (field == QStringLiteral("parentId")) {
        // Cycle check: walk the parent chain from the candidate; if we encounter
        // `id`, setting this parentId would create a cycle — reject.
        const QString candidateParent = value.toString();
        // Iter-14: Inbox cannot become a child OR a parent. The
        // existing `p->isInbox()` guard at the top of this function blocks
        // making Inbox a child of something else; the symmetric block here
        // stops any project from being parented under Inbox. applyCommit
        // already has this guard (iter-7) — bringing the local path back in
        // line so cross-device behaviour matches the cross-process behaviour.
        if (!candidateParent.isEmpty()) {
            const Project *candidate = m_store->projectById(candidateParent);
            if (candidate && candidate->isInbox()) {
                qWarning("ProjectViewModel::updateProject: rejected Inbox as parent "
                         "for project '%s'", qUtf8Printable(id));
                return false;
            }
        }
        // Iter-14: bounded cycle walk. Without MAX_DEPTH a state.json file
        // carrying a pre-existing A→B→A cycle (legacy data, hand-edit)
        // would spin forever and freeze the UI thread the moment the user
        // tried to reparent anything. Mirrors the guard already in
        // applyCommit (iter-7).
        constexpr int MAX_DEPTH = 1000;
        QString cur = candidateParent;
        for (int i = 0; i < MAX_DEPTH && !cur.isEmpty(); ++i) {
            if (cur == id) {
                qWarning("ProjectViewModel::updateProject: rejected parentId '%s' for "
                         "project '%s' — would create a cycle",
                         qUtf8Printable(candidateParent), qUtf8Printable(id));
                return false;
            }
            const Project *ancestor = m_store->projectById(cur);
            if (!ancestor) break;
            const QString next = ancestor->parentId();
            if (next == cur) break; // self-loop on the ancestor — stop walking
            cur = next;
        }
        p->setParentId(candidateParent);
        jsonField = QStringLiteral("parent_id");
    }
    else return false;

    p->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markProjectFieldsModified(id, {jsonField});
    emit projectChanged(id);

    int idx = m_projects.indexOf(p);
    if (idx >= 0)
        emit dataChanged(index(idx), index(idx));

    return true;
}

bool ProjectViewModel::deleteProject(const QString &id)
{
    bool ok = m_store->removeProject(id);
    if (ok) emit projectRemoved(id);
    return ok;
}

bool ProjectViewModel::archiveProject(const QString &id)
{
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_archive"), {});
    return updateProject(id, QStringLiteral("isArchived"), true);
}

bool ProjectViewModel::unarchiveProject(const QString &id)
{
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_unarchive"), {});
    return updateProject(id, QStringLiteral("isArchived"), false);
}

bool ProjectViewModel::setFavorite(const QString &id, bool favorite)
{
    return updateProject(id, QStringLiteral("isFavorite"), favorite);
}

bool ProjectViewModel::setViewStyle(const QString &id, const QString &style)
{
    return updateProject(id, QStringLiteral("viewStyle"), style);
}

int ProjectViewModel::taskCountForProject(const QString &id) const
{
    const auto tasks = m_store->tasksForProject(id);
    int count = 0;
    for (const auto *t : tasks) {
        if (!t->isCompleted()) ++count;
    }
    return count;
}

Project *ProjectViewModel::projectAt(int index) const
{
    if (index < 0 || index >= m_projects.size()) return nullptr;
    return m_projects.at(index);
}

Project *ProjectViewModel::projectById(const QString &id) const
{
    return m_store->projectById(id);
}

bool ProjectViewModel::reorderProject(int sourceIndex, int targetIndex)
{
    // Границы: учитываем только индексы в пределах видимого списка.
    if (sourceIndex < 0 || sourceIndex >= m_projects.size()) return false;
    if (targetIndex < 0 || targetIndex >= m_projects.size()) return false;
    if (sourceIndex == targetIndex) return false;

    // Перемещаем указатель в плоском списке.
    QList<Project *> reordered = m_projects;
    reordered.move(sourceIndex, targetIndex);

    // Перенумеровываем order по новым позициям (1-based, как в остальном коде).
    // Inbox всегда идёт первым — пропускаем его при нумерации.
    //
    // markProjectModified() emits projectsChanged → refresh() → its own
    // begin/endResetModel, which interleaves with our own pair and triggers
    // "endResetModel called without beginResetModel" warnings (and breaks
    // sibling reorder visible in the UI). Collect IDs first, mark after the
    // outer endResetModel.
    QStringList toMark;
    beginResetModel();
    m_projects = reordered;
    int orderCounter = 1;
    for (int i = 0; i < m_projects.size(); ++i) {
        Project *p = m_projects.at(i);
        if (p->isInbox()) continue;            // inbox не трогаем
        p->setOrder(orderCounter++);
        toMark.append(p->id());
    }
    endResetModel();
    for (const QString &id : toMark)
        m_store->markProjectFieldsModified(id, {QStringLiteral("order")});

    emit countChanged();

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_reorder"), {});

    return true;
}

bool ProjectViewModel::reorderProjectById(const QString &draggedId,
                                           const QString &targetId,
                                           bool above)
{
    if (draggedId.isEmpty() || targetId.isEmpty() || draggedId == targetId)
        return false;

    Project *dragged = m_store->projectById(draggedId);
    Project *target  = m_store->projectById(targetId);
    if (!dragged || !target) return false;
    if (dragged->isInbox()) return false; // Inbox is pinned to top

    const QString destParent = target->parentId();

    // Iter-task a1a57171: allow cross-parent reorder — the user dragging a
    // child of A onto the visible sibling line of a child of B clearly wants
    // both effects: reparent + slot into B's children. Previously this
    // returned false and the drag silently no-op'd. We now reuse the local
    // updateProject guards (Inbox-as-parent, cycle walk MAX_DEPTH=1000) to
    // validate the move before renumbering.
    const bool reparent = (dragged->parentId() != destParent);
    if (reparent) {
        // Block: dragging UNDER Inbox.
        if (target->isInbox()) {
            qWarning("reorderProjectById: refused — Inbox cannot be a parent");
            return false;
        }
        // Block: would create a cycle (dragging A under its own descendant).
        constexpr int MAX_DEPTH = 1000;
        QString cur = destParent;
        for (int i = 0; i < MAX_DEPTH && !cur.isEmpty(); ++i) {
            if (cur == draggedId) {
                qWarning("reorderProjectById: refused — would create cycle");
                return false;
            }
            const Project *anc = m_store->projectById(cur);
            if (!anc) break;
            const QString next = anc->parentId();
            if (next == cur) break;
            cur = next;
        }
        // OK — the new parent is applied below, only once the indices have
        // been validated (V14).
    }

    // Collect every visible sibling under the destination parent, in current
    // order. Including `dragged` is fine — we sort, remove it, then re-insert
    // at the requested slot. (Membership is decided by destParent / identity,
    // so this works before dragged's parent_id is switched.)
    QList<Project *> siblings;
    for (Project *p : std::as_const(m_projects)) {
        if (!p || p->isInbox() || p->isDeleted() || p->isArchived())
            continue;
        if (p->parentId() == destParent || p == dragged)
            siblings.append(p);
    }
    std::sort(siblings.begin(), siblings.end(),
              [](const Project *a, const Project *b) { return a->order() < b->order(); });

    const int fromIdx = siblings.indexOf(dragged);
    int targetIdx     = siblings.indexOf(target);
    if (fromIdx < 0 || targetIdx < 0) return false;

    siblings.removeAt(fromIdx);
    // After removal, target may have shifted left by one if it was after dragged.
    int insertAt = siblings.indexOf(target);
    if (insertAt < 0) return false;
    if (!above) ++insertAt;
    siblings.insert(insertAt, dragged);

    // V14: switch the parent only now that the move is known to succeed.
    // Setting it before the index checks above left an archived/hidden
    // target's early `return false` with a changed parent_id that was neither
    // stamped nor notified (it then leaked out with the next unrelated save).
    // The renumber below ignores dragged's old siblings (their order stays a
    // valid sequence), so only the destination level is touched.
    if (reparent)
        dragged->setParentId(destParent);

    // Renumber siblings 1..N (preserving the 1-based convention used by
    // reorderProject above). Other projects' order values keep their gaps —
    // sync merges this as a per-field update on just the touched siblings.
    QStringList toMark;
    int orderCounter = 1;
    for (Project *p : std::as_const(siblings)) {
        if (p->order() != orderCounter)
            p->setOrder(orderCounter);
        toMark.append(p->id());
        ++orderCounter;
    }

    // If dragged switched parent, stamp parent_id explicitly. The new parent
    // is now its real parent_id, and the merger must see the per-field stamp
    // — otherwise a concurrent peer's edit on the OLD parent wins by empty-
    // meta fallback after sync.
    if (toMark.contains(draggedId))
        m_store->markProjectFieldsModified(draggedId,
            {QStringLiteral("order"), QStringLiteral("parent_id")});

    for (const QString &id : std::as_const(toMark)) {
        if (id == draggedId) continue; // already stamped above
        m_store->markProjectFieldsModified(id, {QStringLiteral("order")});
    }

    // refresh() runs on m_store->projectsChanged via markProjectFieldsModified,
    // re-sorting m_projects and emitting our model signals — no manual reset.
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_reorder"), {});
    return true;
}

int ProjectViewModel::removeCompletedTasksInProject(const QString &projectId)
{
    if (projectId.isEmpty()) return 0;
    Project *p = m_store->projectById(projectId);
    if (!p) return 0;

    // V8: walk EVERY live task of the project, not tasksForProject() — that
    // returns top-level tasks only, so completed subtasks of still-open
    // parents were never removed. And removeTask() tombstones the whole
    // descendant subtree, ACTIVE subtasks included — so a completed task that
    // still has an active descendant is kept (removing it would silently
    // delete open work).
    //
    // Active-descendant walk: depth-bounded BFS with a visited set so a
    // pre-existing parent_id cycle can't spin forever. subtasks() already
    // excludes tombstones.
    auto hasActiveDescendant = [this](const Task *root) {
        constexpr int MAX_DEPTH = 100;
        QSet<QString> visited{root->id()};
        QList<Task *> frontier = m_store->subtasks(root->id());
        for (int depth = 0; !frontier.isEmpty() && depth < MAX_DEPTH; ++depth) {
            QList<Task *> next;
            for (Task *child : std::as_const(frontier)) {
                if (!child || visited.contains(child->id())) continue;
                visited.insert(child->id());
                if (!child->isCompleted()) return true;
                next.append(m_store->subtasks(child->id()));
            }
            frontier = next;
        }
        return false;
    };

    // Collect ids first — do not mutate while iterating the live list.
    QStringList toRemove;
    for (const Task *t : m_store->allTasks()) {
        if (!t || t->isDeleted() || !t->isCompleted()) continue;
        if (t->projectId() != projectId) continue;
        if (hasActiveDescendant(t)) continue;
        toRemove.append(t->id());
    }

    int n = 0;
    for (const QString &id : std::as_const(toRemove)) {
        const Task *t = m_store->taskById(id);
        if (!t) continue;
        // Already tombstoned by an ancestor's cascade earlier in this loop
        // (every descendant of a removed task is completed, see above) —
        // still one of the tasks this call removed.
        if (t->isDeleted()) { ++n; continue; }
        if (m_store->removeTask(id)) ++n;
    }

    if (m_telemetry && n > 0)
        m_telemetry->trackEvent(QStringLiteral("project_remove_completed"),
                                {{QStringLiteral("count"), n}});
    return n;
}

int ProjectViewModel::defaultPriorityForProject(const QString &projectId) const
{
    if (projectId.isEmpty()) return 4;
    QSettings s;
    const int v = s.value(QStringLiteral("projects/%1/defaultPriority").arg(projectId), 4).toInt();
    if (v < 1 || v > 4) return 4;
    return v;
}

void ProjectViewModel::setDefaultPriorityForProject(const QString &projectId, int priority)
{
    if (projectId.isEmpty()) return;
    int p = priority;
    if (p < 1) p = 1;
    if (p > 4) p = 4;
    QSettings s;
    s.setValue(QStringLiteral("projects/%1/defaultPriority").arg(projectId), p);

    if (m_telemetry) {
        m_telemetry->trackEvent(QStringLiteral("project_default_priority_set"),
                                {{QStringLiteral("project_id"), projectId},
                                 {QStringLiteral("priority"), p}});
    }
}

QStringList ProjectViewModel::defaultLabelsForProject(const QString &projectId) const
{
    if (projectId.isEmpty()) return {};
    QSettings s;
    return s.value(QStringLiteral("projects/%1/defaultLabels").arg(projectId)).toStringList();
}

void ProjectViewModel::setDefaultLabelsForProject(const QString &projectId, const QStringList &labelIds)
{
    if (projectId.isEmpty()) return;
    QSettings s;
    s.setValue(QStringLiteral("projects/%1/defaultLabels").arg(projectId), labelIds);

    if (m_telemetry) {
        m_telemetry->trackEvent(QStringLiteral("project_default_labels_set"),
                                {{QStringLiteral("project_id"), projectId},
                                 {QStringLiteral("count"), labelIds.size()}});
    }
}

QVariantList ProjectViewModel::childProjects(const QString &parentId) const
{
    // Return QVariantMap entries (NOT QVariant::fromValue(Project*)) — Qt 6.11
    // does NOT auto-convert a QVariantList containing QObject*-bearing
    // QVariants into a usable JS array in QML: `.length` is undefined and
    // `for (let i = 0; i < arr.length; ++i)` runs zero iterations. The
    // SidebarContent `hasChildren` and the project-tree `rebuild()` both
    // iterate this list — with the broken shape, every parent project
    // looked childless and the chevron stayed hidden, leaving the user
    // with no way to expand sub-projects.
    //
    // QVariantMap entries are reliably converted: each map's keys become
    // JS object properties and `.length` of the surrounding array works.
    // Mirrors the fix pattern used for TaskViewModel::subtasksOf().
    QVariantList result;
    for (auto *p : m_projects) {
        if (!p || p->parentId() != parentId) continue;
        QVariantMap map;
        map[QStringLiteral("id")]         = p->id();
        map[QStringLiteral("name")]       = p->name();
        map[QStringLiteral("color")]      = p->color();
        map[QStringLiteral("icon")]       = p->icon();
        map[QStringLiteral("parentId")]   = p->parentId();
        map[QStringLiteral("isArchived")] = p->isArchived();
        map[QStringLiteral("isInbox")]    = p->isInbox();
        map[QStringLiteral("isFavorite")] = p->isFavorite();
        map[QStringLiteral("order")]      = p->order();
        result.append(map);
    }
    return result;
}

void ProjectViewModel::onProjectsChanged()
{
    refresh();
}

int ProjectViewModel::weeklyCompletedForProject(const QString &projectId) const
{
    if (projectId.isEmpty()) return 0;

    const QDate today = QDate::currentDate();
    int isoYear = 0;
    const int currentWeek = today.weekNumber(&isoYear);
    if (currentWeek <= 0) return 0;

    int count = 0;
    const auto tasks = m_store->tasksForProject(projectId);
    for (const Task *t : tasks) {
        if (!t || t->isDeleted()) continue;
        if (!t->isCompleted()) continue;
        const QDateTime ca = t->completedAt();
        if (!ca.isValid()) continue;
        const QDate completedDate = ca.toLocalTime().date();
        int compYear = 0;
        const int compWeek = completedDate.weekNumber(&compYear);
        if (compYear == isoYear && compWeek == currentWeek)
            ++count;
    }
    return count;
}

void ProjectViewModel::onTasksChanged()
{
    // Task counts may have changed — refresh the relevant role immediately
    // (cheap now that DataStore buckets the task queries).
    if (!m_projects.isEmpty())
        emit dataChanged(index(0), index(m_projects.size() - 1), {TaskCountRole});

    // Coalesce the heavier weekly-goal scan across a burst of edits.
    m_goalCheckTimer->start();
}

void ProjectViewModel::checkWeeklyGoals()
{
    // Weekly-goal celebration: a task completion can transition any project
    // from below its weekly goal to >= goal. Detect the crossing and emit
    // exactly once per (projectId, ISO week).
    const QDate today = QDate::currentDate();
    int isoYear = 0;
    const int currentWeek = today.weekNumber(&isoYear);
    if (currentWeek <= 0) return;
    const int weekKey = isoYear * 100 + currentWeek;

    const auto allProjects = m_store->allProjects();
    for (Project *p : allProjects) {
        if (!p) continue;
        const int goal = p->weeklyGoal();
        if (goal <= 0) continue;

        const int completed = weeklyCompletedForProject(p->id());

        // Memoize using ISO week key so a new week resets the latch.
        const int lastWeekKey = m_celebratedWeek.value(p->id(), -1);

        if (completed >= goal && lastWeekKey != weekKey) {
            m_celebratedWeek.insert(p->id(), weekKey);

            if (m_telemetry) {
                m_telemetry->trackEvent(
                    QStringLiteral("project_goal_reached"),
                    {{QStringLiteral("project_id"), p->id()}});
            }

            emit weeklyGoalReached(p->id());
        } else if (completed < goal && lastWeekKey == weekKey) {
            // Allow re-celebration in the same week if the user un-completed
            // a task and then crosses the threshold again.
            m_celebratedWeek.remove(p->id());
        }
    }
}

void ProjectViewModel::refresh()
{
    beginResetModel();
    m_projects.clear();

    const auto all = m_store->allProjects();
    for (auto *p : all) {
        if (!m_showArchived && p->isArchived()) continue;
        m_projects.append(p);
    }

    std::sort(m_projects.begin(), m_projects.end(), [](const Project *a, const Project *b) {
        // Inbox always first
        if (a->isInbox()) return true;
        if (b->isInbox()) return false;
        return a->order() < b->order();
    });

    endResetModel();
    emit countChanged();
}
