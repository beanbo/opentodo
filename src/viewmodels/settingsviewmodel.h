#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QQmlEngine>

class DataStore;

class SettingsViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SettingsViewModel is constructed in C++ and exposed via context property.")

    // General settings
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(int dailyGoal READ dailyGoal WRITE setDailyGoal NOTIFY dailyGoalChanged)
    Q_PROPERTY(int weeklyGoal READ weeklyGoal WRITE setWeeklyGoal NOTIFY weeklyGoalChanged)
    Q_PROPERTY(bool vacationMode READ vacationMode WRITE setVacationMode NOTIFY vacationModeChanged)
    Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY startPageChanged)
    Q_PROPERTY(int timeFormat READ timeFormat WRITE setTimeFormat NOTIFY timeFormatChanged)
    Q_PROPERTY(QString dateFormat READ dateFormat WRITE setDateFormat NOTIFY dateFormatChanged)
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged)
    Q_PROPERTY(int nextWeekDay READ nextWeekDay WRITE setNextWeekDay NOTIFY nextWeekDayChanged)
    Q_PROPERTY(QString defaultReminderTime READ defaultReminderTime WRITE setDefaultReminderTime NOTIFY defaultReminderTimeChanged)
    Q_PROPERTY(int autoReminderOffset READ autoReminderOffset WRITE setAutoReminderOffset NOTIFY autoReminderOffsetChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    // Sidebar settings
    Q_PROPERTY(bool showInboxCount READ showInboxCount WRITE setShowInboxCount NOTIFY sidebarSettingsChanged)
    Q_PROPERTY(bool showTodayCount READ showTodayCount WRITE setShowTodayCount NOTIFY sidebarSettingsChanged)
    Q_PROPERTY(bool showUpcomingCount READ showUpcomingCount WRITE setShowUpcomingCount NOTIFY sidebarSettingsChanged)

    // Auto-archive / daily review
    Q_PROPERTY(int autoArchiveDays READ autoArchiveDays WRITE setAutoArchiveDays NOTIFY autoArchiveDaysChanged)
    Q_PROPERTY(bool dailyReviewEnabled READ dailyReviewEnabled WRITE setDailyReviewEnabled NOTIFY dailyReviewEnabledChanged)

    // Task row density
    Q_PROPERTY(bool compactDensity READ compactDensity WRITE setCompactDensity NOTIFY compactDensityChanged)

    // Q2.26-B: search accent sensitivity. Default false ⇒ "café" finds "cafe".
    Q_PROPERTY(bool searchAccentSensitive READ searchAccentSensitive
               WRITE setSearchAccentSensitive NOTIFY searchAccentSensitiveChanged)

    // System tray icon (desktop only)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)

    // Custom accent color override (mirrors ThemeManager.customAccentHex). When
    // an empty string is stored the active theme palette's native accent wins;
    // otherwise this "#RRGGBB" string overrides it across the whole UI and is
    // also surfaced as a QColor (accentColor) so C++ services (e.g. the tray
    // icon badge) can consume it without going through QML.
    Q_PROPERTY(QString customAccentHex READ customAccentHex WRITE setCustomAccentHex NOTIFY accentColorChanged)
    Q_PROPERTY(QColor  accentColor     READ accentColor     NOTIFY accentColorChanged)

    // Sync settings
    Q_PROPERTY(int syncInterval READ syncInterval WRITE setSyncInterval NOTIFY syncIntervalChanged)

    // Permissions / first-run gates
    Q_PROPERTY(bool notificationPermissionAsked READ notificationPermissionAsked
               WRITE setNotificationPermissionAsked NOTIFY notificationPermissionAskedChanged)

    // Q3.10: storage-cap settings (backup retention + vacuum threshold).
    // Defaults applied on first read when the underlying setting is unset.
    Q_PROPERTY(int backupRetentionCount READ backupRetentionCount
               WRITE setBackupRetentionCount NOTIFY backupRetentionCountChanged)
    Q_PROPERTY(int backupRetentionDays  READ backupRetentionDays
               WRITE setBackupRetentionDays  NOTIFY backupRetentionDaysChanged)
    Q_PROPERTY(qint64 vacuumThresholdBytes READ vacuumThresholdBytes
               WRITE setVacuumThresholdBytes NOTIFY vacuumThresholdBytesChanged)

public:
    explicit SettingsViewModel(DataStore *store, QObject *parent = nullptr);

    // General
    QString theme() const;
    void setTheme(const QString &theme);

    int dailyGoal() const;
    void setDailyGoal(int goal);

    int weeklyGoal() const;
    void setWeeklyGoal(int goal);

    bool vacationMode() const;
    void setVacationMode(bool enabled);

    QString startPage() const;
    void setStartPage(const QString &page);

    int timeFormat() const;
    void setTimeFormat(int format);

    QString dateFormat() const;
    void setDateFormat(const QString &format);

    int weekStart() const;
    void setWeekStart(int day);

    int nextWeekDay() const;
    void setNextWeekDay(int day);

    QString defaultReminderTime() const;
    void setDefaultReminderTime(const QString &time);

    int autoReminderOffset() const;
    void setAutoReminderOffset(int minutes);

    QString language() const;
    void setLanguage(const QString &lang);

    // Sidebar
    bool showInboxCount() const;
    void setShowInboxCount(bool show);

    bool showTodayCount() const;
    void setShowTodayCount(bool show);

    bool showUpcomingCount() const;
    void setShowUpcomingCount(bool show);

    // Auto-archive / daily review
    int autoArchiveDays() const;
    void setAutoArchiveDays(int days);

    bool dailyReviewEnabled() const;
    void setDailyReviewEnabled(bool enabled);

    // Task row density
    bool compactDensity() const;
    void setCompactDensity(bool compact);

    // Q2.26-B
    bool searchAccentSensitive() const;
    void setSearchAccentSensitive(bool sensitive);

    // System tray icon (desktop only) — default true.
    bool showTrayIcon() const;
    void setShowTrayIcon(bool show);

    // Custom accent color override. customAccentHex() returns the stored
    // "#RRGGBB" string (empty when no override is active). accentColor()
    // resolves that to a QColor — when no override is set it falls back to
    // a sensible default (#db4c3f, matching the todoist_light/dark palettes)
    // so C++ consumers always have a valid color to paint with even before
    // the user picks a custom one. The setter accepts only valid "#RRGGBB"
    // strings (case-insensitive) plus the empty string for "reset to palette
    // default"; anything else is silently ignored.
    QString customAccentHex() const;
    void setCustomAccentHex(const QString &hex);
    QColor accentColor() const;

    // Sync
    int syncInterval() const;
    void setSyncInterval(int seconds);

    // Permissions
    bool notificationPermissionAsked() const;
    void setNotificationPermissionAsked(bool asked);

    // Q3.10: storage caps.
    int backupRetentionCount() const;
    void setBackupRetentionCount(int n);

    int backupRetentionDays() const;
    void setBackupRetentionDays(int days);

    qint64 vacuumThresholdBytes() const;
    void setVacuumThresholdBytes(qint64 bytes);

    // Methods
    Q_INVOKABLE void updateSetting(const QString &key, const QVariant &value);
    QVariant getSetting(const QString &key) const;
    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE QStringList availableThemes() const;
    Q_INVOKABLE QStringList availableLanguages() const;

    // Returns true on platforms where the app can relaunch itself in-process
    // (desktop). Android/WASM return false — the user must restart manually.
    Q_INVOKABLE bool canRestartApp() const;
    // Launches a new instance of the application and quits the current one.
    // No-op when canRestartApp() returns false.
    Q_INVOKABLE void restartApp();

signals:
    void themeChanged();
    void dailyGoalChanged();
    void weeklyGoalChanged();
    void vacationModeChanged();
    void startPageChanged();
    void timeFormatChanged();
    void dateFormatChanged();
    void weekStartChanged();
    void nextWeekDayChanged();
    void defaultReminderTimeChanged();
    void autoReminderOffsetChanged();
    void languageChanged();
    void sidebarSettingsChanged();
    void autoArchiveDaysChanged();
    void dailyReviewEnabledChanged();
    void compactDensityChanged();
    void searchAccentSensitiveChanged();
    void showTrayIconChanged();
    void accentColorChanged();
    void syncIntervalChanged();
    void notificationPermissionAskedChanged();
    void backupRetentionCountChanged();
    void backupRetentionDaysChanged();
    void vacuumThresholdBytesChanged();
    void settingChanged(const QString &key);

private slots:
    void onSettingsChanged();

private:
    QVariant setting(const QString &key) const;
    void set(const QString &key, const QVariant &value);

    DataStore *m_store;
    // Tracks the canonical key being written by the current setter call so that
    // onSettingsChanged (fired by DataStore::settingsChanged) can distinguish a
    // single-key update (setter handles the NOTIFY) from a bulk replace (sync).
    QString m_pendingNotifyKey;
};
