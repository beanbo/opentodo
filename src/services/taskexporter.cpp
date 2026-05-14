#include "taskexporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/comment.h"
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

// CSV escape: wrap in quotes, escape internal quotes by doubling.
static QString csvEscape(const QString &s) {
    if (s.contains(QChar(',')) || s.contains(QChar('"')) || s.contains(QChar('\n')))
        return QStringLiteral("\"%1\"").arg(QString(s).replace(QChar('"'), QStringLiteral("\"\"")));
    return s;
}

TaskExporter::TaskExporter(DataStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

QString TaskExporter::exportToCsv(const QString &dir)
{
    const QString outDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : dir;
    QDir().mkpath(outDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString path = outDir + QStringLiteral("/opentodo-export-") + stamp + QStringLiteral(".csv");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFailed(QStringLiteral("Не удалось открыть файл для записи: ") + path);
        return {};
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    // Header
    out << "id,project,section,content,description,priority,due_date,labels,is_completed,created_at\n";

    for (const Task *t : m_store->allTasks()) {
        if (t->isDeleted()) continue;
        Project *p = m_store->projectById(t->projectId());
        Section *s = m_store->sectionById(t->sectionId());
        out << csvEscape(t->id()) << ","
            << csvEscape(p ? p->name() : QString()) << ","
            << csvEscape(s ? s->name() : QString()) << ","
            << csvEscape(t->content()) << ","
            << csvEscape(t->description()) << ","
            << t->priority() << ","
            << csvEscape(t->dueDate()) << ","
            << csvEscape(t->labels().join(QChar('|'))) << ","
            << (t->isCompleted() ? "true" : "false") << ","
            << csvEscape(t->createdAt().toString(Qt::ISODate)) << "\n";
    }
    f.close();
    emit exportSucceeded(path);
    return path;
}

QString TaskExporter::exportToMarkdown(const QString &dir)
{
    const QString outDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : dir;
    QDir().mkpath(outDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString path = outDir + QStringLiteral("/opentodo-export-") + stamp + QStringLiteral(".md");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFailed(QStringLiteral("Не удалось открыть файл для записи: ") + path);
        return {};
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "# OpenTodo — экспорт от "
        << QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))
        << "\n\n";

    for (const Project *p : m_store->allProjects()) {
        if (p->isDeleted() || p->isArchived()) continue;
        out << "## " << p->name() << "\n\n";
        const auto tasks = m_store->tasksForProject(p->id());
        for (const Task *t : tasks) {
            if (t->isDeleted()) continue;
            const QChar mark = t->isCompleted() ? QChar('x') : QChar(' ');
            out << "- [" << mark << "] " << t->content();
            if (!t->dueDate().isEmpty()) out << " \xF0\x9F\x93\x85 " << t->dueDate();
            if (t->priority() < 4) out << " \xE2\x9A\x91P" << t->priority();
            out << "\n";
            if (!t->description().isEmpty())
                out << "  > " << QString(t->description()).replace(QStringLiteral("\n"), QStringLiteral(" ")) << "\n";
        }
        out << "\n";
    }
    f.close();
    emit exportSucceeded(path);
    return path;
}

QString TaskExporter::exportProjectToJson(const QString &projectId,
                                           const QString &dir)
{
    Project *p = m_store->projectById(projectId);
    if (!p) {
        emit exportFailed(QStringLiteral("Проект не найден"));
        return {};
    }

    const QString outDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : dir;
    QDir().mkpath(outDir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString safeName = QString(p->name())
        .replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"))
        .left(80);
    const QString path = outDir + QStringLiteral("/opentodo-project-") + safeName
                         + QStringLiteral("-") + stamp + QStringLiteral(".json");

    QJsonObject root;
    root[QStringLiteral("format")]         = QStringLiteral("opentodo-project-snapshot");
    root[QStringLiteral("format_version")] = 1;
    root[QStringLiteral("exported_at")]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root[QStringLiteral("project")]        = p->toJson();

    // Sections
    QJsonArray sectionsArr;
    for (const Section *s : m_store->sectionsForProject(projectId)) {
        if (s) sectionsArr.append(s->toJson());
    }
    root[QStringLiteral("sections")] = sectionsArr;

    // Tasks — start with tasksForProject, then sweep allTasks to pick up any
    // subtasks or tasks that tasksForProject might have omitted.
    QJsonArray tasksArr;
    QSet<QString> projectTaskIds;

    for (const Task *t : m_store->tasksForProject(projectId)) {
        if (!t || t->isDeleted()) continue;
        tasksArr.append(t->toJson());
        projectTaskIds.insert(t->id());
    }

    for (const Task *t : m_store->allTasks()) {
        if (!t || t->isDeleted()) continue;
        if (t->projectId() != projectId) continue;
        if (projectTaskIds.contains(t->id())) continue;
        tasksArr.append(t->toJson());
        projectTaskIds.insert(t->id());
    }
    root[QStringLiteral("tasks")] = tasksArr;

    // Comments — task-level then project-level
    QJsonArray commentsArr;
    for (const QString &tid : std::as_const(projectTaskIds)) {
        for (const Comment *c : m_store->commentsForTask(tid)) {
            if (c && !c->isDeleted()) commentsArr.append(c->toJson());
        }
    }
    for (const Comment *c : m_store->commentsForProject(projectId)) {
        if (c && !c->isDeleted()) commentsArr.append(c->toJson());
    }
    root[QStringLiteral("comments")] = commentsArr;

    // Write
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        emit exportFailed(QStringLiteral("Не удалось открыть файл для записи: ") + path);
        return {};
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();

    emit exportSucceeded(path);
    return path;
}
