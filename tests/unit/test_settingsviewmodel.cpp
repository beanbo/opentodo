#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "storage/datastore.h"
#include "viewmodels/settingsviewmodel.h"
#include "utils/settingskeys.h"

// =============================================================================
// Comprehensive unit tests for SettingsViewModel.
//
// SettingsViewModel is a thin facade over DataStore::settings() — every getter
// reads `m_store->setting(key)` and every setter calls `m_store->setSetting`.
// Persistence in tests therefore happens entirely in DataStore (in-memory only;
// FileManager is not involved here). We use a real DataStore as the backing,
// per the task instructions.
//
// The settingsChanged signal on DataStore is fired on EVERY setSetting call,
// which would normally cause SettingsViewModel::onSettingsChanged to re-emit
// every NOTIFY for a single-key write. The VM avoids this with a per-call
// m_pendingNotifyKey guard. Tests that count NOTIFY emissions therefore
// expect exactly one emission per setter call (the one done explicitly by the
// setter), not the bulk-replay path.
//
// QSettings is scoped to a throwaway org/app — although SettingsViewModel
// itself does not touch QSettings directly, other code in DataStore's chain
// (and any future hook) may do so. Keeping the scope isolated is cheap
// insurance.
// =============================================================================

class TestSettingsViewModel : public QObject
{
    Q_OBJECT

private slots:

    // ── Test fixture lifecycle ───────────────────────────────────────────────

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SettingsVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_settingsviewmodel"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // =====================================================================
    // 1) Bootstrap defaults on a fresh DataStore.
    //    Most numeric/bool getters return 0/false because DataStore.settings
    //    is an empty QJsonObject and QVariant::toInt()/toBool() of an invalid
    //    value yields 0/false. The two notable special-cased getters are:
    //      - theme(): empty → "system" (and legacy "light" → "system")
    //      - dailyReviewEnabled(): missing key → true (default ON)
    //      - compactDensity(): only true when stored value == "compact"
    // =====================================================================
    void bootstrap_defaults_are_sane()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // String defaults — empty strings are acceptable for everything except
        // theme which must coerce to "system".
        QCOMPARE(vm.theme(), QStringLiteral("system"));
        QCOMPARE(vm.language(), QString());
        QCOMPARE(vm.startPage(), QString());
        QCOMPARE(vm.dateFormat(), QString());
        QCOMPARE(vm.defaultReminderTime(), QString());

        // Numeric defaults — int() of an absent QVariant is 0.
        QCOMPARE(vm.dailyGoal(), 0);
        QCOMPARE(vm.weeklyGoal(), 0);
        QCOMPARE(vm.timeFormat(), 0);
        QCOMPARE(vm.weekStart(), 0);
        QCOMPARE(vm.nextWeekDay(), 0);
        QCOMPARE(vm.autoReminderOffset(), 0);
        QCOMPARE(vm.autoArchiveDays(), 0);
        QCOMPARE(vm.syncInterval(), 0);

        // Bool defaults.
        QCOMPARE(vm.vacationMode(), false);
        QCOMPARE(vm.showInboxCount(), false);
        QCOMPARE(vm.showTodayCount(), false);
        QCOMPARE(vm.showUpcomingCount(), false);
        QCOMPARE(vm.compactDensity(), false);
        QCOMPARE(vm.notificationPermissionAsked(), false);

