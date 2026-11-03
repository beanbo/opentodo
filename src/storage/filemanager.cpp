#include "filemanager.h"
#include "datastore.h"
#include "utils/settingskeys.h"
#include "utils/uuid.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSaveFile>
#include <QUrl>
#include <QVariant>
#include <algorithm>

#ifdef Q_OS_WASM
#  include <emscripten.h>
#endif

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

    // D10: flush pending edits when the app leaves the foreground. On Android
    // a backgrounded app can be killed without aboutToQuit ever firing, so
    // anything still inside the 1 s autosave debounce was lost. (Desktop: the
    // window losing focus just saves a little earlier.) Only with a GUI app —
    // QCoreApplication-based tools/tests have no application state.
    if (auto *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(guiApp, &QGuiApplication::applicationStateChanged, this,
                [this, last = guiApp->applicationState()](Qt::ApplicationState state) mutable {
            const bool leftForeground = (last == Qt::ApplicationActive
                                         && state != Qt::ApplicationActive);
            last = state;
            if (leftForeground && (m_dirty || m_saveTimer.isActive()))
                saveImmediate();
        });
    }

    // Q3.10: hydrate retention policy from settings AFTER load() runs.
    // We can't read m_store->setting() here (settings are loaded later by
    // load() → loadFromJson). settingsChanged fires once at the end of
    // loadFromJson, so we sample the trio of keys at that point.
    connect(m_store, &DataStore::settingsChanged, this, [this]() {
        const QVariant cnt = m_store->setting(
            QLatin1String(SettingsKeys::BackupRetentionCount));
        if (cnt.isValid()) {
            const int n = cnt.toInt();
            if (n > 0 && n != m_backupRetentionCount) {
                m_backupRetentionCount = n;
                emit backupRetentionCountChanged();
            }
        }
        const QVariant days = m_store->setting(
            QLatin1String(SettingsKeys::BackupRetentionDays));
        if (days.isValid()) {
            const int n = days.toInt();
            if (n >= 0 && n != m_backupRetentionDays) {
                m_backupRetentionDays = n;
                emit backupRetentionDaysChanged();
            }
        }
        const QVariant thr = m_store->setting(
            QLatin1String(SettingsKeys::VacuumThresholdBytes));
        if (thr.isValid()) {
            const qint64 n = thr.toLongLong();
            if (n > 0 && n != m_vacuumThresholdBytes) {
                m_vacuumThresholdBytes = n;
                emit vacuumThresholdBytesChanged();
            }
        }
    });
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
#ifdef Q_OS_WASM
    // D11: restore the IndexedDB-backed data directory first. This is async;
    // startWasmPersistentFs() calls load() again once the restore finished.
    if (m_wasmFsPending)
        return;
    if (!m_wasmFsReady) {
        startWasmPersistentFs();
        return;
    }
