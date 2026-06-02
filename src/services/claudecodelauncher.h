#pragma once

#include <QObject>
#include <QString>
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
};