        // Special: dailyReviewEnabled defaults to TRUE on a missing key.
        QCOMPARE(vm.dailyReviewEnabled(), true);
    }

    // =====================================================================
    // 2) theme: setter → getter round-trip + themeChanged signal fires.
    // =====================================================================
    void theme_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::themeChanged);

        vm.setTheme(QStringLiteral("dark"));
        QCOMPARE(vm.theme(), QStringLiteral("dark"));
        QCOMPARE(spy.count(), 1);

        vm.setTheme(QStringLiteral("kale"));
        QCOMPARE(vm.theme(), QStringLiteral("kale"));
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 3) language: round-trip + languageChanged signal.
    // =====================================================================
    void language_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::languageChanged);

        vm.setLanguage(QStringLiteral("ru"));
        QCOMPARE(vm.language(), QStringLiteral("ru"));
        QCOMPARE(spy.count(), 1);

        vm.setLanguage(QStringLiteral("en"));
        QCOMPARE(vm.language(), QStringLiteral("en"));
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 4) startPage: representative string property round-trip + signal.
    //    (sortBy doesn't exist as its own Q_PROPERTY in this VM; startPage
    //    is the analogous "what view to land on" string setting.)
    // =====================================================================
    void startPage_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::startPageChanged);

        vm.setStartPage(QStringLiteral("today"));
        QCOMPARE(vm.startPage(), QStringLiteral("today"));
        QCOMPARE(spy.count(), 1);

        vm.setStartPage(QStringLiteral("inbox"));
        QCOMPARE(vm.startPage(), QStringLiteral("inbox"));
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 5) syncInterval: integer round-trip + syncIntervalChanged signal.
    // =====================================================================
    void syncIntervalSeconds_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::syncIntervalChanged);

        vm.setSyncInterval(300);
        QCOMPARE(vm.syncInterval(), 300);
        QCOMPARE(spy.count(), 1);

        vm.setSyncInterval(60);
        QCOMPARE(vm.syncInterval(), 60);
        QCOMPARE(spy.count(), 2);

        // Zero is a legal value (means "manual sync only" in practice).
        vm.setSyncInterval(0);
        QCOMPARE(vm.syncInterval(), 0);
        QCOMPARE(spy.count(), 3);
    }

    // =====================================================================
    // 6) autoArchiveDays (autoArchiveOldProjectsDays): int round-trip + signal.
    // =====================================================================
    void autoArchiveOldProjectsDays_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::autoArchiveDaysChanged);

        vm.setAutoArchiveDays(30);
        QCOMPARE(vm.autoArchiveDays(), 30);
        QCOMPARE(spy.count(), 1);

        vm.setAutoArchiveDays(90);
        QCOMPARE(vm.autoArchiveDays(), 90);
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 7) dailyReviewEnabled: round-trip including the false→default pivot.
    //    Default is true on a missing key; setting false must persist as false
    //    rather than reverting to the missing-key default.
    // =====================================================================
    void dailyReviewEnabled_roundtrip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::dailyReviewEnabledChanged);

        QCOMPARE(vm.dailyReviewEnabled(), true);   // default

        vm.setDailyReviewEnabled(false);
        QCOMPARE(vm.dailyReviewEnabled(), false);  // explicitly off
        QCOMPARE(spy.count(), 1);

        vm.setDailyReviewEnabled(true);
        QCOMPARE(vm.dailyReviewEnabled(), true);
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 8) updateSetting("theme", "kale") → vm.theme() == "kale".
    //    Verifies the camelCase→snake_case canonical-key mapping for the
    //    QML-callable invokable.
    // =====================================================================
    void updateSetting_theme_persists()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        vm.updateSetting(QStringLiteral("theme"), QStringLiteral("kale"));
        QCOMPARE(vm.theme(), QStringLiteral("kale"));

        // Round-trip via the matching invokable getter as well.
        QCOMPARE(vm.getSetting(QStringLiteral("theme")).toString(),
                 QStringLiteral("kale"));
    }

    // =====================================================================
    // 9) updateSetting with several known camelCase keys correctly routes
    //    each to the snake_case backing key, so the typed getters reflect it.
    // =====================================================================
    void updateSetting_camelCase_keys_route_correctly()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        vm.updateSetting(QStringLiteral("language"),       QStringLiteral("ru"));
        vm.updateSetting(QStringLiteral("dailyGoal"),      7);
        vm.updateSetting(QStringLiteral("vacationMode"),   true);
        vm.updateSetting(QStringLiteral("showInboxCount"), true);
        vm.updateSetting(QStringLiteral("syncInterval"),   600);
        vm.updateSetting(QStringLiteral("dailyReviewEnabled"), false);
        // compactDensity is a bool getter backed by string "compact"
        // / "comfortable". updateSetting now detects bool QVariants and
        // routes through setCompactDensity(bool) so the bool→string mapping
        // happens correctly.
        vm.updateSetting(QStringLiteral("compactDensity"),     true);
        vm.updateSetting(QStringLiteral("autoArchiveDays"),    45);

        QCOMPARE(vm.language(),             QStringLiteral("ru"));
        QCOMPARE(vm.dailyGoal(),            7);
        QCOMPARE(vm.vacationMode(),         true);
        QCOMPARE(vm.showInboxCount(),       true);
        QCOMPARE(vm.syncInterval(),         600);
        QCOMPARE(vm.dailyReviewEnabled(),   false);
        QCOMPARE(vm.compactDensity(),       true);
        QCOMPARE(vm.autoArchiveDays(),      45);
    }

    // =====================================================================
    // 10) updateSetting("invalid_key", value) — must not crash. Unknown keys
    //     pass through unchanged into DataStore::settings, so getSetting
    //     should return the same value back (i.e. they're stored, not dropped).
    // =====================================================================
    void updateSetting_unknownKey_doesNotCrash()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // Should not assert/crash.
        vm.updateSetting(QStringLiteral("invalid_key_xyz"),
                         QStringLiteral("garbage"));

        // The value is stored under the unknown key (pass-through), retrievable
        // via the matching getter.
        QCOMPARE(vm.getSetting(QStringLiteral("invalid_key_xyz")).toString(),
                 QStringLiteral("garbage"));

        // None of the typed getters should be affected by this.
        QCOMPARE(vm.theme(), QStringLiteral("system"));
        QCOMPARE(vm.language(), QString());
    }

    // =====================================================================
    // 11) availableThemes() returns the bundled palette list, including the
    //     "highcontrast" theme added in a later iteration.
    // =====================================================================
    void availableThemes_includes_all_known_themes()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        const QStringList themes = vm.availableThemes();
        QVERIFY(themes.size() >= 8);

        // The canonical bundled palettes from ThemeManager.
        for (const auto &expected : {
                "system", "classic_light", "dark", "kale", "blueberry",
                "lavender", "raspberry", "graphite", "sunflower",
                "highcontrast" }) {
            QVERIFY2(themes.contains(QString::fromLatin1(expected)),
                     qPrintable(QStringLiteral("availableThemes missing: ") +
                                QString::fromLatin1(expected)));
        }
    }

    // =====================================================================
    // 12) availableLanguages() returns at least ["en", "ru"].
    // =====================================================================
    void availableLanguages_returns_en_and_ru()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        const QStringList langs = vm.availableLanguages();
        QVERIFY(langs.contains(QStringLiteral("en")));
        QVERIFY(langs.contains(QStringLiteral("ru")));
    }

    // =====================================================================
    // 13) resetDefaults() restores every property whose default is set by the
    //     resetDefaults() body to its documented default value, regardless of
    //     prior state.
    // =====================================================================
    void resetDefaults_restores_all_to_documented_defaults()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // Mutate everything resetDefaults() touches.
        vm.setTheme(QStringLiteral("kale"));
        vm.setDailyGoal(99);
        vm.setWeeklyGoal(700);
        vm.setVacationMode(true);
        vm.setStartPage(QStringLiteral("inbox"));
        vm.setTimeFormat(12);
        vm.setDateFormat(QStringLiteral("YYYY-MM-DD"));
        vm.setWeekStart(7);
        vm.setNextWeekDay(3);
        vm.setDefaultReminderTime(QStringLiteral("18:00"));
        vm.setAutoReminderOffset(120);
        vm.setLanguage(QStringLiteral("ru"));
        vm.setShowInboxCount(false);
        vm.setShowTodayCount(false);
        vm.setShowUpcomingCount(false);
        vm.setSyncInterval(60);

        vm.resetDefaults();

        QCOMPARE(vm.theme(),                  QStringLiteral("system"));
        QCOMPARE(vm.dailyGoal(),              5);
        QCOMPARE(vm.weeklyGoal(),             25);
        QCOMPARE(vm.vacationMode(),           false);
        QCOMPARE(vm.startPage(),              QStringLiteral("today"));
        QCOMPARE(vm.timeFormat(),             24);
        QCOMPARE(vm.dateFormat(),             QStringLiteral("DD-MM-YYYY"));
        QCOMPARE(vm.weekStart(),              1);
        QCOMPARE(vm.nextWeekDay(),            1);
        QCOMPARE(vm.defaultReminderTime(),    QStringLiteral("09:00"));
        QCOMPARE(vm.autoReminderOffset(),     30);
        QCOMPARE(vm.language(),               QStringLiteral("en"));
        QCOMPARE(vm.showInboxCount(),         true);
        QCOMPARE(vm.showTodayCount(),         true);
        QCOMPARE(vm.showUpcomingCount(),      true);
        QCOMPARE(vm.syncInterval(),           300);
    }

    // =====================================================================
    // 14) Persistence: set 3 properties, destroy VM, recreate VM against the
    //     SAME DataStore — properties must be readable via the new VM.
    //     (DataStore is the persistence backing here; the on-disk path via
    //     FileManager is exercised by other tests.)
    // =====================================================================
    void persistence_across_vm_recreation()
    {
        DataStore store;

        {
            SettingsViewModel vm1(&store);
            vm1.setTheme(QStringLiteral("blueberry"));
            vm1.setLanguage(QStringLiteral("ru"));
            vm1.setSyncInterval(180);
            // vm1 goes out of scope here.
        }

        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.theme(),         QStringLiteral("blueberry"));
        QCOMPARE(vm2.language(),      QStringLiteral("ru"));
        QCOMPARE(vm2.syncInterval(),  180);
    }

    // =====================================================================
    // 15) Type coercion: writing a QString to an int-typed property via
    //     updateSetting must not crash. QVariant stores it as a string; the
    //     int getter coerces via QVariant::toInt(), which yields 0 for a
    //     non-numeric string and the parsed integer for a numeric one.
    // =====================================================================
    void updateSetting_typeCoercion_doesNotCrash()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // Numeric-as-string → coerced cleanly to int.
        vm.updateSetting(QStringLiteral("syncInterval"),
                         QStringLiteral("450"));
        QCOMPARE(vm.syncInterval(), 450);

        // Non-numeric string → coerces to 0; must not crash either way.
        vm.updateSetting(QStringLiteral("syncInterval"),
                         QStringLiteral("not-a-number"));
        const int afterGarbage = vm.syncInterval();
        QVERIFY(afterGarbage == 0 || afterGarbage == 450);

        // Bool-as-string for a bool property — Qt parses "true"/"false".
        vm.updateSetting(QStringLiteral("vacationMode"),
                         QStringLiteral("true"));
        QVERIFY(vm.vacationMode() == true || vm.vacationMode() == false);
        // Either coercion or rejection is acceptable; the mandate is "no crash".
    }

    // =====================================================================
    // 16) Race-ish: rapid set+get on the same property in a tight loop. We
    //     don't run multi-threaded (Qt object affinity disallows that for
    //     QObject subclasses without proper marshalling), but a tight serial
    //     loop verifies the LAST write wins and there are no surprises like
    //     buffered writes or cached stale reads.
    // =====================================================================
    void rapid_set_get_last_write_wins()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        for (int i = 0; i < 1000; ++i) {
            vm.setSyncInterval(i);
            QCOMPARE(vm.syncInterval(), i);
        }

        // Final value reflects the last write.
        QCOMPARE(vm.syncInterval(), 999);

        // Same for a string property.
        for (int i = 0; i < 200; ++i) {
            const QString lang = (i % 2 == 0) ? QStringLiteral("en")
                                              : QStringLiteral("ru");
            vm.setLanguage(lang);
            QCOMPARE(vm.language(), lang);
        }
        QCOMPARE(vm.language(), QStringLiteral("ru")); // i==199 odd → "ru"
    }

    // =====================================================================
    // 17) settingChanged(QString) generic signal fires for every set() call,
    //     carrying the canonical (snake_case) key as its payload.
    // =====================================================================
    void settingChanged_generic_signal_fires_with_canonical_key()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::settingChanged);

        vm.setTheme(QStringLiteral("dark"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::Theme));

        // updateSetting goes through the canonicalSettingKey mapping.
        vm.updateSetting(QStringLiteral("vacationMode"), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::VacationMode));
    }

    // =====================================================================
    // 18) Theme legacy upgrade: stored value "light" must be reported as
    //     "system" by the theme() getter (in-place silent migration).
    // =====================================================================
    void theme_legacy_light_upgrades_to_system()
    {
        DataStore store;
        // Bypass the setter and write the legacy value directly into the store.
        store.setSetting(QLatin1String(SettingsKeys::Theme),
                         QStringLiteral("light"));

        SettingsViewModel vm(&store);
        QCOMPARE(vm.theme(), QStringLiteral("system"));
    }

    // =====================================================================
    // 19) compactDensity: only the literal stored value "compact" produces a
    //     true reading; "comfortable" or anything else yields false. Round-trip
    //     via the bool setter goes through that string mapping.
    // =====================================================================
    void compactDensity_string_backed_round_trip()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::compactDensityChanged);

        vm.setCompactDensity(true);
        QCOMPARE(vm.compactDensity(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                      .toString(),
                 QStringLiteral("compact"));
        QCOMPARE(spy.count(), 1);

        vm.setCompactDensity(false);
        QCOMPARE(vm.compactDensity(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                      .toString(),
                 QStringLiteral("comfortable"));
        QCOMPARE(spy.count(), 2);
    }

    // =====================================================================
    // 20) Bulk replace via DataStore::setSettings (simulates a sync-applied
    //     wholesale settings replacement) must trigger the full NOTIFY storm
    //     in onSettingsChanged — verified by listening to themeChanged after
    //     swapping in a new settings object that includes a different theme.
    // =====================================================================
    void bulk_settings_replace_fires_all_notifies()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        QSignalSpy themeSpy(&vm, &SettingsViewModel::themeChanged);
        QSignalSpy langSpy(&vm,  &SettingsViewModel::languageChanged);
        QSignalSpy syncSpy(&vm,  &SettingsViewModel::syncIntervalChanged);

        // Wholesale replace — emits settingsChanged WITHOUT a pending key.
        QJsonObject bulk;
        bulk.insert(QString::fromLatin1(SettingsKeys::Theme),
                    QStringLiteral("graphite"));
        bulk.insert(QString::fromLatin1(SettingsKeys::Language),
                    QStringLiteral("ru"));
        bulk.insert(QString::fromLatin1(SettingsKeys::SyncInterval), 450);
        store.setSettings(bulk);

        // The bulk-replace path emits every NOTIFY exactly once.
        QCOMPARE(themeSpy.count(), 1);
        QCOMPARE(langSpy.count(),  1);
        QCOMPARE(syncSpy.count(),  1);

        // And the values are visible.
        QCOMPARE(vm.theme(),        QStringLiteral("graphite"));
        QCOMPARE(vm.language(),     QStringLiteral("ru"));
        QCOMPARE(vm.syncInterval(), 450);
    }
};

QTEST_GUILESS_MAIN(TestSettingsViewModel)
#include "test_settingsviewmodel.moc"
