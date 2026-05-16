#include "projectviewmodel.h"
#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "services/telemetry.h"

#include <QDate>
#include <QDateTime>
#include <QSettings>
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
    if (!parentId.isEmpty()) p->setParentId(parentId);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_add"), {});

    emit projectAdded(p->id());
    return p->id();
}

bool ProjectViewModel::updateProject(const QString &id, const QString &field, const QVariant &value)
{
    Project *p = m_store->projectById(id);
    if (!p) return false;

    if (field == QStringLiteral("name")) p->setName(value.toString());
    else if (field == QStringLiteral("color")) p->setColor(value.toString());
    else if (field == QStringLiteral("description")) p->setDescription(value.toString());
    else if (field == QStringLiteral("notes")) p->setNotes(value.toString());
    else if (field == QStringLiteral("viewStyle")) p->setViewStyle(value.toString());
    else if (field == QStringLiteral("isFavorite")) p->setIsFavorite(value.toBool());
    else if (field == QStringLiteral("isArchived")) p->setIsArchived(value.toBool());
    else if (field == QStringLiteral("order")) p->setOrder(value.toInt());
    else if (field == QStringLiteral("icon")) p->setIcon(value.toString());
    else if (field == QStringLiteral("weeklyGoal")) p->setWeeklyGoal(value.toInt());
    else if (field == QStringLiteral("parentId")) {
        // Cycle check: walk the parent chain from the candidate; if we encounter
        // `id`, setting this parentId would create a cycle — reject.
        const QString candidateParent = value.toString();
        QString cur = candidateParent;
        while (!cur.isEmpty()) {
            if (cur == id) {
                qWarning("ProjectViewModel::updateProject: rejected parentId '%s' for "
                         "project '%s' — would create a cycle",
                         qUtf8Printable(candidateParent), qUtf8Printable(id));
                return false;
            }
            const Project *ancestor = m_store->projectById(cur);
            if (!ancestor) break;
            cur = ancestor->parentId();
        }
        p->setParentId(candidateParent);
    }
    else return false;

    p->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markProjectModified(id);
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
        m_store->markProjectModified(id);

    emit countChanged();

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("project_reorder"), {});

    return true;
}

int ProjectViewModel::removeCompletedTasksInProject(const QString &projectId)
{
    if (projectId.isEmpty()) return 0;
    Project *p = m_store->projectById(projectId);
    if (!p) return 0;

    // Collect ids first — do not mutate while iterating the live list.
    QStringList toRemove;
    for (const Task *t : m_store->tasksForProject(projectId)) {
        if (t && !t->isDeleted() && t->isCompleted())
            toRemove.append(t->id());
    }

    int n = 0;
    for (const QString &id : toRemove) {
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
    // Task counts may have changed — refresh the relevant role.
    if (!m_projects.isEmpty())
        emit dataChanged(index(0), index(m_projects.size() - 1), {TaskCountRole});

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
