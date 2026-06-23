#pragma once

#include <QObject>
#include <QQmlEngine>

// HapticService — short tactile feedback for touch interactions.
//
// On Android it drives the system Vibrator via VibrationEffect.createOneShot
// (minSdk 28 > API 26, so always available; the VIBRATE permission is declared
// in android/AndroidManifest.xml). On desktop / WASM every method is a no-op.
//
// Exposed to QML as the `haptics` context property; QML calls
// haptics.light()/medium()/heavy()/selection() from gestures and key actions.
// All Android JNI calls are exception-guarded — a haptic failure can never
// crash the app (worst case: no vibration).
class HapticService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("HapticService is exposed via the `haptics` context property.")

    // Lets QML / a future setting disable haptics wholesale without threading a
    // check through every call site.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit HapticService(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // Light tap — checkbox toggle, chip selection, small confirmations.
    Q_INVOKABLE void light();
    // Medium — swipe-complete, long-press fire, sheet snap.
    Q_INVOKABLE void medium();
    // Heavy — destructive / bulk actions (delete, bulk-complete).
    Q_INVOKABLE void heavy();
    // Selection tick — lighter than light(), for scrubbing / iterating.
    Q_INVOKABLE void selection();

signals:
    void enabledChanged();

private:
    void vibrate(int ms, int amplitude);
    bool m_enabled = true;
};
