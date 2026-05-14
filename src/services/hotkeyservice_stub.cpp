// No-op HotkeyService implementation for platforms without a native global
// hotkey API (currently: Android, Linux, macOS, WASM). Linked instead of
// hotkeyservice_win.cpp so main.cpp can stay platform-agnostic.
//
// All accessors return inert values; setters update the in-memory field but
// don't touch any OS API. wouldConflict() always returns false (nothing to
// conflict with).

#include "hotkeyservice.h"

HotkeyService::HotkeyService(QObject *parent)
    : QObject(parent)
    , m_shortcut(QStringLiteral("Ctrl+Alt+A"))
{}
HotkeyService::~HotkeyService() = default;

bool HotkeyService::registerQuickAdd() { return false; }
void HotkeyService::unregister() {}
bool HotkeyService::isRegistered() const { return false; }
QString HotkeyService::shortcut() const { return m_shortcut; }

bool HotkeyService::isValidShortcut(const QString &) const { return false; }

bool HotkeyService::setShortcut(const QString &shortcut)
{
    // Update the in-memory field so QML can still display the chosen combo,
    // but don't claim it's registered.
    if (m_shortcut == shortcut) return true;
    m_shortcut = shortcut;
    emit shortcutChanged();
    return false;
}

bool HotkeyService::wouldConflict(const QString &)
{
    // No native registration → nothing to conflict with on these platforms.
    return false;
}

bool HotkeyService::nativeEventFilter(const QByteArray &, void *, qintptr *)
{
    return false;
}
