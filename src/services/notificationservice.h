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
    // Cancels the pending schedule for `id` AND dismisses it if it is already
    // displayed (Android). Use for reminders that are gone for good (deleted
    // reminder, completed / deleted task).
    virtual void cancelNotification(const QString &id) = 0;
    // Cancels only the PENDING (not yet fired) schedule for `id` and leaves an
    // already-displayed notification alone. ReminderScheduler calls this on
    // every reschedule pass before re-arming: using cancelNotification() there
    // made every edit / sync / app start dismiss reminders the user had not
    // acted on yet (Android NotificationManager.cancel). Timer-based backends
    // have nothing displayed to keep, so the default is a plain cancel.
    virtual void cancelScheduled(const QString &id) { cancelNotification(id); }
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
    // Fired when the user taps an ACTION BUTTON on a reminder notification
    // (e.g. "complete" / "snooze") rather than the body. `action` is a stable
    // verb ("complete" | "snooze"). Wired in main.cpp to apply the action
    // without opening the app. Currently emitted by the Android backend; other
    // platforms may emit it once their native action-button path exists.
    void notificationActionInvoked(const QString &taskId, const QString &action);
    void permissionGranted();
    void permissionDenied();

    // Q-Android-Audit #2.1: emitted exactly once after the OS returns from a
    // permission prompt (regardless of grant / deny), so the QML layer can flip
    // its "we've asked the user" persistent flag in a Connections handler
    // rather than synchronously after calling requestPermission(). On
    // platforms where requestPermission() is a no-op (Windows, stub) the
    // signal still fires once with granted=true so the QML flow is uniform.
    void permissionRequestCompleted(bool granted);

    // Fired when a platform implementation can't (or won't) show a real OS toast
    // and wants the QML layer to render an in-app banner instead. WASM is the
    // primary user — Web Notifications API needs HTTPS + service worker. Native
    // platforms can also emit this if they want to mirror the toast inside the app.
    void inAppBannerRequested(const QString &title, const QString &body,
                              const QString &taskId);

    // Emitted by the platform backend when a notification handed to
    // scheduleNotification() actually fires (timer / alarm expiry, or the
    // immediate grace-window path), carrying the id it was scheduled under.
    // The backend only knows that id; ReminderScheduler maps it back to the
    // task (DataStore::taskIdForNotification) and emits reminderFired.
    void scheduledNotificationFired(const QString &id, const QString &title,
                                    const QString &body);

    // Fired when a task reminder really goes off (i.e. at trigger time, not
    // when it is scheduled). Distinct from inAppBannerRequested: this is a
    // semantic "a reminder happened" event used by NotificationsViewModel to
    // log it in the in-app notification centre. Emitted by ReminderScheduler
    // in response to scheduledNotificationFired.
    void reminderFired(const QString &taskId, const QString &title,
                       const QString &body);
};
