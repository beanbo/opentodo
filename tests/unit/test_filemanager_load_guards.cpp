// =============================================================================
// FileManager data-loss guards (global fix pass F2):
//
//   D4  importData rejects a settings snapshot (DataStore::exportSettings) and
//       any object without a `tasks` / `projects` array — such a file used to
//       pass the key check and wipe every task and project.
//   D5  an existing state.json that cannot be opened is never overwritten
//       with the default state: it is moved aside (then backups / defaults),
//       or — if it cannot be moved — saving is refused for the session.
//   D13 loading an unchanged state.json does not schedule a save (each launch
//       used to rewrite it and rotate a redundant copy through .bak).
// =============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/project.h"
#include "model/task.h"

class TestFileManagerLoadGuards : public QObject
{
    Q_OBJECT

    QTemporaryDir m_settingsTmp;

    QString dataDir() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo");
    }
    QString statePath() const { return dataDir() + QStringLiteral("/state.json"); }

    void wipeAll() const
    {
        QDir dir(dataDir());
        if (!dir.exists())
            return;
        const QFileInfoList entries = dir.entryInfoList(
            QStringList{QStringLiteral("state.json*")},
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir())
                QDir(fi.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(fi.absoluteFilePath());
        }
    }

    QStringList asideEntries() const
    {
        return QDir(dataDir()).entryList(
            QStringList{QStringLiteral("state.json.unreadable-*")},
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    }

    // state.json as a DIRECTORY: QFile::exists() is true but open() fails —
    // a portable stand-in for an unreadable file (chmod does not stop root).
    void makeUnreadableState() const
    {
        QDir().mkpath(statePath());
        QFile marker(statePath() + QStringLiteral("/marker"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("user data");
        marker.close();
    }

    void writeJson(const QString &path, const QJsonObject &obj) const
    {
        QDir().mkpath(dataDir());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(obj).toJson());
    }

    static QJsonObject stateWithProject(const QString &name)
    {
        QJsonObject inbox{{QStringLiteral("id"), QStringLiteral("inbox")},
                          {QStringLiteral("name"), QStringLiteral("Inbox")},
                          {QStringLiteral("is_inbox"), true}};
        QJsonObject proj{{QStringLiteral("id"), QStringLiteral("p1")},
                         {QStringLiteral("name"), name}};
        QJsonObject root;
        root[QStringLiteral("schema_version")] = DataStore::CURRENT_SCHEMA_VERSION;
        root[QStringLiteral("settings")] = QJsonObject();
        root[QStringLiteral("projects")] = QJsonArray{inbox, proj};
        root[QStringLiteral("tasks")] = QJsonArray();
        return root;
    }

    static bool hasProject(const DataStore &ds, const QString &name)
    {
        for (Project *p : ds.allProjects())
            if (p->name() == name) return true;
        return false;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsTmp.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_FileMgrLoadGuards"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filemanager_load_guards"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsTmp.path());
        QStandardPaths::setTestModeEnabled(true);
        wipeAll();
    }

    void cleanup() { wipeAll(); }

    // ── D4 ──────────────────────────────────────────────────────────────────

    void importData_rejectsSettingsSnapshot()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));
        ds.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));

        QTemporaryDir out;
        QVERIFY(out.isValid());
        const QString path = ds.exportSettings(out.path());
        QVERIFY(!path.isEmpty());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString snapshot = QString::fromUtf8(f.readAll());

        QVERIFY2(!fm.importData(snapshot),
                 "a settings snapshot is not a state backup and must not wipe the data");
        QVERIFY(hasProject(ds, QStringLiteral("Sentinel")));
    }

    void importData_requiresTasksOrProjectsArray()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Sentinel"));

        QVERIFY(!fm.importData(QStringLiteral(R"({"schema_version":2,"settings":{"theme":"x"}})")));
        QVERIFY(!fm.importData(QStringLiteral(R"({"tasks":{},"projects":"nope"})")));
        QVERIFY(hasProject(ds, QStringLiteral("Sentinel")));

        // A real export still round-trips.
        const QString exported = fm.exportData();
        QVERIFY(fm.importData(exported));
        QVERIFY(hasProject(ds, QStringLiteral("Sentinel")));
    }

    // ── D5 ──────────────────────────────────────────────────────────────────

    void load_unreadableState_isMovedAside_notOverwritten()
    {
        makeUnreadableState();
        DataStore ds;
        FileManager fm(&ds);
        QSignalSpy errSpy(&fm, &FileManager::loadError);
        fm.load();

        QCOMPARE(errSpy.count(), 1);
        const QStringList aside = asideEntries();
        QCOMPARE(aside.size(), 1);
        QVERIFY2(QFile::exists(dataDir() + QLatin1Char('/') + aside.first()
                               + QStringLiteral("/marker")),
                 "the unreadable original must be preserved");
        // A fresh, readable state.json replaced it.
        QVERIFY(QFileInfo(statePath()).isFile());
        QVERIFY(ds.inboxProject());
    }

    void load_unreadableState_recoversFromBackup()
    {
        writeJson(statePath() + QStringLiteral(".bak"), stateWithProject(QStringLiteral("FromBak")));
        makeUnreadableState();
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        QVERIFY(hasProject(ds, QStringLiteral("FromBak")));
        QCOMPARE(asideEntries().size(), 1);
        QVERIFY(QFileInfo(statePath()).isFile());
    }

    void load_unreadableAndUnmovableState_blocksSaving()
    {
        makeUnreadableState();
        // Occupy every aside name the load can pick within the next seconds,
        // so the rename fails the way it does for a locked file.
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QDir().mkpath(dataDir());
        for (int s = 0; s < 5; ++s) {
            QFile occ(statePath() + QStringLiteral(".unreadable-")
                      + now.addSecs(s).toString(QStringLiteral("yyyyMMddhhmmss")));
            QVERIFY(occ.open(QIODevice::WriteOnly));
        }

        DataStore ds;
        FileManager fm(&ds);
        QSignalSpy savedSpy(&fm, &FileManager::saved);
        QSignalSpy saveErrSpy(&fm, &FileManager::saveError);
        fm.load();

        ds.addProject(QStringLiteral("EditWhileBlocked"));
        fm.saveImmediate();

        QCOMPARE(savedSpy.count(), 0);
        QVERIFY(saveErrSpy.count() >= 1);
        QVERIFY2(QFileInfo(statePath()).isDir()
                 && QFile::exists(statePath() + QStringLiteral("/marker")),
                 "state.json must be left exactly as it was");
    }

    // ── D13 ─────────────────────────────────────────────────────────────────

    void load_unchangedState_doesNotSave()
    {
        writeJson(statePath(), stateWithProject(QStringLiteral("Stable")));
        const QDateTime before = QFileInfo(statePath()).lastModified();

        DataStore ds;
        FileManager fm(&ds);
        QSignalSpy savedSpy(&fm, &FileManager::saved);
        fm.load();
        QVERIFY(hasProject(ds, QStringLiteral("Stable")));
        QVERIFY(!fm.isDirty());

        QTest::qWait(1400);   // > the 1 s autosave debounce
        QCOMPARE(savedSpy.count(), 0);
        QVERIFY2(!QFile::exists(statePath() + QStringLiteral(".bak")),
                 "a no-op launch must not rotate the backup chain");
        QCOMPARE(QFileInfo(statePath()).lastModified(), before);

        // A real edit still autosaves.
        ds.addProject(QStringLiteral("Edit"));
        QVERIFY(savedSpy.wait(2000));
    }

    void load_stateRepairedOnLoad_isSaved()
    {
        // Two Inboxes → loadFromJson merges them → must be persisted.
        QJsonObject root = stateWithProject(QStringLiteral("X"));
        QJsonArray projects = root[QStringLiteral("projects")].toArray();
        projects.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("inbox-2")},
                                    {QStringLiteral("name"), QStringLiteral("Inbox")},
                                    {QStringLiteral("is_inbox"), true},
                                    {QStringLiteral("created_at"),
                                     QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
        projects[0] = QJsonObject{{QStringLiteral("id"), QStringLiteral("inbox")},
                                  {QStringLiteral("name"), QStringLiteral("Inbox")},
                                  {QStringLiteral("is_inbox"), true},
                                  {QStringLiteral("created_at"),
                                   QDateTime::currentDateTimeUtc().addDays(-1)
                                       .toString(Qt::ISODateWithMs)}};
        root[QStringLiteral("projects")] = projects;
        writeJson(statePath(), root);

        DataStore ds;
        FileManager fm(&ds);
        QSignalSpy savedSpy(&fm, &FileManager::saved);
        fm.load();
        QVERIFY(fm.isDirty());
        QVERIFY(savedSpy.wait(2000));
    }
};

QTEST_GUILESS_MAIN(TestFileManagerLoadGuards)
#include "test_filemanager_load_guards.moc"
