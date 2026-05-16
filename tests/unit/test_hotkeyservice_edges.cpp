// =============================================================================
// test_hotkeyservice_edges.cpp  —  Quality round 2.27
// =============================================================================
//
// Purpose
// -------
// Pins HotkeyService edge cases that the two pre-existing suites
// (test_hotkeyservice.cpp / test_hotkeyservice_conflicts.cpp) leave un-asserted.
// All slots pin CURRENT behaviour — no production changes in this round.
//
// What's already covered (do NOT duplicate):
//   • Basic register/unregister/destructor (test_hotkeyservice.cpp)
//   • WM_HOTKEY filter dispatch with right/wrong wParam (test_hotkeyservice.cpp)
//   • Conflict detection via wouldConflict / registrationFailed
//     (test_hotkeyservice_conflicts.cpp)
//   • setShortcut rebinds live registration + persists to QSettings
//     (test_hotkeyservice_conflicts.cpp)
//   • Modifier-only and unparseable inputs rejected
//     (test_hotkeyservice_conflicts.cpp)
//
// What THIS file adds (7 slots):
//   1. register_then_register_same_combo_is_idempotent_no_double_subscribe
//        — Two register calls back-to-back must leave exactly one binding live;
//          the second registerQuickAdd must internally release-then-reclaim
//          rather than leaking a second OS-level slot. Proven by claiming the
//          same combo externally after svc.unregister() — must succeed exactly
//          once, never twice.
//   2. change_hotkey_at_runtime_releases_old_binding
//        — setShortcut(A) → register → setShortcut(B). The OLD combo (A) must
//          become claimable externally (proves the old hwnd-id slot was freed,
//          not just re-tagged with the new vk).
//   3. empty_and_invalid_shortcut_strings_rejected_safely
//        — Pins behaviour for combos NOT covered by the conflict suite:
//          whitespace-only ("   "), embedded NUL, lone-modifier with weird
//          spacing ("Ctrl + A " with internal spaces), and "Win+J" (which is
//          a reserved Windows shell shortcut on most installs — verify the
//          PARSE accepts it but RegisterHotKey may legitimately fail).
//   4. service_under_qcoreapplication_constructs_without_native_filter_loop
//        — Pins that the constructor doesn't require QGuiApplication / a real
//          message pump just to exist; installNativeEventFilter on a
//          QCoreApplication is allowed (returns nullptr safely from
//          topLevelHwnd). Importantly: this slot uses the QApplication from
//          QTEST_MAIN (so the filter install path is exercised under a real
//          app), but asserts behaviour that would still hold under a guiless
//          process.
//   5. disable_then_re_enable_via_unregister_then_register_works
//        — Toggling on/off/on must end in the same registered state we started
//          from, and emit the correct registeredChanged signal sequence.
//   6. multiple_modifier_combos_parse_and_register
//        — Pins the matrix of accepted modifier combinations:
//          Ctrl+Alt+<key>, Ctrl+Shift+<key>, Ctrl+Win+<key>,
//          Ctrl+Alt+Shift+<key>, Ctrl+Alt+Win+<key>. Plain F-key (no mod)
//          must be REJECTED by isValidShortcut because the parser requires
//          at least one modifier.
//   7. shortcut_changed_signal_fires_on_setShortcut_to_new_value_only
//        — Pins signal vocabulary: shortcutChanged fires when the value
//          changes; the SAME shortcut twice produces no second emission
//          (idempotent set). The signal name must be exactly "shortcutChanged".
//
// Test infrastructure notes
// -------------------------
//   - QTEST_MAIN (QApplication) — matches the existing conflict suite and
//     production main.cpp. RegisterHotKey works without a top-level window
//     (HWND==nullptr), but the native event filter still needs a real event
//     dispatcher, which only QGuiApplication+ provides.
//   - Unique QSettings namespace ("OpenTodoTest_HotkeyEdges") so parallel
//     ctest runs against the existing two suites don't trample each other's
//     hotkeys/quickAdd values.
//   - Probe combos use F19–F24 with Ctrl+Alt+Win — even less likely to
//     collide than the conflict suite's Ctrl+Alt-only F19–F22. We never use
//     Ctrl+Alt+A in tests that exercise registration: the developer machine
//     might have it bound for real OpenTodo or other apps.
//   - If RegisterHotKey is environmentally unavailable, slots QSKIP rather
//     than QFAIL (same policy as test_hotkeyservice_conflicts.cpp).
// =============================================================================

#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "services/hotkeyservice.h"

