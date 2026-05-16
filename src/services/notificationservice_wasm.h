#pragma once

#include "notificationservice.h"

#include <QMap>
#include <QTimer>

// WASM notification service — uses console.log for diagnostic output since
// the Web Notifications API would require a service worker and HTTPS.
// For v1 WASM the app simply prints to the browser dev-console and shows
// a QML-level in-app banner (triggered via the base-class signals).
class WasmNotificationService : public NotificationService
{
    Q_OBJECT

public:
    explicit WasmNotificationService(QObject *parent = nullptr);
    ~WasmNotificationService() override = default;

    Q_INVOKABLE void showNotification(const QString &title, const QString &body,
                                      const QString &taskId = {}) override;
    Q_INVOKABLE void scheduleNotification(const QString &title, const QString &body,
                                          const QDateTime &when, const QString &id) override;
    void cancelNotification(const QString &id) override;
    void cancelAll() override;
    Q_INVOKABLE bool hasPermission() const override { return true; }

    // Q-Android-Audit #2.1: emit permissionRequestCompleted(true) async so
    // the QML SyncIndicator's "asked" gating runs through the same code path
    // as Android. WASM has no runtime grant for in-app banners (we never use
    // the real Web Notifications API in v1), so this always succeeds.
    Q_INVOKABLE void requestPermission() override;

private:
    struct ScheduledEntry {
        QString title;
        QString body;
        QString id;
        QDateTime when;
        QTimer   *timer = nullptr;
    };
    QMap<QString, ScheduledEntry> m_scheduled;
};
