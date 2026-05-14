#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QString>
#include <QVariant>
#include <QJsonObject>

#include "storage/datastore.h"
#include "viewmodels/settingsviewmodel.h"
#include "utils/settingskeys.h"

// =============================================================================
// Live-reactivity tests for SettingsViewModel.
//
// Background — bug fix history:
//   updateSetting("theme", value) used to dispatch through the generic set()
//   helper, which only emits settingChanged(key) — not the typed
//   themeChanged() signal. QML bindings on `settingsViewModel.theme` did not
//   re-evaluate, so theme switches silently failed until app restart. The fix
//   was to route updateSetting() through the dedicated typed setters
//   (setTheme, setLanguage, setTimeFormat, ...) so that the per-property
//   NOTIFY fires on every code path.
//
// This file pins down that contract for EVERY Q_PROPERTY:
//   1. Calling the typed setter fires the NOTIFY exactly once.
//   2. Calling updateSetting(camelCaseKey, value) fires the SAME NOTIFY
//      exactly once (NOT just the generic settingChanged).
//   3. Both paths emit the generic settingChanged(canonicalKey) once,
//      with the snake_case canonical key as payload.
//   4. After either call, vm.<getter>() and DataStore::setting(canonical)
//      both reflect the new value.
//   5. For 5 representative properties, persistence across VM recreation
//      against the same DataStore is verified.
//
// Note on the no-spurious-signal axis: SettingsViewModel does NOT short-
// circuit when the new value equals the old value. Setting the same value
// twice fires the NOTIFY twice. We pin this current behaviour explicitly
// so a future change to add an equality guard is a deliberate decision,
// not an accident.
//
// QSettings org/app is uniquely scoped per binary so this test cannot
// cross-contaminate with sibling tests in the same suite.
// =============================================================================

class TestSettingsViewModelReactivity : public QObject
{
    Q_OBJECT

private slots:

    // ---------------------------------------------------------------- fixture

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_Reactivity"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_settingsviewmodel_reactivity"));
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

    // ============================================================ theme
    // The original bug. Pin both paths fire themeChanged + settingChanged
    // exactly once, and that the canonical key is "theme".

    void theme_typedSetter_firesThemeChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy themeSpy(&vm, &SettingsViewModel::themeChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setTheme(QStringLiteral("kale"));

        QCOMPARE(themeSpy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::Theme));
        QCOMPARE(vm.theme(), QStringLiteral("kale"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::Theme)).toString(),
                 QStringLiteral("kale"));
    }

    void theme_updateSetting_firesThemeChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy themeSpy(&vm, &SettingsViewModel::themeChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.updateSetting(QStringLiteral("theme"), QStringLiteral("dark"));

        QCOMPARE(themeSpy.count(), 1);            // <-- the regression pin
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::Theme));
        QCOMPARE(vm.theme(), QStringLiteral("dark"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::Theme)).toString(),
                 QStringLiteral("dark"));
    }

    void theme_settingSameValueTwice_firesEachTime()
    {
        // Pin current behaviour: no equality short-circuit. Both paths
        // re-emit on identical writes.
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::themeChanged);

        vm.setTheme(QStringLiteral("kale"));
        vm.setTheme(QStringLiteral("kale"));
        QCOMPARE(spy.count(), 2);

        spy.clear();
        vm.updateSetting(QStringLiteral("theme"), QStringLiteral("kale"));
        vm.updateSetting(QStringLiteral("theme"), QStringLiteral("kale"));
        QCOMPARE(spy.count(), 2);
    }

    // ============================================================ language

    void language_bothPathsFireLanguageChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy langSpy(&vm, &SettingsViewModel::languageChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setLanguage(QStringLiteral("ru"));
        QCOMPARE(langSpy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::Language));
        QCOMPARE(vm.language(), QStringLiteral("ru"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::Language)).toString(),
                 QStringLiteral("ru"));

        vm.updateSetting(QStringLiteral("language"), QStringLiteral("en"));
        QCOMPARE(langSpy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::Language));
        QCOMPARE(vm.language(), QStringLiteral("en"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::Language)).toString(),
                 QStringLiteral("en"));
    }

    // ============================================================ timeFormat

    void timeFormat_bothPathsFireTimeFormatChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::timeFormatChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setTimeFormat(24);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::TimeFormat));
        QCOMPARE(vm.timeFormat(), 24);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TimeFormat)).toInt(),
                 24);

        vm.updateSetting(QStringLiteral("timeFormat"), 12);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::TimeFormat));
        QCOMPARE(vm.timeFormat(), 12);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TimeFormat)).toInt(),
                 12);
    }

    // ============================================================ dateFormat

    void dateFormat_bothPathsFireDateFormatChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::dateFormatChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setDateFormat(QStringLiteral("YYYY-MM-DD"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DateFormat));
        QCOMPARE(vm.dateFormat(), QStringLiteral("YYYY-MM-DD"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DateFormat)).toString(),
                 QStringLiteral("YYYY-MM-DD"));

        vm.updateSetting(QStringLiteral("dateFormat"),
                         QStringLiteral("DD-MM-YYYY"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DateFormat));
        QCOMPARE(vm.dateFormat(), QStringLiteral("DD-MM-YYYY"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DateFormat)).toString(),
                 QStringLiteral("DD-MM-YYYY"));
    }

    // ============================================================ dailyGoal

    void dailyGoal_bothPathsFireDailyGoalChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::dailyGoalChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setDailyGoal(7);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DailyGoal));
        QCOMPARE(vm.dailyGoal(), 7);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DailyGoal)).toInt(),
                 7);

        vm.updateSetting(QStringLiteral("dailyGoal"), 12);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DailyGoal));
        QCOMPARE(vm.dailyGoal(), 12);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DailyGoal)).toInt(),
                 12);
    }

    // ============================================================ weeklyGoal

    void weeklyGoal_bothPathsFireWeeklyGoalChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::weeklyGoalChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setWeeklyGoal(35);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::WeeklyGoal));
        QCOMPARE(vm.weeklyGoal(), 35);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::WeeklyGoal)).toInt(),
                 35);

        vm.updateSetting(QStringLiteral("weeklyGoal"), 60);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::WeeklyGoal));
        QCOMPARE(vm.weeklyGoal(), 60);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::WeeklyGoal)).toInt(),
                 60);
    }

    // ============================================================ vacationMode

    void vacationMode_bothPathsFireVacationModeChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::vacationModeChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setVacationMode(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::VacationMode));
        QCOMPARE(vm.vacationMode(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::VacationMode)).toBool(),
                 true);

        vm.updateSetting(QStringLiteral("vacationMode"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::VacationMode));
        QCOMPARE(vm.vacationMode(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::VacationMode)).toBool(),
                 false);
    }

    // ============================================================ startPage

    void startPage_bothPathsFireStartPageChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::startPageChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setStartPage(QStringLiteral("today"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::StartPage));
        QCOMPARE(vm.startPage(), QStringLiteral("today"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::StartPage)).toString(),
                 QStringLiteral("today"));

        vm.updateSetting(QStringLiteral("startPage"), QStringLiteral("inbox"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::StartPage));
        QCOMPARE(vm.startPage(), QStringLiteral("inbox"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::StartPage)).toString(),
                 QStringLiteral("inbox"));
    }

    // ============================================================ weekStart

    void weekStart_bothPathsFireWeekStartChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::weekStartChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setWeekStart(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::WeekStart));
        QCOMPARE(vm.weekStart(), 1);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::WeekStart)).toInt(),
                 1);

        vm.updateSetting(QStringLiteral("weekStart"), 7);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::WeekStart));
        QCOMPARE(vm.weekStart(), 7);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::WeekStart)).toInt(),
                 7);
    }

    // ============================================================ nextWeekDay

    void nextWeekDay_bothPathsFireNextWeekDayChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::nextWeekDayChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setNextWeekDay(2);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::NextWeekDay));
        QCOMPARE(vm.nextWeekDay(), 2);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::NextWeekDay)).toInt(),
                 2);

        vm.updateSetting(QStringLiteral("nextWeekDay"), 5);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::NextWeekDay));
        QCOMPARE(vm.nextWeekDay(), 5);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::NextWeekDay)).toInt(),
                 5);
    }

    // ============================================================ defaultReminderTime

    void defaultReminderTime_bothPathsFireDefaultReminderTimeChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::defaultReminderTimeChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setDefaultReminderTime(QStringLiteral("09:00"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DefaultReminderTime));
        QCOMPARE(vm.defaultReminderTime(), QStringLiteral("09:00"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DefaultReminderTime))
                     .toString(),
                 QStringLiteral("09:00"));

        vm.updateSetting(QStringLiteral("defaultReminderTime"),
                         QStringLiteral("18:30"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::DefaultReminderTime));
        QCOMPARE(vm.defaultReminderTime(), QStringLiteral("18:30"));
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::DefaultReminderTime))
                     .toString(),
                 QStringLiteral("18:30"));
    }

    // ============================================================ autoReminderOffset

    void autoReminderOffset_bothPathsFireAutoReminderOffsetChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::autoReminderOffsetChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setAutoReminderOffset(15);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::AutoReminderOffset));
        QCOMPARE(vm.autoReminderOffset(), 15);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::AutoReminderOffset))
                     .toInt(),
                 15);

        vm.updateSetting(QStringLiteral("autoReminderOffset"), 60);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::AutoReminderOffset));
        QCOMPARE(vm.autoReminderOffset(), 60);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::AutoReminderOffset))
                     .toInt(),
                 60);
    }

    // ============================================================ showInboxCount
    // Sidebar booleans share the sidebarSettingsChanged NOTIFY.

    void showInboxCount_bothPathsFireSidebarSettingsChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::sidebarSettingsChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setShowInboxCount(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowInboxCount));
        QCOMPARE(vm.showInboxCount(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::ShowInboxCount))
                     .toBool(),
                 true);

        vm.updateSetting(QStringLiteral("showInboxCount"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowInboxCount));
        QCOMPARE(vm.showInboxCount(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::ShowInboxCount))
                     .toBool(),
                 false);
    }

    // ============================================================ showTodayCount

    void showTodayCount_bothPathsFireSidebarSettingsChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::sidebarSettingsChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setShowTodayCount(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowTodayCount));
        QCOMPARE(vm.showTodayCount(), true);

        vm.updateSetting(QStringLiteral("showTodayCount"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowTodayCount));
        QCOMPARE(vm.showTodayCount(), false);
    }

    // ============================================================ showUpcomingCount

    void showUpcomingCount_bothPathsFireSidebarSettingsChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::sidebarSettingsChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setShowUpcomingCount(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowUpcomingCount));
        QCOMPARE(vm.showUpcomingCount(), true);

        vm.updateSetting(QStringLiteral("showUpcomingCount"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::ShowUpcomingCount));
        QCOMPARE(vm.showUpcomingCount(), false);
    }

    // ============================================================ syncInterval

    void syncInterval_bothPathsFireSyncIntervalChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::syncIntervalChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setSyncInterval(120);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::SyncInterval));
        QCOMPARE(vm.syncInterval(), 120);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::SyncInterval)).toInt(),
                 120);

        vm.updateSetting(QStringLiteral("syncInterval"), 600);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::SyncInterval));
        QCOMPARE(vm.syncInterval(), 600);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::SyncInterval)).toInt(),
                 600);
    }

    // ============================================================ autoArchiveDays

    void autoArchiveDays_bothPathsFireAutoArchiveDaysChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::autoArchiveDaysChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setAutoArchiveDays(30);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::AutoArchiveOldProjectsDays));
        QCOMPARE(vm.autoArchiveDays(), 30);
        QCOMPARE(store.setting(
                     QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays))
                     .toInt(),
                 30);

        vm.updateSetting(QStringLiteral("autoArchiveDays"), 90);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::AutoArchiveOldProjectsDays));
        QCOMPARE(vm.autoArchiveDays(), 90);
        QCOMPARE(store.setting(
                     QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays))
                     .toInt(),
                 90);
    }

    // ============================================================ dailyReviewEnabled
    // Backed by literal key "daily_review_enabled" (no SettingsKeys constant).

    void dailyReviewEnabled_bothPathsFireDailyReviewEnabledChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::dailyReviewEnabledChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setDailyReviewEnabled(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QStringLiteral("daily_review_enabled"));
        QCOMPARE(vm.dailyReviewEnabled(), false);
        QCOMPARE(store.setting(QStringLiteral("daily_review_enabled")).toBool(),
                 false);

        vm.updateSetting(QStringLiteral("dailyReviewEnabled"), true);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QStringLiteral("daily_review_enabled"));
        QCOMPARE(vm.dailyReviewEnabled(), true);
        QCOMPARE(store.setting(QStringLiteral("daily_review_enabled")).toBool(),
                 true);
    }

    // ============================================================ compactDensity
    // Special: bool typed setter writes the STRING "compact"/"comfortable"
    // into DataStore. updateSetting() routes through setCompactDensity()
    // for either invocation shape:
    //   - vm.setCompactDensity(true) → store has "compact", getter returns true
    //   - vm.updateSetting("compactDensity", true) → bool path: setCompactDensity(true)
    //     is called directly, store gets "compact", getter returns true.
    //   - vm.updateSetting("compactDensity", "compact") → string path: the
    //     literal "compact"/"comfortable" string is compared and mapped.
    // Both invocation shapes are valid and exercised below.

    void compactDensity_typedSetterFiresAndStoresString()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::compactDensityChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setCompactDensity(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(SettingsKeys::TaskDensity));
        QCOMPARE(vm.compactDensity(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("compact"));

        vm.setCompactDensity(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(vm.compactDensity(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("comfortable"));
    }

    void compactDensity_updateSettingWithStringWorksAndFiresChanged()
    {
        // Documented working invocation from QML: pass the literal string.
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::compactDensityChanged);

        vm.updateSetting(QStringLiteral("compactDensity"),
                         QStringLiteral("compact"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm.compactDensity(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("compact"));

        vm.updateSetting(QStringLiteral("compactDensity"),
                         QStringLiteral("comfortable"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(vm.compactDensity(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("comfortable"));
    }

    void compactDensity_updateSettingWithBoolMapsCorrectly()
    {
        // Regression pin: passing a raw bool through updateSetting must route
        // through the typed setter's bool→string mapping, NOT the generic
        // QVariant::toString() path (which would store the literal "true"
        // /"false"). Previously this stored "true" and getter returned false;
        // the dispatch now detects bool QVariants and routes through
        // setCompactDensity(bool) directly.
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm, &SettingsViewModel::compactDensityChanged);

        vm.updateSetting(QStringLiteral("compactDensity"), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm.compactDensity(), true);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("compact"));

        vm.updateSetting(QStringLiteral("compactDensity"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(vm.compactDensity(), false);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::TaskDensity))
                     .toString(),
                 QStringLiteral("comfortable"));
    }

    // ============================================================ notificationPermissionAsked

    void notificationPermissionAsked_bothPathsFireChanged()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy spy(&vm,
                       &SettingsViewModel::notificationPermissionAskedChanged);
        QSignalSpy genericSpy(&vm, &SettingsViewModel::settingChanged);

        vm.setNotificationPermissionAsked(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(
                     SettingsKeys::NotificationPermissionAsked));
        QCOMPARE(vm.notificationPermissionAsked(), true);
        QCOMPARE(store.setting(
                     QLatin1String(SettingsKeys::NotificationPermissionAsked))
                     .toBool(),
                 true);

        vm.updateSetting(QStringLiteral("notificationPermissionAsked"), false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(genericSpy.count(), 1);
        QCOMPARE(genericSpy.takeFirst().at(0).toString(),
                 QString::fromLatin1(
                     SettingsKeys::NotificationPermissionAsked));
        QCOMPARE(vm.notificationPermissionAsked(), false);
        QCOMPARE(store.setting(
                     QLatin1String(SettingsKeys::NotificationPermissionAsked))
                     .toBool(),
                 false);
    }

    // ============================================================ default theme

    void theme_default_isSystemWhenNoKeySet()
    {
        DataStore store;
        SettingsViewModel vm(&store);

        // Bootstrap: nothing in DataStore for "theme".
        QVERIFY(!store.setting(QLatin1String(SettingsKeys::Theme)).isValid()
                || store.setting(QLatin1String(SettingsKeys::Theme)).toString()
                       .isEmpty());
        QCOMPARE(vm.theme(), QStringLiteral("system"));
    }

    // ============================================================ legacy theme upgrade

    void theme_legacyLightUpgradesToSystem()
    {
        DataStore store;
        // Plant the legacy value directly via DataStore (bypass the setter so
        // the migration path runs only on read).
        store.setSetting(QLatin1String(SettingsKeys::Theme),
                         QStringLiteral("light"));

        SettingsViewModel vm(&store);
        QCOMPARE(vm.theme(), QStringLiteral("system"));

        // The store still holds the legacy value — upgrade is read-side only.
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::Theme)).toString(),
                 QStringLiteral("light"));
    }

    // ============================================================ persistence
    // Five representative properties: recreate VM against the same DataStore
    // and confirm values restore correctly. DataStore is the in-process
    // backing; the on-disk path via FileManager is exercised by other tests.

    void persistence_theme_acrossVmRecreation()
    {
        DataStore store;
        {
            SettingsViewModel vm(&store);
            vm.setTheme(QStringLiteral("blueberry"));
        }
        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.theme(), QStringLiteral("blueberry"));
    }

    void persistence_language_acrossVmRecreation()
    {
        DataStore store;
        {
            SettingsViewModel vm(&store);
            vm.setLanguage(QStringLiteral("ru"));
        }
        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.language(), QStringLiteral("ru"));
    }

    void persistence_syncInterval_acrossVmRecreation()
    {
        DataStore store;
        {
            SettingsViewModel vm(&store);
            vm.setSyncInterval(450);
        }
        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.syncInterval(), 450);
    }

    void persistence_dailyGoal_acrossVmRecreation()
    {
        DataStore store;
        {
            SettingsViewModel vm(&store);
            vm.setDailyGoal(11);
        }
        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.dailyGoal(), 11);
    }

    void persistence_dailyReviewEnabled_acrossVmRecreation()
    {
        DataStore store;
        {
            SettingsViewModel vm(&store);
            vm.setDailyReviewEnabled(false);
        }
        SettingsViewModel vm2(&store);
        QCOMPARE(vm2.dailyReviewEnabled(), false);
    }
};

QTEST_GUILESS_MAIN(TestSettingsViewModelReactivity)
#include "test_settingsviewmodel_reactivity.moc"