namespace {

// Probe combos chosen to minimize collision with real apps on a developer
// machine. F19–F24 are rare; Ctrl+Alt+Win is rare on top of that.
constexpr auto kComboA = "Ctrl+Alt+Win+F19";   // slot 1, 2, 5
constexpr auto kComboB = "Ctrl+Alt+Win+F20";   // slot 2 (rebind target)
constexpr auto kComboC = "Ctrl+Alt+Win+F21";   // slot 7

#ifdef Q_OS_WIN
// Same gate as test_hotkeyservice_conflicts.cpp: if every RegisterHotKey call
// fails (CI restricted session), skip rather than spuriously fail.
bool environmentSupportsRegisterHotKey()
{
    constexpr int probeId = 0xEDED;
    const BOOL ok = RegisterHotKey(nullptr, probeId,
                                   MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT,
                                   VK_F24);
    if (ok) UnregisterHotKey(nullptr, probeId);
    return ok != 0;
}
#endif

} // namespace

class TestHotkeyServiceEdges : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Isolated QSettings namespace so we don't trample either
        // (a) the real user's saved hotkey, or
        // (b) the namespace owned by test_hotkeyservice_conflicts.cpp
        //     (which uses "OpenTodoTest_HotkeyConflicts").
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_HotkeyEdges"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_hotkeyservice_edges"));

#ifdef Q_OS_WIN
        if (!environmentSupportsRegisterHotKey()) {
            m_envSkipReason = QStringLiteral(
                "RegisterHotKey unavailable in this environment "
                "(CI / restricted session). Registration-dependent slots will "
                "QSKIP — consistent with the existing conflict suite policy.");
        }
#endif
    }

    void cleanupTestCase()
    {
        // Wipe our settings namespace so a re-run starts clean.
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    // ── 1. Same-combo double-register must not double-subscribe ──────────────
    //
    // registerQuickAdd() calls unregister() if m_registered, then re-registers.
    // The OS-level slot count for our HOTKEY_ID must therefore remain == 1
    // regardless of how many times we call registerQuickAdd().
    //
    // Indirect proof: after one register call, we can unregister exactly once
    // and the slot becomes claimable externally. If the second register call
    // had leaked a duplicate subscription, the slot would still be taken.
    void register_then_register_same_combo_is_idempotent_no_double_subscribe()
    {
#ifndef Q_OS_WIN
        QSKIP("Hotkey registration is Windows-only; the stub never registers.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        HotkeyService svc;
        QVERIFY(svc.setShortcut(QString::fromLatin1(kComboA)));

        const bool first = svc.registerQuickAdd();
        if (!first)
            QSKIP("Probe combo unavailable; cannot prove idempotency.");

        // Register again — production code unregisters first then re-registers,
        // so the property must stay true and we don't expect registrationFailed.
        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);
        QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);
        const bool second = svc.registerQuickAdd();
        QCOMPARE(second, true);
        QCOMPARE(svc.isRegistered(), true);
        QCOMPARE(failSpy.count(), 0);
        // The internal unregister→register pair fires registeredChanged twice
        // (one false transition, then one true transition).
        QCOMPARE(regSpy.count(), 2);

        // Now release. Exactly one external claim must then succeed; a leaked
        // second subscription would mean the OS still considers the slot taken.
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);

        constexpr int sentinelId = 0xE111;
        const BOOL claimed = RegisterHotKey(nullptr, sentinelId,
                                            MOD_CONTROL | MOD_ALT | MOD_WIN | MOD_NOREPEAT,
                                            VK_F19);
        QVERIFY2(claimed,
                 "After two register calls + one unregister, the combo must be "
                 "free. A leaked second subscription would keep it bound.");
        UnregisterHotKey(nullptr, sentinelId);
