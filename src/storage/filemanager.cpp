#include "filemanager.h"
#include "datastore.h"
#include "utils/settingskeys.h"
#include "utils/uuid.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSaveFile>
#include <QUrl>
#include <QVariant>

FileManager::FileManager(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
#ifdef Q_OS_ANDROID
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/OpenTodo");
#endif

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000); // 1 second debounce

    connect(&m_saveTimer, &QTimer::timeout, this, &FileManager::performSave);
    connect(m_store, &DataStore::dataChanged, this, &FileManager::onDataChanged);
}

QString FileManager::dataPath() const
{
    return m_dataPath;
}

bool FileManager::isDirty() const
{
    return m_dirty;
}

void FileManager::load()
{
    ensureDataDirectory();

    const QString filePath = stateFilePath();
    QFile file(filePath);

    if (!file.exists()) {
        createDefaultState();
        emit loaded();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit loadError(QStringLiteral("Cannot open state file: %1").arg(file.errorString()));
        createDefaultState();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || doc.isNull() || !doc.isObject()) {
        // Auto-recovery: try .bak, .bak.1, .bak.2 in order before giving up.
        const QStringList bakCandidates = {
            filePath + QStringLiteral(".bak"),
            filePath + QStringLiteral(".bak.1"),
            filePath + QStringLiteral(".bak.2"),
        };
        for (const QString &candidate : bakCandidates) {
            QFile bakFile(candidate);
            if (!bakFile.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bakData = bakFile.readAll();
            bakFile.close();
            QJsonParseError bakErr;
            QJsonDocument bakDoc = QJsonDocument::fromJson(bakData, &bakErr);
            if (bakErr.error == QJsonParseError::NoError && !bakDoc.isNull() && bakDoc.isObject()) {
                qWarning("FileManager: state.json corrupted; recovered from %s",
                         qPrintable(candidate));
                m_store->loadFromJson(bakDoc.object());
                m_dirty = false;
                emit loaded();
                return;
            }
        }
        qWarning("FileManager: state.json and all backups (.bak/.bak.1/.bak.2) invalid or missing; starting fresh");

        // S-1.22: rename the corrupt file before falling through to defaults,
        // so the next save does not silently overwrite it with blank data.
        const QString corruptPath = filePath + QStringLiteral(".corrupt-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss"));
        QFile::rename(filePath, corruptPath);
        emit loadError(QStringLiteral("JSON parse error: %1 (corrupted file renamed to %2)")
                       .arg(parseError.errorString(), corruptPath));
        createDefaultState();
        return;
    }

    const QJsonObject rootObj = doc.object();
    // F-1.60: check schema_version so future migrations can be gated.
    // Use DataStore::CURRENT_SCHEMA_VERSION as the single source of truth —
    // the previous hardcoded `kKnownSchemaVersion = 1` produced a permanent
    // false-positive warning ("schema_version 2 is newer than this build (1)")
    // every time DataStore was bumped to v2 but FileManager wasn't kept in
    // sync. The warning misled debugging into thinking a stale exe was being
    // run when in fact the message was just stale.
    const int schemaVersion = rootObj[QStringLiteral("schema_version")].toInt(0);
    if (schemaVersion > DataStore::CURRENT_SCHEMA_VERSION) {
        qWarning("FileManager::load: state.json schema_version %d is newer than "
                 "this build (%d) — data may be partially unreadable.",
                 schemaVersion, DataStore::CURRENT_SCHEMA_VERSION);
    }

    m_store->loadFromJson(rootObj);

    // Auto-archive stale projects if user opted in via settings.
    const int archiveDays = m_store->setting(
        QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays)).toInt();
    if (archiveDays > 0) {
        const int n = m_store->autoArchiveStaleProjects(archiveDays);
        if (n > 0)
            qInfo("FileManager: auto-archived %d stale project(s)", n);
    }

    m_dirty = false;
    emit loaded();
}

void FileManager::save()
{
    // B-1.20: always restart the timer so the debounce resets on every change.
    // The timer is single-shot; calling start() on an active timer restarts it,
    // ensuring we save 1s after the LAST change, not the first.
    m_saveTimer.start();
}

