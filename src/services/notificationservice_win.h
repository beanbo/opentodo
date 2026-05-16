#pragma once

#include "notificationservice.h"
#include <QMap>
#include <QTimer>
#include <QSystemTrayIcon>

// Only compiled on Windows
#ifdef Q_OS_WIN

struct ScheduledNotification {
    QString id;
    QString title;
    QString body;
    QDateTime when;
    QTimer *timer = nullptr;
};

class WindowsNotificationService : public NotificationService
{
    Q_OBJECT

public:
    explicit WindowsNotificationService(QObject *parent = nullptr);
    ~WindowsNotificationService() override;

    void showNotification(const QString &title, const QString &body, const QString &taskId = {}) override;
    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override;
    void cancelNotification(const QString &id) override;
    void cancelAll() override;

    // Q-Android-Audit #2.1: Windows has no runtime POST_NOTIFICATIONS prompt
    // (toasts are gated by app-level Windows Settings, not by a per-call grant
    // dialog). Override emits permissionRequestCompleted(true) asynchronously
    // so the QML SyncIndicator's flag-flip path is identical to Android.
    void requestPermission() override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onScheduledNotificationDue(const QString &id);

private:
    void ensureTrayIcon();

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMap<QString, ScheduledNotification> m_scheduled;
    QString m_lastTaskId;
};

#endif // Q_OS_WIN
