#include "settingsviewmodel.h"
#include "storage/datastore.h"
#include "utils/settingskeys.h"

#include <QCoreApplication>
#include <QHash>
#include <QProcess>
#include <QSettings>
#include <QTimer>

SettingsViewModel::SettingsViewModel(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &DataStore::settingsChanged, this, &SettingsViewModel::onSettingsChanged);
}

// --- Helpers ---

QVariant SettingsViewModel::setting(const QString &key) const
{
    return m_store->setting(key);
}

void SettingsViewModel::set(const QString &key, const QVariant &value)
{
    // Mark this as a single-key update so onSettingsChanged() (fired by the
    // DataStore::settingsChanged signal) knows not to re-emit all NOTIFYs.
    m_pendingNotifyKey = key;
    m_store->setSetting(key, value);
    emit settingChanged(key);
    // m_pendingNotifyKey is cleared by onSettingsChanged().
}

// --- General ---

QString SettingsViewModel::theme() const {
    const QString t = setting(QLatin1String(SettingsKeys::Theme)).toString();
    // First-run / never-set → follow the OS color scheme.
    if (t.isEmpty())                  return QStringLiteral("system");
    // Legacy "light" was the default in older builds before "system" existed;
    // upgrade it so existing installs pick up the OS color scheme.
    if (t == QStringLiteral("light")) return QStringLiteral("system");
    return t;
}
void SettingsViewModel::setTheme(const QString &theme) { set(QLatin1String(SettingsKeys::Theme), theme); emit themeChanged(); }

int SettingsViewModel::dailyGoal() const { return setting(QLatin1String(SettingsKeys::DailyGoal)).toInt(); }
void SettingsViewModel::setDailyGoal(int goal) { set(QLatin1String(SettingsKeys::DailyGoal), goal); emit dailyGoalChanged(); }

int SettingsViewModel::weeklyGoal() const { return setting(QLatin1String(SettingsKeys::WeeklyGoal)).toInt(); }
void SettingsViewModel::setWeeklyGoal(int goal) { set(QLatin1String(SettingsKeys::WeeklyGoal), goal); emit weeklyGoalChanged(); }

bool SettingsViewModel::vacationMode() const { return setting(QLatin1String(SettingsKeys::VacationMode)).toBool(); }
void SettingsViewModel::setVacationMode(bool enabled) { set(QLatin1String(SettingsKeys::VacationMode), enabled); emit vacationModeChanged(); }

QString SettingsViewModel::startPage() const { return setting(QLatin1String(SettingsKeys::StartPage)).toString(); }
void SettingsViewModel::setStartPage(const QString &page) { set(QLatin1String(SettingsKeys::StartPage), page); emit startPageChanged(); }

int SettingsViewModel::timeFormat() const { return setting(QLatin1String(SettingsKeys::TimeFormat)).toInt(); }
void SettingsViewModel::setTimeFormat(int format) { set(QLatin1String(SettingsKeys::TimeFormat), format); emit timeFormatChanged(); }

QString SettingsViewModel::dateFormat() const { return setting(QLatin1String(SettingsKeys::DateFormat)).toString(); }
void SettingsViewModel::setDateFormat(const QString &format) { set(QLatin1String(SettingsKeys::DateFormat), format); emit dateFormatChanged(); }

int SettingsViewModel::weekStart() const { return setting(QLatin1String(SettingsKeys::WeekStart)).toInt(); }
void SettingsViewModel::setWeekStart(int day) { set(QLatin1String(SettingsKeys::WeekStart), day); emit weekStartChanged(); }

int SettingsViewModel::nextWeekDay() const { return setting(QLatin1String(SettingsKeys::NextWeekDay)).toInt(); }
void SettingsViewModel::setNextWeekDay(int day) { set(QLatin1String(SettingsKeys::NextWeekDay), day); emit nextWeekDayChanged(); }

QString SettingsViewModel::defaultReminderTime() const { return setting(QLatin1String(SettingsKeys::DefaultReminderTime)).toString(); }
void SettingsViewModel::setDefaultReminderTime(const QString &time) { set(QLatin1String(SettingsKeys::DefaultReminderTime), time); emit defaultReminderTimeChanged(); }

int SettingsViewModel::autoReminderOffset() const { return setting(QLatin1String(SettingsKeys::AutoReminderOffset)).toInt(); }
void SettingsViewModel::setAutoReminderOffset(int minutes) { set(QLatin1String(SettingsKeys::AutoReminderOffset), minutes); emit autoReminderOffsetChanged(); }

