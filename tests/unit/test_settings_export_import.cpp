#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "viewmodels/settingsviewmodel.h"
#include "utils/settingskeys.h"

// =============================================================================
// Round-trip tests for DataStore::exportSettings() / importSettings().
//
// API shape (DataStore):
//   QString exportSettings(const QString &dir)
//     - Writes "<dir>/opentodo-settings-<yyyyMMdd-HHmmss>.json".
//     - Empty dir falls back to QStandardPaths::DocumentsLocation; tests
//       always pass an explicit dir under QTemporaryDir to keep things
//       hermetic.
//     - Returns the absolute path on success, "" on failure.
//
//   bool importSettings(const QString &filePath)
//     - Reads JSON, REQUIRES root.format == "opentodo-settings-snapshot",
//       calls DataStore::setSettings(root.settings) which emits both
//       settingsChanged() and dataChanged().
//     - Returns false on parse error, missing format sentinel, or non-object.
//
// File schema:
//   { "format":         "opentodo-settings-snapshot",
//     "format_version": 1,
//     "exported_at":    "<ISO8601 UTC>",
//     "settings":       { "theme": "...", "language": "...", ... } }
//
// Notes on documented semantics that drive these tests:
//   - importSettings REPLACES m_settings wholesale (setSettings does
//     m_settings = settings;). It does not merge — anything not present
//     in the import payload is dropped.
//   - exportSettings writes EXACTLY whatever's in m_settings; on a fresh
//     DataStore that's an empty {}. The file is still valid (sentinel +
//     empty settings object).
//   - The format sentinel is mandatory; importing any JSON without it
//     (even structurally valid {settings: {...}}) returns false and
//     leaves the store unchanged.
// =============================================================================

class TestSettingsExportImport : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmpDir = nullptr;

    QString tmpPath() const {
        return m_tmpDir ? m_tmpDir->path() : QString();
    }

    // Read an exported file back into a QJsonObject for direct inspection.
    QJsonObject readExported(const QString &path) const
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QByteArray data = f.readAll();
        f.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return {};
        return doc.object();
    }

    // Write our own JSON file at `path` for the negative-path tests
    // (custom format sentinel, missing fields, etc.).
    bool writeRaw(const QString &path, const QJsonObject &root) const
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
        return true;
    }