#endif

    // A fresh load attempt decides afresh whether state.json is writable.
    m_saveBlocked = false;

    ensureDataDirectory();

    const QString filePath = stateFilePath();
    QFile file(filePath);

    if (!file.exists()) {
        // Iter-night-11: state.json missing but backups may still exist.
        // Previously this branch jumped straight to createDefaultState(),
        // which then ran performSave() → after a few autosaves the rolling
        // .bak / .bak.1 / .bak.2 chain was overwritten with empty default
        // state, and the user's real data was silently ground out. The
        // corruption-recovery branch below has always walked the backup
        // chain — make this branch do the same so a missing state.json
        // with a valid .bak is recoverable, not a one-way trip to a
        // pristine-looking install.
        if (tryRecoverFromBackups(filePath, "missing")) {
            // Materialize state.json from the in-memory recovered state.
            // Without this, state.json stays absent until the next user
            // mutation triggers an autosave — which means a peaceful
            // close-then-reopen would re-enter the recovery path each
            // time, and `first_launch_creates_state_then_debounces`-
            // style invariants in test fixtures would also break.
            // performSave's rotation is gated on QFile::exists(statePath),
            // so the rotation step is skipped on this first save (state.json
            // doesn't exist yet) — the recovered .bak is preserved untouched.
            saveImmediate();
            emit loaded();
            return;
        }
        createDefaultState();
        emit loaded();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        // D5: the file exists but cannot be read (permissions, a lock held by
        // another process, a directory in its place…). It used to fall
        // straight through to createDefaultState(), whose saveImmediate()
        // wrote the empty default state OVER the user's real data. Treat it
        // like corruption instead: move it aside, then recover from backups
        // or start fresh. If it cannot even be moved aside, keep it intact and
        // refuse to write state.json for this session.
        const QString openError = file.errorString();
        const QString asidePath = filePath + QStringLiteral(".unreadable-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss"));
        if (QFile::rename(filePath, asidePath)) {
            qWarning("FileManager: cannot open state.json (%s); moved it to %s",
                     qPrintable(openError), qPrintable(asidePath));
            emit loadError(QStringLiteral("Cannot open state file: %1 (file moved to %2)")
                           .arg(openError, asidePath));
            if (tryRecoverFromBackups(filePath, "unreadable")) {
                saveImmediate();   // state.json is gone now — no rotation, .bak kept
                emit loaded();
                return;
            }
            createDefaultState();
            return;
        }

        qWarning("FileManager: cannot open or move state.json (%s); saving disabled "
                 "so the file is not overwritten", qPrintable(openError));
        emit loadError(QStringLiteral("Cannot open state file: %1. Changes will not be "
                                      "saved until it can be read.").arg(openError));
        m_saveBlocked = true;
        // Still show the user their most recent backup (read-only session).
        if (!tryRecoverFromBackups(filePath, "unreadable"))
            createDefaultState();   // its saveImmediate() is refused while blocked
        m_saveTimer.stop();
        m_dirty = false;
        emit dirtyChanged();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || doc.isNull() || !doc.isObject()) {
        // Auto-recovery: try .bak / .bak.1 / .bak.2 first (LRU-cheapest),
        // then fall through to the dated rolling backups in mtime order.
        // Q3.10: dated backups extend the recovery window beyond the 3-gen
        // rolling chain, capped by the retention policy.
        if (tryRecoverFromBackups(filePath, "corrupted")) {
            emit loaded();
            return;
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
    bool changedOnLoad = m_store->lastLoadModifiedState();

    // Auto-archive stale projects if user opted in via settings.
    const int archiveDays = m_store->setting(
        QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays)).toInt();
    if (archiveDays > 0) {
        const int n = m_store->autoArchiveStaleProjects(archiveDays);
        if (n > 0) {
            qInfo("FileManager: auto-archived %d stale project(s)", n);
            changedOnLoad = true;
        }
    }

    // D13: loadFromJson() emits dataChanged(), which started the autosave
    // debounce — so every launch rewrote an unchanged state.json one second
    // later and rotated a redundant copy through the .bak chain (pushing the
    // older, distinct generations out). Only keep that save when the load
    // itself changed something (Inbox repair, auto-archive).
    if (changedOnLoad) {
        m_dirty = true;
        m_saveTimer.start();
    } else {
        m_saveTimer.stop();
        m_dirty = false;
    }
    emit dirtyChanged();
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
    // Q3.10: include BOTH rolling (.bak / .bak.1 / .bak.2) and dated
    // (.bak.YYYYMMDDhhmmss) backups, sorted by mtime descending so the
    // most-recent appears first regardless of naming flavor.
    const QStringList paths = allBackupPaths();
    QList<QFileInfo> infos;
    infos.reserve(paths.size());
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.exists())
            infos.append(fi);
    }
    std::sort(infos.begin(), infos.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  return a.lastModified() > b.lastModified();
              });
    for (const QFileInfo &fi : infos) {
        QVariantMap m;
        m[QStringLiteral("name")]  = fi.fileName();
        m[QStringLiteral("path")]  = fi.absoluteFilePath();
        m[QStringLiteral("size")]  = fi.size();            // qint64 → bytes
        m[QStringLiteral("mtime")] = fi.lastModified();   // QDateTime → JS Date in QML
        result.append(m);
    }
    return result;
}

QStringList FileManager::allBackupPaths() const
{
    QStringList out;
    const QString statePath = stateFilePath();
    // Rolling chain — always present in expected names.
    out << statePath + QStringLiteral(".bak");
    out << statePath + QStringLiteral(".bak.1");
    out << statePath + QStringLiteral(".bak.2");
    // Pre-vacuum safety backup (preserved across rotations).
    out << statePath + QStringLiteral(".pre-vacuum.bak");
    // Dated backups discovered on disk.
    out << datedBackupPaths();
    return out;
}

