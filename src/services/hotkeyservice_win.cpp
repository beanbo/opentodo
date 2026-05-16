#include "hotkeyservice.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QStringList>
#include <QWindow>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN

// Maps the textual portion of a shortcut (the part after the last "+") to a
// Win32 virtual-key code. Returns 0 for unknown / unsupported keys.
//
// Coverage: letters A-Z, digits 0-9, function keys F1-F24, plus a handful of
// common named keys (Space, Tab, Enter, ...). Anything else is rejected on
// purpose — RegisterHotKey would refuse it anyway, and accepting unknown
// names silently led to bad UX (set "F25" → silent failure → user thinks
// the binding is live).
UINT mainKeyVk(const QString &name)
{
    const QString k = name.trimmed().toUpper();
    if (k.isEmpty())
        return 0;

    // Single letter A-Z
    if (k.size() == 1) {
        const QChar c = k.at(0);
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            return static_cast<UINT>(c.toLatin1());        // VK 'A'..'Z'
        if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            return static_cast<UINT>(c.toLatin1());        // VK '0'..'9'
    }

    // Function keys F1..F24 (VK_F1 == 0x70, VK_F24 == 0x87)
    if (k.size() >= 2 && k.startsWith(QLatin1Char('F'))) {
        bool ok = false;
        const int n = k.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return static_cast<UINT>(VK_F1 + (n - 1));
    }

    // Named keys (the subset that's both useful and accepted by RegisterHotKey).
    static const QHash<QString, UINT> named = {
        { QStringLiteral("SPACE"),     VK_SPACE     },
        { QStringLiteral("TAB"),       VK_TAB       },
        { QStringLiteral("RETURN"),    VK_RETURN    },
        { QStringLiteral("ENTER"),     VK_RETURN    },
        { QStringLiteral("ESC"),       VK_ESCAPE    },
        { QStringLiteral("ESCAPE"),    VK_ESCAPE    },
        { QStringLiteral("BACKSPACE"), VK_BACK      },
        { QStringLiteral("DEL"),       VK_DELETE    },
        { QStringLiteral("DELETE"),    VK_DELETE    },
        { QStringLiteral("INS"),       VK_INSERT    },
        { QStringLiteral("INSERT"),    VK_INSERT    },
        { QStringLiteral("HOME"),      VK_HOME      },
        { QStringLiteral("END"),       VK_END       },
        { QStringLiteral("PGUP"),      VK_PRIOR     },
        { QStringLiteral("PAGEUP"),    VK_PRIOR     },
        { QStringLiteral("PGDOWN"),    VK_NEXT      },
        { QStringLiteral("PAGEDOWN"),  VK_NEXT      },
        { QStringLiteral("UP"),        VK_UP        },
        { QStringLiteral("DOWN"),      VK_DOWN      },
        { QStringLiteral("LEFT"),      VK_LEFT      },
        { QStringLiteral("RIGHT"),     VK_RIGHT     },
    };
    return named.value(k, 0u);
}

// Parses a portable shortcut string ("Ctrl+Alt+A") into Win32 modifier flags
// and a virtual-key code. Returns false if:
//   - the string is empty
//   - it contains zero modifiers
//   - it contains zero main keys (modifier-only, e.g. "Ctrl+Alt")
//   - the main key isn't known to mainKeyVk()
//   - it contains a token that isn't a modifier and isn't the trailing key
//
// Modifiers recognized: Ctrl/Control, Alt, Shift, Meta/Win/Super.
bool parseShortcut(const QString &shortcut, UINT *mods, UINT *vk)
{
    if (mods)  *mods = 0;
    if (vk)    *vk   = 0;

    const QString s = shortcut.trimmed();
    if (s.isEmpty())
        return false;

    const QStringList parts = s.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    if (parts.size() < 2)
        return false;      // need at least one modifier + one key
    // Reject trailing "+" e.g. "Ctrl+" → ["Ctrl", ""]
    for (const QString &p : parts)
        if (p.trimmed().isEmpty())
            return false;

    UINT m = 0;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString t = parts.at(i).trimmed().toUpper();
        if (t == QStringLiteral("CTRL") || t == QStringLiteral("CONTROL"))
            m |= MOD_CONTROL;
        else if (t == QStringLiteral("ALT"))
            m |= MOD_ALT;
        else if (t == QStringLiteral("SHIFT"))
            m |= MOD_SHIFT;
        else if (t == QStringLiteral("META") || t == QStringLiteral("WIN")
                 || t == QStringLiteral("SUPER"))
            m |= MOD_WIN;
        else
            return false;   // not a known modifier in a modifier slot
    }
    if (m == 0)
        return false;

    const UINT k = mainKeyVk(parts.last());
    if (k == 0)
        return false;       // main key unknown

    if (mods) *mods = m | MOD_NOREPEAT;
    if (vk)   *vk   = k;
    return true;
}

QString win32ErrorText(DWORD err)
{
    LPWSTR buf = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString reason = buf
        ? QString::fromWCharArray(buf).trimmed()
        : QStringLiteral("Error 0x%1").arg(err, 8, 16, QLatin1Char('0'));
    if (buf) LocalFree(buf);
    return reason;
}

// Returns the HWND we should attach the hotkey to. Empty when there is no
// top-level window yet (e.g. main.cpp calls registerQuickAdd() before the
// QML engine has created its window). RegisterHotKey works with nullptr
// just fine — WM_HOTKEY is then posted to the calling thread's queue,
// which our native event filter sees regardless.
HWND topLevelHwnd()
{
    const auto windows = QGuiApplication::topLevelWindows();
    if (!windows.isEmpty() && windows.first())
        return reinterpret_cast<HWND>(windows.first()->winId());
    return nullptr;
}

