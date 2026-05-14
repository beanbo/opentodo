#pragma once

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

    // Sync settings
    Q_PROPERTY(int syncInterval READ syncInterval WRITE setSyncInterval NOTIFY syncIntervalChanged)

    // Permissions / first-run gates
    Q_PROPERTY(bool notificationPermissionAsked READ notificationPermissionAsked
               WRITE setNotificationPermissionAsked NOTIFY notificationPermissionAskedChanged)

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

    // Sync
    int syncInterval() const;
    void setSyncInterval(int seconds);

    // Permissions
    bool notificationPermissionAsked() const;
    void setNotificationPermissionAsked(bool asked);

    // Methods
    Q_INVOKABLE void updateSetting(const QString &key, const QVariant &value);
    QVariant getSetting(const QString &key) const;
    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE QStringList availableThemes() const;
    Q_INVOKABLE QStringList availableLanguages() const;

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
    void syncIntervalChanged();
    void notificationPermissionAskedChanged();
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
