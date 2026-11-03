// WebAssembly notification service implementation.
// The Web Notifications API requires a service worker + HTTPS; for the initial
// WASM port we log to the browser console and rely on in-app QML banners.
// The timer-based scheduler still fires so the ReminderScheduler integration
// works: QML catches the notificationClicked signal and shows an inline alert.

#include "notificationservice_wasm.h"

#include <QDebug>
#include <QDateTime>
#include <QTimer>

#ifdef Q_OS_WASM
#  include <emscripten.h>
#endif

static void wasmLog(const QString &title, const QString &body)
{
#ifdef Q_OS_WASM
    const QByteArray msg = QStringLiteral("[OpenTodo] %1: %2").arg(title, body).toUtf8();
    emscripten_log(EM_LOG_CONSOLE | EM_LOG_INFO, "%s", msg.constData());
#else
    qDebug() << "[WasmNotification]" << title << body;
#endif
}

WasmNotificationService::WasmNotificationService(QObject *parent)
    : NotificationService(parent)
{
}

void WasmNotificationService::showNotification(const QString &title, const QString &body,
                                               const QString &taskId)
{
    wasmLog(title, body);
    // Emit the dedicated in-app banner signal so QML can render a toast.
    // (Previously this misused notificationClicked, which fires when the user
    // clicks an OS toast — emitting it on show meant any reminder was treated
    // as if the user had already clicked it, opening the editor unprompted.)
    emit inAppBannerRequested(title, body, taskId);
}

void WasmNotificationService::scheduleNotification(const QString &title, const QString &body,
                                                   const QDateTime &when, const QString &id)
{
    cancelNotification(id); // Remove any prior timer for same id.

    const QDateTime now = QDateTime::currentDateTime();
    qint64 msUntil = now.msecsTo(when);

    // B-5.8/9: if target is more than 1 hour in the past, skip silently.
    if (msUntil <= 0) {
        if (when < now.addSecs(-3600)) {
            qDebug() << "[WasmNotification] skipping stale past-due notification:" << id
                     << "target was" << when.toString(Qt::ISODate);
            return;
        }
        // Within the 1-hour grace window — fire immediately.
        showNotification(title, body, id);
        emit scheduledNotificationFired(id, title, body);
        return;
    }

    ScheduledEntry entry;
    entry.title = title;
    entry.body  = body;
    entry.id    = id;
    entry.when  = when;
    entry.timer = new QTimer(this);
    entry.timer->setSingleShot(true);
    entry.timer->setInterval(static_cast<int>(qMin(msUntil, qint64(INT_MAX))));

    connect(entry.timer, &QTimer::timeout, this, [this, id]() {
        auto it = m_scheduled.find(id);
        if (it != m_scheduled.end()) {
            // The interval was clamped to INT_MAX ms (~24.8 days): re-arm with
            // the remaining time instead of firing a far-future reminder early.
            const qint64 remaining = QDateTime::currentDateTime().msecsTo(it->when);
            if (remaining > 0) {
                it->timer->start(static_cast<int>(qMin(remaining, qint64(INT_MAX))));
                return;
            }
            const QString title = it->title;
            const QString body  = it->body;
            if (it->timer) {
                it->timer->deleteLater();
            }
            m_scheduled.erase(it);
            showNotification(title, body, id);
            // The real "it fired" event — ReminderScheduler logs it.
            emit scheduledNotificationFired(id, title, body);
        }
    });

    entry.timer->start();
    m_scheduled.insert(id, entry);
}

void WasmNotificationService::cancelNotification(const QString &id)
{
    auto it = m_scheduled.find(id);
    if (it != m_scheduled.end()) {
        if (it->timer) {
            it->timer->stop();
            it->timer->deleteLater();
        }
        m_scheduled.erase(it);
    }
}

void WasmNotificationService::cancelAll()
{
    for (auto &entry : m_scheduled) {
        if (entry.timer) {
            entry.timer->stop();
            entry.timer->deleteLater();
        }
    }
    m_scheduled.clear();
}

void WasmNotificationService::requestPermission()
{
    // Q-Android-Audit #2.1: WASM uses in-app banners only (no Web Notifications
    // API integration in v1). Treat the "permission" as always granted, but
    // still emit asynchronously so the QML SyncIndicator's Connections block
    // observes the same async semantics as Android.
    QMetaObject::invokeMethod(this, [this]() {
        emit permissionRequestCompleted(true);
    }, Qt::QueuedConnection);
}
