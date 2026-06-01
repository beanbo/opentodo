#include "notificationservice_win.h"

#ifdef Q_OS_WIN

#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QSettings>
#include <climits>

#include <windows.h>
#include <shellapi.h>

WindowsNotificationService::WindowsNotificationService(QObject *parent)
    : NotificationService(parent)
{
    // No tray icon is created here. In production main.cpp injects the single
    // shared TrayIconService icon via setSharedTrayIcon() before any
    // notification can fire; standalone callers (unit tests, or platforms with
    // no TrayIconService) get a private fallback icon created lazily on the
    // first showNotification(). Eagerly creating our own icon here is what
    // produced a SECOND icon in the notification area next to TrayIconService.
}

WindowsNotificationService::~WindowsNotificationService()
{
    cancelAll();
}

void WindowsNotificationService::setSharedTrayIcon(QSystemTrayIcon *icon)
{
    if (m_sharedTrayIcon == icon)
        return;
    m_sharedTrayIcon = icon;

    if (icon) {
        // Listen for messageClicked ONLY — not activated. The icon's
        // click/double-click activation is owned by TrayIconService (it raises
        // the main window); handling it here too would also fire
        // notificationClicked on every plain tray click. messageClicked fires
        // only when the user clicks the balloon/toast itself, which
        // TrayIconService does not handle.
        connect(icon, &QSystemTrayIcon::messageClicked,
                this, [this]() { emit notificationClicked(m_lastTaskId); },
                Qt::UniqueConnection);
    }

    // If a notification fired before the shared icon was injected we may have
    // spun up our own fallback icon — retire it so exactly one icon remains.
    if (m_trayIcon) {
        m_trayIcon->hide();
        m_trayIcon->deleteLater();
        m_trayIcon = nullptr;
    }
}

QSystemTrayIcon *WindowsNotificationService::activeTrayIcon()
{
    if (m_sharedTrayIcon)
        return m_sharedTrayIcon;
    ensureOwnTrayIcon();
    return m_trayIcon;
}

void WindowsNotificationService::ensureOwnTrayIcon()
{
    if (m_trayIcon)
        return;
    m_trayIcon = new QSystemTrayIcon(this);
    // B-9.9: use the actual resource path registered in CMakeLists.txt
    m_trayIcon->setIcon(QIcon(QStringLiteral(":/icons/resources/icons/icon_32.png")));
    m_trayIcon->setToolTip(QStringLiteral("OpenTodo"));
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &WindowsNotificationService::onTrayActivated);
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked,
            this, [this]() { emit notificationClicked(m_lastTaskId); });
}

