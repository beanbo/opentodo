#include "claudecodelauncher.h"

#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#  include <QProcessEnvironment>
#endif

namespace {

// cmd.exe rejects command lines over 8191 characters ("The command line is
// too long"), which would leave the user staring at an error in the new
// window after launch() already reported success. Leave headroom for the
// cmd.exe / claude paths and switches around the prompt.
constexpr int kMaxPromptChars = 6000;

// Windows Terminal splits its command line into subcommands at every ';'
// that is not preceded by a backslash — even inside a quoted argument — and
// turns "\;" back into ";" afterwards (wt's Commandline::AddArg). Unescaped,
// task text "x; <cmdline>" would make wt open a second tab running <cmdline>.
QString escapeWtDelimiters(QString arg)
{
    arg.replace(QLatin1Char(';'), QStringLiteral("\\;"));
    return arg;
}

}  // namespace

ClaudeCodeLauncher::ClaudeCodeLauncher(QObject *parent)
    : QObject(parent)
{
}

// Threat model: the prompt is untrusted text placed inside ONE double-quoted
// argument that is parsed by cmd.exe (twice in the `start` fallback, plus the
// CALL re-parse, plus claude.cmd's %*). Inside double quotes cmd treats
// & | < > ^ ( ) literally, so the prompt stays inert as long as it can never
// close that quote and nothing else in it is expanded:
//   * '"' would toggle cmd's quote state and expose what follows → it becomes
//     an apostrophe (the model reads 'x' and "x" the same way).
//   * '%' is expanded by cmd even inside quotes, so %SOME_TOKEN% in a synced
//     task would silently paste the user's environment (API keys) into the
//     prompt → it becomes U+FF05 FULLWIDTH PERCENT SIGN.
//   * CR/LF would end the command; other C0 controls are dropped.
//   * A leading '-' would let the task text be parsed by the claude CLI as an
//     option (e.g. a pre-approved tool list) → a space is prepended.
// The fixed hint suffix contains spaces, so Qt and wt always quote the prompt.
QString ClaudeCodeLauncher::buildPrompt(const QString &taskContent,
                                        const QString &taskDescription)
{
    // Point the launched session at the `opentodo` CLI so it can see the rest
    // of this project's tasks and report progress back — not just act on this
    // one task. Requires `opentodo-cli` on PATH. Must stay free of '"', '%'
    // and ';' — it is appended after sanitising.
    static const QString hint = QStringLiteral(
        " (This repo is linked to an OpenTodo project: run "
        "`opentodo-cli list --json` to see all its tasks, "
        "`opentodo-cli done <id>` to report completion, and "
        "`opentodo-cli add <text>` to capture follow-ups.)");

    QString body = taskContent;
    if (!taskDescription.trimmed().isEmpty())
        body += QStringLiteral("\n\n") + taskDescription;

    QString sanitized;
    sanitized.reserve(body.size());
    for (QChar ch : body) {
        const char16_t u = ch.unicode();
        if (u == u'\n' || u == u'\r')
            sanitized.append(QLatin1Char(' '));
        else if (u == u'"')
            sanitized.append(QLatin1Char('\''));
        else if (u == u'%')
            sanitized.append(QChar(0xFF05));
        else if (u >= 0x20 || u == u'\t')
            sanitized.append(ch);
        // else: drop other C0 control codes (BEL, ESC, etc.)
    }

    const int budget = kMaxPromptChars - int(hint.size());
    if (sanitized.size() > budget) {
        int cut = budget - 1;  // room for the ellipsis
        if (cut > 0 && sanitized.at(cut - 1).isHighSurrogate())
            --cut;             // don't split a surrogate pair
        sanitized.truncate(cut);
        sanitized.append(QChar(0x2026));
    }

    QString prompt = sanitized + hint;
    if (prompt.startsWith(QLatin1Char('-')))
        prompt.prepend(QLatin1Char(' '));
    return prompt;
}

// `call` keeps the first character after /k from being a quote. Without it a
// claude path with a space ("C:\Users\First Last\…\claude.cmd", which Qt / wt
// quote) triggers cmd's /K rule "more than two quotes ⇒ strip the first and
// the last one": the launch fails AND the prompt is shifted outside its
// quotes, where & | < > in the task text become live. (Side effect of CALL:
// a '^' in the prompt reaches claude doubled — cosmetic.)
QStringList ClaudeCodeLauncher::windowsTerminalArgs(const QString &nativeRepo,
                                                    const QString &cmdExe,
                                                    const QString &claudePath,
                                                    const QString &prompt)
{
    QStringList args{ QStringLiteral("-d"), nativeRepo,
                      cmdExe, QStringLiteral("/k"), QStringLiteral("call"),
                      claudePath, prompt };
    for (QString &a : args)
        a = escapeWtDelimiters(a);
    return args;
}