QStringList FileManager::datedBackupPaths() const
{
    // Dated backups follow the pattern: state.json.bak.YYYYMMDDhhmmss
    // (14 trailing digits after ".bak.") so they don't collide with the
    // rolling .bak.1 / .bak.2 names. We scan the data directory rather
    // than reconstruct paths so a manual user-renamed backup is also
    // visible (and subject to retention) — provided it matches the regex.
    QStringList out;
    QDir dir(m_dataPath);
    if (!dir.exists())
        return out;
    const QFileInfoList list = dir.entryInfoList(
        QStringList{} << QStringLiteral("state.json.bak.*"),
        QDir::Files | QDir::NoSymLinks);
    const QRegularExpression datedRx(
        QStringLiteral("^state\\.json\\.bak\\.(\\d{14})$"));
    for (const QFileInfo &fi : list) {
        const auto match = datedRx.match(fi.fileName());
        if (match.hasMatch())
            out.append(fi.absoluteFilePath());
    }
    return out;
}

bool FileManager::tryRecoverFromBackups(const QString &filePath,
                                       const char *reason)
{
    // Walk the rolling 3-generation chain (LRU-cheapest first), then the
    // dated rotation in newest-first order. Same shape as the corruption
    // recovery branch used pre-iter-night-11 — now shared with the
    // missing-state branch so a deleted/quarantined state.json with a
    // valid .bak doesn't lead to silent data loss.
    QStringList bakCandidates = {
        filePath + QStringLiteral(".bak"),
        filePath + QStringLiteral(".bak.1"),
        filePath + QStringLiteral(".bak.2"),
    };
    QStringList dated = datedBackupPaths();
    std::sort(dated.begin(), dated.end(),
              [](const QString &a, const QString &b) {
                  return QFileInfo(a).lastModified()
                         > QFileInfo(b).lastModified();
              });
    bakCandidates << dated;
    for (const QString &candidate : bakCandidates) {
        QFile bakFile(candidate);
        if (!bakFile.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bakData = bakFile.readAll();
        bakFile.close();
        QJsonParseError bakErr;
        QJsonDocument bakDoc = QJsonDocument::fromJson(bakData, &bakErr);
        if (bakErr.error == QJsonParseError::NoError && !bakDoc.isNull() && bakDoc.isObject()) {
            qWarning("FileManager: state.json %s; recovered from %s",
                     reason, qPrintable(candidate));
            m_store->loadFromJson(bakDoc.object());
            m_dirty = false;
            return true;
        }
    }
    return false;
}

void FileManager::writeDatedBackup()
{
    // One dated backup per save. The 14-digit timestamp uses second
    // precision; saves that fire within the same second simply skip
    // (existence check below) — the rolling .bak chain already covers
    // sub-second granularity. Pre-condition: state.json exists.
    const QString statePath = stateFilePath();
    if (!QFile::exists(statePath))
        return;
    const QString stamp = QDateTime::currentDateTimeUtc()
        .toString(QStringLiteral("yyyyMMddHHmmss"));
    const QString datedPath = statePath + QStringLiteral(".bak.") + stamp;
    if (QFile::exists(datedPath))
        return;   // same-second save, dedup
    if (!QFile::copy(statePath, datedPath)) {
        qWarning("FileManager: failed to write dated backup %s",
                 qPrintable(datedPath));
    }
}

int FileManager::sweepOldBackups()
{
    // Retention policy: keep at most m_backupRetentionCount dated backups
    // AND drop any older than m_backupRetentionDays — whichever cap hits
    // first. The rolling chain (.bak / .bak.1 / .bak.2) is excluded from
    // sweep so the legacy recovery path is preserved unchanged. The
    // single most-recent dated backup is ALWAYS preserved regardless of
    // age, so the user is never left with zero recovery options.
    QStringList dated = datedBackupPaths();
    if (dated.isEmpty())
        return 0;

    // Sort by mtime descending (most-recent first). Capture file infos
    // up-front because we'll delete via path later.
    QList<QFileInfo> infos;
    infos.reserve(dated.size());
    for (const QString &p : dated) {
        const QFileInfo fi(p);
        if (fi.exists())
            infos.append(fi);
    }
    std::sort(infos.begin(), infos.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  return a.lastModified() > b.lastModified();
              });

    int removed = 0;
    const QDateTime ageCutoff = m_backupRetentionDays > 0
        ? QDateTime::currentDateTime().addDays(-m_backupRetentionDays)
        : QDateTime();

    // Walk newest → oldest so we delete the OLDEST files first when
    // trimming for the count cap (we delete from the tail of `infos`),
    // and we never delete index 0 (the most-recent — safety floor).
    for (int i = infos.size() - 1; i > 0; --i) {
        const QFileInfo &fi = infos.at(i);
        bool shouldDelete = false;

        // Count cap: indices [m_backupRetentionCount, end) are over the limit.
        if (m_backupRetentionCount > 0 && i >= m_backupRetentionCount)
            shouldDelete = true;

        // Age cap: anything older than the cutoff is over the limit.
        if (ageCutoff.isValid() && fi.lastModified() < ageCutoff)
            shouldDelete = true;

        if (!shouldDelete)
            continue;

        if (QFile::remove(fi.absoluteFilePath()))
            ++removed;
        else
            qWarning("FileManager: sweepOldBackups failed to remove %s",
                     qPrintable(fi.absoluteFilePath()));
    }
    return removed;
}

