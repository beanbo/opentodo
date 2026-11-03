#include "notificationservice_win.h"

#ifdef Q_OS_WIN

#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <climits>

#include <windows.h>
#include <shellapi.h>

#ifdef OPENTODO_HAS_WINRT_TOAST
// ── Native WinRT toast with Выполнить / Отложить action buttons ──────────────
//
// MinGW ships its own WRL (ComPtr, HStringReference) plus roapi/winstring, but
// NOT the Windows.UI.Notifications / Windows.Data.Xml.Dom ABI headers, and its
// WRL has no Callback. We therefore hand-declare the handful of interfaces we
// call — all with stable, non-parameterized IIDs, and only the leading vtable
// slots we actually invoke — and drive activation through a registered
// `opentodo:` URI protocol rather than an in-process Activated handler. That
// sidesteps the parameterized ITypedEventHandler<ToastNotification,…> delegate
// IID, which cannot be reproduced without the (MSVC-only) SDK headers. Clicking
// a button relaunches the exe with the URI; main.cpp records the action
// (recordToastActivation) and the running instance drains it (drainToastActions
// → onToastActivated).
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <propsys.h>
#include <string>

// Hand-declared WinRT interfaces (MinGW lacks the notification/XML ABI headers).
// GUIDs are attached with __CRT_UUID_DECL — MinGW's g++ IGNORES __declspec(uuid)
// (it warns "uuid attribute directive ignored"), so __uuidof() / ComPtr::As()
// only see a real IID through this macro. These must live at GLOBAL scope:
// __CRT_UUID_DECL specializes a global template, which is ill-formed from inside
// an anonymous namespace. Only the leading vtable slots we actually call are
// declared; IInspectable contributes 3 slots over IUnknown, so the first method
// of each interface lands at vtable slot 6.
struct IXmlDocument_ : public IInspectable {};
__CRT_UUID_DECL(IXmlDocument_, 0xf7f3a506, 0x1e87, 0x42d6, 0xbc, 0xfb, 0xb8, 0xc8, 0x09, 0xfa, 0x54, 0x94)

struct IXmlDocumentIO_ : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadXmlWithSettings(HSTRING xml, void *settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveToFileAsync(void *file, void **op) = 0;
};
__CRT_UUID_DECL(IXmlDocumentIO_, 0x6cd0e74e, 0xee65, 0x4489, 0x9e, 0xbf, 0xca, 0x43, 0xe8, 0x7b, 0xa6, 0x37)

struct IToastNotification_ : public IInspectable {};
__CRT_UUID_DECL(IToastNotification_, 0x997e2675, 0x059e, 0x4e60, 0x8b, 0x06, 0x17, 0x60, 0x91, 0x7c, 0x8b, 0x80)

struct IToastNotificationFactory_ : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(IXmlDocument_ *content, IToastNotification_ **value) = 0;
};
__CRT_UUID_DECL(IToastNotificationFactory_, 0x04124b20, 0x82c6, 0x4229, 0xb1, 0x09, 0xfd, 0x9e, 0xd4, 0x66, 0x2b, 0x53)

struct IToastNotifier_ : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Show(IToastNotification_ *notification) = 0;
};
__CRT_UUID_DECL(IToastNotifier_, 0x75927b93, 0x03f3, 0x41ec, 0x91, 0xd3, 0x6e, 0x5b, 0xac, 0x1b, 0x38, 0xe7)

struct IToastNotificationManagerStatics_ : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier_ **result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING applicationId, IToastNotifier_ **result) = 0;
};
__CRT_UUID_DECL(IToastNotificationManagerStatics_, 0x50ac103f, 0xd235, 0x4598, 0xbb, 0xef, 0x98, 0xfe, 0x4d, 0x1a, 0x3a, 0xd4)

namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

static const wchar_t *kAumid = L"OpenTodo.Reminder.App";

void ensureRuntime()
{
    static bool inited = false;
    if (!inited) {
        // Qt's GUI thread is an STA (OleInitialize). RoInitialize as STA is
        // compatible; any non-S_OK (already-init / changed-mode) is harmless.
        RoInitialize(RO_INIT_SINGLETHREADED);
        inited = true;
    }
}

// Start-Menu shortcut tagged with the AUMID — Windows requires it before an
// unpackaged app may raise a toast. Idempotent (no-op if it already exists).
void ensureShortcut()
{
    wchar_t programs[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, programs)))
        return;
    std::wstring lnk = std::wstring(programs) + L"\\OpenTodo.lnk";
    if (GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return;
    link->SetPath(exe);
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(link.As(&props))) {
        PROPVARIANT pv;
        if (SUCCEEDED(InitPropVariantFromString(kAumid, &pv))) {
            props->SetValue(PKEY_AppUserModel_ID, pv);
            props->Commit();
            PropVariantClear(&pv);
        }
    }
    ComPtr<IPersistFile> file;
    if (SUCCEEDED(link.As(&file)))
        file->Save(lnk.c_str(), TRUE);
}

