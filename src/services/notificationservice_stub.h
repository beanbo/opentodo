#pragma once
#include "notificationservice.h"
#include <QDebug>

// Stub notification service for platforms without a native implementation
// (Linux, macOS, etc.). All methods are no-ops; debug output is written to
// the Qt log so notifications are visible during development.
class StubNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit StubNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        qDebug() << "[StubNotification] show:" << title << body << taskId;
        emit inAppBannerRequested(title, body, taskId);
    }

    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override
    {
        qDebug() << "[StubNotification] schedule:" << title << when << id;
        Q_UNUSED(body)
    }

    void cancelNotification(const QString &id) override
    {
        qDebug() << "[StubNotification] cancel:" << id;
    }

    void cancelAll() override
    {
        qDebug() << "[StubNotification] cancelAll";
    }
};