void FileManager::setBackupRetentionCount(int count)
{
    if (count < 0) count = 0;
    if (m_backupRetentionCount == count) return;
    m_backupRetentionCount = count;
    emit backupRetentionCountChanged();
}

void FileManager::setBackupRetentionDays(int days)
{
    if (days < 0) days = 0;
    if (m_backupRetentionDays == days) return;
    m_backupRetentionDays = days;
    emit backupRetentionDaysChanged();
}

void FileManager::setVacuumThresholdBytes(qint64 bytes)
{
    if (bytes < 0) bytes = 0;
    if (m_vacuumThresholdBytes == bytes) return;
    m_vacuumThresholdBytes = bytes;
    emit vacuumThresholdBytesChanged();
}

QString FileManager::createPreVacuumBackup()
{
    const QString statePath = stateFilePath();
    if (!QFile::exists(statePath)) {
        qWarning("FileManager::createPreVacuumBackup: state.json does not exist");
        return {};
    }
    const QString safetyPath = statePath + QStringLiteral(".pre-vacuum.bak");
    // Overwrite any previous pre-vacuum backup — the user explicitly opted in.
    QFile::remove(safetyPath);
    if (!QFile::copy(statePath, safetyPath)) {
        qWarning("FileManager::createPreVacuumBackup: copy failed for %s",
                 qPrintable(safetyPath));
        return {};
    }
    return safetyPath;
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
    m_saveBlocked = false;   // D5: an explicit restore may replace state.json
    saveImmediate();

    qInfo("FileManager: successfully restored from %s", qPrintable(backupPath));
    return true;
}

bool FileManager::importData(const QString &jsonString)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);

    // Iter-night-15: harden the validation gate. Pre-fix, ONLY parse failures
    // were rejected. A successfully-parsed JSON array (e.g. a package.json
    // shaped as a list), an empty object, or any unrelated object (Todoist
    // export, random API payload, …) all passed through — `doc.object()`
    // silently returns an empty QJsonObject for arrays/nulls, and
    // `m_store->loadFromJson({})` calls `clear()` which wipes every task,
    // project, label, filter, reminder, comment, and the settings dict. The
    // QML "Load backup" file dialog routes user-picked files straight here,
    // so one wrong file pick became silent total data loss.
    //
    // The cousin `restoreBackup` (filemanager.cpp:444+) already does the
    // right thing: validates `!doc.isNull() && doc.isObject()` AND takes a
    // pre-restore safety snapshot via `saveImmediate()` BEFORE clearing the
    // store, so the user can "undo" by restoring from the now-fresh `.bak`.
    // Mirror both protections here.
    if (parseError.error != QJsonParseError::NoError
        || doc.isNull() || !doc.isObject())
        return false;

    const QJsonObject obj = doc.object();
    // D4: an OpenTodo state export always carries a `tasks` and/or `projects`
    // ARRAY (DataStore::toJson / createDefaultState write both). The previous
    // "any one of schema_version / projects / tasks / settings" gate let a
    // settings snapshot ({"format":"opentodo-settings-snapshot","settings":
    // {...}} from DataStore::exportSettings) through, and loading it wiped
    // every task and project. A state export never has a `format` sentinel —
    // that marks the other OpenTodo JSON formats (settings snapshot, project
    // snapshot, shared project/tasks), none of which is a full state.
    if (obj.contains(QStringLiteral("format")))
        return false;
    if (!obj.value(QStringLiteral("tasks")).isArray()
        && !obj.value(QStringLiteral("projects")).isArray())
        return false;

    // Pre-import safety snapshot — matches restoreBackup's behaviour so a
    // user who triggered an unwanted import can recover via the freshly-
    // rotated .bak chain. Without this, even a deliberate-but-mistaken
    // import had no undo path; the prior .bak was from the last autosave,
    // not from "the moment right before import".
    saveImmediate();
    m_store->loadFromJson(obj);
    // D5: an explicit import replaces the state on purpose, so it may write
    // state.json even if the startup load could not read it.
    m_saveBlocked = false;
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
    // D11: the IndexedDB restore has not finished — the store is still empty
    // and writing it would replace the persisted state on the next syncfs.
    if (m_wasmFsPending)
        return;
    // D5: state.json could not be read at load time and is still in place.
    if (m_saveBlocked) {
        m_saveTimer.stop();
        emit saveError(QStringLiteral("Not saving: state.json could not be read at "
                                      "startup and would be overwritten."));
        return;
    }

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

    // Q3.10: write a dated rolling backup before the new state lands. This
    // augments the 3-generation chain with a longer history that respects
    // m_backupRetentionCount / m_backupRetentionDays. Skipped on the very
    // first save (statePath doesn't exist yet — same guard as above).
    //
    // perf: the dated backup is a full-file copy and the sweep is a dir scan, so
    // running them on EVERY 1s autosave is wasteful during a burst of edits. The
    // 3-gen .bak chain above already covers immediate recovery, so throttle the
    // dated backup + sweep to at most once per ~5 min.
    if (QFile::exists(statePath)) {
        static constexpr qint64 kDatedBackupIntervalMs = 5 * 60 * 1000;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_lastDatedBackupMs == 0
            || nowMs - m_lastDatedBackupMs >= kDatedBackupIntervalMs) {
            writeDatedBackup();
            sweepOldBackups();
            m_lastDatedBackupMs = nowMs;
        }
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