QString SettingsViewModel::language() const { return setting(QLatin1String(SettingsKeys::Language)).toString(); }
void SettingsViewModel::setLanguage(const QString &lang) {
    set(QLatin1String(SettingsKeys::Language), lang);
    // Also mirror to QSettings so main.cpp can read the language BEFORE
    // DataStore loads on next launch — the QTranslator must be installed
    // before any QML loads, but state.json is parsed later in the boot
    // sequence. Mirroring lets main.cpp::main() do a simple QSettings read
    // at startup and avoids parsing state.json twice.
    {
        QSettings s;
        s.setValue(QLatin1String(SettingsKeys::Language), lang);
    }
    emit languageChanged();
}

// --- Auto-archive / daily review ---

int SettingsViewModel::autoArchiveDays() const {
    return setting(QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays)).toInt();
}
void SettingsViewModel::setAutoArchiveDays(int days) {
    set(QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays), days);
    emit autoArchiveDaysChanged();
}

bool SettingsViewModel::dailyReviewEnabled() const {
    const QVariant v = setting(QStringLiteral("daily_review_enabled"));
    // Default ON: missing key (invalid variant) counts as true.
    return !v.isValid() || v.toBool();
}
void SettingsViewModel::setDailyReviewEnabled(bool enabled) {
    set(QStringLiteral("daily_review_enabled"), enabled);
    emit dailyReviewEnabledChanged();
}

// --- Task row density ---

bool SettingsViewModel::compactDensity() const {
    return setting(QLatin1String(SettingsKeys::TaskDensity)).toString()
           == QStringLiteral("compact");
}
void SettingsViewModel::setCompactDensity(bool compact) {
    set(QLatin1String(SettingsKeys::TaskDensity),
        compact ? QStringLiteral("compact") : QStringLiteral("comfortable"));
    emit compactDensityChanged();
}

// --- Sidebar ---

bool SettingsViewModel::showInboxCount() const { return setting(QLatin1String(SettingsKeys::ShowInboxCount)).toBool(); }
void SettingsViewModel::setShowInboxCount(bool show) { set(QLatin1String(SettingsKeys::ShowInboxCount), show); emit sidebarSettingsChanged(); }

bool SettingsViewModel::showTodayCount() const { return setting(QLatin1String(SettingsKeys::ShowTodayCount)).toBool(); }
void SettingsViewModel::setShowTodayCount(bool show) { set(QLatin1String(SettingsKeys::ShowTodayCount), show); emit sidebarSettingsChanged(); }

bool SettingsViewModel::showUpcomingCount() const { return setting(QLatin1String(SettingsKeys::ShowUpcomingCount)).toBool(); }
void SettingsViewModel::setShowUpcomingCount(bool show) { set(QLatin1String(SettingsKeys::ShowUpcomingCount), show); emit sidebarSettingsChanged(); }

// --- Sync ---

int SettingsViewModel::syncInterval() const { return setting(QLatin1String(SettingsKeys::SyncInterval)).toInt(); }
void SettingsViewModel::setSyncInterval(int seconds) { set(QLatin1String(SettingsKeys::SyncInterval), seconds); emit syncIntervalChanged(); }

// --- Permissions ---

bool SettingsViewModel::notificationPermissionAsked() const {
    return setting(QLatin1String(SettingsKeys::NotificationPermissionAsked)).toBool();
}
void SettingsViewModel::setNotificationPermissionAsked(bool asked) {
    set(QLatin1String(SettingsKeys::NotificationPermissionAsked), asked);
    emit notificationPermissionAskedChanged();
}

// --- Methods ---

