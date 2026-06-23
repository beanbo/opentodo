#include "hapticservice.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

HapticService::HapticService(QObject *parent)
    : QObject(parent)
{
}

void HapticService::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    emit enabledChanged();
}

void HapticService::light()     { vibrate(12, 80); }
void HapticService::medium()    { vibrate(20, 150); }
void HapticService::heavy()     { vibrate(34, 230); }
void HapticService::selection() { vibrate(8, 60); }

void HapticService::vibrate(int ms, int amplitude)
{
    if (!m_enabled)
        return;

#ifdef Q_OS_ANDROID
    // minSdk is 28 (> API 26), so VibrationEffect.createOneShot(ms, amplitude)
    // and Vibrator.vibrate(VibrationEffect) are always available. Every JNI call
    // is guarded: any pending Java exception is cleared so a haptic failure can
    // never propagate / crash — the worst case is simply no vibration.
    QJniEnvironment env;
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;

    const QJniObject vibratorService = QJniObject::getStaticObjectField(
        "android/content/Context", "VIBRATOR_SERVICE", "Ljava/lang/String;");
    if (env.checkAndClearExceptions() || !vibratorService.isValid())
        return;

    QJniObject vibrator = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        vibratorService.object<jstring>());
    if (env.checkAndClearExceptions() || !vibrator.isValid())
        return;

    const QJniObject effect = QJniObject::callStaticObjectMethod(
        "android/os/VibrationEffect", "createOneShot",
        "(JI)Landroid/os/VibrationEffect;",
        static_cast<jlong>(ms), static_cast<jint>(amplitude));
    if (env.checkAndClearExceptions() || !effect.isValid())
        return;

    vibrator.callMethod<void>("vibrate", "(Landroid/os/VibrationEffect;)V",
                              effect.object());
    env.checkAndClearExceptions();
#else
    Q_UNUSED(ms);
    Q_UNUSED(amplitude);
#endif
}
