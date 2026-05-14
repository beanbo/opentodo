#pragma once
#include <QObject>
#include <QTimer>
#include <QDate>

class DataStore;
class NotificationService;

class DailyReviewService : public QObject
{
    Q_OBJECT
public:
    // Setting key for "is daily review enabled" lives in DataStore directly
    // (not SettingsViewModel) to keep this service in the Infra layer without
    // pulling in App-layer dependencies.
    DailyReviewService(DataStore *store,
                       NotificationService *notif,
                       QObject *parent = nullptr);

    // Re-reads productivity/dailyReviewHour and productivity/dailyReviewMinute
    // from QSettings and reschedules the timer. Called from QML after the user
    // edits the time in Settings → Продуктивность.
    Q_INVOKABLE void rescheduleFromSettings();

private slots:
    void checkAndNotify();
    void rescheduleNextTrigger();

private:
    QDateTime nextTrigger() const;
    void      loadTimeFromSettings();

    DataStore           *m_store;
    NotificationService *m_notif;
    QTimer               m_timer;
    QDate                m_lastNotifiedDate;
    int                  m_hour   = 18;
    int                  m_minute = 0;
};