void FileManager::saveImmediate()
{
    m_saveTimer.stop();
    performSave();
}

QString FileManager::exportData() const
{
    const QJsonDocument doc(m_store->toJson());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

bool FileManager::exportTo(const QString &filePathOrUrl) const
{
    // Accept either a plain path or a "file:///..." URL — QML FileDialog hands
    // back QUrls.
    QString path = filePathOrUrl;
    if (path.startsWith(QStringLiteral("file:")))
        path = QUrl(path).toLocalFile();

    const QJsonDocument doc(m_store->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(data);
    return out.commit();
}

QVariantList FileManager::listBackups() const
{
    QVariantList result;
    const QString statePath = stateFilePath();
    const QStringList paths = {
        statePath + QStringLiteral(".bak"),
        statePath + QStringLiteral(".bak.1"),
        statePath + QStringLiteral(".bak.2"),
    };
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (!fi.exists())
            continue;
        QVariantMap m;
        m[QStringLiteral("name")]  = fi.fileName();
        m[QStringLiteral("path")]  = fi.absoluteFilePath();
        m[QStringLiteral("size")]  = fi.size();            // qint64 → bytes
        m[QStringLiteral("mtime")] = fi.lastModified();   // QDateTime → JS Date in QML
        result.append(m);
    }
    return result;
}

bool FileManager::restoreBackup(const QString &backupPath)
{
    if (!QFile::exists(backupPath)) {
        qWarning("FileManager::restoreBackup: path does not exist: %s",
                 qPrintable(backupPath));
        return false;
    }

    // Read and validate the backup before touching state.json.
    QFile bak(backupPath);
    if (!bak.open(QIODevice::ReadOnly)) {
        qWarning("FileManager::restoreBackup: cannot open backup for reading: %s",
                 qPrintable(bak.errorString()));
        return false;
    }
    const QByteArray data = bak.readAll();
    bak.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || doc.isNull() || !doc.isObject()) {
        qWarning("FileManager::restoreBackup: backup contains invalid JSON (%s): %s",
                 qPrintable(err.errorString()), qPrintable(backupPath));
        return false;
    }

    // Flush the current in-memory state through the normal rotation chain so
    // .bak/.bak.1/.bak.2 carry a snapshot of "before restore" — the user can
    // undo by calling restoreBackup again on the freshly written .bak.
    saveImmediate();

    // Replace in-memory store with backup data, then persist atomically.
    m_store->clear();
    m_store->loadFromJson(doc.object());
    saveImmediate();

    qInfo("FileManager: successfully restored from %s", qPrintable(backupPath));
    return true;
}

bool FileManager::importData(const QString &jsonString)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
        return false;

    m_store->loadFromJson(doc.object());
    saveImmediate();
    return true;
}

void FileManager::onDataChanged()
{
    m_dirty = true;
    emit dirtyChanged();
    save();
}