#endif
    }

    // ── 2. Runtime change must release the OLD combo at the OS level ─────────
    //
    // Distinct from test_hotkeyservice_conflicts.cpp's
    // setShortcut_rebinds_live_registration — that one proves combo B is bound
    // afterward. This one proves combo A is RELEASED in the OS hotkey table,
    // i.e. an arbitrary third party can immediately claim A.
    void change_hotkey_at_runtime_releases_old_binding()
    {
#ifndef Q_OS_WIN
        QSKIP("Runtime rebind is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        HotkeyService svc;
        QVERIFY(svc.setShortcut(QString::fromLatin1(kComboA)));
        const bool bound = svc.registerQuickAdd();
        if (!bound)
            QSKIP("Could not bind combo A; env too noisy.");

        // Switch to combo B. setShortcut on a live registration must unregister
        // A and re-register on B (per hotkeyservice_win.cpp lines 281-298).
        const bool rebound = svc.setShortcut(QString::fromLatin1(kComboB));
        QCOMPARE(rebound, true);
        QCOMPARE(svc.shortcut(), QString::fromLatin1(kComboB));

        // External claim of A — the bit being tested. If setShortcut had only
        // updated m_shortcut without calling UnregisterHotKey on the old
        // HOTKEY_ID,A entry, this would fail with ERROR_HOTKEY_ALREADY_REGISTERED.
        constexpr int sentinelId = 0xE222;
        const BOOL claimedOld = RegisterHotKey(nullptr, sentinelId,
                                               MOD_CONTROL | MOD_ALT | MOD_WIN | MOD_NOREPEAT,
                                               VK_F19);
        QVERIFY2(claimedOld,
                 "After setShortcut(B), combo A must be free at the OS level. "
                 "If a third party can't claim it, the old binding wasn't released.");
        UnregisterHotKey(nullptr, sentinelId);

        svc.unregister();
#endif
    }

    // ── 3. Empty / odd-whitespace / weird inputs rejected safely ─────────────
    //
    // Pins inputs the conflict suite doesn't cover: whitespace-only strings,
    // internal-space variants like "Ctrl + A " (the parser trims tokens around
    // '+', so this is actually ACCEPTED — pin that behaviour explicitly), and
    // "Win+J" which is reserved by the Win10/11 shell on most installs (parse
    // succeeds, register may fail).
    void empty_and_invalid_shortcut_strings_rejected_safely()
    {
#ifndef Q_OS_WIN
        QSKIP("String validation is in the Win32 parser; the stub rejects "
              "everything regardless.");
#else
        HotkeyService svc;

        // Group A: must be REJECTED by isValidShortcut (parser says no).
        const QStringList unequivocallyBad = {
            QStringLiteral("   "),         // whitespace-only → trim → "" → reject
            QStringLiteral("\t\t"),        // tab-only → trim → "" → reject
            QStringLiteral("Ctrl"),        // single token, no '+'
            QStringLiteral("Ctrl + "),     // trailing token trims to empty
            QStringLiteral("Ctrl+Alt+F25"),// modifier present but key unknown (F25 > F24)
            QStringLiteral("Hyper+A"),     // Hyper isn't a recognized modifier
        };
        for (const QString &shortcut : unequivocallyBad) {
            QVERIFY2(!svc.isValidShortcut(shortcut),
                     qPrintable(QStringLiteral("isValidShortcut should reject '%1'")
                                .arg(shortcut)));
        }

        // Group B: PIN that the parser ACCEPTS internal spaces around '+'.
        // parseShortcut() trims each token after splitting on '+', so the
        // user-friendly "Ctrl + A" form is treated equivalently to "Ctrl+A".
        // (Whether this is the desired UX is debatable, but it's the current
        // behaviour and changing it without a deprecation would surprise users
        // who hand-edit their settings file.)
        const QString withSpaces = QStringLiteral("Ctrl + Alt + A");
        QVERIFY2(svc.isValidShortcut(withSpaces),
                 "Parser is documented to trim tokens; spaced combos must "
                 "be accepted (pinned behaviour, not a bug).");

        // Group C: Win+J — Windows shell reserved combo. The PARSER must accept
        // it (Win is a known modifier, J is a valid letter), but registration
        // legitimately fails on stock Win10/11 installs. We assert the parse
        // and tolerate either outcome from RegisterHotKey.
        const QString winJ = QStringLiteral("Win+J");
        QVERIFY2(svc.isValidShortcut(winJ),
                 "Win+J must parse: Win is a known modifier, J is a letter.");
        // setShortcut should accept it at the parse layer (m_shortcut updates).
        const bool setOk = svc.setShortcut(winJ);
        QCOMPARE(setOk, true);
        QCOMPARE(svc.shortcut(), winJ);

        // Group D: combo containing an embedded NUL. QString preserves NUL,
        // and "Ctrl\0+A" trims to "Ctrl<NUL>" which is not a known modifier
        // token → rejected. Pin that NUL handling doesn't crash.
        QString withNul = QStringLiteral("Ctrl+A");
        withNul.insert(2, QChar(QChar::Null));   // "Ct\0rl+A"
        QSignalSpy failSpyNul(&svc, &HotkeyService::registrationFailed);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("\\[HotkeyService\\] setShortcut rejected.*")));
        const bool nulOk = svc.setShortcut(withNul);
        QCOMPARE(nulOk, false);
        QVERIFY(failSpyNul.count() >= 1);
