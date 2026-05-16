// Smoke tests for HotkeyService.
//
// HotkeyService exposes a deliberately tiny C++ surface: a parameterless
// registerQuickAdd() that hard-codes Ctrl+Alt+A on Windows, plus an
// unregister(), an isRegistered() Q_PROPERTY, and three signals
// (quickAddRequested, registeredChanged, registrationFailed). There is no
// public setShortcut(QKeySequence) / enabled property — the task spec mentions
// those, but the production code does not have them. We test what is
// reachable through the real API and call out the rest with qInfo().
//
// We deliberately do NOT inject real WM_HOTKEY events via SendInput / PostMessage
// — that would race the developer's foreground app and risk firing whatever
// real Ctrl+Alt+A binding might exist. Instead we drive the public API and the
// registered-state machine directly, and synthesize a WM_HOTKEY MSG into the
// nativeEventFilter() to verify the quickAddRequested signal path.

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "services/hotkeyservice.h"

class TestHotkeyService : public QObject
{
    Q_OBJECT

private slots:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void default_ctor_does_not_crash_and_is_unregistered()
    {
        HotkeyService svc;
        // On every platform, freshly-constructed → not registered.
        QCOMPARE(svc.isRegistered(), false);
        // Q_PROPERTY visible via meta-object (sanity: catches accidental rename).
        const int idx = svc.metaObject()->indexOfProperty("registered");
        QVERIFY(idx >= 0);
        QCOMPARE(svc.metaObject()->property(idx).read(&svc).toBool(), false);
    }

    // ── registerQuickAdd: behavior on each platform ───────────────────────────

    void register_quick_add_returns_bool_matching_property()
    {
        HotkeyService svc;
        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);
        QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);

        const bool ok = svc.registerQuickAdd();
        // Whichever way it goes, the bool must agree with the property.
        QCOMPARE(svc.isRegistered(), ok);

#ifdef Q_OS_WIN
        if (ok) {
            // Successful registration → registeredChanged fired exactly once.
            QCOMPARE(regSpy.count(), 1);
            QCOMPARE(failSpy.count(), 0);
        } else {
            // Registration failure path → registrationFailed fires with a
            // non-empty Win32 reason string. We allow either ordering wrt
            // registeredChanged (current impl only emits on success, so the
            // expected count is 0).
            QCOMPARE(regSpy.count(), 0);
            QVERIFY(failSpy.count() >= 1);
            // Signal is registrationFailed(shortcut, reason); reason is value(1).
            const QString shortcut = failSpy.first().value(0).toString();
            const QString reason   = failSpy.first().value(1).toString();
            QVERIFY2(!shortcut.isEmpty(),
                     "registrationFailed should name the shortcut that failed");
            QVERIFY2(!reason.isEmpty(),
                     "registrationFailed should carry a reason");
        }
#else
        // Stub impl always returns false, never registers, never emits.
        QCOMPARE(ok, false);
        QCOMPARE(regSpy.count(), 0);
        QCOMPARE(failSpy.count(), 0);
        qInfo() << "Limited test: non-Windows stub returns false unconditionally;"
                << "registration state-machine paths are Windows-only.";
#endif
    }

    // ── Re-registration releases prior binding ───────────────────────────────

    void reregister_releases_prior_binding()
    {
        HotkeyService svc;

        const bool first = svc.registerQuickAdd();
        // Calling registerQuickAdd a second time should not leak and not crash.
        // The implementation explicitly calls unregister() before re-binding,
        // so on Windows-success path we should still end up registered.
        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);

        const bool second = svc.registerQuickAdd();
        QCOMPARE(svc.isRegistered(), second);

#ifdef Q_OS_WIN
        if (first && second) {
            // First call had taken the binding; re-register goes through
            // unregister()→false (1 emit) then re-register→true (1 emit).
            QCOMPARE(regSpy.count(), 2);
        } else if (!first && !second) {
            // Both attempts failed (e.g. environment without a message pump
            // that still lets the API succeed). At minimum: did not crash.
            qInfo() << "Limited test: RegisterHotKey unavailable in this env;"
                    << "re-register path verified only at the API level.";
        }
#else
        Q_UNUSED(first); Q_UNUSED(second);
        QCOMPARE(svc.isRegistered(), false);
