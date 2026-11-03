// Q3.10: Tests for FileManager dated-backup retention sweep + vacuum
// threshold signalling.
//
// FileManager writes a dated backup `state.json.bak.YYYYMMDDhhmmss` on save
// (in addition to the legacy 3-generation rolling .bak / .bak.1 / .bak.2
// chain) — throttled to at most one per ~5 minutes, so most slots seed dated
// backups on disk and call sweepOldBackups() directly. The sweep (also run by
// performSave together with the dated backup):
//   * caps the dated backup count at m_backupRetentionCount (default 50)
//   * drops dated backups older than m_backupRetentionDays (default 30)
//   * NEVER deletes the single most-recent dated backup
//
// performSave ALSO probes state.json size; if > m_vacuumThresholdBytes
// (default 10 MB) it emits vacuumRecommended(size). The signal is
// advisory — no auto-mutation, the UI decides what to do.

#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QRegularExpression>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "model/task.h"
#include "model/project.h"
#include "utils/settingskeys.h"

class TestFileManagerRetention : public QObject
{
    Q_OBJECT

private:
    QString stateFilePath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo/state.json");
    }

    QString dataDir() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/OpenTodo");
    }

    // Remove every state-related artefact from the test sandbox so each
    // slot starts clean. Includes the rolling chain, dated backups, the
    // pre-vacuum safety backup, and the active state.json itself.
    void wipeAll() const
    {
        const QString path = stateFilePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::remove(path + QStringLiteral(".bak.1"));
        QFile::remove(path + QStringLiteral(".bak.2"));
        QFile::remove(path + QStringLiteral(".pre-vacuum.bak"));

        QDir dir(dataDir());
        const QFileInfoList list = dir.entryInfoList(
            QStringList{} << QStringLiteral("state.json.bak.*"),
            QDir::Files);
        for (const QFileInfo &fi : list)
            QFile::remove(fi.absoluteFilePath());
    }

    // Return all dated backups on disk matching the YYYYMMDDhhmmss suffix.
    QStringList datedBackups() const
    {
        QStringList out;
        QDir dir(dataDir());
        const QFileInfoList list = dir.entryInfoList(
            QStringList{} << QStringLiteral("state.json.bak.*"),
            QDir::Files);
        const QRegularExpression rx(
            QStringLiteral("^state\\.json\\.bak\\.(\\d{14})$"));
        for (const QFileInfo &fi : list) {
            if (rx.match(fi.fileName()).hasMatch())
                out.append(fi.absoluteFilePath());
        }
        return out;
    }

    // Write `n` dated backups (distinct second-resolution names) whose mtime
    // is `ageDays` days in the past, newest first. performSave() writes at
    // most one dated backup per ~5 minutes (perf throttle), so the retention
    // sweep cannot be exercised by saving repeatedly — seed the files and
    // call sweepOldBackups() directly instead.
    QStringList seedDatedBackups(int n, int ageDays, int startOffsetSecs = 0) const
    {
        QDir().mkpath(dataDir());
        QStringList out;
        const QDateTime base = QDateTime::currentDateTime().addDays(-ageDays);
        for (int i = 0; i < n; ++i) {
            const QDateTime ts = base.addSecs(-(startOffsetSecs + i));
            const QString path = stateFilePath() + QStringLiteral(".bak.")
                + ts.toUTC().toString(QStringLiteral("yyyyMMddHHmmss"));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly))
                continue;
            f.write("{}");
            f.flush();   // a later buffered write would reset the mtime
            f.setFileTime(ts, QFileDevice::FileModificationTime);
            f.close();
            out << path;
        }
        return out;
    }

    // Touch all dated backups so their mtime equals (now - daysOffset days).
    // Used to simulate the passage of time without sleeping in tests.
    void ageAllDatedBackups(int daysOffset) const
    {
        // Qt has no public API to set a file's mtime portably, so we
        // re-open & touch each file with a known datetime.
        // QFileDevice::setFileTime is Qt 6.0+ and works on Win/Linux.
        const QDateTime ts = QDateTime::currentDateTime().addDays(-daysOffset);
        const QStringList paths = datedBackups();
        for (const QString &p : paths) {
            QFile f(p);
            if (f.open(QIODevice::ReadWrite)) {
                f.setFileTime(ts, QFileDevice::FileModificationTime);
                f.close();
            }
        }
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_FileMgrRetention"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filemanager_retention"));
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

    // ── Dated backups appear at all ────────────────────────────────────────
    //
    // First save: no backups (state.json didn't exist before).
    // Second save: ONE dated backup carrying generation A snapshot.
    void dated_backup_is_written_on_subsequent_save()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();                      // 1st save — creates state.json

        QCOMPARE(datedBackups().size(), 0);

        ds.addProject(QStringLiteral("Trigger"));
        fm.saveImmediate();             // 2nd save — writes dated backup

        QCOMPARE(datedBackups().size(), 1);
    }

    // ── Count cap (51 → 50) ────────────────────────────────────────────────
    //
    // The spec acceptance case: after 51 writes, only 50 dated backups
    // should remain. We bump the retention down to 5 so the test stays
    // fast and exercises the same sweep code path.
    void count_cap_keeps_at_most_N_dated_backups()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setBackupRetentionCount(5);
        fm.setBackupRetentionDays(0);   // disable age cap so count is the only signal

        const QStringList seeded = seedDatedBackups(7, 0);
        QCOMPARE(datedBackups().size(), 7);

        QCOMPARE(fm.sweepOldBackups(), 2);
        const QStringList dated = datedBackups();
        QCOMPARE(dated.size(), 5);
        // The newest five survive; the two oldest went.
        QVERIFY(QFile::exists(seeded.first()));
        QVERIFY(!QFile::exists(seeded.last()));
    }

    // ── Age cap ────────────────────────────────────────────────────────────
    //
    // Backups older than retention_days are deleted (except the most recent
    // — safety floor).
    void age_cap_drops_backups_older_than_retention_days()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setBackupRetentionCount(0);  // disable count cap
        fm.setBackupRetentionDays(30);

        // Three backups past the 30-day cutoff plus one fresh one.
        const QStringList aged = seedDatedBackups(3, 60);
        const QStringList fresh = seedDatedBackups(1, 0);
        QCOMPARE(datedBackups().size(), 4);

        // Every aged-out backup goes; the fresh one is kept.
        QCOMPARE(fm.sweepOldBackups(), 3);
        const QStringList dated = datedBackups();
        QCOMPARE(dated.size(), 1);
        QVERIFY(QFile::exists(fresh.first()));
        for (const QString &p : aged)
            QVERIFY(!QFile::exists(p));
    }

    // ── Safety floor: never delete most-recent ─────────────────────────────
    //
    // Even if all dated backups are very old AND the count is 0, we keep
    // the single most-recent dated backup.
    void safety_floor_preserves_most_recent_dated_backup()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setBackupRetentionCount(0);  // count = 0 implies "no count cap"
        fm.setBackupRetentionDays(1);

        ds.addProject(QStringLiteral("Only"));
        fm.saveImmediate();             // 2nd save → the one dated backup
        QCOMPARE(datedBackups().size(), 1);

        // Age it (and two older ones) well past the cutoff.
        ageAllDatedBackups(365);
        seedDatedBackups(2, 400);
        QCOMPARE(datedBackups().size(), 3);

        // All three are over the age cap, but the newest is the safety floor.
        QCOMPARE(fm.sweepOldBackups(), 2);
        QCOMPARE(datedBackups().size(), 1);
    }

    // ── Rolling chain is left alone by sweep ───────────────────────────────
    //
    // The 3-generation rolling backup chain (.bak / .bak.1 / .bak.2) must
    // never be touched by sweepOldBackups, even with very aggressive
    // retention settings.
    void sweep_does_not_touch_rolling_chain()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setBackupRetentionCount(1);
        fm.setBackupRetentionDays(1);

        // 4 saves → full rolling chain populated; plus several dated backups.
        for (int i = 0; i < 4; ++i) {
            ds.addProject(QStringLiteral("R%1").arg(i));
            fm.saveImmediate();
        }
        seedDatedBackups(3, 30);
        fm.sweepOldBackups();

        QVERIFY(QFile::exists(stateFilePath() + QStringLiteral(".bak")));
        QVERIFY(QFile::exists(stateFilePath() + QStringLiteral(".bak.1")));
        QVERIFY(QFile::exists(stateFilePath() + QStringLiteral(".bak.2")));
    }

    // ── Vacuum threshold signal ────────────────────────────────────────────
    //
    // When state.json size exceeds vacuumThresholdBytes, performSave emits
    // vacuumRecommended(size). Setting the threshold absurdly low (10 bytes)
    // guarantees the signal fires.
    void vacuum_recommended_signal_fires_when_state_exceeds_threshold()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setVacuumThresholdBytes(10);

        QSignalSpy spy(&fm, &FileManager::vacuumRecommended);

        ds.addProject(QStringLiteral("Big"));
        fm.saveImmediate();

        QVERIFY(spy.count() >= 1);
        const QList<QVariant> args = spy.last();
        const qint64 size = args.first().toLongLong();
        QVERIFY(size > 10);  // matches threshold semantics
    }

    // Vacuum signal does NOT fire below the threshold.
    void vacuum_recommended_silent_when_under_threshold()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        fm.setVacuumThresholdBytes(100 * 1024 * 1024);  // 100 MB — well above tiny state

        QSignalSpy spy(&fm, &FileManager::vacuumRecommended);

        ds.addProject(QStringLiteral("Tiny"));
        fm.saveImmediate();

        QCOMPARE(spy.count(), 0);
    }

    // ── createPreVacuumBackup ──────────────────────────────────────────────
    //
    // Creating a pre-vacuum backup copies state.json verbatim and returns
    // the destination path. The file persists across normal rotations.
    void pre_vacuum_backup_is_created_and_preserved()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();
        ds.addProject(QStringLiteral("Before"));
        fm.saveImmediate();

        const QString out = fm.createPreVacuumBackup();
        QVERIFY(!out.isEmpty());
        QVERIFY(QFile::exists(out));

        // Subsequent rolling saves do NOT touch the pre-vacuum backup.
        for (int i = 0; i < 5; ++i) {
            ds.addProject(QStringLiteral("Q%1").arg(i));
            fm.saveImmediate();
            QTest::qWait(50);
        }
        QVERIFY(QFile::exists(out));
    }

    // ── listBackups now includes dated backups ─────────────────────────────
    //
    // listBackups() should enumerate both rolling and dated backups, sorted
    // by mtime descending.
    void listBackups_includes_dated_entries()
    {
        DataStore ds;
        FileManager fm(&ds);
        fm.load();

        for (int i = 0; i < 3; ++i) {
            ds.addProject(QStringLiteral("L%1").arg(i));
            fm.saveImmediate();
        }
        seedDatedBackups(2, 1);

        const QVariantList list = fm.listBackups();

        // The rolling chain (.bak / .bak.1 / .bak.2), the dated backup the
        // second save wrote, and the two seeded dated backups.
        QVERIFY2(list.size() >= 6,
                 qPrintable(QStringLiteral("listBackups returned %1 entries").arg(list.size())));
        int datedCount = 0;
        for (const QVariant &v : list) {
            if (QRegularExpression(QStringLiteral("^state\\.json\\.bak\\.\\d{14}$"))
                    .match(v.toMap().value(QStringLiteral("name")).toString()).hasMatch())
                ++datedCount;
        }
        QCOMPARE(datedCount, 3);

        // Verify newest-first ordering by mtime.
        QDateTime prev;
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            const QDateTime mtime = m.value(QStringLiteral("mtime")).toDateTime();
            QVERIFY(mtime.isValid());
            if (prev.isValid())
                QVERIFY(mtime <= prev);
            prev = mtime;
        }
    }

    // ── Settings → FileManager hydration ───────────────────────────────────
    //
    // FileManager hydrates its retention policy from DataStore settings on
    // the next settingsChanged emission (which fires from loadFromJson).
    void settings_hydrate_retention_policy_on_load()
    {
        // Producer: write a state.json with custom retention settings.
        {
            DataStore producer;
            FileManager pfm(&producer);
            pfm.load();
            producer.setSetting(QLatin1String(SettingsKeys::BackupRetentionCount), 7);
            producer.setSetting(QLatin1String(SettingsKeys::BackupRetentionDays), 14);
            producer.setSetting(QLatin1String(SettingsKeys::VacuumThresholdBytes),
                                qint64(5 * 1024 * 1024));
            pfm.saveImmediate();
        }

        // Consumer: new FileManager picks up the persisted settings.
        DataStore consumer;
        FileManager cfm(&consumer);
        cfm.load();

        QCOMPARE(cfm.backupRetentionCount(), 7);
        QCOMPARE(cfm.backupRetentionDays(), 14);
        QCOMPARE(cfm.vacuumThresholdBytes(), qint64(5 * 1024 * 1024));
    }
};

QTEST_GUILESS_MAIN(TestFileManagerRetention)
#include "test_filemanager_retention.moc"