void WindowsNotificationService::showNotification(const QString &title, const QString &body, const QString &taskId)
{
    m_lastTaskId = taskId;

    QSystemTrayIcon *tray = activeTrayIcon();

    // The shared TrayIconService icon follows the user's "show tray icon"
    // setting; when they've hidden it there is no registered icon for Windows
    // to anchor a balloon on (QSystemTrayIcon::showMessage silently no-ops on a
    // hidden icon). Rather than drop the notification, surface it as an in-app
    // banner. Our own fallback icon is always shown, so this branch only trips
    // for the shared-and-hidden case.
    if (!tray || !tray->isVisible()) {
        showInAppBanner(title, body, taskId);
        return;
    }

    // Read user preference for notification sound. Default ON for backwards compat.
    // The spec asks for "<audio silent=\"true\"/>" semantics — on Windows we
    // achieve the same effect via the Shell_NotifyIcon NIIF_NOSOUND flag. When
    // sound is enabled we keep using Qt's QSystemTrayIcon::showMessage which
    // plays the default system sound.
    //
    // Bug #3 (May 2026): key name MUST match what QML's Settings element
    // writes from qml/components/settings/SettingsSectionReminders.qml.
    // That element declares `Settings { category: "notifications"; property
    // bool enabled: true }` which serialises to the key
    // "notifications/enabled" (NOT "notifications/soundEnabled" — Qt builds
    // the key from category + property name). Aligning the reader here
    // preserves whatever the user had toggled previously.
    QSettings s;
    const bool soundEnabled = s.value(QStringLiteral("notifications/enabled"), true).toBool();

    if (soundEnabled) {
        tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
        return;
    }

    // Silent path: drive the same shell notification area icon directly so we
    // can attach NIIF_NOSOUND to the balloon. We reuse the QSystemTrayIcon's
    // window handle (the hidden message-only window Qt uses to host the icon).
    // If we cannot locate a handle, fall back to showMessage; the audible
    // sound is the lesser of two evils vs. no notification at all.
    HWND hwnd = ::FindWindowW(L"QTrayIconMessageWindow", nullptr);
    if (!hwnd) {
        tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
        return;
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 0; // Qt registers its tray icon with uID 0 by default.
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND; // <audio silent="true"/> equivalent.

    const std::wstring wTitle = title.toStdWString();
    const std::wstring wBody  = body.toStdWString();
    ::wcsncpy_s(nid.szInfoTitle, wTitle.c_str(), _TRUNCATE);
    ::wcsncpy_s(nid.szInfo,      wBody.c_str(),  _TRUNCATE);

    if (!::Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // If the direct shell call fails (different uID, wrong window class on
        // future Qt versions), fall back to the audible Qt path so the user
        // still gets the notification.
        tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
    }
}

void WindowsNotificationService::scheduleNotification(const QString &title, const QString &body,
                                                       const QDateTime &when, const QString &id)
{
    cancelNotification(id);

    const QDateTime now = QDateTime::currentDateTime();
    qint64 msUntil = now.msecsTo(when);

    // B-5.8/9: if target is more than 1 hour in the past, skip silently.
    if (msUntil <= 0) {
        if (when < now.addSecs(-3600)) {
            qDebug() << "[WinNotification] skipping stale past-due notification:" << id
                     << "target was" << when.toString(Qt::ISODate);
            return;
        }
        // Within the 1-hour grace window — fire immediately.
        showNotification(title, body, id);
        return;
    }

    ScheduledNotification sn;
    sn.id = id;
    sn.title = title;
    sn.body = body;
    sn.when = when;  // absolute target stored for rearm (B-9.18)
    sn.timer = new QTimer(this);
    sn.timer->setSingleShot(true);

    // B-9.18: QTimer::setInterval takes int (max ~24.8 days). Clamp and rearm
    // on timeout if we have not yet reached the absolute target time.
    const int intervalMs = static_cast<int>(qMin(msUntil, qint64(INT_MAX)));
    sn.timer->setInterval(intervalMs);

    connect(sn.timer, &QTimer::timeout, this, [this, id]() {
        onScheduledNotificationDue(id);
    });

    sn.timer->start();
    m_scheduled.insert(id, sn);
}

void WindowsNotificationService::cancelNotification(const QString &id)
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

void WindowsNotificationService::cancelAll()
{
    for (auto &sn : m_scheduled) {
        if (sn.timer) {
            sn.timer->stop();
            sn.timer->deleteLater();
        }
    }
    m_scheduled.clear();
}

void WindowsNotificationService::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        // Window activation is handled by QML via the hotkeyService.quickAddRequested
        // signal; the tray click simply emits notificationClicked so the application
        // can decide what to do (e.g. raise the window, open a task).
        emit notificationClicked(m_lastTaskId);
    }
}

void WindowsNotificationService::requestPermission()
{
    // Q-Android-Audit #2.1: no runtime permission grant on Windows — just
    // emit permissionRequestCompleted(true) after the call returns so QML
    // SyncIndicator flips its "asked" flag uniformly across platforms.
    QMetaObject::invokeMethod(this, [this]() {
        emit permissionRequestCompleted(true);
    }, Qt::QueuedConnection);
}

void WindowsNotificationService::onScheduledNotificationDue(const QString &id)
{
    auto it = m_scheduled.find(id);
    if (it == m_scheduled.end())
        return;

    const QDateTime now = QDateTime::currentDateTime();

    // B-9.18: if the absolute target has not yet been reached (can happen when
    // the original msUntil exceeded INT_MAX and we armed a partial interval),
    // rearm with the remaining time instead of firing early.
    if (now < it->when) {
        const qint64 remaining = now.msecsTo(it->when);
        const int nextInterval = static_cast<int>(qMin(remaining, qint64(INT_MAX)));
        it->timer->setInterval(nextInterval);
        it->timer->start();
        return;
    }

    showNotification(it->title, it->body, it->id);
    if (it->timer)
        it->timer->deleteLater();
    m_scheduled.erase(it);
}

#endif // Q_OS_WIN