private slots:

    // ── Test fixture lifecycle ───────────────────────────────────────────────

    void initTestCase()
    {
        // Unique org/app for parallel safety (ctest -j).
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_SettingsExportImport"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_settings_export_import"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    void init()
    {
        m_tmpDir = new QTemporaryDir;
        QVERIFY(m_tmpDir->isValid());
        QSettings s; s.clear(); s.sync();
    }

    void cleanup()
    {
        delete m_tmpDir;
        m_tmpDir = nullptr;
        QSettings s; s.clear(); s.sync();
    }

    // =====================================================================
    // 1) Full round-trip via SettingsViewModel: set 5 representative
    //    settings, export, then import the same JSON into a *fresh*
    //    DataStore and verify each value is restored.
    //
    //    Covers a string, two ints, and one bool — the canonical mix of
    //    QVariant types the app stores.
    // =====================================================================
    void roundtrip_five_settings_via_viewmodel()
    {
        // ---- producer side ----
        DataStore prodStore;
        SettingsViewModel prodVm(&prodStore);

        prodVm.setTheme(QStringLiteral("kale"));
        prodVm.setLanguage(QStringLiteral("ru"));
        prodVm.setDailyGoal(7);
        prodVm.setSyncInterval(180);
        prodVm.setDailyReviewEnabled(false);   // explicit false, not the default

        const QString outPath = prodStore.exportSettings(tmpPath());
        QVERIFY2(!outPath.isEmpty(), "exportSettings returned empty path");
        QVERIFY(QFile::exists(outPath));

        // Inspect the file directly to confirm shape & values.
        const QJsonObject root = readExported(outPath);
        QCOMPARE(root.value(QStringLiteral("format")).toString(),
                 QStringLiteral("opentodo-settings-snapshot"));
        QVERIFY(root.value(QStringLiteral("format_version")).toInt() >= 1);
        QVERIFY(root.contains(QStringLiteral("exported_at")));

        const QJsonObject inSettings =
            root.value(QStringLiteral("settings")).toObject();
        QCOMPARE(inSettings.value(QString::fromLatin1(SettingsKeys::Theme))
                     .toString(),                       QStringLiteral("kale"));
        QCOMPARE(inSettings.value(QString::fromLatin1(SettingsKeys::Language))
                     .toString(),                       QStringLiteral("ru"));
        QCOMPARE(inSettings.value(QString::fromLatin1(SettingsKeys::DailyGoal))
                     .toInt(),                          7);
        QCOMPARE(inSettings.value(QString::fromLatin1(SettingsKeys::SyncInterval))
                     .toInt(),                          180);
        QCOMPARE(inSettings.value(QStringLiteral("daily_review_enabled"))
                     .toBool(),                         false);

        // ---- consumer side: a brand-new DataStore ----
        DataStore consStore;
        SettingsViewModel consVm(&consStore);

        // Sanity: the consumer starts at documented defaults
        // (theme→"system", language→"", daily_review_enabled→true default).
        QCOMPARE(consVm.theme(),              QStringLiteral("system"));
        QCOMPARE(consVm.language(),           QString());
        QCOMPARE(consVm.dailyGoal(),          0);
        QCOMPARE(consVm.syncInterval(),       0);
        QCOMPARE(consVm.dailyReviewEnabled(), true);

        const bool ok = consStore.importSettings(outPath);
        QVERIFY2(ok, "importSettings returned false");

        // Every restored value must match what we exported.
        QCOMPARE(consVm.theme(),              QStringLiteral("kale"));
        QCOMPARE(consVm.language(),           QStringLiteral("ru"));
        QCOMPARE(consVm.dailyGoal(),          7);
        QCOMPARE(consVm.syncInterval(),       180);
        QCOMPARE(consVm.dailyReviewEnabled(), false);
    }

    // =====================================================================
    // 2) Empty store → exportSettings still produces a structurally valid
    //    file with the format sentinel and an empty (but present) settings
    //    object. This is intentional behaviour: a fresh install can still
    //    snapshot its (empty) settings as a baseline backup.
    // =====================================================================
    void empty_store_export_produces_valid_file()
    {
        DataStore store;
        const QString outPath = store.exportSettings(tmpPath());
        QVERIFY(!outPath.isEmpty());
        QVERIFY(QFile::exists(outPath));

        const QJsonObject root = readExported(outPath);
        QVERIFY(!root.isEmpty());
        QCOMPARE(root.value(QStringLiteral("format")).toString(),
                 QStringLiteral("opentodo-settings-snapshot"));
        QVERIFY(root.contains(QStringLiteral("settings")));
        // The settings object exists and is empty (no defaults are inserted
        // by the export path — it serialises whatever's in m_settings).
        QVERIFY(root.value(QStringLiteral("settings")).isObject());
        QCOMPARE(root.value(QStringLiteral("settings")).toObject().size(), 0);
    }

    // =====================================================================
    // 3) Importing a file whose `settings` payload is empty {} replaces
    //    the existing settings with empty — no crash, all keys gone.
    //
    //    This documents the wholesale-replace semantics of setSettings:
    //    the import is NOT a merge.
    // =====================================================================
    void empty_settings_payload_clears_existing()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        vm.setTheme(QStringLiteral("dark"));
        vm.setLanguage(QStringLiteral("ru"));
        QCOMPARE(vm.theme(), QStringLiteral("dark"));

        QJsonObject root;
        root.insert(QStringLiteral("format"),
                    QStringLiteral("opentodo-settings-snapshot"));
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), QJsonObject{});

        const QString path = tmpPath() + QStringLiteral("/empty_payload.json");
        QVERIFY(writeRaw(path, root));

        QSignalSpy spy(&store, &DataStore::settingsChanged);
        const bool ok = store.importSettings(path);
        QVERIFY(ok);
        QCOMPARE(spy.count(), 1);

        // Wholesale replace ⇒ formerly-set keys are gone.  theme() falls
        // back to its missing-key default ("system"); language() is empty.
        QCOMPARE(vm.theme(),    QStringLiteral("system"));
        QCOMPARE(vm.language(), QString());
    }

    // =====================================================================
    // 4) Partial import — only a subset of keys present.  After import,
    //    those 3 keys exist in the store; everything else is absent
    //    (because setSettings replaces wholesale — it does not merge into
    //    existing data).  Verifies the documented behaviour explicitly so
    //    a future regression to "merge" semantics is caught.
    // =====================================================================
    void partial_import_replaces_wholesale_other_keys_dropped()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // Pre-populate with five keys.
        vm.setTheme(QStringLiteral("dark"));
        vm.setLanguage(QStringLiteral("en"));
        vm.setDailyGoal(5);
        vm.setSyncInterval(300);
        vm.setVacationMode(true);

        // Build a snapshot containing only 3 keys (theme, language, dailyGoal).
        QJsonObject settings;
        settings.insert(QString::fromLatin1(SettingsKeys::Theme),
                        QStringLiteral("graphite"));
        settings.insert(QString::fromLatin1(SettingsKeys::Language),
                        QStringLiteral("ru"));
        settings.insert(QString::fromLatin1(SettingsKeys::DailyGoal), 12);

        QJsonObject root;
        root.insert(QStringLiteral("format"),
                    QStringLiteral("opentodo-settings-snapshot"));
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), settings);

        const QString path = tmpPath() + QStringLiteral("/partial.json");
        QVERIFY(writeRaw(path, root));

        QVERIFY(store.importSettings(path));

        // The 3 supplied keys are restored.
        QCOMPARE(vm.theme(),     QStringLiteral("graphite"));
        QCOMPARE(vm.language(),  QStringLiteral("ru"));
        QCOMPARE(vm.dailyGoal(), 12);

        // Keys not in the import payload are gone (wholesale replace).
        // syncInterval defaults back to 0; vacationMode back to false.
        QCOMPARE(vm.syncInterval(), 0);
        QCOMPARE(vm.vacationMode(), false);

        // The underlying QJsonObject contains exactly the 3 keys we sent.
        QCOMPARE(store.settings().size(), 3);
        QVERIFY(store.settings().contains(
                    QString::fromLatin1(SettingsKeys::Theme)));
        QVERIFY(!store.settings().contains(
                    QString::fromLatin1(SettingsKeys::SyncInterval)));
    }

    // =====================================================================
    // 5) Import with extra/unknown keys not in the current schema — these
    //    must be accepted silently (stored in m_settings, retrievable via
    //    setting()) without affecting any typed getter or crashing.
    //
    //    Matches the "unknown key" behaviour of updateSetting() (pass-through).
    // =====================================================================
    void import_with_extra_unknown_keys_ignored_gracefully()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        QJsonObject settings;
        settings.insert(QString::fromLatin1(SettingsKeys::Theme),
                        QStringLiteral("blueberry"));
        // Unknown / future keys.
        settings.insert(QStringLiteral("future_feature_x"),
                        QStringLiteral("on"));
        settings.insert(QStringLiteral("experimental_threshold"), 42);
        settings.insert(QStringLiteral("nested_extra"),
                        QJsonObject{{QStringLiteral("k"), QStringLiteral("v")}});

        QJsonObject root;
        root.insert(QStringLiteral("format"),
                    QStringLiteral("opentodo-settings-snapshot"));
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), settings);

        const QString path = tmpPath() + QStringLiteral("/with_extras.json");
        QVERIFY(writeRaw(path, root));

        QVERIFY(store.importSettings(path));

        // Known typed getter sees the value.
        QCOMPARE(vm.theme(), QStringLiteral("blueberry"));

        // Unknown keys are stored verbatim, retrievable via setting().
        QCOMPARE(store.setting(QStringLiteral("future_feature_x")).toString(),
                 QStringLiteral("on"));
        QCOMPARE(store.setting(QStringLiteral("experimental_threshold")).toInt(),
                 42);

        // No typed-getter side-effects.
        QCOMPARE(vm.language(), QString());
        QCOMPARE(vm.dailyGoal(), 0);
    }

    // =====================================================================
    // 6) Negative path: importing a file that's missing the format
    //    sentinel is rejected — even when the inner `settings` object is
    //    structurally valid.  Existing settings remain untouched.
    // =====================================================================
    void import_without_format_sentinel_rejected_state_unchanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        vm.setTheme(QStringLiteral("kale"));
        vm.setSyncInterval(450);

        // Build a JSON file that LOOKS like an export but lacks the sentinel.
        QJsonObject settings;
        settings.insert(QString::fromLatin1(SettingsKeys::Theme),
                        QStringLiteral("dark"));
        QJsonObject root;
        // No "format" key on purpose.
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), settings);

        const QString path = tmpPath() + QStringLiteral("/no_sentinel.json");
        QVERIFY(writeRaw(path, root));

        QSignalSpy spy(&store, &DataStore::settingsChanged);
        const bool ok = store.importSettings(path);
        QVERIFY2(!ok, "importSettings should reject missing format sentinel");
        QCOMPARE(spy.count(), 0);

        // Existing values intact.
        QCOMPARE(vm.theme(),        QStringLiteral("kale"));
        QCOMPARE(vm.syncInterval(), 450);

        // Likewise: a wrong sentinel value is rejected.
        QJsonObject wrongRoot = root;
        wrongRoot.insert(QStringLiteral("format"),
                         QStringLiteral("not-our-format"));
        const QString path2 = tmpPath() + QStringLiteral("/wrong_sentinel.json");
        QVERIFY(writeRaw(path2, wrongRoot));
        QVERIFY(!store.importSettings(path2));
        QCOMPARE(vm.theme(),        QStringLiteral("kale"));
        QCOMPARE(vm.syncInterval(), 450);
    }

    // =====================================================================
    // 7) Negative path: importSettings on a non-existent file or a file
    //    with malformed JSON returns false and leaves the store unchanged.
    // =====================================================================
    void import_missing_or_malformed_returns_false()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        vm.setLanguage(QStringLiteral("en"));

        // (a) non-existent path.
        QVERIFY(!store.importSettings(tmpPath()
                                      + QStringLiteral("/does_not_exist.json")));

        // (b) malformed JSON.
        const QString badPath = tmpPath() + QStringLiteral("/garbage.json");
        QFile f(badPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("{ this is not json :: at all");
        f.close();
        QVERIFY(!store.importSettings(badPath));

        // (c) JSON that parses but is an array, not an object.
        const QString arrPath = tmpPath() + QStringLiteral("/an_array.json");
        QFile fa(arrPath);
        QVERIFY(fa.open(QIODevice::WriteOnly | QIODevice::Text));
        fa.write("[1, 2, 3]");
        fa.close();
        QVERIFY(!store.importSettings(arrPath));

        // State untouched after all three failed imports.
        QCOMPARE(vm.language(), QStringLiteral("en"));
    }

    // =====================================================================
    // 8) Persistence: import → save state.json via FileManager → spin up
    //    a brand-new DataStore + FileManager pointing at the same path,
    //    load(), and verify every imported setting is still readable.
    //
    //    This proves the round-trip survives a serialise/deserialise cycle
    //    through DataStore::toJson/loadFromJson, which is what would happen
    //    on app restart after the user imports a backup.
    // =====================================================================
    void imported_settings_persist_through_filemanager()
    {
        // Build a snapshot file we will import.
        QJsonObject settings;
        settings.insert(QString::fromLatin1(SettingsKeys::Theme),
                        QStringLiteral("lavender"));
        settings.insert(QString::fromLatin1(SettingsKeys::Language),
                        QStringLiteral("ru"));
        settings.insert(QString::fromLatin1(SettingsKeys::DailyGoal), 9);
        settings.insert(QString::fromLatin1(SettingsKeys::SyncInterval), 240);
        settings.insert(QStringLiteral("daily_review_enabled"), false);

        QJsonObject root;
        root.insert(QStringLiteral("format"),
                    QStringLiteral("opentodo-settings-snapshot"));
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), settings);

        const QString snapPath = tmpPath() + QStringLiteral("/snap.json");
        QVERIFY(writeRaw(snapPath, root));

        // ── Phase 1: import + save state.json ────────────────────────────
        const QString stateFile = QStandardPaths::writableLocation(
                                      QStandardPaths::AppDataLocation)
                                  + QStringLiteral("/OpenTodo/state.json");
        QFile::remove(stateFile);

        {
            DataStore ds;
            FileManager fm(&ds);
            fm.load(); // creates default state with Inbox

            QVERIFY(ds.importSettings(snapPath));

            // Sanity inside the same process before we shut down.
            SettingsViewModel vm(&ds);
            QCOMPARE(vm.theme(),              QStringLiteral("lavender"));
            QCOMPARE(vm.language(),           QStringLiteral("ru"));
            QCOMPARE(vm.dailyGoal(),          9);
            QCOMPARE(vm.syncInterval(),       240);
            QCOMPARE(vm.dailyReviewEnabled(), false);

            // Force the on-disk write — the autosave is debounced 1s.
            fm.saveImmediate();
            QVERIFY(QFile::exists(stateFile));
        }

        // ── Phase 2: brand-new DataStore reads state.json ────────────────
        {
            DataStore ds2;
            FileManager fm2(&ds2);
            fm2.load();

            SettingsViewModel vm2(&ds2);
            QCOMPARE(vm2.theme(),              QStringLiteral("lavender"));
            QCOMPARE(vm2.language(),           QStringLiteral("ru"));
            QCOMPARE(vm2.dailyGoal(),          9);
            QCOMPARE(vm2.syncInterval(),       240);
            QCOMPARE(vm2.dailyReviewEnabled(), false);
        }

        // Cleanup.
        QFile::remove(stateFile);
    }

    // =====================================================================
    // 9) Bonus: importSettings emits dataChanged on success (so the
    //    FileManager's autosave hook gets armed).  This is what makes
    //    "import then app-quit" actually persist without a manual save.
    // =====================================================================
    void successful_import_emits_dataChanged_for_autosave()
    {
        DataStore store;

        QJsonObject settings;
        settings.insert(QString::fromLatin1(SettingsKeys::Theme),
                        QStringLiteral("sunflower"));

        QJsonObject root;
        root.insert(QStringLiteral("format"),
                    QStringLiteral("opentodo-settings-snapshot"));
        root.insert(QStringLiteral("format_version"), 1);
        root.insert(QStringLiteral("settings"), settings);

        const QString path = tmpPath() + QStringLiteral("/sig.json");
        QVERIFY(writeRaw(path, root));

        QSignalSpy dataSpy(&store, &DataStore::dataChanged);
        QSignalSpy settingsSpy(&store, &DataStore::settingsChanged);

        QVERIFY(store.importSettings(path));

        QCOMPARE(settingsSpy.count(), 1);
        QVERIFY(dataSpy.count() >= 1);
    }

    // =====================================================================
    // 10) Two-step round-trip: export → re-import into the SAME store
    //     (after a clearing replace) reproduces the original settings
    //     byte-for-byte.  Tests that exportSettings serialises the full
    //     m_settings object faithfully — no hidden filtering or coercion.
    // =====================================================================
    void export_then_reimport_into_same_store_is_idempotent()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        vm.setTheme(QStringLiteral("raspberry"));
        vm.setLanguage(QStringLiteral("en"));
        vm.setStartPage(QStringLiteral("today"));
        vm.setDailyGoal(11);
        vm.setSyncInterval(120);
        vm.setShowInboxCount(true);

        const QJsonObject before = store.settings();
        QVERIFY(!before.isEmpty());

        const QString outPath = store.exportSettings(tmpPath());
        QVERIFY(!outPath.isEmpty());

        // Wipe the in-memory settings, then re-import the file we just wrote.
        store.setSettings(QJsonObject{});
        QVERIFY(store.settings().isEmpty());

        QVERIFY(store.importSettings(outPath));

        // Every key/value present before is present after.
        const QJsonObject after = store.settings();
        QCOMPARE(after.size(), before.size());
        for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
            QVERIFY2(after.contains(it.key()),
                     qPrintable(QStringLiteral("missing key after re-import: ")
                                + it.key()));
            QCOMPARE(after.value(it.key()), it.value());
        }
    }
};

QTEST_GUILESS_MAIN(TestSettingsExportImport)
#include "test_settings_export_import.moc"
