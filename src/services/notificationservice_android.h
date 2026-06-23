#pragma once
#include "notificationservice.h"
#ifdef Q_OS_ANDROID
#include <QJniObject>

class AndroidNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit AndroidNotificationService(QObject *parent = nullptr);
    void showNotification(const QString &title, const QString &body, const QString &taskId = {}) override;
    void scheduleNotification(const QString &title, const QString &body, const QDateTime &when, const QString &id) override;
    void cancelNotification(const QString &id) override;
    void cancelAll() override;
    bool hasPermission() const override;
    void requestPermission() override;
private:
    void createNotificationChannel();
    // Cold-start: drain any notification-action presses that occurred while the
    // app process was dead (persisted by Java to SharedPreferences) and re-emit
    // notificationActionInvoked for each, queued so listeners are wired first.
    void drainPendingActions();
    bool m_channelCreated = false;
};
#endif