#ifdef Q_OS_WASM
    persistWasmFs();
#endif

    // Q3.10: post-save housekeeping. The backup sweep now rides the throttled
    // dated-backup block above (no need to dir-scan on every save). Here we only
    // probe state.json size; if > threshold, signal the UI so the user can decide
    // whether to vacuum. We do NOT auto-vacuum because vacuum is destructive
    // (drops tombstones not yet seen by peers) and the user may not want a silent
    // mutation.
    if (m_vacuumThresholdBytes > 0) {
        const qint64 size = QFileInfo(statePath).size();
        if (size > m_vacuumThresholdBytes)
            emit vacuumRecommended(size);
    }
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

#ifdef Q_OS_WASM
// D11: Emscripten's default filesystem is MEMFS, so without an IDBFS mount
// everything under the data directory (state.json, backups, local_log/) is
// gone on page reload — `-lidbfs.js` only links the backend, nothing mounted
// it. Mount IDBFS over the data directory and populate it from IndexedDB
// before the first real load. FS.syncfs is asynchronous and the
// single-threaded build cannot block on it, so the completion is published
// on a JS global and polled from a Qt timer; load() then resumes. Saves are
// held back while the restore is pending (an empty store would otherwise be
// written and later synced over the user's data).
void FileManager::startWasmPersistentFs()
{
    m_wasmFsPending = true;
    QDir().mkpath(m_dataPath);   // IDBFS mounts onto an existing directory

    const QByteArray dir = m_dataPath.toUtf8();
    EM_ASM({
        var dir = UTF8ToString($0);
        globalThis.__opentodoIdbfsState = 0;   // 0 pending, 1 ok, 2 failed
        try {
            FS.mount(IDBFS, {}, dir);
        } catch (e) {
            // Already mounted (e.g. a second FileManager): just re-populate.
        }
        FS.syncfs(true, function (err) {
            if (err)
                console.warn("OpenTodo: restoring data from IndexedDB failed", err);
            globalThis.__opentodoIdbfsState = err ? 2 : 1;
        });
    }, dir.constData());

    auto *poll = new QTimer(this);
    poll->setInterval(20);
    connect(poll, &QTimer::timeout, this, [this, poll]() {
        const int state = EM_ASM_INT({ return globalThis.__opentodoIdbfsState | 0; });
        if (state == 0)
            return;
        poll->stop();
        poll->deleteLater();
        if (state == 2)
            qWarning("FileManager: IndexedDB unavailable; data will not persist across reloads");
        m_wasmFsPending = false;
        m_wasmFsReady = true;
        load();
    });
    poll->start();
}

void FileManager::persistWasmFs()
{
    if (!m_wasmFsReady)
        return;
    // Coalesce: Emscripten warns about (and redoes work for) overlapping
    // syncfs calls, so run at most one and re-run once if more saves landed.
    EM_ASM({
        if (globalThis.__opentodoIdbfsBusy) {
            globalThis.__opentodoIdbfsAgain = true;
            return;
        }
        var run = function () {
            globalThis.__opentodoIdbfsBusy = true;
            globalThis.__opentodoIdbfsAgain = false;
            FS.syncfs(false, function (err) {
                if (err)
                    console.warn("OpenTodo: saving data to IndexedDB failed", err);
                globalThis.__opentodoIdbfsBusy = false;
                if (globalThis.__opentodoIdbfsAgain)
                    run();
            });
        };
        run();
    });
}
#endif