void FileManager::performSave()
{
    ensureDataDirectory();

    const QString statePath = stateFilePath();
    const QString bak0 = statePath + QStringLiteral(".bak");
    const QString bak1 = statePath + QStringLiteral(".bak.1");
    const QString bak2 = statePath + QStringLiteral(".bak.2");

    // 3-generation rolling backup: rotate before every save so users have a
    // longer recovery window.  Rotation is best-effort — a failure on any
    // individual step is logged and ignored; the save always proceeds.
    // Skip on the very first save (state.json doesn't exist yet).
    if (QFile::exists(statePath)) {
        if (QFile::exists(bak1)) {
            QFile::remove(bak2);
            if (!QFile::rename(bak1, bak2))
                qWarning("FileManager: failed to rotate .bak.1 → .bak.2");
        }
        if (QFile::exists(bak0)) {
            QFile::remove(bak1);
            if (!QFile::rename(bak0, bak1))
                qWarning("FileManager: failed to rotate .bak → .bak.1");
        }
        QFile::remove(bak0);
        if (!QFile::copy(statePath, bak0))
            qWarning("FileManager: failed to copy state.json → .bak");
    }

    const QJsonDocument doc(m_store->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    // Atomic write via QSaveFile (writes to temp, then renames)
    QSaveFile file(statePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit saveError(QStringLiteral("Cannot open file for writing: %1").arg(file.errorString()));
        return;
    }

    file.write(data);

    if (!file.commit()) {
        emit saveError(QStringLiteral("Failed to commit save: %1").arg(file.errorString()));
        return;
    }

    m_dirty = false;
    emit dirtyChanged();
    emit saved();
}

void FileManager::ensureDataDirectory()
{
    QDir dir(m_dataPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QDir logDir(localLogPath());
    if (!logDir.exists()) {
        logDir.mkpath(QStringLiteral("."));
    }
}

void FileManager::createDefaultState()
{
    QJsonObject defaultSettings;
    // "system" → ThemeManager resolves to dark/light based on OS color scheme.
    defaultSettings[QLatin1String(SettingsKeys::Theme)]               = QStringLiteral("system");
    defaultSettings[QLatin1String(SettingsKeys::DailyGoal)]           = 5;
    defaultSettings[QLatin1String(SettingsKeys::WeeklyGoal)]          = 25;
    defaultSettings[QLatin1String(SettingsKeys::VacationMode)]        = false;
    defaultSettings[QLatin1String(SettingsKeys::StartPage)]           = QStringLiteral("today");
    defaultSettings[QLatin1String(SettingsKeys::TimeFormat)]          = 24;
    defaultSettings[QLatin1String(SettingsKeys::DateFormat)]          = QStringLiteral("DD-MM-YYYY");
    defaultSettings[QLatin1String(SettingsKeys::WeekStart)]           = 1;
    defaultSettings[QLatin1String(SettingsKeys::NextWeekDay)]         = 1;
    defaultSettings[QLatin1String(SettingsKeys::Language)]            = QStringLiteral("en");
    defaultSettings[QLatin1String(SettingsKeys::KarmaScore)]          = 0;
    defaultSettings[QLatin1String(SettingsKeys::KarmaLevel)]          = QStringLiteral("beginner");
    defaultSettings[QLatin1String(SettingsKeys::DailyStreak)]         = 0;
    defaultSettings[QLatin1String(SettingsKeys::WeeklyStreak)]        = 0;

    QJsonObject root;
    // F-1.60: record schema version for future migrations.
    root[QStringLiteral("schema_version")] = 1;
    root[QStringLiteral("settings")] = defaultSettings;

    // Create default Inbox project.
    // B-1.58: use a real UUID, not the literal "inbox" string.
    // The inbox is identified by the is_inbox flag, not by its id.
    const QString nowStr = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QJsonArray projects;
    QJsonObject inbox;
    inbox[QStringLiteral("id")] = UuidHelper::generate();
    inbox[QStringLiteral("name")] = QStringLiteral("Входящие");
    inbox[QStringLiteral("color")] = QStringLiteral("grey");
    inbox[QStringLiteral("is_inbox")] = true;
    inbox[QStringLiteral("order")] = 0;
    inbox[QStringLiteral("is_favorite")] = false;
    inbox[QStringLiteral("is_archived")] = false;
    inbox[QStringLiteral("view_style")] = QStringLiteral("list");
    inbox[QStringLiteral("is_deleted")] = false;
    inbox[QStringLiteral("created_at")] = nowStr;
    inbox[QStringLiteral("updated_at")] = nowStr;
    projects.append(inbox);
    root[QStringLiteral("projects")] = projects;

    root[QStringLiteral("tasks")] = QJsonArray();
    root[QStringLiteral("completed_tasks")] = QJsonArray();
    root[QStringLiteral("sections")] = QJsonArray();
    root[QStringLiteral("labels")] = QJsonArray();
    root[QStringLiteral("filters")] = QJsonArray();
    root[QStringLiteral("reminders")] = QJsonArray();
    root[QStringLiteral("comments")] = QJsonArray();

    m_store->loadFromJson(root);
    saveImmediate();
}

QString FileManager::stateFilePath() const
{
    return m_dataPath + QStringLiteral("/state.json");
}

QString FileManager::localLogPath() const
{
    return m_dataPath + QStringLiteral("/local_log");
}
