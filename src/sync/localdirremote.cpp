#include "localdirremote.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

LocalDirRemote::LocalDirRemote(const QString &rootDir)
    : m_root(rootDir)
{
}

void LocalDirRemote::setRootDir(const QString &rootDir)
{
    m_root = rootDir;
}

QString LocalDirRemote::resolve(const QString &remotePath) const
{
    if (m_root.isEmpty())
        return {};
    const QString rel = QDir::fromNativeSeparators(remotePath.trimmed());
    // Reject absolute paths, drive letters, UNC, and any traversal segment.
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/'))
        || rel.contains(QLatin1Char(':')) || rel.contains(QStringLiteral("..")))
        return {};

    const QString rootCanon = QDir::cleanPath(QDir(m_root).absolutePath());
    const QString full = QDir::cleanPath(rootCanon + QLatin1Char('/') + rel);
    // Final guard: the cleaned path must still live under the root.
    if (full != rootCanon && !full.startsWith(rootCanon + QLatin1Char('/')))
        return {};
    return full;
}

bool LocalDirRemote::isAuthenticated() const
{
    // "Authenticated" for a local store means a usable root directory.
    if (m_root.isEmpty())
        return false;
    QDir dir(m_root);
    return dir.exists() || dir.mkpath(QStringLiteral("."));
}

bool LocalDirRemote::acquireLock()
{
    if (m_root.isEmpty())
        return false;
    QDir().mkpath(m_root);
    const QString lockPath = resolve(QStringLiteral(".lock"));
    if (lockPath.isEmpty())
        return false;

    QFileInfo fi(lockPath);
    if (fi.exists()) {
        // Held unless stale (older than the TTL), in which case we reclaim it.
        const qint64 ageSec = fi.lastModified().secsTo(QDateTime::currentDateTimeUtc());
        if (ageSec >= 0 && ageSec < LOCK_TTL_SECONDS)
            return false;
        QFile::remove(lockPath);
    }

    QSaveFile f(lockPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
    return f.commit();
}

bool LocalDirRemote::releaseLock()
{
    const QString lockPath = resolve(QStringLiteral(".lock"));
    if (lockPath.isEmpty())
        return false;
    if (!QFile::exists(lockPath))
        return true;
    return QFile::remove(lockPath);
}

bool LocalDirRemote::uploadFile(const QString &remotePath, const QByteArray &data)
{
    const QString full = resolve(remotePath);
    if (full.isEmpty())
        return false;
    // Ensure the parent directory exists (e.g. "log/").
    const QFileInfo fi(full);
    if (!QDir().mkpath(fi.absolutePath()))
        return false;
    QSaveFile f(full);          // atomic write (write-temp-then-rename)
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size())
        return false;
    return f.commit();
}

bool LocalDirRemote::downloadFile(const QString &remotePath, QByteArray &data,
                                  bool notFoundIsOk, qint64 maxBytes)
{
    data.clear();
    const QString full = resolve(remotePath);
    if (full.isEmpty())
        return false;
    QFile f(full);
    if (!f.exists())
        return notFoundIsOk;     // first sync: missing HEAD/state.json is fine
    if (maxBytes > 0 && f.size() > maxBytes)
        return false;            // refuse oversized payloads (OOM guard)
    if (!f.open(QIODevice::ReadOnly))
        return false;
    data = f.readAll();
    return true;
}

bool LocalDirRemote::deleteFile(const QString &remotePath)
{
    const QString full = resolve(remotePath);
    if (full.isEmpty())
        return false;
    if (!QFile::exists(full))
        return true;             // idempotent — already gone (matches Drive)
    return QFile::remove(full);
}

QStringList LocalDirRemote::listFiles(const QString &folderPath)
{
    const QString full = resolve(folderPath);
    if (full.isEmpty())
        return {};
    QDir dir(full);
    if (!dir.exists())
        return {};
    return dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}
