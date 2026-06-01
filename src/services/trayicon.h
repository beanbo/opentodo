#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

class QSystemTrayIcon;
class QMenu;
class QAction;

// Desktop-only system tray icon with a context menu offering quick access to
// the main app actions (Open, Quick Add, Settings, Quit).
//
// The tray icon is the canonical Windows "always-available" surface — it
// stays alive even when the main window is hidden, so the user can still
// trigger Quick Add or raise the window from the system notification area
// without needing the global hotkey.
//
// The class itself only owns the QSystemTrayIcon + QMenu and exposes
// signals; main.cpp wires those signals to the existing ViewModel slots so
// the tray menu and the global hotkey share the same entry points.
//
// Behavior is gated on QSystemTrayIcon::isSystemTrayAvailable() — on
// minimal Linux DEs without a system-tray extension (Wayland under GNOME
// Shell, some headless test environments) the constructor silently no-ops
// and the public toggle becomes a no-op.
//
// Excluded entirely from Android and WASM builds at the CMake level.
class TrayIconService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)

public:
    explicit TrayIconService(QObject *parent = nullptr);
    ~TrayIconService() override;

    // True when the host platform exposes a working system tray. False on
    // Wayland sessions without a tray extension, in some CI containers, and
    // any platform where QSystemTrayIcon::isSystemTrayAvailable() returns
    // false at construction time.
    bool isAvailable() const { return m_trayIcon != nullptr; }

    // Expose the single underlying QSystemTrayIcon so the platform
    // NotificationService can anchor its toast/balloon messages on THIS icon
    // instead of registering a second QSystemTrayIcon of its own — two live
    // icons showed up as a duplicate entry in the Windows notification area.
    // Returns nullptr when no system tray is available (isAvailable() == false).
    QSystemTrayIcon *systemTrayIcon() const { return m_trayIcon; }

    // Whether the tray icon is currently shown in the notification area.
    // Setting to false hides the icon (used by the showTrayIcon setting
    // toggle); setting to true re-shows it. A no-op if isAvailable() is
    // false.
    bool isVisible() const;
    void setVisible(bool visible);

    // Refresh the tray icon's tooltip — e.g. when the pending-task count
    // changes. main.cpp is free to call this whenever it wants the user to
    // see a fresh number on hover. No-op when isAvailable() is false.
    void setToolTip(const QString &tip);

    // Re-translate the menu action labels (Open / Quick Add / Settings /
    // Quit). Call after the user changes the UI language at runtime — the
    // tr() strings are captured at action-construction time and would
    // otherwise stay in the original locale until next launch.
    void retranslate();

    // ── Badge overlay ──────────────────────────────────────────────────────
    //
    // The tray icon can paint a small circular badge in the bottom-right
    // corner of the base icon to surface pending notifications / overdue
    // tasks at a glance. The badge is tinted with the user's accent color
    // (setBadgeAccentColor) and only shown when the count is > 0
    // (setBadgeCount). When the count returns to 0 the base icon is shown
    // untouched.
    //
    // Both setters are idempotent: setting the same color/count again is a
    // no-op (so it is safe to call from a NOTIFY without rate-limiting).
    // The actual QPixmap composite happens on the next refresh — at most
    // once per call to either setter. No-op when isAvailable() is false.
    void setBadgeAccentColor(const QColor &color);
    void setBadgeCount(int count);
    QColor badgeAccentColor() const { return m_badgeColor; }
    int    badgeCount() const       { return m_badgeCount; }

signals:
    // Fired when the user selects "Open OpenTodo" from the context menu,
    // double-clicks the tray icon, or single-clicks it (Trigger reason).
    // main.cpp connects this to the QML root window's raise/activate.
    void openWindowRequested();

    // Fired when the user selects "Quick Add..." from the context menu.
    // main.cpp connects this to the same QML wiring used by the global
    // hotkey so both entry points behave identically.
    void quickAddRequested();

    // Fired when the user selects "Settings" from the context menu.
    // main.cpp connects this to SidebarViewModel::navigateTo("settings").
    void settingsRequested();

    // Fired when the user selects "Quit" from the context menu. main.cpp
    // connects this to QCoreApplication::quit() so the app shuts down
    // cleanly through aboutToQuit (autosave + sync flush still fire).
    void quitRequested();

    void visibleChanged();

private:
    void buildMenu();
    void wireActivationSignal();

    // Repaint the tray icon from the cached base icon + current badge state.
    // Called from setBadgeAccentColor / setBadgeCount when their respective
    // inputs actually change. When m_badgeCount <= 0 the base icon is shown
    // without any overlay, restoring the original Windows tray appearance.
    void refreshIcon();

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu           *m_menu     = nullptr;
    QAction         *m_openAction     = nullptr;
    QAction         *m_quickAddAction = nullptr;
    QAction         *m_settingsAction = nullptr;
    QAction         *m_quitAction     = nullptr;

    // Base (badge-free) icon, captured once at construction. We rebuild the
    // badged variant from this every time, so successive recolors do not
    // accumulate ghost overlays on top of each other.
    QIcon  m_baseIcon;
    QColor m_badgeColor = QColor(QStringLiteral("#db4c3f")); // sane default
    int    m_badgeCount = 0;
};
