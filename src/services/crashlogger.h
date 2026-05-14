#pragma once
#include <QObject>
#include <QFile>
#include <QMutex>

// CrashLogger installs a Qt message handler that captures every
// qDebug / qWarning / qCritical / qFatal line and writes it to a
// rotating log file at $APPDATA/OpenTodo/error.log.
//
// Usage (in main.cpp, right after QGuiApplication construction):
//   auto *crashLogger = new CrashLogger(&app);
//   crashLogger->install();
//
// Exposed to QML as the "crashLogger" context property so the About
// settings page can surface log path, size, and modification time.
class CrashLogger : public QObject
{
    Q_OBJECT
public:
    explicit CrashLogger(QObject *parent = nullptr);
    ~CrashLogger() override;

    // Install the Qt message handler.  Call once, immediately after
    // QGuiApplication is constructed, so startup messages are captured.
    void install();

    // Absolute path to the active log file.
    Q_INVOKABLE QString logFilePath() const;

    // Path to the rotated backup (error.log.1).
    Q_INVOKABLE QString logFilePathRotated() const;

    // Current size of the active log in bytes (0 if the file does not exist).
    Q_INVOKABLE qint64 logFileSize() const;

    // Last-modified time of the active log as an ISO-8601 string, or an
    // empty string if the file does not exist.
    Q_INVOKABLE QString logFileModifiedIso() const;

    // Opens the directory that contains the log file in the OS file manager
    // (Explorer on Windows, Finder on macOS, etc.).  Returns true on success.
    Q_INVOKABLE bool openLogFolderInExplorer() const;

private:
    // Static Qt message handler forwarded to by qInstallMessageHandler.
    static void handleMessage(QtMsgType type,
                              const QMessageLogContext &ctx,
                              const QString &msg);

    // Owned by the singleton instance; written from any thread via s_writeMutex.
    static QFile        *s_logFile;
    static QMutex        s_writeMutex;
    static QtMessageHandler s_prevHandler;
    static CrashLogger  *s_instance;

    QString m_logPath;
};
