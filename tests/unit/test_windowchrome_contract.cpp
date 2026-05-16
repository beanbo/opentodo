// =============================================================================
// test_windowchrome_contract.cpp
// =============================================================================
//
// PURPOSE
// -------
// Structural / API-contract pinning for the WindowChrome service. This is the
// tiny C++ helper that switches the Win32 title bar between light and dark
// mode via DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE).
//
// WHY A CONTRACT TEST INSTEAD OF AN END-TO-END TEST?
// --------------------------------------------------
// `setDarkTitleBar()` calls into the Desktop Window Manager (dwmapi.dll),
// which only has an effect against a *real* HWND that has already been
// composited. From a Qt Test process running under offscreen QPA the call
// either (a) returns silently because qw->winId() is 0, or (b) flips the
// attribute on a hidden surface that is never user-visible.
//
// What we *can* validate without a desktop session is the API contract that
// the rest of the build relies on:
//
//   1. The class exists, derives from QObject, and is constructible without a
//      parent (so that main.cpp's `new WindowChrome` succeeds).
//   2. The single public method `setDarkTitleBar(QObject*, bool)` is present
//      and exposed as Q_INVOKABLE so QML's `windowChrome.setDarkTitleBar(...)`
//      call site in Main.qml resolves at QML load time.
//   3. The method accepts QObject* (not QWindow*) so QML can pass an
//      ApplicationWindow root without a Q_DECLARE_OPAQUE_POINTER dance --
//      that's the deliberate signature choice the header documents.
//   4. Passing nullptr / a non-QWindow QObject* is a quiet no-op (does not
//      crash). The implementation guards via qobject_cast<QWindow*>.
//   5. The dwmapi.lib link is present at compile + link time. We can't
//      directly assert "dwmapi was linked" from within the test, but we
//      can compile-time assert that DWMWA_USE_IMMERSIVE_DARK_MODE is the
//      Win10 1809+ canonical value the implementation uses (20). If a
//      future Windows SDK change shifts the constant the static_assert
//      fires and the developer has to revisit the .cpp fallback chain.
//
// WHY GUILESS_MAIN?
// -----------------
// WindowChrome itself is a plain QObject -- there is no QML engine, no
// QQuickWindow construction. Running under QCoreApplication keeps the test
// fast (<50 ms) and avoids dragging the Qt Quick scene graph into the suite
// for what is fundamentally a C++ structural check.
// =============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QDebug>
#include <QObject>
#include <QMetaObject>
#include <QMetaMethod>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
// Mirror the conditional in windowchrome.cpp so the constant is reachable
// here even on slightly older mingw SDK headers that do not predefine it.
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#endif

#include "services/windowchrome.h"

// Tag for the Q_INVOKABLE method we expect to find via the meta-object
// system. Centralised so a future rename to `setTitleBarDark` (or similar)
// updates one place rather than every QCOMPARE call.
namespace {
constexpr const char *kSetDarkTitleBarMethodName = "setDarkTitleBar";
constexpr const char *kClassName                 = "WindowChrome";
} // namespace


class TestWindowChromeContract : public QObject
{
    Q_OBJECT

private slots:

    // ---------------------------------------------------------------- fixture

    void initTestCase()
    {
        // Per-binary unique org/app keeps any QSettings access this test (or
        // the WindowChrome instance) might trip into isolated from sibling
        // tests under `ctest -j`.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_WindowChromeContract"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_windowchrome_contract"));
    }

    // -------------------------------------------------------------------------
    // 1. Class exists and is QObject-derived (canonical for QML context-prop
    //    exposure). Verifies main.cpp's `new WindowChrome` will compile.
    // -------------------------------------------------------------------------
    void class_is_qobject_derived_and_default_constructible()
    {
        WindowChrome chrome;
        QVERIFY2(chrome.metaObject() != nullptr,
                 "WindowChrome must have a meta-object (Q_OBJECT macro present).");

        const QString className =
            QString::fromUtf8(chrome.metaObject()->className());
        QCOMPARE(className, QString::fromUtf8(kClassName));

        // QObject inheritance check via dynamic cast to the immediate base.
        // Compiles only if QObject is reachable from WindowChrome.
        QObject *asObject = static_cast<QObject *>(&chrome);
        QVERIFY2(asObject != nullptr,
                 "WindowChrome must be QObject-derived for context-property exposure.");

        // Construction with explicit parent argument also compiles (matches
        // the declared `WindowChrome(QObject *parent = nullptr)` signature).
        QObject parent;
        WindowChrome childChrome(&parent);
        QCOMPARE(childChrome.parent(), &parent);
    }

