#include "claudecodelauncher.h"

#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

#ifdef Q_OS_WIN
#  include <QProcessEnvironment>
#endif

ClaudeCodeLauncher::ClaudeCodeLauncher(QObject *parent)
    : QObject(parent)
{
}

bool ClaudeCodeLauncher::launch(const QString &taskContent,
                                const QString &taskDescription,
                                const QString &repoPath)
{
#ifdef Q_OS_WIN
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

    // ── Build the prompt ─────────────────────────────────────────────────────
    QString prompt = taskContent;
    if (!taskDescription.trimmed().isEmpty())
        prompt += QStringLiteral("\n\n") + taskDescription;

    // Windows cmd.exe quoting: escape embedded double-quotes as "".
    // Newlines survive as-is inside a cmd /k argument when passed via
    // QProcess argument list (Qt handles the shell escaping per-argument).
    QString escapedPrompt = prompt;
    escapedPrompt.replace(QStringLiteral("\""), QStringLiteral("\"\""));

    // ── Detect preferred terminal ────────────────────────────────────────────
    const QString wt = QStandardPaths::findExecutable(QStringLiteral("wt"));

    // ── Build argument list and launch ───────────────────────────────────────
    // The process is started DETACHED so the terminal lives independently of
    // OpenTodo — closing or crashing OpenTodo does not kill the Claude session.
    QString program;
    QStringList args;

    // Normalise repoPath to native separators so wt / cmd are happy.
    const QString nativeRepo = QDir::toNativeSeparators(repoPath);

    if (!wt.isEmpty()) {
        // wt.exe -d "<repoPath>" cmd /k claude "<prompt>"
        program = wt;
        args << QStringLiteral("-d") << nativeRepo
             << QStringLiteral("cmd") << QStringLiteral("/k")
             << claudePath
             << escapedPrompt;
    } else {
        // cmd /c start "Claude Code" /D "<repoPath>" cmd /k claude "<prompt>"
        program = QStringLiteral("cmd.exe");
        args << QStringLiteral("/c")
             << QStringLiteral("start")
             << QStringLiteral("Claude Code")
             << QStringLiteral("/D") << nativeRepo
             << QStringLiteral("cmd") << QStringLiteral("/k")
             << claudePath
             << escapedPrompt;
    }

    return QProcess::startDetached(program, args);

#else
    // TODO: implement for Linux (xterm / gnome-terminal / konsole) and macOS (Terminal.app / iTerm2).
    Q_UNUSED(taskContent)
    Q_UNUSED(taskDescription)
    Q_UNUSED(repoPath)
    return false;
#endif
}
