#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>

// ClaudeCodeLauncher spawns the `claude` CLI in a new terminal window,
// rooted at the given repository path, with a task prompt.
//
// Detection order for the `claude` executable on Windows:
//   1. QStandardPaths::findExecutable("claude")   — respects PATH
//   2. %APPDATA%\npm\claude.cmd
//   3. %LOCALAPPDATA%\npm\claude.cmd
//
// Terminal preference: Windows Terminal (wt.exe) → cmd.exe fallback.
//
// Linux / macOS: stub — returns false (TODO: add xterm / Terminal.app path).
//
// The task text is untrusted (it syncs from peers and arrives via Todoist
// import) and ends up on a cmd.exe command line, so the command-line
// construction lives in the pure static helpers below — compiled on every
// platform so the quoting rules are unit-tested off Windows too.
class ClaudeCodeLauncher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ClaudeCodeLauncher is constructed in C++ and exposed via context property.")

public:
    explicit ClaudeCodeLauncher(QObject *parent = nullptr);

    // Launch claude CLI in a detached terminal at repoPath.
    // prompt = taskContent + optionally "\n\n" + taskDescription.
    // Returns true when the process was started; false when the claude
    // executable could not be located (caller should show a toast).
    Q_INVOKABLE bool launch(const QString &taskContent,
                            const QString &taskDescription,
                            const QString &repoPath);

    // Best-known path to the `opentodo-cli` companion binary. PATH lookup first
    // (the user may have it installed/aliased), else the file next to this
    // running app — CMake emits opentodo-cli(.exe) into the same directory as
    // the desktop binary. Used by the "Добавить навык в Claude" dialog as the
    // fallback path baked into the generated skill. Returns native separators.
    Q_INVOKABLE QString opentodoCliPath() const;

    // ── Command-line construction (pure; public for tests) ──────────────────

    // The initial prompt: content, optional description and the opentodo-cli
    // hint, flattened to one line and made safe to sit inside ONE
    // double-quoted cmd.exe argument (no '"', no '%', no control chars, never
    // starting with '-'), capped below cmd.exe's 8191-character line limit.
    static QString buildPrompt(const QString &taskContent,
                               const QString &taskDescription);

    // Arguments for `wt.exe`: -d <repo> <cmdExe> /k call <claude> <prompt>,
    // with ';' escaped so wt does not split them into extra subcommands.
    static QStringList windowsTerminalArgs(const QString &nativeRepo,
                                           const QString &cmdExe,
                                           const QString &claudePath,
                                           const QString &prompt);

    // Arguments for the `cmd.exe /c start` fallback. The repo is NOT on the
    // command line: the caller makes it the process working directory, which
    // `start` hands on to the new window.
    static QStringList cmdStartArgs(const QString &cmdExe,
                                    const QString &claudePath,
                                    const QString &prompt);
};
