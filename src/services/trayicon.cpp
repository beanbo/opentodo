#include "trayicon.h"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QSystemTrayIcon>

// TrayIconService is desktop-only at the CMake layer (excluded on Android
// and WASM). We still guard the runtime construction on
// QSystemTrayIcon::isSystemTrayAvailable() so that minimal Linux desktops
// (Wayland w/o tray extension, CI containers) degrade to a no-op service
// rather than spawning an icon that nothing can render.

TrayIconService::TrayIconService(QObject *parent)
    : QObject(parent)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        // No tray on this platform — leave m_trayIcon null. isAvailable()
        // returns false; every public method is a no-op; setVisible(true)
        // will not magically conjure an icon. The caller can still expose
        // the service as a context property without breakage.
        return;
    }

    // Reuse the multi-resolution app icon. Qt picks the right size for the
    // current OS scale factor automatically because QIcon below carries
    // both the SVG and the rendered PNGs (16/32/48/64/128/256/512). The
    // 32 px PNG is what Windows actually paints in the notification area
    // at 100 % scaling.
    //
    // We cache the icon on m_baseIcon so the badge composite path can
    // always rebuild from a clean source — repainting on top of an already
    // badged pixmap would let the colored circles accumulate every time
    // the user changed accent.
    m_baseIcon = QIcon();
    m_baseIcon.addFile(QStringLiteral(":/icons/resources/icon.svg"));
    for (int size : {16, 32, 48, 64, 128, 256, 512})
        m_baseIcon.addFile(QStringLiteral(":/icons/resources/icons/icon_%1.png").arg(size));

    m_trayIcon = new QSystemTrayIcon(m_baseIcon, this);
    m_trayIcon->setToolTip(QStringLiteral("OpenTodo"));

    buildMenu();
    wireActivationSignal();

    // Tray icons are hidden by default until the parent decides to show
    // them — main.cpp consults SettingsViewModel::showTrayIcon at startup
    // and calls setVisible(true) when the user has it enabled.
}

TrayIconService::~TrayIconService()
{
    // m_menu was created with `new QMenu()` (no parent — QMenu is a QWidget
    // and we have no widget context to anchor it to). Delete it explicitly
    // so it doesn't leak across the app lifetime. QSystemTrayIcon does NOT
    // take ownership of the menu it's handed via setContextMenu, so this
    // is our responsibility. Order matters: clear the context menu on the
    // tray icon first so it can't try to redraw while the menu is being
    // torn down.
    if (m_trayIcon) {
        m_trayIcon->setContextMenu(nullptr);
        m_trayIcon->hide();
    }
    delete m_menu;
    m_menu = nullptr;
}

void TrayIconService::buildMenu()
{
    m_menu = new QMenu();   // top-level menu, parented to no widget

    m_openAction     = m_menu->addAction(tr("Open OpenTodo"));
    m_quickAddAction = m_menu->addAction(tr("Quick Add..."));
    // Display the global hotkey on the menu item so the user can see the
    // keyboard alternative. QSystemTrayIcon/QMenu does NOT invoke this on
    // press (the binding is handled separately by HotkeyService); we only
    // surface it for discoverability.
    m_quickAddAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+A")));

    m_menu->addSeparator();
    m_settingsAction = m_menu->addAction(tr("Settings"));

    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(tr("Quit"));

    // Translate the QAction::triggered() signals to the public signals the
    // caller wires to its ViewModels. Keeping the indirection on this side
    // keeps main.cpp free of QtWidgets includes.
    connect(m_openAction,     &QAction::triggered, this, &TrayIconService::openWindowRequested);
    connect(m_quickAddAction, &QAction::triggered, this, &TrayIconService::quickAddRequested);
    connect(m_settingsAction, &QAction::triggered, this, &TrayIconService::settingsRequested);
    connect(m_quitAction,     &QAction::triggered, this, &TrayIconService::quitRequested);

    m_trayIcon->setContextMenu(m_menu);
}

void TrayIconService::wireActivationSignal()
{
    // Trigger == single click (Linux only; Windows does nothing on single
    // click for tray icons), DoubleClick == double click (Windows default
    // gesture for "open the main window"). MiddleClick / Context handled
    // by QMenu itself, so we don't need branches for them.
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick ||
            reason == QSystemTrayIcon::Trigger) {
            emit openWindowRequested();
        }
    });
}

bool TrayIconService::isVisible() const
{
    return m_trayIcon && m_trayIcon->isVisible();
}

void TrayIconService::setVisible(bool visible)
{
    if (!m_trayIcon)
        return;
    if (m_trayIcon->isVisible() == visible)
        return;
    if (visible)
        m_trayIcon->show();
    else
        m_trayIcon->hide();
    emit visibleChanged();
}