namespace {
// QML callers pass camelCase keys ("showInboxCount", "vacationMode", ...);
// the on-disk store uses snake_case ("show_inbox_count", "vacation_mode").
// Without translation, updateSetting() wrote into the camelCase key while
// the getter read from the snake_case key, so toggles silently did nothing.
QString canonicalSettingKey(const QString &key)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("theme"),               QLatin1String(SettingsKeys::Theme)},
        {QStringLiteral("language"),            QLatin1String(SettingsKeys::Language)},
        {QStringLiteral("startPage"),           QLatin1String(SettingsKeys::StartPage)},
        {QStringLiteral("timeFormat"),          QLatin1String(SettingsKeys::TimeFormat)},
        {QStringLiteral("dateFormat"),          QLatin1String(SettingsKeys::DateFormat)},
        {QStringLiteral("weekStart"),           QLatin1String(SettingsKeys::WeekStart)},
        {QStringLiteral("nextWeekDay"),         QLatin1String(SettingsKeys::NextWeekDay)},
        {QStringLiteral("vacationMode"),        QLatin1String(SettingsKeys::VacationMode)},
        {QStringLiteral("defaultReminderTime"), QLatin1String(SettingsKeys::DefaultReminderTime)},
        {QStringLiteral("autoReminderOffset"),  QLatin1String(SettingsKeys::AutoReminderOffset)},
        {QStringLiteral("showInboxCount"),      QLatin1String(SettingsKeys::ShowInboxCount)},
        {QStringLiteral("showTodayCount"),      QLatin1String(SettingsKeys::ShowTodayCount)},
        {QStringLiteral("showUpcomingCount"),   QLatin1String(SettingsKeys::ShowUpcomingCount)},
        {QStringLiteral("syncInterval"),        QLatin1String(SettingsKeys::SyncInterval)},
        {QStringLiteral("dailyGoal"),           QLatin1String(SettingsKeys::DailyGoal)},
        {QStringLiteral("weeklyGoal"),          QLatin1String(SettingsKeys::WeeklyGoal)},
        {QStringLiteral("notificationPermissionAsked"),
                                                QLatin1String(SettingsKeys::NotificationPermissionAsked)},
        {QStringLiteral("autoArchiveDays"),     QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays)},
        {QStringLiteral("dailyReviewEnabled"),  QStringLiteral("daily_review_enabled")},
        {QStringLiteral("compactDensity"),      QLatin1String(SettingsKeys::TaskDensity)},
    };
    return map.value(key, key); // unknown camelCase keys pass through unchanged
}
} // namespace

void SettingsViewModel::updateSetting(const QString &key, const QVariant &value)
{
    // Route through the dedicated setters where one exists, so that the
    // per-property NOTIFY signal (themeChanged, languageChanged, ...) fires.
    // Without this routing the QML-facing updateSetting() path goes through
    // the generic set() helper, which only emits settingChanged(key) — bindings
    // on `settingsViewModel.theme` etc. never re-evaluate, so e.g. theme
    // changes silently failed to apply until app restart.
    const QString canonical = canonicalSettingKey(key);
    if (canonical == QLatin1String(SettingsKeys::Theme))                  { setTheme(value.toString()); return; }
    if (canonical == QLatin1String(SettingsKeys::Language))               { setLanguage(value.toString()); return; }
    if (canonical == QLatin1String(SettingsKeys::StartPage))              { setStartPage(value.toString()); return; }
    if (canonical == QLatin1String(SettingsKeys::TimeFormat))             { setTimeFormat(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::DateFormat))             { setDateFormat(value.toString()); return; }
    if (canonical == QLatin1String(SettingsKeys::WeekStart))              { setWeekStart(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::NextWeekDay))            { setNextWeekDay(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::VacationMode))           { setVacationMode(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::DefaultReminderTime))    { setDefaultReminderTime(value.toString()); return; }
    if (canonical == QLatin1String(SettingsKeys::AutoReminderOffset))     { setAutoReminderOffset(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::ShowInboxCount))         { setShowInboxCount(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::ShowTodayCount))         { setShowTodayCount(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::ShowUpcomingCount))      { setShowUpcomingCount(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::SyncInterval))           { setSyncInterval(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::DailyGoal))              { setDailyGoal(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::WeeklyGoal))             { setWeeklyGoal(value.toInt()); return; }
    if (canonical == QLatin1String(SettingsKeys::NotificationPermissionAsked))
                                                                          { setNotificationPermissionAsked(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::AutoArchiveOldProjectsDays))
                                                                          { setAutoArchiveDays(value.toInt()); return; }
    if (canonical == QStringLiteral("daily_review_enabled"))              { setDailyReviewEnabled(value.toBool()); return; }
    if (canonical == QLatin1String(SettingsKeys::TaskDensity)) {
        // Two valid invocation shapes from QML:
        //   - bool     → updateSetting("compactDensity", true/false)
        //   - string   → updateSetting("compactDensity", "compact"/"comfortable")
        // For a bool, route through the typed setter directly (toBool()).
        // For a string, the typed setter does the string→bool mapping itself,
        // so we compare to the literal "compact" before delegating.
        if (value.metaType().id() == QMetaType::Bool) {
            setCompactDensity(value.toBool());
        } else {
            setCompactDensity(value.toString() == QStringLiteral("compact"));
        }
        return;
    }
    set(canonical, value);
}