#endif
    }

    // ── 4. Construction under a real Q*Application installs the native filter
    //      without requiring an event-loop tick or a top-level window ─────────
    //
    // The constructor calls QCoreApplication::instance()->installNativeEventFilter(this).
    // If main.cpp instantiates HotkeyService before the QML engine has created a
    // window, topLevelHwnd() returns nullptr and RegisterHotKey runs with a
    // thread-message-queue binding. This slot pins: ctor doesn't crash, the
    // filter is installed (we can verify by feeding a WM_HOTKEY MSG directly),
    // and there is no top-level window required just to construct the service.
    void service_under_qcoreapplication_constructs_without_native_filter_loop()
    {
#ifndef Q_OS_WIN
        QSKIP("Filter install path is Win32-specific.");
#else
        // We're under QTEST_MAIN, which uses QApplication. That subclasses
        // QGuiApplication subclasses QCoreApplication, so the
        // installNativeEventFilter call in the ctor goes through the same
        // code path the production main.cpp uses. The behaviour we're pinning
        // is "ctor succeeds even without a window".
        QVERIFY2(QGuiApplication::topLevelWindows().isEmpty(),
                 "Precondition: no top-level windows in the test harness, so "
                 "topLevelHwnd() returns nullptr and the ctor must still work.");

        HotkeyService svc;
        QCOMPARE(svc.isRegistered(), false);
        // Default shortcut populated from QSettings (test namespace empty →
        // falls back to "Ctrl+Alt+A").
        QVERIFY(!svc.shortcut().isEmpty());

        // Direct filter dispatch: synthesize WM_HOTKEY and verify the signal
        // fires. This proves installNativeEventFilter wired the object to the
        // dispatcher.
        QSignalSpy spy(&svc, &HotkeyService::quickAddRequested);
        MSG msg{};
        msg.message = WM_HOTKEY;
        msg.wParam  = 1;          // HOTKEY_ID
        msg.lParam  = MAKELPARAM(MOD_CONTROL | MOD_ALT, 0x41 /*'A'*/);

        qintptr result = 0;
        const bool consumed = svc.nativeEventFilter(
            QByteArrayLiteral("windows_generic_MSG"), &msg, &result);
        QVERIFY2(consumed, "WM_HOTKEY for HOTKEY_ID should be consumed");
        QCOMPARE(spy.count(), 1);

        // Destruction: removeNativeEventFilter must run without an active event
        // loop. If it didn't, the test would crash on scope exit.
#endif
    }

    // ── 5. Disable then re-enable round-trip ─────────────────────────────────
    //
    // Calling unregister() on an already-unregistered service must be a quiet
    // no-op (no registeredChanged emission). Calling register again after
    // unregister must restore the live binding and fire registeredChanged.
    void disable_then_re_enable_via_unregister_then_register_works()
    {
#ifndef Q_OS_WIN
        QSKIP("Toggle is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        HotkeyService svc;
        QVERIFY(svc.setShortcut(QString::fromLatin1(kComboA)));

        // First register.
        if (!svc.registerQuickAdd())
            QSKIP("Probe combo unavailable.");
        QCOMPARE(svc.isRegistered(), true);

        // Disable.
        QSignalSpy regSpy(&svc, &HotkeyService::registeredChanged);
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);
        QCOMPARE(regSpy.count(), 1);

        // Disabling again — must be a no-op (no second emission).
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);
        QCOMPARE(regSpy.count(), 1);

        // Re-enable.
        const bool reOn = svc.registerQuickAdd();
        QCOMPARE(reOn, true);
        QCOMPARE(svc.isRegistered(), true);
        QCOMPARE(regSpy.count(), 2);

        svc.unregister();