void TrayIconService::setToolTip(const QString &tip)
{
    if (m_trayIcon)
        m_trayIcon->setToolTip(tip);
}

void TrayIconService::retranslate()
{
    if (!m_menu) return;
    if (m_openAction)     m_openAction->setText(tr("Open OpenTodo"));
    if (m_quickAddAction) m_quickAddAction->setText(tr("Quick Add..."));
    if (m_settingsAction) m_settingsAction->setText(tr("Settings"));
    if (m_quitAction)     m_quitAction->setText(tr("Quit"));
}

// ─── Badge overlay ──────────────────────────────────────────────────────────

void TrayIconService::setBadgeAccentColor(const QColor &color)
{
    if (!m_trayIcon)
        return;
    if (!color.isValid())
        return;
    if (color == m_badgeColor)
        return;
    m_badgeColor = color;
    // Only repaint if a badge is actually visible — recoloring while the
    // badge is hidden would be a wasted composite.
    if (m_badgeCount > 0)
        refreshIcon();
}

void TrayIconService::setBadgeCount(int count)
{
    if (!m_trayIcon)
        return;
    // Clamp negatives to zero so callers don't have to: an underflowing
    // unreadCount() from a buggy VM should still resolve to "no badge".
    if (count < 0)
        count = 0;
    if (count == m_badgeCount)
        return;
    m_badgeCount = count;
    refreshIcon();
}

void TrayIconService::refreshIcon()
{
    if (!m_trayIcon)
        return;

    // count == 0: restore the original badge-free icon by handing the cached
    // multi-resolution QIcon back to the tray. No QPainter needed.
    if (m_badgeCount <= 0) {
        m_trayIcon->setIcon(m_baseIcon);
        return;
    }

    // Compose the badged variant from a 32 px base — this is the size
    // Windows actually paints at 100 % DPI. The OS rescales as needed for
    // higher DPI bars; the alternative (composing at every size and
    // attaching them all to a QIcon) would marginally improve hi-DPI
    // sharpness but cost 7× the QPainter work on every badge update.
    constexpr int kSize = 32;
    QPixmap pixmap = m_baseIcon.pixmap(kSize, kSize);
    if (pixmap.isNull())
        return; // resource genuinely missing; nothing to overlay on

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    // Badge geometry: 14 px circle anchored at the bottom-right with a 1 px
    // gap from the icon edge. Centre at (24, 24); radius 7; outer bounding
    // box (17, 17, 14, 14). The values were picked to leave the app glyph
    // visually intact at the canonical 32 px tray size while still being
    // big enough to read a 1- or 2-digit count.
    constexpr qreal kBadgeDiameter = 14.0;
    const QRectF badgeRect(kSize - kBadgeDiameter - 1.0,
                           kSize - kBadgeDiameter - 1.0,
                           kBadgeDiameter,
                           kBadgeDiameter);

    // Soft white halo so the badge stays visible against both dark and
    // light icons. 1.5 px outer pad on every side.
    p.setBrush(QColor(255, 255, 255, 230));
    p.setPen(Qt::NoPen);
    p.drawEllipse(badgeRect.adjusted(-1.5, -1.5, 1.5, 1.5));

    // The accent-tinted disc itself.
    p.setBrush(m_badgeColor);
    p.drawEllipse(badgeRect);

    // Numeric count, white-on-accent. Clamp display at "9+" so we never
    // overflow the 14 px disc with a 3-digit number — anything beyond
    // single-digit is "lots of stuff to look at" anyway.
    const QString label = m_badgeCount > 9
                              ? QStringLiteral("9+")
                              : QString::number(m_badgeCount);

    QFont font = QApplication::font();
    font.setBold(true);
    // Pick a pixel size that fits inside the disc. 9 px for one char,
    // 7 px for the two-char "9+" so it doesn't clip.
    font.setPixelSize(label.size() == 1 ? 9 : 7);
    p.setFont(font);

    // Foreground: pick whichever of black/white contrasts more with the
    // badge color so a pale accent (e.g. sunflower yellow) doesn't end up
    // with white-on-yellow text. Standard luminance heuristic.
    const qreal luminance = 0.299 * m_badgeColor.redF()
                          + 0.587 * m_badgeColor.greenF()
                          + 0.114 * m_badgeColor.blueF();
    p.setPen(luminance > 0.6 ? Qt::black : Qt::white);
    p.drawText(badgeRect, Qt::AlignCenter, label);

    p.end();

    m_trayIcon->setIcon(QIcon(pixmap));
}