QStringList ClaudeCodeLauncher::cmdStartArgs(const QString &cmdExe,
                                             const QString &claudePath,
                                             const QString &prompt)
{
    // cmd.exe /c start "Claude Code" <cmdExe> /k call <claude> <prompt>
    // ("Claude Code" is start's window-title argument.)
    return { QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("Claude Code"),
             cmdExe, QStringLiteral("/k"), QStringLiteral("call"),
             claudePath, prompt };
}

bool ClaudeCodeLauncher::launch(const QString &taskContent,
                                const QString &taskDescription,
                                const QString &repoPath)
{
#ifdef Q_OS_WIN
    // ── Validate repoPath ────────────────────────────────────────────────────
    // Reject empty / whitespace-only path: `wt -d ""` opens in the user's
    // home and the cmd fallback would start in OpenTodo's own cwd — either
    // way a session in the wrong directory that we'd misreport as
    // "Запущено в новом окне".
    if (repoPath.trimmed().isEmpty())
        return false;
    // The directory must actually exist. linkedRepoPath round-trips through
    // sync, so a peer can land a stale path here without QML ever validating.
    if (!QFileInfo(repoPath).isDir())
        return false;

    // ── Locate the claude CLI ────────────────────────────────────────────────
    // 1. Try PATH-based lookup (covers global installs and npm-linked CLIs).
    QString claudePath = QStandardPaths::findExecutable(QStringLiteral("claude"));

    // 2. Common npm-global locations not always on PATH in every terminal.
    if (claudePath.isEmpty()) {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QStringList candidates = {
            env.value(QStringLiteral("APPDATA"))   + QStringLiteral("\\npm\\claude.cmd"),
            env.value(QStringLiteral("LOCALAPPDATA")) + QStringLiteral("\\npm\\claude.cmd"),
        };
        for (const QString &c : candidates) {
            if (QFileInfo::exists(c)) {
                claudePath = c;
                break;
            }
        }
    }

    if (claudePath.isEmpty()) {
        // Claude CLI not found — caller must show an install hint.
        return false;
    }

    const QString prompt = buildPrompt(taskContent, taskDescription);

    // Absolute cmd.exe (%ComSpec%) rather than a bare "cmd": `start` resolves
    // a bare name through the current directory first, and in the fallback
    // below that directory is the (synced, not necessarily trusted) repo.
    QString cmdExe = qEnvironmentVariable("ComSpec");
    if (!QFileInfo(cmdExe).isAbsolute() || !QFileInfo::exists(cmdExe))
        cmdExe = QStandardPaths::findExecutable(QStringLiteral("cmd"));
    if (cmdExe.isEmpty())
        cmdExe = QStringLiteral("cmd.exe");
    cmdExe = QDir::toNativeSeparators(cmdExe);

    // ── Detect preferred terminal ────────────────────────────────────────────
    const QString wt = QStandardPaths::findExecutable(QStringLiteral("wt"));

    // Normalise repoPath to native separators so wt / cmd are happy.
    const QString nativeRepo = QDir::toNativeSeparators(repoPath);

    // The process is started DETACHED so the terminal lives independently of
    // OpenTodo — closing or crashing OpenTodo does not kill the Claude session.
    if (!wt.isEmpty())
        return QProcess::startDetached(
            wt, windowsTerminalArgs(nativeRepo, cmdExe, QDir::toNativeSeparators(claudePath), prompt));

    // Fallback: the outer cmd.exe runs IN the repo and `start` passes that
    // directory to the new window, so the repo path never has to be quoted
    // onto a cmd.exe command line (a directory named "a&b" would split it).
    return QProcess::startDetached(
        cmdExe, cmdStartArgs(cmdExe, QDir::toNativeSeparators(claudePath), prompt), nativeRepo);

#else
    // TODO: implement for Linux (xterm / gnome-terminal / konsole) and macOS (Terminal.app / iTerm2).
    Q_UNUSED(taskContent)
    Q_UNUSED(taskDescription)
    Q_UNUSED(repoPath)
    return false;
#endif
}

QString ClaudeCodeLauncher::opentodoCliPath() const
{
    // PATH first — respects a global install or an `opentodo-cli` alias the user
    // set up. Otherwise fall back to the binary shipped next to this app: the
    // CLI target is emitted into the same build/deploy directory as the GUI exe
    // (see CMakeLists "opentodo-cli" target), so applicationDirPath() resolves it.
    QString cli = QStandardPaths::findExecutable(QStringLiteral("opentodo-cli"));
    if (cli.isEmpty()) {
        const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
        cli = QDir(dir).filePath(QStringLiteral("opentodo-cli.exe"));
#else
        cli = QDir(dir).filePath(QStringLiteral("opentodo-cli"));
#endif
    }
    return QDir::toNativeSeparators(cli);
}
