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
    // ── Validate repoPath ────────────────────────────────────────────────────
    // Reject empty / whitespace-only path: `wt -d ""` opens in the user's
    // home and `cmd /D ""` errors silently, both producing a flash-and-die
    // terminal that we'd misreport as "Запущено в новом окне".
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

    // ── Build the prompt ─────────────────────────────────────────────────────
    QString prompt = taskContent;
    if (!taskDescription.trimmed().isEmpty())
        prompt += QStringLiteral("\n\n") + taskDescription;

    // Strip control characters that survive Qt's per-argument escaping but
    // get re-interpreted by `cmd /k`. A task content of "foo\r\necho pwned"
    // would, after cmd opens, paste the second line as a fresh command at the
    // claude prompt. Replace CR/LF with spaces, drop everything else below 0x20.
    // Tab survives (	) — claude treats it as ordinary whitespace.
    QString sanitized;
    sanitized.reserve(prompt.size());
    for (QChar ch : prompt) {
        const ushort u = ch.unicode();
        if (u == '\n' || u == '\r')
            sanitized.append(QLatin1Char(' '));
        else if (u >= 0x20 || u == '\t')
            sanitized.append(ch);
        // else: drop other C0 control codes (BEL, ESC, etc.)
    }
    // Windows cmd.exe quoting: escape embedded double-quotes as "".
    QString escapedPrompt = sanitized;
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
