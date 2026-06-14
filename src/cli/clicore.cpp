#include "cli/clicore.h"

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/comment.h"
#include "nlp/dateparser.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {

// Canonicalize a filesystem path for comparison. canonicalFilePath() resolves
// symlinks / ".." and only succeeds for existing paths; fall back to a cleaned
// absolute path otherwise. Lower-cased + forward slashes so a linked_repo_path
// stored as "C:\Main\…" matches a cwd reported as "C:/main/…" on Windows.
QString canonPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    QFileInfo fi(trimmed);
    QString c = fi.canonicalFilePath();
    if (c.isEmpty())
        c = QDir::cleanPath(fi.absoluteFilePath());
    return QDir::fromNativeSeparators(c).toLower();
}

} // namespace

QJsonObject clicore::taskToJson(const Task *t, const DataStore *store)
{
    QJsonObject o;
    o[QStringLiteral("id")] = t->id();
    o[QStringLiteral("content")] = t->content();
    if (!t->description().isEmpty())
        o[QStringLiteral("description")] = t->description();
    o[QStringLiteral("project_id")] = t->projectId();
    if (store) {
        if (Project *p = store->projectById(t->projectId()))
            o[QStringLiteral("project")] = p->name();
    }
    o[QStringLiteral("priority")] = t->priority();
    if (!t->labels().isEmpty())
        o[QStringLiteral("labels")] = QJsonArray::fromStringList(t->labels());

    const QString due = t->dueDatetime().isEmpty() ? t->dueDate() : t->dueDatetime();
    if (!due.isEmpty())
        o[QStringLiteral("due")] = due;
    if (!t->dueString().isEmpty())
        o[QStringLiteral("due_string")] = t->dueString();
    if (t->dueIsRecurring())
        o[QStringLiteral("recurring")] = true;
    if (!t->deadline().isEmpty())
        o[QStringLiteral("deadline")] = t->deadline();

    o[QStringLiteral("is_completed")] = t->isCompleted();
    if (!t->parentId().isEmpty())
        o[QStringLiteral("parent_id")] = t->parentId();
    if (t->createdAt().isValid())
        o[QStringLiteral("created_at")] = t->createdAt().toUTC().toString(Qt::ISODate);
    if (t->updatedAt().isValid())
        o[QStringLiteral("updated_at")] = t->updatedAt().toUTC().toString(Qt::ISODate);
    return o;
}

Project *clicore::resolveProject(const DataStore *store, const QString &repoPath,
                                 const QString &projectName, bool *matchedByRepo)
{
    if (matchedByRepo)
        *matchedByRepo = false;
    const QList<Project *> projects = store->allProjects();

    // Explicit --project name wins over repo auto-scope.
    const QString name = projectName.trimmed();
    if (!name.isEmpty()) {
        for (Project *p : projects)
            if (!p->isDeleted() && p->name().compare(name, Qt::CaseInsensitive) == 0)
                return p;
        return nullptr;
    }

    const QString want = canonPath(repoPath);
    if (want.isEmpty())
        return nullptr;
    for (Project *p : projects) {
        if (p->isDeleted())
            continue;
        const QString linked = canonPath(p->linkedRepoPath());
        if (!linked.isEmpty() && linked == want) {
            if (matchedByRepo)
                *matchedByRepo = true;
            return p;
        }
    }
    return nullptr;
}

QList<Task *> clicore::scopedTasks(const DataStore *store, const Project *project,
                                   bool includeCompleted)
{
    const QList<Task *> source = project ? store->tasksForProject(project->id())
                                         : store->allTasks();
    QList<Task *> out;
    out.reserve(source.size());
    for (Task *t : source) {
        if (t->isDeleted())
            continue;
        if (!includeCompleted && t->isCompleted())
            continue;
        out.append(t);
    }
    std::sort(out.begin(), out.end(), [](const Task *a, const Task *b) {
        if (a->priority() != b->priority())
            return a->priority() < b->priority(); // 1 (urgent) first
        return a->order() < b->order();
    });
    return out;
}

