#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>

class NotificationService : public QObject
{
    Q_OBJECT

public:
    explicit NotificationService(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~NotificationService() = default;

    Q_INVOKABLE virtual void showNotification(const QString &title, const QString &body, const QString &taskId = {}) = 0;
    Q_INVOKABLE virtual void scheduleNotification(const QString &title, const QString &body,
                                                    const QDateTime &when, const QString &id) = 0;
    virtual void cancelNotification(const QString &id) = 0;
    virtual void cancelAll() = 0;
    Q_INVOKABLE virtual bool hasPermission() const { return true; }
    Q_INVOKABLE virtual void requestPermission() {}

    // Cross-platform helper for non-task UX events (e.g. "hotkey conflict",
    // "sync failed") that should appear in the QML banner stack regardless of
    // whether the OS-level toast path is wired up. Public so any subsystem
    // can route a message through the same banner channel that reminders
    // use. Internally just emits inAppBannerRequested(); having it as a
    // member instead of an external emit() keeps signal access encapsulated.
    Q_INVOKABLE void showInAppBanner(const QString &title, const QString &body,
                                     const QString &taskId = {})
    {
        emit inAppBannerRequested(title, body, taskId);
    }

signals:
    void notificationClicked(const QString &taskId);
    void permissionGranted();
    void permissionDenied();

    // Fired when a platform implementation can't (or won't) show a real OS toast
    // and wants the QML layer to render an in-app banner instead. WASM is the
    // primary user — Web Notifications API needs HTTPS + service worker. Native
    // platforms can also emit this if they want to mirror the toast inside the app.
    void inAppBannerRequested(const QString &title, const QString &body,
                              const QString &taskId);

    // Fired when a Reminder entity is dispatched to the platform (i.e. a real
    // task reminder fires). Distinct from inAppBannerRequested: this is a
    // semantic "a reminder happened" event used by NotificationsViewModel to
    // log it in the in-app notification centre. Currently emitted by
    // ReminderScheduler when it hands a reminder to scheduleNotification().
    void reminderFired(const QString &taskId, const QString &title,
                       const QString &body);
};
