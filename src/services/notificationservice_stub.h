#pragma once
#include "notificationservice.h"
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QTimer>

// Notification service for platforms without a native OS backend (Linux,
// macOS, etc.). There is no OS toast: a notification is shown as the QML
// in-app banner (inAppBannerRequested). Scheduled notifications are armed
// with in-process QTimers — like the WASM backend — so reminders really fire
// on Linux desktop while the app runs (they used to be only logged).
class StubNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit StubNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}
    ~StubNotificationService() override { cancelAll(); }

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        qDebug() << "[StubNotification] show:" << title << body << taskId;
        emit inAppBannerRequested(title, body, taskId);
    }

    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override
    {
        cancelNotification(id); // replace any pending schedule for the same id

        const QDateTime now = QDateTime::currentDateTime();
        const qint64 msUntil = now.msecsTo(when);
        if (msUntil <= 0) {
            // Same policy as the other backends (B-5.8/9): more than an hour
            // late → drop silently; within the grace window → fire now.
            if (when < now.addSecs(-3600)) {
                qDebug() << "[StubNotification] skipping stale past-due notification:" << id;
                return;
            }
            fire(title, body, id);
            return;
        }

        Entry entry;
        entry.title = title;
        entry.body  = body;
        entry.when  = when;
        entry.timer = new QTimer(this);
        entry.timer->setSingleShot(true);
        connect(entry.timer, &QTimer::timeout, this, [this, id]() { onTimerDue(id); });
        m_scheduled.insert(id, entry);
        arm(entry.timer, msUntil);
    }

    void cancelNotification(const QString &id) override
    {
        auto it = m_scheduled.find(id);
        if (it == m_scheduled.end())
            return;
        if (it->timer) {
            it->timer->stop();
            it->timer->deleteLater();
        }
        m_scheduled.erase(it);
    }

    void cancelAll() override
    {
        for (auto &entry : m_scheduled) {
            if (entry.timer) {
                entry.timer->stop();
                entry.timer->deleteLater();
            }
        }
        m_scheduled.clear();
    }

    // Test/diagnostic helper: is a schedule pending for `id`?
    bool isScheduled(const QString &id) const { return m_scheduled.contains(id); }

    // Q-Android-Audit #2.1: on platforms where requestPermission() is a no-op
    // (Linux/macOS dev stub), still fire permissionRequestCompleted(true) so
    // the QML SyncIndicator flow is uniform — the gating flag flips in the
    // signal handler instead of synchronously after the call. We use
    // QMetaObject::invokeMethod with QueuedConnection to defer the emit until
    // after the caller returns; SyncIndicator subscribes in its
    // Component.onCompleted, and a synchronous emit during construction would
    // race the Connections binding setup.
    void requestPermission() override
    {
        QMetaObject::invokeMethod(this, [this]() {
            emit permissionRequestCompleted(true);
        }, Qt::QueuedConnection);
    }

private:
    struct Entry {
        QString title;
        QString body;
        QDateTime when;    // absolute target; the timer is re-armed until reached
        QTimer *timer = nullptr;
    };

    // QTimer intervals are int milliseconds (~24.8 days max), and its
    // monotonic clock does not advance while the machine is suspended. Arm at
    // most one minute ahead and re-check the wall clock on every expiry:
    // far-future reminders never overflow, and a reminder that came due during
    // a suspend fires within a minute of wake-up.
    static void arm(QTimer *timer, qint64 msUntil)
    {
        constexpr qint64 kMaxChunkMs = 60 * 1000;
        timer->start(static_cast<int>(qBound<qint64>(1, msUntil, kMaxChunkMs)));
    }

    void onTimerDue(const QString &id)
    {
        auto it = m_scheduled.find(id);
        if (it == m_scheduled.end())
            return;
        const qint64 remaining = QDateTime::currentDateTime().msecsTo(it->when);
        if (remaining > 0) {
            arm(it->timer, remaining);
            return;
        }
        const QString title = it->title;
        const QString body  = it->body;
        if (it->timer)
            it->timer->deleteLater();
        m_scheduled.erase(it);
        fire(title, body, id);
    }

    void fire(const QString &title, const QString &body, const QString &id)
    {
        showNotification(title, body, id);
        emit scheduledNotificationFired(id, title, body);
    }

    QMap<QString, Entry> m_scheduled;
};
