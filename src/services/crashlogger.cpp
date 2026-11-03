#include "crashlogger.h"

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>
#include <QDesktopServices>
#include <QUrl>

// ── Static member definitions ─────────────────────────────────────────────────
QFile           *CrashLogger::s_logFile    = nullptr;
QMutex           CrashLogger::s_writeMutex;
QtMessageHandler CrashLogger::s_prevHandler = nullptr;
CrashLogger     *CrashLogger::s_instance   = nullptr;

// ── Constructor ───────────────────────────────────────────────────────────────
CrashLogger::CrashLogger(QObject *parent)
    : QObject(parent)
{
    // Compute the log directory ($APPDATA/OpenTodo on Windows, ~/.local/share/OpenTodo on Linux).
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    if (dataDir.isEmpty()) {
        // QStandardPaths returned nothing — fall back to stderr only.
        qWarning("CrashLogger: could not determine AppDataLocation; "
                 "log file will not be written");
        return;
    }

    QDir dir(dataDir);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning("CrashLogger: failed to create log directory %s; "
                     "log file will not be written",
                     qPrintable(dataDir));
            return;
        }
    }

    m_logPath = dataDir + QStringLiteral("/error.log");

    // ── Rotation ──────────────────────────────────────────────────────────────
    // If the existing log exceeds 1 MB, rename it to error.log.1 (overwriting
    // any prior .1 file) before opening a fresh error.log.  We keep at most
    // two files: the active one and one archived generation.
    constexpr qint64 kRotateThreshold = 1 * 1024 * 1024; // 1 MB
    QFileInfo logInfo(m_logPath);
    if (logInfo.exists() && logInfo.size() >= kRotateThreshold) {
        const QString backupPath = m_logPath + QStringLiteral(".1");
        // Remove the old .1 if it exists (QFile::rename fails if target exists).
        if (QFile::exists(backupPath))
            QFile::remove(backupPath);
        QFile::rename(m_logPath, backupPath);
    }

    // ── Open the log file ─────────────────────────────────────────────────────
    auto *f = new QFile(m_logPath, this);
    if (!f->open(QIODevice::Append | QIODevice::Text)) {
        qWarning("CrashLogger: failed to open %s for writing (%s); "
                 "log file will not be written",
                 qPrintable(m_logPath),
                 qPrintable(f->errorString()));
        delete f;
        return;
    }

    s_logFile   = f;
    s_instance  = this;
}

// ── Destructor ────────────────────────────────────────────────────────────────
CrashLogger::~CrashLogger()
{
    // Restore the previous handler before closing the file so no subsequent
    // message can slip through to a dangling pointer.
    if (s_prevHandler) {
        qInstallMessageHandler(s_prevHandler);
        s_prevHandler = nullptr;
    }

    // Take the write mutex: a message from a worker thread (e.g. a QNAM HTTP
    // thread warning during teardown) may be inside handleMessage right now.
    // The QFile is a child of this object and is deleted right after us, so
    // it must be unpublished under the same lock the writer holds.
    {
        QMutexLocker locker(&s_writeMutex);
        if (s_logFile) {
            s_logFile->flush();
            s_logFile->close();
            s_logFile = nullptr;
        }
    }

    if (s_instance == this)
        s_instance = nullptr;
}

// ── install() ─────────────────────────────────────────────────────────────────
void CrashLogger::install()
{
    // Only install when the file was successfully opened.
    if (!s_logFile)
        return;

    s_prevHandler = qInstallMessageHandler(CrashLogger::handleMessage);
}

// ── logFilePath() ─────────────────────────────────────────────────────────────
QString CrashLogger::logFilePath() const
{
    return m_logPath;
}

// ── logFilePathRotated() ──────────────────────────────────────────────────────
QString CrashLogger::logFilePathRotated() const
{
    return m_logPath + QStringLiteral(".1");
}

// ── logFileSize() ─────────────────────────────────────────────────────────────
qint64 CrashLogger::logFileSize() const
{
    const QFileInfo fi(m_logPath);
    return fi.exists() ? fi.size() : 0;
}

// ── logFileModifiedIso() ──────────────────────────────────────────────────────
QString CrashLogger::logFileModifiedIso() const
{
    const QFileInfo fi(m_logPath);
    if (!fi.exists())
        return {};
    return fi.lastModified().toString(Qt::ISODate);
}

// ── openLogFolderInExplorer() ─────────────────────────────────────────────────
bool CrashLogger::openLogFolderInExplorer() const
{
    const QFileInfo fi(m_logPath);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

// ── Static message handler ────────────────────────────────────────────────────
void CrashLogger::handleMessage(QtMsgType type,
                                const QMessageLogContext &ctx,
                                const QString &msg)
{
    // Map QtMsgType → short label.
    const char *level = [type]() -> const char * {
        switch (type) {
        case QtDebugMsg:    return "DEBUG";
        case QtInfoMsg:     return "INFO ";
        case QtWarningMsg:  return "WARN ";
        case QtCriticalMsg: return "CRIT ";
        case QtFatalMsg:    return "FATAL";
        default:            return "?????";
        }
    }();

    // Timestamp in ISO-8601 with milliseconds:  2026-05-08T12:34:56.789
    const QString timestamp =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    // Source location — ctx.file / ctx.line may be null in release builds.
    QString location;
    if (ctx.file && ctx.file[0] != '\0') {
        location = QStringLiteral(" [%1:%2]")
                       .arg(QString::fromUtf8(ctx.file))
                       .arg(ctx.line);
    }

    // Final log line:
    //   [2026-05-08T12:34:56.789] [WARN ] [src/foo.cpp:42] message text
    const QString line =
        QStringLiteral("[%1] [%2]%3 %4\n")
            .arg(timestamp, QString::fromLatin1(level), location, msg);

    // Write to file under the mutex. Re-check s_logFile once the lock is held:
    // the destructor clears it under the same mutex.
    {
        QMutexLocker locker(&s_writeMutex);
        if (s_logFile) {
            s_logFile->write(line.toUtf8());
            // Flush after every write — especially important for qFatal so the
            // last lines reach disk before the process is aborted.
            s_logFile->flush();
        }
    }

    // Forward to the previous handler (keeps stderr output intact).
    // For qFatal we forward AFTER flushing so the abort() that the default
    // handler issues does not cut off our write. Read the pointer once — the
    // destructor may reset it concurrently.
    if (const QtMessageHandler prev = s_prevHandler)
        prev(type, ctx, msg);
}
