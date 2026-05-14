#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QString>

// Global Quick-Add hotkey service.
//
// On Windows the active shortcut is bound via RegisterHotKey() and delivered
// through a WM_HOTKEY native event filter. On other platforms the stub
// implementation in hotkeyservice_stub.cpp turns every call into a no-op.
//
// The default shortcut is Ctrl+Alt+A. The user can rebind it at runtime via
// setShortcut(), and probe alternatives with wouldConflict() before saving.
// All public methods are safe to call from QML.
class HotkeyService : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)
    Q_PROPERTY(QString shortcut READ shortcut WRITE setShortcutProp
               NOTIFY shortcutChanged)
public:
    explicit HotkeyService(QObject *parent = nullptr);
    ~HotkeyService() override;

    // Registers the current shortcut (defaults to Ctrl+Alt+A). Returns true
    // on success. On failure, emits registrationFailed(shortcut, reason)
    // with a localized Win32 message.
    Q_INVOKABLE bool registerQuickAdd();
    void unregister();
    bool isRegistered() const;

    // Currently configured shortcut, in QKeySequence::PortableText form
    // (e.g. "Ctrl+Alt+A"). Initialized from QSettings ("hotkeys/quickAdd")
    // and persisted on every successful setShortcut().
    QString shortcut() const;

    // Validate + apply a new shortcut. Returns true if:
    //   1. The string parses to (>=1 modifier) + (1 main key), AND
    //   2. The new combo registers successfully (Win32), OR the service was
    //      not previously registered (in which case we just update the field).
    // On failure emits registrationFailed(shortcut, reason). The previous
    // binding is always released first so the caller can never end up with
    // two live hotkeys.
    Q_INVOKABLE bool setShortcut(const QString &shortcut);

    // Probe whether the given shortcut would register successfully RIGHT NOW.
    // Implementation: parse → temporarily RegisterHotKey under a sentinel id →
    // immediately UnregisterHotKey → return whether the call succeeded.
    // Does not disturb the live registration. Returns true if the combo is
    // already taken (i.e. would conflict), false if it is free or invalid.
    // Invalid strings (modifier-only, empty, unparseable) return true too —
    // setShortcut() would also reject them, so they "conflict" from the
    // caller's point of view.
    Q_INVOKABLE bool wouldConflict(const QString &shortcut);

    // Pure validation: does the string parse into a usable (modifier + key)
    // combo? Used by the settings UI to gray out "Save" before even trying
    // to register. Does not touch the OS.
    Q_INVOKABLE bool isValidShortcut(const QString &shortcut) const;

    // QAbstractNativeEventFilter
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void quickAddRequested();
    void registeredChanged();
    void shortcutChanged();
    // Emitted when RegisterHotKey fails (Windows only).
    // shortcut is the combo that failed (e.g. "Ctrl+Alt+A"); reason contains
    // the Win32 FormatMessage text for GetLastError() (or an internal
    // explanation like "Сочетание содержит только модификаторы").
    void registrationFailed(const QString &shortcut, const QString &reason);

private:
    // Property-setter trampoline (Q_PROPERTY WRITE needs a void slot, not bool).
    void setShortcutProp(const QString &s) { setShortcut(s); }

    bool m_registered = false;
    QString m_shortcut;
    static constexpr int HOTKEY_ID = 1;
    static constexpr int CONFLICT_PROBE_ID = 2;  // sentinel for wouldConflict
};