bool clicore::completeTaskOffline(DataStore *store, const QString &id, QString *outError)
{
    Task *t = store->taskById(id);
    if (!t) {
        if (outError) *outError = QStringLiteral("task not found");
        return false;
    }
    if (t->isCompleted()) {
        if (outError) *outError = QStringLiteral("task already completed");
        return false;
    }
    // Recurrence advancing lives in TaskViewModel::completeTask — refuse here so
    // a CLI completion never slides the schedule. Run it via the live app.
    if (t->dueIsRecurring() && !t->dueString().isEmpty()) {
        if (outError)
            *outError = QStringLiteral("recurring task — complete it from the running app so the schedule advances correctly");
        return false;
    }
    t->setIsCompleted(true);
    t->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // The CRDT _meta HLC stamp (NOT a sync commit) is what makes this win LWW on
    // the next sync — mirrors TaskViewModel::completeTask.
    store->markTaskFieldsModified(id, {QStringLiteral("is_completed"),
                                       QStringLiteral("completed_at")});
    return true;
}

QString clicore::addTaskOffline(DataStore *store, const QString &content,
                                const QString &projectId, int priority,
                                const QString &dueString, const QString &lang)
{
    if (content.trimmed().isEmpty())
        return {};
    QString pid = projectId;
    if (pid.isEmpty()) {
        if (Project *inbox = store->inboxProject())
            pid = inbox->id();
    }
    Task *t = store->addTask(content, pid);
    if (!t)
        return {};

    QStringList fields;
    if (priority >= 1 && priority <= 4) {
        t->setPriority(priority);
        fields << QStringLiteral("priority");
    }

    const QString useLang = lang.isEmpty() ? QStringLiteral("en") : lang;
    if (!dueString.trimmed().isEmpty()) {
        DateParser parser;
        const DateParseResult r = parser.parse(dueString, useLang);
        if (r.valid) {
            t->setDueString(dueString);
            t->setDueDate(r.date);
            if (!r.datetime.isEmpty())
                t->setDueDatetime(r.datetime);
            t->setDueIsRecurring(r.isRecurring);
            t->setDueRecurrenceRule(r.recurrenceRule);
            t->setDueLang(useLang);
            fields << QStringLiteral("due");
        }
    }
    t->setUpdatedAt(QDateTime::currentDateTimeUtc());
    if (!fields.isEmpty())
        store->markTaskFieldsModified(t->id(), fields);
    return t->id();
}

QString clicore::addCommentOffline(DataStore *store, const QString &taskId,
                                   const QString &text, const QString &author)
{
    Task *t = store->taskById(taskId);
    if (!t || text.trimmed().isEmpty())
        return {};
    Comment *c = store->addComment(text, taskId, t->projectId(), author);
    return c ? c->id() : QString();
}