ComPtr<IXmlDocument_> buildXmlDoc(const std::wstring &xml)
{
    ComPtr<IInspectable> insp;
    if (FAILED(RoActivateInstance(HStringReference(L"Windows.Data.Xml.Dom.XmlDocument").Get(), &insp)))
        return nullptr;
    ComPtr<IXmlDocumentIO_> io;
    if (FAILED(insp.As(&io)))
        return nullptr;
    if (FAILED(io->LoadXml(HStringReference(xml.c_str()).Get())))
        return nullptr;
    ComPtr<IXmlDocument_> doc;
    if (FAILED(insp.As(&doc)))
        return nullptr;
    return doc;
}

QString xmlEsc(const QString &s)
{
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
     .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
     .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
     .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return o;
}

// Returns true if a native toast was shown (caller then skips the balloon).
bool showToastNative(const QString &title, const QString &body, const QString &taskId)
{
    ensureRuntime();
    ensureShortcut();

    const QString T = xmlEsc(title), B = xmlEsc(body);
    // Task IDs are UUIDs (URL-safe), but percent-encode defensively; the
    // running instance decodes via QUrlQuery when it drains the action.
    const QString ID = QString::fromLatin1(QUrl::toPercentEncoding(taskId));
    QString xml;
    xml += QStringLiteral("<toast activationType=\"protocol\" launch=\"opentodo:open?id=") + ID + QStringLiteral("\">");
    xml += QStringLiteral("<visual><binding template=\"ToastGeneric\">");
    xml += QStringLiteral("<text>") + T + QStringLiteral("</text><text>") + B + QStringLiteral("</text>");
    xml += QStringLiteral("</binding></visual>");
    if (!taskId.isEmpty()) {
        xml += QStringLiteral("<actions>");
        xml += QStringLiteral("<action content=\"Выполнить\" activationType=\"protocol\" arguments=\"opentodo:complete?id=") + ID + QStringLiteral("\"/>");
        xml += QStringLiteral("<action content=\"Отложить\" activationType=\"protocol\" arguments=\"opentodo:snooze?id=") + ID + QStringLiteral("\"/>");
        xml += QStringLiteral("</actions>");
    }
    xml += QStringLiteral("</toast>");

    ComPtr<IXmlDocument_> doc = buildXmlDoc(xml.toStdWString());
    if (!doc)
        return false;

    ComPtr<IToastNotificationFactory_> factory;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.UI.Notifications.ToastNotification").Get(),
                                      __uuidof(IToastNotificationFactory_), &factory)))
        return false;
    ComPtr<IToastNotification_> toast;
    if (FAILED(factory->CreateToastNotification(doc.Get(), &toast)))
        return false;

    ComPtr<IToastNotificationManagerStatics_> mgr;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.UI.Notifications.ToastNotificationManager").Get(),
                                      __uuidof(IToastNotificationManagerStatics_), &mgr)))
        return false;
    ComPtr<IToastNotifier_> notifier;
    if (FAILED(mgr->CreateToastNotifierWithId(HStringReference(kAumid).Get(), &notifier)))
        return false;
    return SUCCEEDED(notifier->Show(toast.Get()));
}

// Register the `opentodo:` URI scheme (HKCU, no admin) so a toast action button
// relaunches this exe with its action URI. Raw Win32 registry API to write the
// (Default) values unambiguously. Idempotent.
void registerToastProtocol()
{
    auto setStr = [](const wchar_t *subkey, const wchar_t *name, const std::wstring &val) {
        HKEY h = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_WRITE,
                            nullptr, &h, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(val.c_str()),
                           static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(h);
        }
    };
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring cmd = L"\"" + std::wstring(exe) + L"\" \"%1\"";
    setStr(L"Software\\Classes\\opentodo", nullptr, L"URL:OpenTodo Protocol");
    setStr(L"Software\\Classes\\opentodo", L"URL Protocol", L"");
    setStr(L"Software\\Classes\\opentodo\\shell\\open\\command", nullptr, cmd);
}

} // namespace
#endif // OPENTODO_HAS_WINRT_TOAST

