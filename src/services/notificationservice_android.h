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
    bool m_channelCreated = false;
};
#endif