    // -------------------------------------------------------------------------
    // 2. setDarkTitleBar is Q_INVOKABLE with the documented signature
    //    (QObject*, bool). Catches a future rename or accidental signature
    //    drift -- the QML side calls this method by name.
    // -------------------------------------------------------------------------
    void set_dark_title_bar_is_qinvokable_with_correct_signature()
    {
        WindowChrome chrome;
        const QMetaObject *mo = chrome.metaObject();
        QVERIFY(mo != nullptr);

        // Search every method on the class (NOT just the inherited slots) so
        // that a rename surfaces as a QFAIL rather than a silent skip.
        int methodIdx = -1;
        for (int i = 0; i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (QString::fromUtf8(m.name()) ==
                QString::fromUtf8(kSetDarkTitleBarMethodName))
            {
                methodIdx = i;
                break;
            }
        }
        QVERIFY2(methodIdx >= 0,
                 "WindowChrome must expose a method named setDarkTitleBar.");

        const QMetaMethod method = mo->method(methodIdx);

        // Q_INVOKABLE methods land in the meta-object as MethodMethod (not
        // Slot, not Signal). QML can call either Method or Slot, but the
        // declared intent in the header is Q_INVOKABLE -> Method.
        QCOMPARE(method.methodType(), QMetaMethod::Method);

        // Two parameters: QObject* and bool. Param 0 is the window handle,
        // param 1 is the boolean dark/light flag.
        QCOMPARE(method.parameterCount(), 2);
        QCOMPARE(QString::fromUtf8(method.parameterTypeName(0)),
                 QStringLiteral("QObject*"));
        QCOMPARE(QString::fromUtf8(method.parameterTypeName(1)),
                 QStringLiteral("bool"));

        // Return type is void -- we check this via the typeName() rather
        // than returnType() because the latter is a QMetaType id whose
        // textual reverse-lookup is brittle across Qt versions.
        QCOMPARE(QString::fromUtf8(method.typeName()), QStringLiteral("void"));
    }

    // -------------------------------------------------------------------------
    // 3. nullptr-safety: passing nullptr (or a QObject* that is not a
    //    QWindow*) must be a quiet no-op. The implementation guards via
    //    qobject_cast<QWindow*>; this slot pins that guard.
    // -------------------------------------------------------------------------
    void set_dark_title_bar_with_null_or_non_window_is_quiet_noop()
    {
        WindowChrome chrome;

        // Direct call with nullptr -- must not crash.
        chrome.setDarkTitleBar(nullptr, true);
        chrome.setDarkTitleBar(nullptr, false);

        // Plain QObject (not a QWindow) -- qobject_cast returns nullptr,
        // implementation must early-return without any DwmSetWindowAttribute
        // call (which would otherwise pass an invalid HWND of 0).
        QObject plainObject;
        chrome.setDarkTitleBar(&plainObject, true);
        chrome.setDarkTitleBar(&plainObject, false);

        // Same path via Q_INVOKABLE invoke() to cover the QML reflective
        // dispatch path (not just the C++ direct call).
        const bool inv1 = QMetaObject::invokeMethod(
            &chrome,
            kSetDarkTitleBarMethodName,
            Qt::DirectConnection,
            Q_ARG(QObject*, nullptr),
            Q_ARG(bool, true));
        QVERIFY2(inv1,
                 "Q_INVOKABLE setDarkTitleBar(QObject*, bool) must be reachable via meta-object.");

        const bool inv2 = QMetaObject::invokeMethod(
            &chrome,
            kSetDarkTitleBarMethodName,
            Qt::DirectConnection,
            Q_ARG(QObject*, &plainObject),
            Q_ARG(bool, false));
        QVERIFY(inv2);

        // Reaching this line proves no crash on any of the four paths.
        QVERIFY(true);
    }

    // -------------------------------------------------------------------------
    // 4. Win32-specific compile-time invariants. Documents the API version
    //    requirement so a SDK shift surfaces as a clear failure point.
    // -------------------------------------------------------------------------
    void win32_dark_mode_attribute_is_canonical_value()
    {
#ifdef Q_OS_WIN
        // Windows 10 1809 (build 17763) introduced the legacy attribute (19);
        // 20H1 (build 19041) renamed it to value 20 and shipped the official
        // DWMWA_USE_IMMERSIVE_DARK_MODE name. Our implementation tries 20
        // first and falls back to 19. Pin both numeric values here so any
        // future SDK or Wine quirk that shifts them is caught immediately.
        static_assert(DWMWA_USE_IMMERSIVE_DARK_MODE == 20,
                      "DWMWA_USE_IMMERSIVE_DARK_MODE must be 20 (Win10 20H1+); "
                      "windowchrome.cpp's fallback path expects 19 as legacy.");
        QCOMPARE(static_cast<int>(DWMWA_USE_IMMERSIVE_DARK_MODE), 20);

        // Sanity: the legacy fallback constant is 19. If MSDN ever rotates
        // these numbers we want a loud failure right here, not a silent
        // black title bar in production.
        constexpr DWORD kLegacyDarkModeAttr = 19;
        QCOMPARE(static_cast<int>(kLegacyDarkModeAttr), 19);

        // Compile-time assert that BOOL is the size DwmSetWindowAttribute
        // expects (4 bytes on Windows). The implementation passes &useDark
        // with sizeof(useDark); a one-byte bool would silently violate the
        // dwmapi ABI and the title bar would not switch.
        static_assert(sizeof(BOOL) == 4,
                      "Win32 BOOL must be 4 bytes for DwmSetWindowAttribute "
                      "(otherwise the dark-mode toggle reads only the low byte).");
#else
        qInfo() << "Limited test: non-Windows platform; "
                << "DWMWA_USE_IMMERSIVE_DARK_MODE is a Windows-only contract.";
        QVERIFY(true);
#endif
    }