QVariant SettingsViewModel::getSetting(const QString &key) const
{
    return setting(canonicalSettingKey(key));
}

void SettingsViewModel::resetDefaults()
{
    setTheme(QStringLiteral("system"));
    setDailyGoal(5);
    setWeeklyGoal(25);
    setVacationMode(false);
    setStartPage(QStringLiteral("today"));
    setTimeFormat(24);
    setDateFormat(QStringLiteral("DD-MM-YYYY"));
    setWeekStart(1);
    setNextWeekDay(1);
    setDefaultReminderTime(QStringLiteral("09:00"));
    setAutoReminderOffset(30);
    setLanguage(QStringLiteral("en"));
    setShowInboxCount(true);
    setShowTodayCount(true);
    setShowUpcomingCount(true);
    setSyncInterval(300);
}

QStringList SettingsViewModel::availableThemes() const
{
    return {
        QStringLiteral("system"),
        QStringLiteral("todoist_light"),
        QStringLiteral("dark"),
        QStringLiteral("kale"),
        QStringLiteral("blueberry"),
        QStringLiteral("lavender"),
        QStringLiteral("raspberry"),
        QStringLiteral("graphite"),
        QStringLiteral("sunflower"),
        QStringLiteral("highcontrast"),
    };
}

QStringList SettingsViewModel::availableLanguages() const
{
    // Order matches the i18n/ .ts files. Russian is the source language;
    // English is the recommended default for non-Russian-speaking users.
    return {
        QStringLiteral("ru"),
        QStringLiteral("en"),
        QStringLiteral("de"),
        QStringLiteral("uk"),
        QStringLiteral("be"),
    };
}

bool SettingsViewModel::canRestartApp() const
{
    // Self-restart is only meaningful on platforms where QProcess::startDetached
    // can spawn another instance of the same binary. Android packages launch
    // through Activity, and WASM lives in a single browser frame — neither
    // supports a clean in-process restart.
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    return false;
#else
    return !QCoreApplication::applicationFilePath().isEmpty();
#endif
}

void SettingsViewModel::restartApp()
{
    if (!canRestartApp())
        return;

    const QString program = QCoreApplication::applicationFilePath();
    const QStringList args = QCoreApplication::arguments().mid(1);

    // Spawn the replacement process detached so it survives our own exit,
    // then quit the current event loop on the next tick to give Qt time to
    // flush pending writes (DataStore auto-save runs via QSaveFile on a
    // 1 s debounce; we depend on settings/language already being persisted
    // by the synchronous QSettings write in setLanguage).
    QProcess::startDetached(program, args);

    // qApp could be null in degenerate test contexts; guard.
    if (QCoreApplication::instance()) {
        QTimer::singleShot(0, QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        });
    }
}

void SettingsViewModel::onSettingsChanged()
{
    // This slot fires when the DataStore emits settingsChanged, which happens
    // for *every* setSetting() call — including the ones from our own setters.
    // The individual setters already emit their specific NOTIFY after calling
    // set(), so we do nothing here to avoid double-firing.  The slot exists to
    // handle the case where settings are replaced wholesale from outside
    // (e.g. sync applied a full settings snapshot); in that case we need to
    // notify all properties.  We detect this by checking whether there is a
    // pending per-key notification: if m_pendingNotifyKey is set the setter
    // handles it; otherwise emit everything.
    if (!m_pendingNotifyKey.isEmpty()) {
        // Single-key update: already handled by the setter — nothing to do.
        m_pendingNotifyKey.clear();
        return;
    }

    // Bulk update (e.g. from sync): fire every NOTIFY once.
    emit themeChanged();
    emit dailyGoalChanged();
    emit weeklyGoalChanged();
    emit vacationModeChanged();
    emit startPageChanged();
    emit timeFormatChanged();
    emit dateFormatChanged();
    emit weekStartChanged();
    emit nextWeekDayChanged();
    emit defaultReminderTimeChanged();
    emit autoReminderOffsetChanged();
    emit languageChanged();
    emit sidebarSettingsChanged();
    emit autoArchiveDaysChanged();
    emit dailyReviewEnabledChanged();
    emit compactDensityChanged();
    emit syncIntervalChanged();
    emit notificationPermissionAskedChanged();
}
