#include "windowchrome.h"

#include <QQuickWindow>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
// DWMWA_USE_IMMERSIVE_DARK_MODE was 19 in earlier Win10 builds, 20 from
// 20H1 onwards. We try the newer one first and fall back automatically.
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#endif

WindowChrome::WindowChrome(QObject *parent) : QObject(parent) {}

void WindowChrome::setDarkTitleBar(QObject *window, bool dark)
{
    auto *qw = qobject_cast<QWindow *>(window);
    if (!qw) return;

#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(qw->winId());
    if (!hwnd) return;

    BOOL useDark = dark ? TRUE : FALSE;
    // Try the newer attribute (Windows 10 20H1+ / Windows 11) first.
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                       &useDark, sizeof(useDark));
    if (FAILED(hr)) {
        // Fallback for early Windows 10 builds (1809–1909).
        const DWORD legacyAttr = 19;
        DwmSetWindowAttribute(hwnd, legacyAttr, &useDark, sizeof(useDark));
    }
#else
    Q_UNUSED(dark);
#endif
}
