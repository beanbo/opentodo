// opentodo — command-line interface to the local OpenTodo task store.
//
// Built for three audiences: the user at a terminal, scripts/hooks, and Claude
// Code running inside a project repo that is linked to an OpenTodo project.
//
// Hybrid transport:
//   * If the OpenTodo desktop app is running, we connect to its CliBridge over a
//     local socket and let the LIVE app apply changes — so karma, autosave and
//     Google-Drive sync all fire exactly as if the user clicked in the UI.
//   * If the app is not running, we fall back to operating on state.json
//     directly (read is always safe; writes stamp the CRDT _meta and save
//     immediately, which is race-free precisely because no app is writing).
//
// Commands: list | next | show <id> | done <id> | add <content> | comment <id> <text>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/task.h"
#include "cli/clicore.h"

namespace {

const char *const kServerName = "OpenTodoCli";

QString jsonString(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("null\n");
    return v.toVariant().toString() + QLatin1Char('\n');
}

QString priorityTag(int p)
{
    switch (p) {
    case 1: return QStringLiteral("[p1]");
    case 2: return QStringLiteral("[p2]");
    case 3: return QStringLiteral("[p3]");
    default: return QStringLiteral("[p4]");
    }
}

// Talk to the running app's CliBridge. Returns the parsed reply and sets
// *connected = true when the app answered (even if it answered with an error).
QJsonObject tryOnline(const QJsonObject &req, bool *connected)
{
    *connected = false;
    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kServerName));
    if (!sock.waitForConnected(400))
        return {};
    *connected = true;

    QByteArray payload = QJsonDocument(req).toJson(QJsonDocument::Compact);
    payload.append('\n');
    sock.write(payload);
    sock.flush();

    QByteArray buf;
    while (!buf.contains('\n')) {
        if (!sock.waitForReadyRead(5000))
            break;
        buf += sock.readAll();
    }
    sock.disconnectFromServer();

    const int nl = buf.indexOf('\n');
    const QByteArray line = (nl >= 0) ? buf.left(nl) : buf;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject o;
        o[QStringLiteral("ok")] = false;
        o[QStringLiteral("error")] = QStringLiteral("malformed reply from app");
        return o;
    }
    return doc.object();
}

void renderTaskLine(QTextStream &out, const QJsonObject &t)
{
    out << "  " << priorityTag(t.value(QStringLiteral("priority")).toInt(4))
        << ' ' << t.value(QStringLiteral("content")).toString();
    if (t.contains(QStringLiteral("project")))
        out << "  #" << t.value(QStringLiteral("project")).toString();
    if (t.contains(QStringLiteral("due")))
        out << "  @" << t.value(QStringLiteral("due")).toString();
    if (t.value(QStringLiteral("recurring")).toBool())
        out << " ↻";
    out << "   [" << t.value(QStringLiteral("id")).toString().left(8) << "]\n";
}

