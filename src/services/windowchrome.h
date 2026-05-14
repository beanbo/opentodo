#pragma once

#include <QObject>
#include <QQmlEngine>

// Tiny helper exposed to QML so Main.qml can switch the Windows title bar to
// dark mode when a dark theme is active. No-op on non-Windows platforms.
class WindowChrome : public QObject
{
    Q_OBJECT
    // Exposed as a QML context property in main.cpp; not a QML singleton because
    // QML_SINGLETON in this build flavour requires a non-default factory hook.

public:
    explicit WindowChrome(QObject *parent = nullptr);

    // Accept QObject* (instead of QWindow*) so QML can pass the
    // ApplicationWindow root without a Q_DECLARE_OPAQUE_POINTER dance.
    Q_INVOKABLE void setDarkTitleBar(QObject *window, bool dark);
};
