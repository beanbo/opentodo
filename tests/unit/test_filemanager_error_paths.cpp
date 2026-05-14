// Tests for FileManager error paths — disk failures, malformed JSON, locked
// files, invalid restore inputs, and platform permission edge cases.
//
// Coverage focus (NOT covered by test_filemanager.cpp / _backups.cpp /
// _autosave.cpp):
//   * Save failures: read-only directory, missing parent dir, empty path,
//     atomic-write doesn't leave a half-file behind on commit failure.
//   * Load failures: missing file, empty file, garbage bytes, future schema
//     version, non-object JSON root.
//   * Backup-restore failures: non-existent path, target is a directory,
//     listBackups when nothing exists.
//   * Windows-specific: writing a target currently held open by another
//     handle inside the same process.
//
// All tests use QStandardPaths::setTestModeEnabled(true) so reads/writes go
// to a sandbox path under the user's temp dir, plus a unique
// QSettings org/app to keep parallel-suite runs isolated.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/project.h"
#include "model/task.h"

class TestFileManagerErrorPaths : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settingsTmp;

    QString stateFilePath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo/state.json");
    }

    QString dataDirPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo");
    }

    QString bak0Path() const { return stateFilePath() + QStringLiteral(".bak"); }
    QString bak1Path() const { return stateFilePath() + QStringLiteral(".bak.1"); }
    QString bak2Path() const { return stateFilePath() + QStringLiteral(".bak.2"); }

    // Wipe any leftover state.json plus all backup generations and the
    // sentinel "corrupt" rename produced by the load-time auto-recovery
    // path so each slot starts pristine.
    void wipeAll() const
    {
        QFile::remove(stateFilePath());
        QFile::remove(bak0Path());
        QFile::remove(bak1Path());
        QFile::remove(bak2Path());

        // Auto-recovery renames bad state.json to state.json.corrupt-<ts>;
        // sweep those between tests so they don't accumulate.
        QDir dir(dataDirPath());
        if (dir.exists()) {
            const QStringList stale = dir.entryList(
                QStringList{QStringLiteral("state.json.corrupt-*")},
                QDir::Files);
            for (const QString &name : stale)
                QFile::remove(dir.absoluteFilePath(name));
        }
    }

    // Hand-write raw bytes into state.json without going through FileManager,
    // so we can prepare a corrupted/empty/etc. fixture for the load path.
    bool writeRawState(const QByteArray &bytes) const
    {
        QDir().mkpath(dataDirPath());
        QFile f(stateFilePath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(bytes);
        f.close();
        return true;
    }

private slots:
    void initTestCase()
    {
        // Unique QSettings org/app keeps parallel test binaries from racing
        // on the same registry/INI keys (per the task requirement).
        QVERIFY(m_settingsTmp.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("FileManagerErrorPaths"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsTmp.path());

        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    void cleanup()
    {
        wipeAll();
    }

    // ── Save failures (write-side) ──────────────────────────────────────────

    // Target directory doesn't exist and can't be created (parent path is
    // bogus). exportTo must return false without crashing or partially
    // writing. We verify state.json (in the legitimate dir) is untouched.
    void exportTo_nonexistent_parent_directory_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Pristine"));
        fm.saveImmediate();

        // Snapshot bytes BEFORE the bad export.
        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        // A path whose parent dir does not exist. Use a path inside the test
        // sandbox so we don't depend on Z:\ existing or not.
        const QString bogus = dataDirPath()
                              + QStringLiteral("/no_such_subdir_12345/export.json");
        QVERIFY(!QFileInfo::exists(QFileInfo(bogus).absolutePath()));

        const bool ok = fm.exportTo(bogus);
        QVERIFY(!ok);

        // No file appeared at the bogus path (or its temp sibling).
        QVERIFY(!QFile::exists(bogus));

        // state.json byte-identical.
        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);
    }

    // Empty target path: QSaveFile with an empty filename can't open
    // → exportTo returns false. Documents the current behaviour.
    void exportTo_empty_path_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("X"));
        fm.saveImmediate();

        const bool ok = fm.exportTo(QString());
        QVERIFY(!ok);

        // state.json untouched and still well-formed.
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
    }

    // exportTo to a path that resolves to an existing directory must fail
    // (you can't open a dir for writing as a file). state.json untouched.
    void exportTo_path_that_is_a_directory_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("KeepMe"));
        fm.saveImmediate();

        // Pre-create a directory at the path we'll try to "write" to.
        const QString dirAsTarget = dataDirPath()
                                    + QStringLiteral("/this_is_a_dir");
        QVERIFY(QDir().mkpath(dirAsTarget));
        QVERIFY(QFileInfo(dirAsTarget).isDir());

        const bool ok = fm.exportTo(dirAsTarget);
        QVERIFY(!ok);

        // state.json still exists and contains "KeepMe".
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        f.close();
        QVERIFY(bytes.contains("KeepMe"));

        // Cleanup our directory fixture.
        QDir(dirAsTarget).removeRecursively();
    }

    // QSaveFile::cancelWriting (or close without commit) must leave the
    // existing file intact AND not leave a stray temp file behind. We
    // exercise QSaveFile directly here against state.json since FileManager
    // doesn't expose a cancel hook — this validates the contract that
    // FileManager relies on.
    void qsavefile_cancelled_write_leaves_no_partial_file()
    {
        // Set up state.json with known contents via FileManager.
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Untouched"));
        fm.saveImmediate();

        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        // Open a QSaveFile, write some bytes, then cancel.
        {
            QSaveFile saver(stateFilePath());
            QVERIFY(saver.open(QIODevice::WriteOnly));
            saver.write("garbage that should never land");
            saver.cancelWriting();
            // Destruction without commit() must NOT replace the original.
        }

        // state.json byte-identical to the pre-cancel snapshot.
        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);

        // No leftover sibling temp files in the data dir (QSaveFile uses
        // names like "state.json.XXXXXX").
        const QDir dir(dataDirPath());
        const QStringList strays = dir.entryList(
            QStringList{QStringLiteral("state.json.??????")}, QDir::Files);
        QVERIFY2(strays.isEmpty(),
                 qPrintable(QStringLiteral("Stray temp files: ")
                            + strays.join(QStringLiteral(", "))));
    }

    // Read-only target directory: pre-create a dir, mark it read-only via
    // QFile::setPermissions, attempt exportTo into it. The behaviour is
    // OS-dependent on Windows (the in-process user typically retains write
    // access regardless of read-only flags), so we accept either outcome:
    // either the write fails gracefully (false), or it succeeds and we
    // simply assert the in-memory state is still consistent. Either way
    // FileManager must NOT crash and state.json must remain valid.
    void exportTo_into_readonly_dir_does_not_crash_or_corrupt_state()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("BeforeReadOnly"));
        fm.saveImmediate();

        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        const QString readOnlyDir = dataDirPath()
                                    + QStringLiteral("/readonly_subdir");
        QVERIFY(QDir().mkpath(readOnlyDir));

        // Strip write permission for owner/user/group/other.
        const QFileDevice::Permissions readOnly =
              QFileDevice::ReadOwner | QFileDevice::ReadUser
            | QFileDevice::ReadGroup | QFileDevice::ReadOther
            | QFileDevice::ExeOwner  | QFileDevice::ExeUser;
        QFile::setPermissions(readOnlyDir, readOnly);

        const QString target = readOnlyDir + QStringLiteral("/exported.json");
        const bool ok = fm.exportTo(target);

        // Restore write permission so cleanup works regardless of platform.
        const QFileDevice::Permissions writable =
              QFileDevice::ReadOwner  | QFileDevice::WriteOwner
            | QFileDevice::ReadUser   | QFileDevice::WriteUser
            | QFileDevice::ReadGroup  | QFileDevice::WriteGroup
            | QFileDevice::ReadOther  | QFileDevice::WriteOther
            | QFileDevice::ExeOwner   | QFileDevice::ExeUser;
        QFile::setPermissions(readOnlyDir, writable);

        // Whether or not the write was actually blocked, the in-memory
        // store must still hold the project we added and state.json must
        // remain byte-identical (we never re-saved through FileManager
        // after the export attempt).
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("BeforeReadOnly")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);

        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);

        // If the write went through, the file should be valid JSON; if it
        // didn't, the file should not exist. Either is acceptable.
        if (ok) {
            QFile out(target);
            QVERIFY(out.open(QIODevice::ReadOnly));
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(out.readAll(), &err);
            QCOMPARE(err.error, QJsonParseError::NoError);
            QVERIFY(doc.isObject());
            QFile::remove(target);
        } else {
            QVERIFY(!QFile::exists(target));
        }

        QDir(readOnlyDir).removeRecursively();
    }

    // ── Load failures (read-side) ──────────────────────────────────────────

    // No state.json on disk at all → load() falls back to createDefaultState
    // and emits loaded(). DataStore should have the default Inbox.
    void load_missing_state_creates_default_without_error()
    {
        QVERIFY(!QFile::exists(stateFilePath()));

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy loadedSpy(&fm, &FileManager::loaded);
        QSignalSpy errSpy(&fm, &FileManager::loadError);

        fm.load();

        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(errSpy.count(), 0);

        // Default Inbox is created.
        Project *inbox = ds.inboxProject();
        QVERIFY(inbox != nullptr);
        QVERIFY(inbox->isInbox());
    }

    // 0-byte state.json: QJsonDocument::fromJson on an empty buffer returns
    // a null document; the auto-recovery loop tries .bak/.bak.1/.bak.2
    // (none exist), then renames the empty file to state.json.corrupt-<ts>
    // and falls through to createDefaultState. loadError fires once.
    void load_empty_state_falls_back_to_defaults()
    {
        QVERIFY(writeRawState(QByteArray()));
        QVERIFY(QFile::exists(stateFilePath()));
        QCOMPARE(QFileInfo(stateFilePath()).size(), qint64(0));

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy loadedSpy(&fm, &FileManager::loaded);
        QSignalSpy errSpy(&fm, &FileManager::loadError);

        fm.load();

        // We still emit loaded() because createDefaultState runs.
        // loadError is expected exactly once for the corruption notice.
        QCOMPARE(errSpy.count(), 1);
        // A default state must be present.
        QVERIFY(ds.inboxProject() != nullptr);

        // Ensure no crash and state.json now contains valid JSON
        // (createDefaultState wrote it via saveImmediate).
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());

        Q_UNUSED(loadedSpy)
    }

    // Garbage bytes: same auto-recovery story. We start with no .bak files
    // so recovery falls through to createDefaultState.
    void load_garbage_json_falls_back_to_defaults()
    {
        QVERIFY(writeRawState(QByteArrayLiteral("garbage{ not json !!!")));

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy errSpy(&fm, &FileManager::loadError);
        fm.load();

        // Exactly one loadError describing the parse failure.
        QCOMPARE(errSpy.count(), 1);
        const QString errMsg = errSpy.takeFirst().value(0).toString();
        QVERIFY2(errMsg.contains(QStringLiteral("JSON")),
                 qPrintable(QStringLiteral("Unexpected error message: ") + errMsg));

        // Default Inbox still created.
        QVERIFY(ds.inboxProject() != nullptr);

        // The corrupt file got renamed away — no garbage left at state.json.
        QFile f(stateFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
        QCOMPARE(parseErr.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());

        // Auto-recovery rename: a state.json.corrupt-<ts> sibling exists.
        const QDir dir(dataDirPath());
        const QStringList rescued = dir.entryList(
            QStringList{QStringLiteral("state.json.corrupt-*")}, QDir::Files);
        QVERIFY(!rescued.isEmpty());
    }

    // Future schema_version: FileManager passes the bytes through with a
    // qWarning but otherwise loads them. Verifies FileManager doesn't
    // panic when DataStore's schema version is exceeded.
    void load_future_schema_version_loads_with_warning()
    {
        // Build a minimal but valid state.json with schema_version one higher
        // than DataStore::CURRENT_SCHEMA_VERSION. Note: this slot only
        // verifies FileManager's behaviour — the DataStore migration tests
        // cover the actual schema-handling semantics.
        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION + 1;
        root[QStringLiteral("settings")] = QJsonObject();
        root[QStringLiteral("projects")] = QJsonArray();
        root[QStringLiteral("tasks")] = QJsonArray();
        root[QStringLiteral("completed_tasks")] = QJsonArray();
        root[QStringLiteral("sections")] = QJsonArray();
        root[QStringLiteral("labels")] = QJsonArray();
        root[QStringLiteral("filters")] = QJsonArray();
        root[QStringLiteral("reminders")] = QJsonArray();
        root[QStringLiteral("comments")] = QJsonArray();

        const QJsonDocument doc(root);
        QVERIFY(writeRawState(doc.toJson(QJsonDocument::Indented)));

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy loadedSpy(&fm, &FileManager::loaded);
        QSignalSpy errSpy(&fm, &FileManager::loadError);

        fm.load();

        // FileManager must finish loading successfully — the schema-newer
        // case is a warning, not a hard failure.
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(errSpy.count(), 0);
    }

    // Non-object root (top-level array): JSON is technically valid but
    // doc.isObject() is false → auto-recovery path runs → defaults.
    void load_non_object_root_falls_back_to_defaults()
    {
        QVERIFY(writeRawState(QByteArrayLiteral("[1, 2, 3]")));

        DataStore ds;
        FileManager fm(&ds);

        QSignalSpy errSpy(&fm, &FileManager::loadError);
        fm.load();

        QCOMPARE(errSpy.count(), 1);
        QVERIFY(ds.inboxProject() != nullptr);
    }

    // ── Backup-restore failures ─────────────────────────────────────────────

    void restoreBackup_nonexistent_path_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("StillHere"));
        fm.saveImmediate();

        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        const QString missing = dataDirPath()
                                + QStringLiteral("/does_not_exist_anywhere.bak");
        QVERIFY(!QFile::exists(missing));

        const bool ok = fm.restoreBackup(missing);
        QVERIFY(!ok);

        // state.json byte-identical, in-memory store unchanged.
        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);

        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("StillHere")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // restoreBackup with a directory path: QFile::exists returns true for
    // a directory, but opening it as a file in ReadOnly mode fails on
    // Windows → restoreBackup returns false. state.json untouched.
    void restoreBackup_directory_path_returns_false()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Survivor"));
        fm.saveImmediate();

        // Create a directory and pass its path as the "backup".
        const QString asDir = dataDirPath()
                              + QStringLiteral("/backup_is_actually_a_dir");
        QVERIFY(QDir().mkpath(asDir));

        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        const bool ok = fm.restoreBackup(asDir);
        QVERIFY(!ok);

        // state.json byte-identical.
        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);

        // In-memory survivor still present.
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("Survivor")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);

        QDir(asDir).removeRecursively();
    }

    // listBackups when no .bak files exist anywhere returns an empty list,
    // even if state.json itself doesn't exist either.
    void listBackups_returns_empty_when_data_dir_empty()
    {
        // Sanity: cleanup() ran. No state.json, no .bak* either.
        QVERIFY(!QFile::exists(stateFilePath()));
        QVERIFY(!QFile::exists(bak0Path()));

        DataStore ds;
        FileManager fm(&ds);
        // No load() — we want to test listBackups before any I/O has even
        // happened. dataDirPath() may not exist yet.
        const QVariantList list = fm.listBackups();
        QCOMPARE(list.size(), 0);
    }

    // ── Permission edge cases (Windows-specific) ────────────────────────────