#endif
    }

    // ── unregister() flips the state and re-emits registeredChanged ──────────

    void unregister_clears_state_when_registered()
    {
        HotkeyService svc;
        const bool ok = svc.registerQuickAdd();

        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);

#ifdef Q_OS_WIN
        if (ok) {
            // On a successful registration, unregister fires the change once.
            QCOMPARE(regSpy.count(), 1);
        } else {
            // No-op: nothing was registered, nothing changes.
            QCOMPARE(regSpy.count(), 0);
        }
#else
        Q_UNUSED(ok);
        QCOMPARE(regSpy.count(), 0);
#endif
    }

    // ── Destructor releases the hotkey (no leak) ──────────────────────────────

    void destructor_releases_hotkey_without_dangling_filter()
    {
        // After destruction, constructing a fresh instance and registering
        // again should not be blocked by a leaked binding from the previous
        // instance. We guard with a scope.
        bool firstOk = false;
        {
            HotkeyService svc;
            firstOk = svc.registerQuickAdd();
            QCOMPARE(svc.isRegistered(), firstOk);
            // Going out of scope should call unregister() and remove the
            // native event filter (Windows path).
        }

        // Spin the event loop briefly so any deferred deletion can run.
        QCoreApplication::processEvents();

        HotkeyService svc2;
        QCOMPARE(svc2.isRegistered(), false);
        const bool secondOk = svc2.registerQuickAdd();
        // If the first registration succeeded, the second one MUST also be
        // able to claim the same Ctrl+Alt+A binding — proving the previous
        // instance released it on destruction.
#ifdef Q_OS_WIN
        if (firstOk) {
            QVERIFY2(secondOk,
                     "Second registration after destructor should succeed; "
                     "this implies the prior binding was released.");
        } else {
            qInfo() << "Limited test: prior registration failed; cannot prove"
                    << "leak-free release. Verified that second ctor + register"
                    << "did not crash.";
        }
#else
        Q_UNUSED(firstOk); Q_UNUSED(secondOk);
#endif
    }

    // ── nativeEventFilter: WM_HOTKEY → quickAddRequested ──────────────────────

    void native_event_filter_emits_quick_add_on_wm_hotkey()
    {
        HotkeyService svc;
        QSignalSpy spy(&svc, &HotkeyService::quickAddRequested);

#ifdef Q_OS_WIN
        // Synthesize a WM_HOTKEY message with wParam == HOTKEY_ID (==1, the
        // private constant in hotkeyservice.h). nativeEventFilter is public,
        // so we can call it directly without befriending.
        MSG msg{};
        msg.message = WM_HOTKEY;
        msg.wParam  = 1;     // HOTKEY_ID
        msg.lParam  = MAKELPARAM(MOD_CONTROL | MOD_ALT, 0x41 /*'A'*/);

        qintptr result = 0;
        const bool consumed = svc.nativeEventFilter(
            QByteArrayLiteral("windows_generic_MSG"), &msg, &result);
        QVERIFY2(consumed, "Filter should consume WM_HOTKEY for our HOTKEY_ID");
        QCOMPARE(spy.count(), 1);

        // Wrong HOTKEY_ID → not consumed, no emission.
        msg.wParam = 99;
        const bool consumed2 = svc.nativeEventFilter(
            QByteArrayLiteral("windows_generic_MSG"), &msg, &result);
        QVERIFY(!consumed2);
        QCOMPARE(spy.count(), 1);

        // Wrong message type → not consumed, no emission.
        msg.message = WM_NULL;
        msg.wParam  = 1;
        const bool consumed3 = svc.nativeEventFilter(
            QByteArrayLiteral("windows_generic_MSG"), &msg, &result);
        QVERIFY(!consumed3);
        QCOMPARE(spy.count(), 1);

        // Wrong native-event-type tag → ignored.
        msg.message = WM_HOTKEY;
        const bool consumed4 = svc.nativeEventFilter(
            QByteArrayLiteral("xcb_generic_event_t"), &msg, &result);
        QVERIFY(!consumed4);
        QCOMPARE(spy.count(), 1);
#else
        // Stub returns false unconditionally and never emits.
        qintptr result = 0;
        char dummy[8] = {};
        const bool consumed = svc.nativeEventFilter(
            QByteArrayLiteral("anything"), dummy, &result);
        QVERIFY(!consumed);
        QCOMPARE(spy.count(), 0);
        qInfo() << "Limited test: stub nativeEventFilter is a no-op; signal-path"
                << "test is Windows-only.";
#endif
    }

    // ── Signal connectivity (cross-platform) ──────────────────────────────────

    void quick_add_signal_can_be_connected_to_arbitrary_slot()
    {
        // Independent of platform, the signal must be emit-able by the object
        // itself (we use the private signal-emit helper via metaObject) and
        // delivered to a connected receiver. This guards against a signal
        // being accidentally renamed.
        HotkeyService svc;

        bool fired = false;
        QObject::connect(&svc, &HotkeyService::quickAddRequested,
                         &svc, [&fired]() { fired = true; });

        // Use the meta-object to invoke the signal — this is the canonical
        // way to fire a signal from outside without a friend declaration.
        const bool ok = QMetaObject::invokeMethod(
            &svc, "quickAddRequested", Qt::DirectConnection);
        QVERIFY2(ok, "quickAddRequested signal not invocable via meta-object");
        QVERIFY(fired);
    }

    // ── Windows-specific: conflict / malformed paths ──────────────────────────
    //
    // Note: the production API is parameterless (Ctrl+Alt+A is hard-coded), so
    // there is no setShortcut() to feed an "invalid sequence". The closest we
    // can do is verify the failure path of registerQuickAdd() when the binding
    // is already taken. We do this by registering twice into the same process
    // via raw RegisterHotKey, *then* calling registerQuickAdd().
    //
    // Important: we don't call this on Linux/Android/WASM because they hit
    // the stub.