QJsonObject clicore::handleRequest(DataStore *store, const QJsonObject &req)
{
    const QString cmd = req.value(QStringLiteral("cmd")).toString();

    auto fail = [](const QString &msg) {
        QJsonObject o;
        o[QStringLiteral("ok")] = false;
        o[QStringLiteral("error")] = msg;
        return o;
    };
    auto okData = [](const QJsonValue &d) {
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("data")] = d;
        return o;
    };

    const QString repo = req.value(QStringLiteral("repo")).toString();
    const QString projectName = req.value(QStringLiteral("project")).toString();

    if (cmd == QLatin1String("list") || cmd == QLatin1String("next")) {
        bool byRepo = false;
        Project *p = resolveProject(store, repo, projectName, &byRepo);
        const bool scopeRequested = !projectName.trimmed().isEmpty() || !repo.trimmed().isEmpty();
        if (scopeRequested && !p && !req.value(QStringLiteral("all")).toBool())
            return fail(QStringLiteral("no OpenTodo project is linked to this scope — "
                                       "pass --project <name>, --all, or set the project's "
                                       "linked repo path in the app"));

        QList<Task *> tasks = scopedTasks(store, p, req.value(QStringLiteral("include_completed")).toBool());

        if (cmd == QLatin1String("next"))
            return okData(tasks.isEmpty() ? QJsonValue(QJsonValue::Null)
                                          : QJsonValue(taskToJson(tasks.first(), store)));

        const int limit = req.value(QStringLiteral("limit")).toInt(0);
        QJsonArray arr;
        for (Task *t : tasks) {
            if (limit > 0 && arr.size() >= limit)
                break;
            arr.append(taskToJson(t, store));
        }
        QJsonObject data;
        data[QStringLiteral("scope")] = p ? p->name() : QStringLiteral("all projects");
        data[QStringLiteral("count")] = arr.size();
        data[QStringLiteral("tasks")] = arr;
        return okData(data);
    }

    if (cmd == QLatin1String("show")) {
        Task *t = store->taskById(req.value(QStringLiteral("id")).toString());
        if (!t)
            return fail(QStringLiteral("task not found"));
        QJsonObject o = taskToJson(t, store);
        QJsonArray comments;
        for (Comment *c : store->commentsForTask(t->id())) {
            QJsonObject co;
            co[QStringLiteral("text")] = c->content();
            if (!c->author().isEmpty())
                co[QStringLiteral("author")] = c->author();
            comments.append(co);
        }
        if (!comments.isEmpty())
            o[QStringLiteral("comments")] = comments;
        QJsonArray subs;
        for (Task *s : store->subtasks(t->id())) {
            if (s->isDeleted())
                continue;
            subs.append(taskToJson(s, store));
        }
        if (!subs.isEmpty())
            o[QStringLiteral("subtasks")] = subs;
        return okData(o);
    }

    if (cmd == QLatin1String("done")) {
        const QString id = req.value(QStringLiteral("id")).toString();
        QString error;
        if (!completeTaskOffline(store, id, &error))
            return fail(error);
        const QString note = req.value(QStringLiteral("note")).toString();
        if (!note.trimmed().isEmpty())
            addCommentOffline(store, id, note, QStringLiteral("claude"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        d[QStringLiteral("mode")] = QStringLiteral("completed");
        return okData(d);
    }

    if (cmd == QLatin1String("add")) {
        Project *p = resolveProject(store, repo, projectName, nullptr);
        const QString id = addTaskOffline(store,
                                          req.value(QStringLiteral("content")).toString(),
                                          p ? p->id() : QString(),
                                          req.value(QStringLiteral("priority")).toInt(4),
                                          req.value(QStringLiteral("due")).toString(),
                                          req.value(QStringLiteral("lang")).toString());
        if (id.isEmpty())
            return fail(QStringLiteral("failed to add task"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        return okData(d);
    }

    if (cmd == QLatin1String("comment")) {
        const QString id = addCommentOffline(store,
                                             req.value(QStringLiteral("id")).toString(),
                                             req.value(QStringLiteral("text")).toString(),
                                             QStringLiteral("claude"));
        if (id.isEmpty())
            return fail(QStringLiteral("failed to add comment (task not found?)"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        return okData(d);
    }

    if (cmd == QLatin1String("delete")) {
        const QString id = req.value(QStringLiteral("id")).toString();
        if (!store->taskById(id))
            return fail(QStringLiteral("task not found"));
        if (!store->removeTask(id)) // soft-delete: tombstone, recoverable in Trash
            return fail(QStringLiteral("failed to delete task"));
        QJsonObject d;
        d[QStringLiteral("id")] = id;
        d[QStringLiteral("deleted")] = true;
        return okData(d);
    }

    return fail(QStringLiteral("unknown command: ") + cmd);
}