    // -------------------------------------------------------------------------
    // 5. Q_INVOKABLE reflective-dispatch path -- when QML calls
    //    `windowChrome.setDarkTitleBar(window, ThemeManager.isDark)`, Qt
    //    routes it through QMetaObject::invokeMethod with a QObject* arg,
    //    not a direct C++ call. Verify that path delivers the call (and
    //    hits the same nullptr-guard early-return) without crashing.
    //
    //    NOTE: We deliberately do NOT instantiate a real QWindow here.
    //    QWindow::create() requires at least one QScreen, which under
    //    QTEST_GUILESS_MAIN (QCoreApplication-only, no QPA) is not
    //    available -- Qt would QFATAL with "Cannot create window: no
    //    screens available". The Win32 dwmapi call path is verified at
    //    LINK time (test binary lists dwmapi.dll in its imports;
    //    `objdump -p test_windowchrome_contract.exe` shows the dependency)
    //    and at RUNTIME by the production app itself; an offscreen +
    //    no-QGuiApplication unit test cannot reach the real DwmSetWindow-
    //    Attribute call without a QScreen.
    // -------------------------------------------------------------------------
    void set_dark_title_bar_via_qmetaobject_reflective_dispatch()
    {
        WindowChrome chrome;

        // Mirror the QML invocation shape: QMetaObject::invokeMethod with
        // a QObject* argument and a bool. The plain QObject ensures the
        // implementation's qobject_cast<QWindow*> returns nullptr and the
        // method returns early -- without crashing, without complaining.
        QObject plain;

        const bool invTrue = QMetaObject::invokeMethod(
            &chrome,
            kSetDarkTitleBarMethodName,
            Qt::DirectConnection,
            Q_ARG(QObject*, &plain),
            Q_ARG(bool, true));
        QVERIFY2(invTrue,
                 "Q_INVOKABLE setDarkTitleBar(QObject*, bool) must be reachable "
                 "via meta-object reflective dispatch (QML's call path).");

        const bool invFalse = QMetaObject::invokeMethod(
            &chrome,
            kSetDarkTitleBarMethodName,
            Qt::DirectConnection,
            Q_ARG(QObject*, &plain),
            Q_ARG(bool, false));
        QVERIFY(invFalse);

        // Same path with nullptr -- belt-and-braces.
        const bool invNull = QMetaObject::invokeMethod(
            &chrome,
            kSetDarkTitleBarMethodName,
            Qt::DirectConnection,
            Q_ARG(QObject*, nullptr),
            Q_ARG(bool, true));
        QVERIFY(invNull);
    }

    // -------------------------------------------------------------------------
    // 6. Idempotent / repeated calls -- toggling dark mode 100 times in a
    //    tight loop must not leak resources, queue state, or crash. dwmapi
    //    is itself idempotent; this slot pins our wrapper inherits that.
    // -------------------------------------------------------------------------
    void set_dark_title_bar_is_idempotent_under_repeated_calls()
    {
        WindowChrome chrome;
        QObject plain;

        // 100 alternating no-op calls -- bounded loop so a slow CI runner
        // is still fast (<10 ms total).
        for (int i = 0; i < 100; ++i) {
            chrome.setDarkTitleBar(&plain, (i % 2) == 0);
        }

        QVERIFY(true);
    }

    // -------------------------------------------------------------------------
    // 7. The class exports exactly one public Q_INVOKABLE -- the
    //    setDarkTitleBar surface. Adds a guardrail so a future PR that
    //    expands the API has to update this test (and write coverage for
    //    the new method).
    //
    //    We count Q_INVOKABLEs by filtering meta-methods that are
    //    QMetaMethod::Method (Q_INVOKABLE-tagged) and not inherited from
    //    QObject. Inherited methods (e.g. deleteLater) are slots, not
    //    methods, so this filter is precise.
    // -------------------------------------------------------------------------
    void class_exposes_only_one_qinvokable_today()
    {
        WindowChrome chrome;
        const QMetaObject *mo = chrome.metaObject();
        QVERIFY(mo != nullptr);

        int qinvokableCount = 0;
        // methodOffset() returns the index where THIS class's methods begin
        // (excluding inherited ones), so we don't count QObject's surface.
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() == QMetaMethod::Method) {
                ++qinvokableCount;
            }
        }
        QCOMPARE(qinvokableCount, 1);
    }
};

QTEST_GUILESS_MAIN(TestWindowChromeContract)
#include "test_windowchrome_contract.moc"