constexpr auto kSettingsKey = "hotkeys/quickAdd";
constexpr auto kDefaultShortcut = "Ctrl+Alt+A";

#endif // Q_OS_WIN

} // namespace

HotkeyService::HotkeyService(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // Restore the user's chosen shortcut from settings; fall back to the
    // documented default. This is consulted by registerQuickAdd() the next
    // time it runs, so changing it at startup is enough — no re-register.
    QSettings settings;
    m_shortcut = settings.value(QString::fromLatin1(kSettingsKey),
                                QString::fromLatin1(kDefaultShortcut))
                     .toString();
    if (m_shortcut.isEmpty())
        m_shortcut = QString::fromLatin1(kDefaultShortcut);

    QCoreApplication::instance()->installNativeEventFilter(this);
#else
    m_shortcut = QStringLiteral("Ctrl+Alt+A");
#endif
}

HotkeyService::~HotkeyService()
{
    unregister();
#ifdef Q_OS_WIN
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
}

bool HotkeyService::registerQuickAdd()
{
#ifdef Q_OS_WIN
    if (m_registered)
        unregister();

    UINT mods = 0, vk = 0;
    if (!parseShortcut(m_shortcut, &mods, &vk)) {
        const QString reason = QStringLiteral(
            "Сочетание клавиш '%1' некорректно (нужны модификатор + клавиша).")
            .arg(m_shortcut);
        qWarning() << "[HotkeyService] RegisterHotKey rejected:" << reason;
        emit registrationFailed(m_shortcut, reason);
        return false;
    }

    HWND hwnd = topLevelHwnd();
    BOOL ok = RegisterHotKey(hwnd, HOTKEY_ID, mods, vk);

    if (ok) {
        m_registered = true;
        emit registeredChanged();
    } else {
        const DWORD err = GetLastError();
        const QString reason = win32ErrorText(err);
        // ERROR_HOTKEY_ALREADY_REGISTERED (1409 / 0x581) is the common case
        // when another OpenTodo instance is running OR another app grabbed
        // the same combo. Demote to qInfo so the log isn't noisy during
        // multi-instance development; other errors stay qWarning.
        if (err == 1409 /* ERROR_HOTKEY_ALREADY_REGISTERED */) {
            qInfo() << "[HotkeyService]" << m_shortcut
                    << "is taken by another app or another instance — skipping registration ("
                    << reason << ")";
        } else {
            qWarning() << "[HotkeyService] RegisterHotKey failed for"
                       << m_shortcut << ":" << reason;
        }
        emit registrationFailed(m_shortcut, reason);
    }
    return m_registered;
#else
    return false;
#endif
}

void HotkeyService::unregister()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(topLevelHwnd(), HOTKEY_ID);
        m_registered = false;
        emit registeredChanged();
    }
#endif
}

bool HotkeyService::isRegistered() const
{
    return m_registered;
}

QString HotkeyService::shortcut() const
{
    return m_shortcut;
}

bool HotkeyService::isValidShortcut(const QString &shortcut) const
{
#ifdef Q_OS_WIN
    UINT mods = 0, vk = 0;
    return parseShortcut(shortcut, &mods, &vk);
#else
    Q_UNUSED(shortcut)
    return false;
#endif
}

bool HotkeyService::setShortcut(const QString &shortcut)
{
#ifdef Q_OS_WIN
    UINT mods = 0, vk = 0;
    if (!parseShortcut(shortcut, &mods, &vk)) {
        const QString reason = QStringLiteral(
            "Сочетание клавиш '%1' некорректно (нужны модификатор + клавиша).")
            .arg(shortcut);
        qWarning() << "[HotkeyService] setShortcut rejected:" << reason;
        emit registrationFailed(shortcut, reason);
        return false;
    }

    // No actual change → nothing to do. The combo is already parseable, so
    // we report success even if not currently registered.
    if (shortcut == m_shortcut) {
        // Idempotent: if currently registered, the binding stays. If not,
        // a subsequent registerQuickAdd() will pick this up.
        return true;
    }

    const bool wasRegistered = m_registered;
    if (wasRegistered)
        unregister();

    m_shortcut = shortcut;
    emit shortcutChanged();

    // Persist immediately so a crash before the next save still remembers
    // the user's choice. QSettings group is the global app one — main.cpp
    // configures it.
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingsKey), m_shortcut);

    if (wasRegistered) {
        // Re-attach the new binding. On failure registrationFailed is emitted
        // by registerQuickAdd() and m_registered stays false; the caller
        // gets a false return to know the new shortcut isn't actually live.
        return registerQuickAdd();
    }
    return true;
#else
    Q_UNUSED(shortcut)
    return false;
#endif
}

bool HotkeyService::wouldConflict(const QString &shortcut)
{
#ifdef Q_OS_WIN
    UINT mods = 0, vk = 0;
    if (!parseShortcut(shortcut, &mods, &vk))
        return true;  // invalid combos "conflict" — caller can't use them anyway

    // Probe under a dedicated id so we never collide with our own live
    // binding. RegisterHotKey is per-(hwnd, id), so even if the live
    // binding is on the same combo, the probe id discriminates them.
    HWND hwnd = topLevelHwnd();
    const BOOL probe = RegisterHotKey(hwnd, CONFLICT_PROBE_ID, mods, vk);
    if (probe) {
        UnregisterHotKey(hwnd, CONFLICT_PROBE_ID);
        return false;  // free
    }
    return true;       // taken
#else
    Q_UNUSED(shortcut)
    return false;
#endif
}

bool HotkeyService::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY) {
            if (static_cast<int>(msg->wParam) == HOTKEY_ID) {
                emit quickAddRequested();
                return true; // Event consumed
            }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif

    return false; // Pass event through
}