void renderSuccess(QTextStream &out, const QString &cmd, const QJsonValue &data,
                   bool jsonMode, bool online)
{
    if (jsonMode) {
        out << jsonString(data);
        return;
    }

    if (cmd == QLatin1String("list")) {
        const QJsonObject d = data.toObject();
        const QJsonArray tasks = d.value(QStringLiteral("tasks")).toArray();
        out << d.value(QStringLiteral("count")).toInt() << " task(s) in "
            << d.value(QStringLiteral("scope")).toString() << ":\n";
        for (const QJsonValue &v : tasks)
            renderTaskLine(out, v.toObject());
        if (tasks.isEmpty())
            out << "  (nothing to do)\n";
    } else if (cmd == QLatin1String("next")) {
        if (data.isNull()) {
            out << "(no actionable task)\n";
        } else {
            out << "Next:\n";
            renderTaskLine(out, data.toObject());
        }
    } else if (cmd == QLatin1String("show")) {
        const QJsonObject t = data.toObject();
        out << t.value(QStringLiteral("content")).toString() << "\n";
        out << "  id:       " << t.value(QStringLiteral("id")).toString() << "\n";
        out << "  priority: p" << t.value(QStringLiteral("priority")).toInt(4) << "\n";
        if (t.contains(QStringLiteral("project")))
            out << "  project:  " << t.value(QStringLiteral("project")).toString() << "\n";
        if (t.contains(QStringLiteral("due")))
            out << "  due:      " << t.value(QStringLiteral("due")).toString() << "\n";
        if (t.contains(QStringLiteral("description")))
            out << "\n" << t.value(QStringLiteral("description")).toString() << "\n";
        const QJsonArray subs = t.value(QStringLiteral("subtasks")).toArray();
        if (!subs.isEmpty()) {
            out << "\n  subtasks:\n";
            for (const QJsonValue &v : subs)
                renderTaskLine(out, v.toObject());
        }
        const QJsonArray comments = t.value(QStringLiteral("comments")).toArray();
        if (!comments.isEmpty()) {
            out << "\n  comments:\n";
            for (const QJsonValue &v : comments)
                out << "  - " << v.toObject().value(QStringLiteral("text")).toString() << "\n";
        }
    } else if (cmd == QLatin1String("done")) {
        out << "✓ completed " << data.toObject().value(QStringLiteral("id")).toString() << "\n";
    } else if (cmd == QLatin1String("add")) {
        out << "+ added " << data.toObject().value(QStringLiteral("id")).toString() << "\n";
    } else if (cmd == QLatin1String("comment")) {
        out << "+ comment added\n";
    } else if (cmd == QLatin1String("delete")) {
        out << "🗑 deleted " << data.toObject().value(QStringLiteral("id")).toString() << "\n";
    }

    if (!online && (cmd == QLatin1String("done") || cmd == QLatin1String("add")
                    || cmd == QLatin1String("comment") || cmd == QLatin1String("delete")))
        out << "  (applied offline — app was not running)\n";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Match the desktop app's identifiers EXACTLY so QStandardPaths resolves the
    // same state.json and QSettings reads the same sync/device_id.
    QCoreApplication::setApplicationName(QStringLiteral("OpenTodo"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTodo"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("opentodo.org"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "opentodo — read and update OpenTodo tasks from the command line.\n\n"
        "Commands:\n"
        "  list                 list active tasks (auto-scoped to this repo)\n"
        "  next                 print the single highest-priority task\n"
        "  show <id>            show one task with its subtasks and comments\n"
        "  done <id>           mark a task completed (optionally --note)\n"
        "  add  <content>      create a task (optionally --due/--priority)\n"
        "  comment <id> <text> append a comment to a task\n"
        "  delete <id>         soft-delete a task (recoverable in Trash)");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("list|next|show|done|add|comment"));
    parser.addPositionalArgument(QStringLiteral("args"),
                                 QStringLiteral("task id / content / text"),
                                 QStringLiteral("[args...]"));

    QCommandLineOption optJson(QStringLiteral("json"),
                               QStringLiteral("Emit JSON instead of human-readable text."));
    QCommandLineOption optProject({QStringLiteral("p"), QStringLiteral("project")},
                                  QStringLiteral("Scope to an OpenTodo project by name."),
                                  QStringLiteral("name"));
    QCommandLineOption optRepo(QStringLiteral("repo"),
                               QStringLiteral("Scope by repo path (default: current directory)."),
                               QStringLiteral("path"));
    QCommandLineOption optAll(QStringLiteral("all"),
                              QStringLiteral("Act across all projects (do not auto-scope)."));
    QCommandLineOption optPriority(QStringLiteral("priority"),
                                   QStringLiteral("Priority 1..4 (1 = urgent) for add."),
                                   QStringLiteral("n"), QStringLiteral("4"));
    QCommandLineOption optDue(QStringLiteral("due"),
                              QStringLiteral("Natural-language due date for add."),
                              QStringLiteral("str"));
    QCommandLineOption optNote(QStringLiteral("note"),
                               QStringLiteral("Comment to attach when completing a task."),
                               QStringLiteral("text"));
    QCommandLineOption optLang(QStringLiteral("lang"),
                               QStringLiteral("Language for due parsing (en|ru)."),
                               QStringLiteral("lang"), QStringLiteral("ru"));
    QCommandLineOption optLimit(QStringLiteral("limit"),
                                QStringLiteral("Max tasks to print for list (0 = no limit)."),
                                QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption optIncludeCompleted(QStringLiteral("include-completed"),
                                           QStringLiteral("Include completed tasks in list."));
    parser.addOptions({optJson, optProject, optRepo, optAll, optPriority, optDue,
                       optNote, optLang, optLimit, optIncludeCompleted});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList pos = parser.positionalArguments();
    if (pos.isEmpty()) {
        err << "error: no command. Run with --help.\n";
        return 2;
    }
    const QString cmd = pos.first().toLower();
    static const QStringList known = {QStringLiteral("list"), QStringLiteral("next"),
                                      QStringLiteral("show"), QStringLiteral("done"),
                                      QStringLiteral("add"), QStringLiteral("comment"),
                                      QStringLiteral("delete")};
    if (!known.contains(cmd)) {
        err << "error: unknown command '" << cmd << "'. Run with --help.\n";
        return 2;
    }

    // ── Build the request object ─────────────────────────────────────────────
    QJsonObject req;
    req[QStringLiteral("cmd")] = cmd;
    if (parser.isSet(optProject))
        req[QStringLiteral("project")] = parser.value(optProject);
    req[QStringLiteral("repo")] = parser.isSet(optRepo) ? parser.value(optRepo)
                                                        : QDir::currentPath();
    if (parser.isSet(optAll))
        req[QStringLiteral("all")] = true;
    if (parser.isSet(optLimit))
        req[QStringLiteral("limit")] = parser.value(optLimit).toInt();
    if (parser.isSet(optIncludeCompleted))
        req[QStringLiteral("include_completed")] = true;
    req[QStringLiteral("lang")] = parser.value(optLang);

    if (cmd == QLatin1String("show") || cmd == QLatin1String("done")
        || cmd == QLatin1String("comment") || cmd == QLatin1String("delete")) {
        if (pos.size() < 2) {
            err << "error: '" << cmd << "' needs a task id.\n";
            return 2;
        }
        req[QStringLiteral("id")] = pos.at(1);
    }
    if (cmd == QLatin1String("done") && parser.isSet(optNote))
        req[QStringLiteral("note")] = parser.value(optNote);
    if (cmd == QLatin1String("comment")) {
        if (pos.size() < 3) {
            err << "error: 'comment <id> <text>' needs comment text.\n";
            return 2;
        }
        req[QStringLiteral("text")] = QStringList(pos.mid(2)).join(QLatin1Char(' '));
    }
    if (cmd == QLatin1String("add")) {
        if (pos.size() < 2) {
            err << "error: 'add <content>' needs task content.\n";
            return 2;
        }
        req[QStringLiteral("content")] = QStringList(pos.mid(1)).join(QLatin1Char(' '));
        req[QStringLiteral("priority")] = parser.value(optPriority).toInt();
        if (parser.isSet(optDue))
            req[QStringLiteral("due")] = parser.value(optDue);
    }

    const bool isWrite = (cmd == QLatin1String("done") || cmd == QLatin1String("add")
                          || cmd == QLatin1String("comment") || cmd == QLatin1String("delete"));

    // ── Try the live app first; fall back to direct state.json access ────────
    bool online = false;
    QJsonObject reply = tryOnline(req, &online);
    if (!online) {
        DataStore store;
        FileManager fm(&store);
        fm.load();
        reply = clicore::handleRequest(&store, req);
        if (isWrite && reply.value(QStringLiteral("ok")).toBool())
            fm.saveImmediate();
    }

    // ── Render ───────────────────────────────────────────────────────────────
    const bool jsonMode = parser.isSet(optJson);
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        const QString msg = reply.value(QStringLiteral("error")).toString(QStringLiteral("unknown error"));
        if (jsonMode) {
            QJsonObject eo;
            eo[QStringLiteral("error")] = msg;
            err << QString::fromUtf8(QJsonDocument(eo).toJson(QJsonDocument::Compact)) << "\n";
        } else {
            err << "error: " << msg << "\n";
        }
        return 1;
    }

    renderSuccess(out, cmd, reply.value(QStringLiteral("data")), jsonMode, online);
    return 0;
}