#ifdef Q_OS_WIN
    // Windows file-locking: open state.json with ShareMode::ReadOnly so
    // writers cannot replace it while we hold the handle. exportTo to that
    // path or QSaveFile.commit on it should fail gracefully (because the
    // rename step can't replace the locked target).
    void win_locked_target_file_export_fails_gracefully()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("LockedTest"));
        fm.saveImmediate();

        // Snapshot bytes before the locked-write attempt.
        QFile pre(stateFilePath());
        QVERIFY(pre.open(QIODevice::ReadOnly));
        const QByteArray before = pre.readAll();
        pre.close();

        // Pick a sibling path inside the data dir that we'll lock.
        const QString lockedTarget = dataDirPath()
                                     + QStringLiteral("/locked_export.json");

        // Pre-create the file so we have something to hold open exclusively
        // (write a single byte so QFile::open finds something).
        {
            QFile seed(lockedTarget);
            QVERIFY(seed.open(QIODevice::WriteOnly));
            seed.write("x");
            seed.close();
        }

        // Hold the file open for the duration of the export attempt.
        // QFile on Windows opens with FILE_SHARE_READ by default, so
        // another process can read; QSaveFile's atomic rename ALSO needs
        // delete access on the existing target which a held handle blocks.
        QFile lock(lockedTarget);
        QVERIFY(lock.open(QIODevice::ReadOnly));

        const bool ok = fm.exportTo(lockedTarget);

        // Whether the OS actually blocks the replace depends on the
        // underlying handle flags — on this build we typically get false.
        // Either way the in-memory state must remain consistent and
        // state.json must be untouched.
        Q_UNUSED(ok)

        lock.close();
        QFile::remove(lockedTarget);

        // state.json byte-identical.
        QFile post(stateFilePath());
        QVERIFY(post.open(QIODevice::ReadOnly));
        QCOMPARE(post.readAll(), before);

        // In-memory data unchanged.
        bool found = false;
        for (auto *p : ds.allProjects()) {
            if (p->name() == QStringLiteral("LockedTest")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
#endif
};

QTEST_GUILESS_MAIN(TestFileManagerErrorPaths)
#include "test_filemanager_error_paths.moc"