#ifdef Q_OS_WIN
    void win_registration_failure_emits_failure_signal_when_taken()
    {
        // First, take the same hotkey ourselves so the production code's call
        // to RegisterHotKey will fail with ERROR_HOTKEY_ALREADY_REGISTERED.
        const int sentinelId = 0xBEEF;
        const BOOL precondition = RegisterHotKey(
            nullptr, sentinelId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 0x41);
        if (!precondition) {
            qInfo() << "Limited test: pre-registration of Ctrl+Alt+A failed in"
                    << "this environment (likely already taken by another app);"
                    << "skipping conflict assertion.";
            return;
        }

        HotkeyService svc;
        QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);
        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);

        // The production code emits qInfo() for ERROR_HOTKEY_ALREADY_REGISTERED
        // (Q3.07-followup: demoted from qWarning since multi-instance dev is
        // a normal flow that shouldn't produce warning-level noise). Suppress
        // the expected log line so the test output isn't noisy.
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression(QStringLiteral("\\[HotkeyService\\].*taken by another app.*")));

        const bool ok = svc.registerQuickAdd();
        QCOMPARE(ok, false);
        QCOMPARE(svc.isRegistered(), false);
        QCOMPARE(regSpy.count(), 0);
        QCOMPARE(failSpy.count(), 1);
        // Signal is registrationFailed(shortcut, reason); reason is value(1).
        const QString shortcut = failSpy.first().value(0).toString();
        const QString reason   = failSpy.first().value(1).toString();
        QVERIFY2(!shortcut.isEmpty(),
                 "registrationFailed should name the shortcut that failed");
        QVERIFY2(!reason.isEmpty(),
                 "registrationFailed reason must include a Win32 error string");

        // Release our sentinel so the test process leaves no global hotkey
        // bindings behind for the next test or the developer's session.
        UnregisterHotKey(nullptr, sentinelId);
    }

    void win_double_register_self_is_idempotent_at_state_level()
    {
        // The implementation calls unregister() at the top of registerQuickAdd()
        // when it sees m_registered. Verifies no leaked binding and the final
        // state is "registered" (assuming the env permits registration at all).
        HotkeyService svc;
        const bool ok1 = svc.registerQuickAdd();
        if (!ok1) {
            qInfo() << "Limited test: initial registerQuickAdd failed; skipping"
                    << "idempotency check.";
            return;
        }
        const bool ok2 = svc.registerQuickAdd();
        QCOMPARE(ok2, true);
        QCOMPARE(svc.isRegistered(), true);
        // Cleanup so we don't leave a global Ctrl+Alt+A hook behind.
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);
    }
#endif // Q_OS_WIN
};

QTEST_GUILESS_MAIN(TestHotkeyService)
#include "test_hotkeyservice.moc"