WindowsNotificationService::WindowsNotificationService(QObject *parent)
    : NotificationService(parent)
{
    // No tray icon is created here. In production main.cpp injects the single
    // shared TrayIconService icon via setSharedTrayIcon() before any
    // notification can fire; standalone callers (unit tests, or platforms with
    // no TrayIconService) get a private fallback icon created lazily on the
    // first showNotification(). Eagerly creating our own icon here is what
    // produced a SECOND icon in the notification area next to TrayIconService.

#ifdef OPENTODO_HAS_WINRT_TOAST
    // Toast action buttons (Выполнить/Отложить) activate via the `opentodo:`
    // URI protocol, which relaunches this exe; that throwaway instance appends
    // the action to toastActionsFilePath() and exits (see main.cpp). Register
    // the protocol, drain anything queued while we were down, and watch the
    // file so live button presses are handled without opening a new window.
    registerToastProtocol();
    const QString actionsPath = toastActionsFilePath();
    QDir().mkpath(QFileInfo(actionsPath).absolutePath());
    if (!QFileInfo::exists(actionsPath)) {
        QFile seed(actionsPath);
        if (seed.open(QIODevice::WriteOnly))
            seed.close();
    }
    m_toastWatcher = new QFileSystemWatcher(this);
    m_toastWatcher->addPath(actionsPath);
    connect(m_toastWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &p) {
        drainToastActions();
        // A truncate-rewrite can drop the inotify-style watch; re-arm it.
        if (!m_toastWatcher->files().contains(p) && QFileInfo::exists(p))
            m_toastWatcher->addPath(p);
    });
    // Cold-start drain, deferred until main.cpp has wired notificationActionInvoked
    // and the event loop is running.
    QTimer::singleShot(0, this, &WindowsNotificationService::drainToastActions);
#endif
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

#ifdef OPENTODO_HAS_WINRT_TOAST
    // Prefer a real WinRT toast — it carries the Выполнить / Отложить action
    // buttons. Falls through to the tray balloon if the toast path fails (no
    // AUMID shortcut yet, COM error, notifications disabled, etc.).
    if (showToastNative(title, body, taskId))
        return;
#endif

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
        emit scheduledNotificationFired(id, title, body);
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

QString WindowsNotificationService::toastActionsFilePath()
{
    // Same path is computed by the relaunched (URI) instance and the running
    // one — both run after setApplicationName/OrganizationName("OpenTodo"), so
    // AppDataLocation resolves identically across the two processes.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/toast_actions.txt");
}

void WindowsNotificationService::recordToastActivation(const QString &uri)
{
    // uri: "opentodo:complete?id=<taskId>" → append one "<verb>|<taskId>" line
    // for the running instance to drain. Called from the throwaway URI process
    // (main.cpp), which exits immediately afterwards.
    const QUrl u(uri);
    const QString verb = u.path();
    const QString id = QUrlQuery(u).queryItemValue(QStringLiteral("id"),
                                                   QUrl::FullyDecoded);
    if (verb.isEmpty())
        return;
    const QString path = toastActionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Cross-process lock: the running instance's drainToastActions() does
    // readAll()+resize(0); without this lock an append landing between those two
    // calls would be truncated to zero and the action lost forever.
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.lock();
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write((verb + QLatin1Char('|') + id + QLatin1Char('\n')).toUtf8());
        f.close();
    }
}

void WindowsNotificationService::drainToastActions()
{
    const QString path = toastActionsFilePath();
    QString all;
    {
        // Hold the lock only for the read+truncate so a concurrent
        // recordToastActivation() append can't be lost; release it before
        // dispatching (onToastActivated emits signals and may take a while).
        QLockFile lock(path + QStringLiteral(".lock"));
        lock.lock();
        QFile f(path);
        if (!f.exists() || !f.open(QIODevice::ReadWrite | QIODevice::Text))
            return;
        all = QString::fromUtf8(f.readAll());
        f.resize(0);
        f.close();
    }
    const QStringList lines = all.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines)
        onToastActivated(line);  // "<verb>|<taskId>"
}

void WindowsNotificationService::onToastActivated(const QString &arg)
{
    // arg is "<verb>|<taskId>": open|… (body click) or complete|… / snooze|… .
    const int bar = arg.indexOf(QLatin1Char('|'));
    const QString verb   = bar >= 0 ? arg.left(bar) : arg;
    const QString taskId = bar >= 0 ? arg.mid(bar + 1) : QString();
    if (verb == QStringLiteral("complete") || verb == QStringLiteral("snooze"))
        emit notificationActionInvoked(taskId, verb);
    else
        emit notificationClicked(taskId.isEmpty() ? m_lastTaskId : taskId);
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

    const QString title = it->title;
    const QString body  = it->body;
    if (it->timer)
        it->timer->deleteLater();
    m_scheduled.erase(it);
    showNotification(title, body, id);
    // The real "it fired" event — ReminderScheduler logs it as a reminder.
    emit scheduledNotificationFired(id, title, body);
}

#endif // Q_OS_WIN
