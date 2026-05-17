#include "sharemanager.h"

#include "gdrive.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/comment.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>

Q_LOGGING_CATEGORY(lcShare, "opentodo.share")

ShareManager::ShareManager(DataStore *store, GDriveClient *drive, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_drive(drive)
{
}

bool ShareManager::isAvailable() const
{
    return m_drive && m_drive->isAuthenticated();
}

void ShareManager::shareProject(const QString &projectId)
{
    if (!m_store || !m_drive) {
        emit shareError(QStringLiteral("Внутренняя ошибка: ShareManager не инициализирован."));
        return;
    }
    if (!m_drive->isAuthenticated()) {
        emit shareError(QStringLiteral("Подключите Google Drive в настройках для публикации ссылок."));
        return;
    }
    Project *p = m_store->projectById(projectId);
    if (!p || p->isDeleted()) {
        emit shareError(QStringLiteral("Проект не найден."));
        return;
    }

    const QJsonObject snapshot = buildProjectSnapshot(projectId);
    const QByteArray body = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString safeName = sanitizeForFilename(p->name());
    const QString fileName = QStringLiteral("OpenTodo project — %1 — %2.json")
                                 .arg(safeName, stamp);

    QString fileId;
    QString url;
    const bool ok = m_drive->uploadPublicFile(fileName, body,
                                              QStringLiteral("application/json"),
                                              &fileId, &url);
    if (!ok || url.isEmpty()) {
        qCInfo(lcShare) << "shareProject: upload failed for project" << projectId;
        emit shareError(QStringLiteral("Не удалось опубликовать проект на Google Drive."));
        return;
    }

    qCInfo(lcShare) << "shareProject: published" << projectId << "as" << fileId;
    emit shareLinkReady(url, fileId);
}

void ShareManager::shareTasks(const QStringList &taskIds)
{
    if (!m_store || !m_drive) {
        emit shareError(QStringLiteral("Внутренняя ошибка: ShareManager не инициализирован."));
        return;
    }
    if (!m_drive->isAuthenticated()) {
        emit shareError(QStringLiteral("Подключите Google Drive в настройках для публикации ссылок."));
        return;
    }
    if (taskIds.isEmpty()) {
        emit shareError(QStringLiteral("Не выбрана ни одна задача."));
        return;
    }

    const QJsonObject snapshot = buildTasksSnapshot(taskIds);
    const QJsonArray tasksArr = snapshot.value(QStringLiteral("tasks")).toArray();
    if (tasksArr.isEmpty()) {
        emit shareError(QStringLiteral("Ни одна из выбранных задач не найдена."));
        return;
    }

    const QByteArray body = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString fileName = tasksArr.size() == 1
        ? QStringLiteral("OpenTodo task — %1 — %2.json")
              .arg(sanitizeForFilename(tasksArr.first().toObject()
                                       .value(QStringLiteral("content")).toString()),
                   stamp)
        : QStringLiteral("OpenTodo tasks (%1) — %2.json")
              .arg(tasksArr.size())
              .arg(stamp);

    QString fileId;
    QString url;
    const bool ok = m_drive->uploadPublicFile(fileName, body,
                                              QStringLiteral("application/json"),
                                              &fileId, &url);
    if (!ok || url.isEmpty()) {
        qCInfo(lcShare) << "shareTasks: upload failed for" << taskIds.size() << "tasks";
        emit shareError(QStringLiteral("Не удалось опубликовать задачи на Google Drive."));
        return;
    }

    qCInfo(lcShare) << "shareTasks: published" << taskIds.size() << "tasks as" << fileId;
    emit shareLinkReady(url, fileId);
}

void ShareManager::revokeShare(const QString &fileId)
{
    if (!m_drive) {
        emit shareError(QStringLiteral("Внутренняя ошибка: ShareManager не инициализирован."));
        return;
    }
    if (!m_drive->isAuthenticated()) {
        emit shareError(QStringLiteral("Подключите Google Drive для отзыва ссылок."));
        return;
    }
    if (fileId.isEmpty()) {
        emit shareError(QStringLiteral("Не указан идентификатор файла."));
        return;
    }

    const bool ok = m_drive->deletePublicFileById(fileId);
    if (!ok) {
        emit shareError(QStringLiteral("Не удалось удалить файл на Google Drive."));
        return;
    }
    emit shareRevoked(fileId);
}

QJsonObject ShareManager::buildProjectSnapshot(const QString &projectId) const
{
    QJsonObject root;
    root[QStringLiteral("format")]         = QStringLiteral("opentodo-shared-project");
    root[QStringLiteral("format_version")] = 1;
    root[QStringLiteral("exported_at")]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    Project *p = m_store->projectById(projectId);
    if (p) root[QStringLiteral("project")] = p->toJson();

    // Sections
    QJsonArray sectionsArr;
    for (const Section *s : m_store->sectionsForProject(projectId)) {
        if (s && !s->isDeleted()) sectionsArr.append(s->toJson());
    }
    root[QStringLiteral("sections")] = sectionsArr;

    // Tasks — collect via tasksForProject AND a sweep of allTasks() so any
    // task whose projectId matches but is somehow missing from the indexed
    // helper (e.g. mid-migration) is also captured. Pattern matches
    // TaskExporter::exportProjectToJson which has the same belt-and-suspenders
    // approach.
    QJsonArray tasksArr;
    QSet<QString> seen;
    for (const Task *t : m_store->tasksForProject(projectId)) {
        if (!t || t->isDeleted()) continue;
        tasksArr.append(t->toJson());
        seen.insert(t->id());
    }
    for (const Task *t : m_store->allTasks()) {
        if (!t || t->isDeleted()) continue;
        if (t->projectId() != projectId) continue;
        if (seen.contains(t->id())) continue;
        tasksArr.append(t->toJson());
        seen.insert(t->id());
    }
    root[QStringLiteral("tasks")] = tasksArr;

    // Comments — both task-level and project-level
    QJsonArray commentsArr;
    for (const QString &tid : std::as_const(seen)) {
        for (const Comment *c : m_store->commentsForTask(tid)) {
            if (c && !c->isDeleted()) commentsArr.append(c->toJson());
        }
    }
    for (const Comment *c : m_store->commentsForProject(projectId)) {
        if (c && !c->isDeleted()) commentsArr.append(c->toJson());
    }
    root[QStringLiteral("comments")] = commentsArr;

    return root;
}

QJsonObject ShareManager::buildTasksSnapshot(const QStringList &taskIds) const
{
    QJsonObject root;
    root[QStringLiteral("format")]         = QStringLiteral("opentodo-shared-tasks");
    root[QStringLiteral("format_version")] = 1;
    root[QStringLiteral("exported_at")]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonArray tasksArr;
    QJsonArray commentsArr;
    QSet<QString> projectIds;
    QSet<QString> includedTaskIds;

    for (const QString &id : taskIds) {
        if (id.isEmpty()) continue;
        Task *t = m_store->taskById(id);
        if (!t || t->isDeleted()) continue;
        if (includedTaskIds.contains(id)) continue;
        tasksArr.append(t->toJson());
        includedTaskIds.insert(id);
        if (!t->projectId().isEmpty())
            projectIds.insert(t->projectId());

        for (const Comment *c : m_store->commentsForTask(id)) {
            if (c && !c->isDeleted()) commentsArr.append(c->toJson());
        }
    }

    root[QStringLiteral("tasks")]    = tasksArr;
    root[QStringLiteral("comments")] = commentsArr;

    // Best-effort: include the owning projects (just for context — the
    // recipient sees the project name and color instead of an opaque
    // project_id reference).
    QJsonArray projectsArr;
    for (const QString &pid : std::as_const(projectIds)) {
        Project *p = m_store->projectById(pid);
        if (p && !p->isDeleted()) projectsArr.append(p->toJson());
    }
    root[QStringLiteral("projects")] = projectsArr;

    return root;
}

QString ShareManager::sanitizeForFilename(const QString &raw)
{
    // Replace anything Drive (or Windows) might choke on with an underscore,
    // collapse whitespace, and clamp the length so the final file name stays
    // well under Drive's 32 KB filename ceiling and OS-level path limits.
    QString cleaned = raw;
    cleaned.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\r\\n\\t]")),
                    QStringLiteral("_"));
    cleaned = cleaned.simplified();
    if (cleaned.isEmpty()) cleaned = QStringLiteral("untitled");
    return cleaned.left(60);
}
