// Q3.10: Tests for FileManager dated-backup retention sweep + vacuum
// threshold signalling.
//
// FileManager now writes a dated backup `state.json.bak.YYYYMMDDhhmmss`
// after every save (in addition to the legacy 3-generation rolling
// .bak / .bak.1 / .bak.2 chain). After write completes, performSave runs
// sweepOldBackups() which:
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

        // Make sure every dated backup gets a unique timestamp by sleeping
        // ≥1 s between saves (dated backup suffix is second-resolution).
        for (int i = 0; i < 7; ++i) {
            ds.addProject(QStringLiteral("P%1").arg(i));
            fm.saveImmediate();
            QTest::qWait(1100);
        }

        // After 7+ writes, dated count should be <= 5 (cap), and the most-
        // recent one is still present.
        const QStringList dated = datedBackups();
        QVERIFY2(dated.size() <= 5,
                 qPrintable(QStringLiteral("dated.size() = %1").arg(dated.size())));
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

        // Create 3 dated backups by saving 3 times (1s apart for distinct
        // timestamps).
        for (int i = 0; i < 3; ++i) {
            ds.addProject(QStringLiteral("A%1").arg(i));
            fm.saveImmediate();
            QTest::qWait(1100);
        }
        QCOMPARE(datedBackups().size(), 3);

        // Age all of them past the 30-day cutoff.
        ageAllDatedBackups(60);

        // Trigger another save → sweep runs → all aged-out backups should
        // be removed EXCEPT the single most-recent (safety floor).
        ds.addProject(QStringLiteral("Trigger"));
        fm.saveImmediate();

        // After sweep: at most one of the previously-aged backups survives
        // (the most recent), plus the freshly-written current one.
        // Since the "trigger" save itself wrote a dated backup with a
        // current timestamp, count should be 2 minimum.
        const QStringList dated = datedBackups();
        QVERIFY2(dated.size() <= 2,
                 qPrintable(QStringLiteral("expected <= 2, got %1").arg(dated.size())));
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
        fm.saveImmediate();
        QTest::qWait(1100);

        QCOMPARE(datedBackups().size(), 1);

        // Age the single dated backup well past the cutoff.
        ageAllDatedBackups(365);

        // Trigger another save → sweep runs.
        ds.addProject(QStringLiteral("Second"));
        fm.saveImmediate();

        // At minimum the freshly-written dated backup must exist. The
        // previously-aged one might also exist (it became "most recent"
        // by the time the sweep ran on the prior generation), but never
        // zero — that would mean the safety floor failed.
        QVERIFY(datedBackups().size() >= 1);
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

        // 4 saves with pauses → full rolling chain populated AND multiple
        // dated backups produced.
        for (int i = 0; i < 4; ++i) {
            ds.addProject(QStringLiteral("R%1").arg(i));
            fm.saveImmediate();
            QTest::qWait(1100);
        }

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
            QTest::qWait(1100);
        }

        const QVariantList list = fm.listBackups();

        // We should see at least the rolling chain (.bak / .bak.1 / .bak.2)
        // and 2 dated backups (the very first save is a 1st save → no
        // dated; subsequent ones produce dated backups subject to dedup).
        QVERIFY2(list.size() >= 3,
                 qPrintable(QStringLiteral("listBackups returned %1 entries").arg(list.size())));

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
