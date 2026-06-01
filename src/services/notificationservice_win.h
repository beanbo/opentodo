#pragma once

#include "notificationservice.h"
#include <QMap>
#include <QTimer>
#include <QPointer>
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

    // Borrow the single tray icon owned by TrayIconService and use it to anchor
    // toast/balloon messages, instead of registering our own second icon (the
    // "two tray icons" bug). main.cpp injects this once both services exist.
    // Passing the shared icon retires any private fallback icon we may already
    // have spun up. Safe to call with nullptr (clears the borrow).
    void setSharedTrayIcon(QSystemTrayIcon *icon);

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onScheduledNotificationDue(const QString &id);

private:
    // The tray icon to anchor toasts on: the SHARED one borrowed from
    // TrayIconService when present, otherwise a lazily-created private fallback
    // (standalone unit tests, or any platform where TrayIconService is absent).
    QSystemTrayIcon *activeTrayIcon();
    // Lazily create + show our own fallback tray icon. Only used when no shared
    // icon has been injected via setSharedTrayIcon().
    void ensureOwnTrayIcon();

    QPointer<QSystemTrayIcon> m_sharedTrayIcon;  // borrowed from TrayIconService, not owned
    QSystemTrayIcon *m_trayIcon = nullptr;       // our own fallback, owned via QObject parent
    QMap<QString, ScheduledNotification> m_scheduled;
    QString m_lastTaskId;
};

#endif // Q_OS_WIN