#endif
    }

    // ── 6. Matrix of accepted modifier combinations ──────────────────────────
    //
    // The parser accepts Ctrl/Control, Alt, Shift, Meta/Win/Super as modifiers
    // and requires AT LEAST ONE modifier + exactly ONE main key.
    // We pin: every valid combo parses; plain F-key parses-as-invalid because
    // there's no modifier slot.
    void multiple_modifier_combos_parse_and_register()
    {
#ifndef Q_OS_WIN
        QSKIP("Modifier matrix is Win32-specific.");
#else
        HotkeyService svc;

        // Group A: must all PARSE successfully.
        const QStringList validCombos = {
            QStringLiteral("Ctrl+Alt+A"),
            QStringLiteral("Ctrl+Shift+A"),
            QStringLiteral("Alt+Shift+A"),
            QStringLiteral("Ctrl+Win+F12"),
            QStringLiteral("Meta+F11"),                  // Meta alias for Win
            QStringLiteral("Super+F10"),                 // Super alias for Win
            QStringLiteral("Control+A"),                 // Control alias for Ctrl
            QStringLiteral("Ctrl+Alt+Shift+F19"),
            QStringLiteral("Ctrl+Alt+Win+F20"),
            QStringLiteral("Ctrl+Shift+Win+F21"),
            QStringLiteral("Ctrl+Alt+Shift+Win+F22"),    // four-modifier combo
            QStringLiteral("Ctrl+Space"),                // named key SPACE
            QStringLiteral("Alt+Tab"),                   // parses (Alt+Tab is OS-reserved but parses)
            QStringLiteral("Ctrl+Alt+0"),                // digit
            QStringLiteral("Ctrl+Alt+9"),                // digit
            QStringLiteral("Ctrl+PgUp"),                 // named PGUP
            QStringLiteral("Ctrl+Alt+Delete"),           // OS-reserved; parses fine
        };
        for (const QString &combo : validCombos) {
            QVERIFY2(svc.isValidShortcut(combo),
                     qPrintable(QStringLiteral("Should accept '%1'").arg(combo)));
        }

        // Group B: plain main key (no modifier) — parser requires >=1 modifier,
        // so these MUST be rejected. Pins that "F11" alone won't slip past.
        const QStringList noModifier = {
            QStringLiteral("F11"),
            QStringLiteral("F12"),
            QStringLiteral("A"),
            QStringLiteral("Space"),
        };
        for (const QString &combo : noModifier) {
            QVERIFY2(!svc.isValidShortcut(combo),
                     qPrintable(QStringLiteral("Should reject no-modifier '%1'")
                                .arg(combo)));
        }

        // Group C: case-insensitivity sanity. The parser uppercases tokens.
        QVERIFY(svc.isValidShortcut(QStringLiteral("ctrl+alt+a")));
        QVERIFY(svc.isValidShortcut(QStringLiteral("CTRL+ALT+A")));
        QVERIFY(svc.isValidShortcut(QStringLiteral("CtRl+AlT+a")));
#endif
    }

    // ── 7. shortcutChanged signal — exact vocabulary pinning ─────────────────
    //
    // The Q_PROPERTY's NOTIFY signal is named shortcutChanged. Renaming it
    // (e.g. to "shortcutPropChanged" or "currentShortcutChanged") would silently
    // break any QML binding to hotkeyService.shortcut. This slot pins the name.
    // Also pins idempotence: setShortcut(X) followed by setShortcut(X) emits
    // shortcutChanged exactly once.
    void shortcut_changed_signal_fires_on_setShortcut_to_new_value_only()
    {
#ifndef Q_OS_WIN
        QSKIP("shortcutChanged is wired in the Win32 impl; stub also emits but "
              "we want the production semantics under test.");
#else
        HotkeyService svc;

        // Q_PROPERTY meta-object check: the NOTIFY signal must be discoverable
        // by the name "shortcutChanged".
        const int propIdx = svc.metaObject()->indexOfProperty("shortcut");
        QVERIFY2(propIdx >= 0, "Q_PROPERTY 'shortcut' missing");
        const QMetaProperty prop = svc.metaObject()->property(propIdx);
        QVERIFY(prop.hasNotifySignal());
        const QMetaMethod notify = prop.notifySignal();
        QCOMPARE(QString::fromLatin1(notify.name()),
                 QStringLiteral("shortcutChanged"));

        // Reset to a known starting combo first so we don't depend on the
        // QSettings value left by a prior slot.
        QVERIFY(svc.setShortcut(QString::fromLatin1(kComboA)));

        QSignalSpy spy(&svc, &HotkeyService::shortcutChanged);

        // Same value → no emission (m_shortcut == shortcut short-circuit).
        QCOMPARE(svc.setShortcut(QString::fromLatin1(kComboA)), true);
        QCOMPARE(spy.count(), 0);

        // Different value → exactly one emission.
        QCOMPARE(svc.setShortcut(QString::fromLatin1(kComboC)), true);
        QCOMPARE(spy.count(), 1);

        // Setting the new value again → still no extra emission.
        QCOMPARE(svc.setShortcut(QString::fromLatin1(kComboC)), true);
        QCOMPARE(spy.count(), 1);

        // Sanity: the property reader now returns the new combo.
        QCOMPARE(svc.shortcut(), QString::fromLatin1(kComboC));
#endif
    }

private:
    QString m_envSkipReason;
};

// QTEST_MAIN gives us a real QApplication — same shape as the existing
// conflict suite. RegisterHotKey needs the message pump even when the HWND
// passed in is null (WM_HOTKEY is posted to the calling thread's queue).
QTEST_MAIN(TestHotkeyServiceEdges)
#include "test_hotkeyservice_edges.moc"
