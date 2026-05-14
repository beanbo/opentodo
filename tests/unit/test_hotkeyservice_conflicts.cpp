// =============================================================================
// test_hotkeyservice_conflicts.cpp  —  Quality round 2.19
// =============================================================================
//
// Purpose
// -------
// Pins the conflict-detection + rebinding contract added to HotkeyService in
// Q2.19. The original error.log entry that prompted this work was
//
//   [HotkeyService] RegisterHotKey failed: "Назначенная клавиша уже
//   зарегистрирована."
//
// — meaning Ctrl+Alt+A was already claimed by something else (another OpenTodo
// instance, Telegram, a game launcher, ...) and the user got no UI feedback.
// The Q2.19 changes:
//
//   1. registrationFailed now carries the (shortcut, reason) pair so QML can
//      name the failed binding in the banner.
//   2. setShortcut(QString) lets the user rebind without restarting; it
//      unregisters the previous combo, persists the new one in QSettings,
//      and re-registers atomically.
//   3. wouldConflict(QString) is a Q_INVOKABLE probe: try RegisterHotKey →
//      immediately UnregisterHotKey → return whether it succeeded. The
//      settings UI can call this on every keystroke to gray out "Save" if
//      the proposed combo is taken.
//   4. setShortcut/registerQuickAdd reject modifier-only ("Ctrl", "Ctrl+Alt"),
//      empty, trailing-"+", and unknown-keyname (e.g. "F25") inputs.
//
// Test slots (Windows-only; QSKIP elsewhere because the stub never registers):
//   - registering_same_combo_twice_returns_false_second_time
//   - unregister_clears_slot_so_re_register_works
//   - wouldConflict_returns_true_for_already_taken_combo
//   - wouldConflict_returns_false_for_unused_combo
//   - invalid_shortcut_string_rejected
//   - modifier_only_shortcut_rejected
//   - setShortcut_rebinds_live_registration
//   - setShortcut_persists_to_qsettings
//
// Test infrastructure notes
// -------------------------
//   - QTEST_MAIN (not GUILESS): RegisterHotKey wants a window handle and
//     a message pump. QApplication + processEvents() satisfies both.
//   - Unique QSettings namespace ("OpenTodoTest_HotkeyConflicts") so we
//     don't trample the real user's hotkeys/quickAdd preference or
//     conflict with the older test_hotkeyservice.cpp suite running in
//     parallel under ctest -j.
//   - All registration probes use combos UNLIKELY to be taken by real apps
//     (Ctrl+Alt+F19, Ctrl+Alt+F20 etc.) so we don't false-positive against
//     the developer's Telegram or screen-recorder bindings.
//   - Each test does its own cleanup; we never leave a global hotkey live
//     when a slot exits. The destructor of HotkeyService unregisters too.
//   - If RegisterHotKey is unavailable for environmental reasons (running
//     under an unprivileged session, a CI runner that blocks it, ...),
//     the probe slots QSKIP rather than QFAIL — the spec explicitly calls
//     this out as acceptable.
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
#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "services/hotkeyservice.h"

namespace {

// Pick a combo that's extremely unlikely to be globally bound on a
// developer machine. F-keys above F12 are rare in real apps; F19/F20 are
// only used by obscure tools, so Ctrl+Alt+F19 etc. should be free almost
// everywhere. We use several distinct ones so consecutive slots don't
// re-use the same one (which would mask leaked bindings).
constexpr auto kComboA = "Ctrl+Alt+F19";
constexpr auto kComboB = "Ctrl+Alt+F20";
constexpr auto kComboC = "Ctrl+Alt+F21";
constexpr auto kComboD = "Ctrl+Alt+F22";

#ifdef Q_OS_WIN
// Try a one-shot RegisterHotKey to see whether the test environment allows
// any registration at all. CI runners under WinRM or restricted sessions
// can return ERROR_INVALID_PARAMETER for every call regardless of combo.
// We treat that as "skip this whole test file" rather than spuriously failing.
bool environmentSupportsRegisterHotKey()
{
    constexpr int probeId = 0xFEED;
    // F24 + every modifier — exceedingly unlikely to be taken.
    const BOOL ok = RegisterHotKey(nullptr, probeId,
                                   MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                                   VK_F24);
    if (ok)
        UnregisterHotKey(nullptr, probeId);
    return ok != 0;
}
#endif

} // namespace

class TestHotkeyServiceConflicts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique QSettings namespace per spec. main.cpp configures the real
        // app under "OpenTodo / OpenTodo"; we override so QSettings() in the
        // service uses our isolated registry hive / .conf file and the user
        // doesn't get their hotkey wiped by a test run.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_HotkeyConflicts"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_hotkeyservice_conflicts"));

#ifdef Q_OS_WIN
        if (!environmentSupportsRegisterHotKey()) {
            m_envSkipReason = QStringLiteral(
                "RegisterHotKey unavailable in this environment "
                "(CI runner / restricted session). All conflict slots will "
                "QSKIP — this is allowed by the Q2.19 spec.");
        }
#endif
    }

    void cleanupTestCase()
    {
        // Wipe the per-test QSettings namespace so re-running the suite
        // starts from a known state.
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    // ── 1. Double-register of the SAME combo via raw RegisterHotKey ──────────
    void registering_same_combo_twice_returns_false_second_time()
    {
#ifndef Q_OS_WIN
        QSKIP("Hotkey conflicts are Windows-only; the stub never registers.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        // Take kComboA at the OS level first (sentinel id 0xA1A1), then ask
        // HotkeyService — configured with the same combo — to register.
        // RegisterHotKey will fail with "already registered" and the service
        // must surface that via registrationFailed(shortcut, reason).
        constexpr int sentinelId = 0xA1A1;
        // Ctrl+Alt+F19 == MOD_CONTROL | MOD_ALT, VK_F19 (0x82)
        const BOOL took = RegisterHotKey(nullptr, sentinelId,
                                         MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                                         VK_F19);
        if (!took) {
            QSKIP("Pre-registration of probe combo failed (already taken by "
                  "another app or env restriction); cannot prove conflict.");
        }

        HotkeyService svc;
        const bool reset = svc.setShortcut(QString::fromLatin1(kComboA));
        QVERIFY2(reset,
                 "setShortcut should accept Ctrl+Alt+F19 as a parseable combo");

        QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("\\[HotkeyService\\] RegisterHotKey failed.*")));

        const bool ok = svc.registerQuickAdd();
        QCOMPARE(ok, false);
        QCOMPARE(svc.isRegistered(), false);
        QCOMPARE(failSpy.count(), 1);
        const QString shortcut = failSpy.first().value(0).toString();
        const QString reason   = failSpy.first().value(1).toString();
        QCOMPARE(shortcut, QString::fromLatin1(kComboA));
        QVERIFY2(!reason.isEmpty(),
                 "registrationFailed must include a non-empty Win32 reason");

        UnregisterHotKey(nullptr, sentinelId);
#endif
    }

    // ── 2. unregister() releases the slot so a second register succeeds ──────
    void unregister_clears_slot_so_re_register_works()
    {
#ifndef Q_OS_WIN
        QSKIP("Hotkey lifecycle is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        HotkeyService svc;
        QVERIFY(svc.setShortcut(QString::fromLatin1(kComboB)));
        const bool first = svc.registerQuickAdd();
        if (!first)
            QSKIP("Could not take probe combo; cannot prove release.");

        QCOMPARE(svc.isRegistered(), true);

        // Releasing should free the slot in the OS table.
        svc.unregister();
        QCOMPARE(svc.isRegistered(), false);

        // Re-binding to the same combo must succeed; if unregister() didn't
        // actually unregister, RegisterHotKey would return ERROR_HOTKEY_ALREADY_REGISTERED.
        const bool second = svc.registerQuickAdd();
        QCOMPARE(second, true);
        QCOMPARE(svc.isRegistered(), true);

        svc.unregister();
#endif
    }

    // ── 3. wouldConflict returns true for an already-taken combo ─────────────
    void wouldConflict_returns_true_for_already_taken_combo()
    {
#ifndef Q_OS_WIN
        QSKIP("wouldConflict is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        constexpr int sentinelId = 0xC0C0;
        // Take Ctrl+Alt+F21
        const BOOL took = RegisterHotKey(nullptr, sentinelId,
                                         MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                                         VK_F21);
        if (!took)
            QSKIP("Pre-registration of probe combo failed.");

        HotkeyService svc;
        const bool conflict = svc.wouldConflict(QString::fromLatin1(kComboC));
        QCOMPARE(conflict, true);

        UnregisterHotKey(nullptr, sentinelId);
#endif
    }

    // ── 4. wouldConflict returns false for a free combo ──────────────────────
    void wouldConflict_returns_false_for_unused_combo()
    {
#ifndef Q_OS_WIN
        QSKIP("wouldConflict is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        // Make sure nothing else in this test process holds kComboD.
        HotkeyService svc;
        const bool conflict = svc.wouldConflict(QString::fromLatin1(kComboD));
        // We can't promise the developer's system doesn't have F22 bound
        // globally, but we can promise that AT LEAST ONE OF the four probe
        // combos is free. If kComboD claims a conflict, we retry with the
        // F23/F24 fallbacks and only fail when all of them collide.
        if (conflict) {
            const QStringList alternates = {
                QStringLiteral("Ctrl+Alt+F23"),
                QStringLiteral("Ctrl+Alt+F24"),
                QStringLiteral("Ctrl+Alt+Shift+F19"),
                QStringLiteral("Ctrl+Alt+Shift+F20"),
            };
            bool anyFree = false;
            for (const QString &alt : alternates) {
                if (!svc.wouldConflict(alt)) {
                    anyFree = true;
                    break;
                }
            }
            QVERIFY2(anyFree,
                     "Every probe combo collided; environment too noisy to "
                     "prove the negative path. Treat as a flaky env, not a "
                     "wouldConflict bug.");
        } else {
            QVERIFY(true);
        }
#endif
    }

    // ── 5. invalid_shortcut_string_rejected ─────────────────────────────────
    //
    // Spec list: "Ctrl+", "", "NotAKey", "F25" (F25 doesn't exist). Every one
    // must be rejected by both setShortcut() and registerQuickAdd().
    void invalid_shortcut_string_rejected()
    {
#ifndef Q_OS_WIN
        QSKIP("Shortcut validation is exercised by the Win32 parser; the stub "
              "rejects everything regardless. Verified at API level.");
#else
        HotkeyService svc;

        const QStringList bad = {
            QStringLiteral("Ctrl+"),     // trailing plus → empty trailing token
            QString(),                   // empty
            QStringLiteral("NotAKey"),   // no plus at all → no modifier
            QStringLiteral("F25"),       // no modifier + unknown key (F25 doesn't exist)
            QStringLiteral("Ctrl+F25"),  // modifier present but key unknown
            QStringLiteral("Foo+A"),     // bad modifier token
            QStringLiteral("+A"),        // leading plus → empty modifier
            QStringLiteral("Ctrl++A"),   // double plus → empty modifier slot
        };

        for (const QString &shortcut : bad) {
            // Validation pure-function: must say no.
            QVERIFY2(!svc.isValidShortcut(shortcut),
                     qPrintable(QStringLiteral("isValidShortcut should reject '%1'").arg(shortcut)));

            // setShortcut: returns false and emits registrationFailed.
            QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("\\[HotkeyService\\] setShortcut rejected.*")));
            const bool accepted = svc.setShortcut(shortcut);
            QVERIFY2(!accepted,
                     qPrintable(QStringLiteral("setShortcut should reject '%1'").arg(shortcut)));
            QVERIFY2(failSpy.count() >= 1,
                     qPrintable(QStringLiteral("setShortcut('%1') should emit registrationFailed")
                                .arg(shortcut)));

            // wouldConflict: returns true for unparseable combos (they
            // "conflict" in the sense that the caller can't use them).
            QCOMPARE(svc.wouldConflict(shortcut), true);
        }
#endif
    }

    // ── 6. modifier_only_shortcut_rejected ──────────────────────────────────
    //
    // Spec list: "Ctrl", "Ctrl+Alt" — no main key. Win32 RegisterHotKey will
    // refuse these (vk == 0), and historically our setter accepted them
    // silently before re-failing at registration time. Now setShortcut must
    // reject them up front so the settings UI can immediately show the user
    // why their input is bad.
    void modifier_only_shortcut_rejected()
    {
#ifndef Q_OS_WIN
        QSKIP("Modifier-only rejection is exercised by the Win32 parser.");
#else
        HotkeyService svc;

        const QStringList modOnly = {
            QStringLiteral("Ctrl"),
            QStringLiteral("Ctrl+Alt"),
            QStringLiteral("Alt+Shift"),
            QStringLiteral("Meta"),
            QStringLiteral("Ctrl+Shift+Meta"),
        };
        for (const QString &shortcut : modOnly) {
            QVERIFY2(!svc.isValidShortcut(shortcut),
                     qPrintable(QStringLiteral("isValidShortcut should reject modifier-only '%1'")
                                .arg(shortcut)));

            QSignalSpy failSpy(&svc, &HotkeyService::registrationFailed);
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression(QStringLiteral("\\[HotkeyService\\] setShortcut rejected.*")));
            const bool accepted = svc.setShortcut(shortcut);
            QVERIFY2(!accepted,
                     qPrintable(QStringLiteral("setShortcut should reject modifier-only '%1'")
                                .arg(shortcut)));
            QCOMPARE(failSpy.count(), 1);
            const QString reportedShortcut = failSpy.first().value(0).toString();
            QCOMPARE(reportedShortcut, shortcut);
        }
#endif
    }

    // ── 7. setShortcut rebinds a live registration on the fly ────────────────
    //
    // The spec's "Allow re-binding" requirement: the user changes the combo
    // in settings, the old binding must be released and the new one bound
    // without a restart. We verify by:
    //   1. setShortcut(A) → registerQuickAdd() → live on A.
    //   2. setShortcut(B) (different combo) → expect: A released, B bound.
    //   3. Take A externally with a raw RegisterHotKey — must succeed because
    //      the service released its prior binding.
    void setShortcut_rebinds_live_registration()
    {
#ifndef Q_OS_WIN
        QSKIP("Live rebinding is Windows-only.");
#else
        if (!m_envSkipReason.isEmpty()) QSKIP(qPrintable(m_envSkipReason));

        HotkeyService svc;
        // Use Shift variants here so we don't collide with other slots in
        // ctest -j parallel runs.
        const QString comboA = QStringLiteral("Ctrl+Alt+Shift+F19");
        const QString comboB = QStringLiteral("Ctrl+Alt+Shift+F20");

        QVERIFY(svc.setShortcut(comboA));
        const bool boundA = svc.registerQuickAdd();
        if (!boundA)
            QSKIP("Couldn't bind probe combo A; env too noisy.");
        QCOMPARE(svc.isRegistered(), true);
        QCOMPARE(svc.shortcut(), comboA);

        // Now rebind to B. setShortcut must:
        //   a) release the live A binding,
        //   b) update m_shortcut,
        //   c) attempt RegisterHotKey on B,
        //   d) emit shortcutChanged on the way through.
        QSignalSpy scSpy(&svc, &HotkeyService::shortcutChanged);
        const bool ok = svc.setShortcut(comboB);
        QCOMPARE(ok, true);
        QCOMPARE(svc.shortcut(), comboB);
        QCOMPARE(svc.isRegistered(), true);
        QVERIFY(scSpy.count() >= 1);

        // Prove the previous A binding was released by claiming it externally.
        constexpr int sentinelId = 0xB0B0;
        const BOOL claimedA = RegisterHotKey(nullptr, sentinelId,
                                             MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                                             VK_F19);
        QVERIFY2(claimedA,
                 "After setShortcut() rebinds to B, combo A must be free for "
                 "another caller to claim — proving the live rebind released "
                 "the prior OS-level registration.");
        UnregisterHotKey(nullptr, sentinelId);

        svc.unregister();
#endif
    }

    // ── 8. setShortcut persists to QSettings ─────────────────────────────────
    //
    // The next process boot must pick up the user's chosen combo. We verify
    // by calling setShortcut on one service instance, then constructing a
    // fresh service in the same QSettings namespace and reading shortcut().
    void setShortcut_persists_to_qsettings()
    {
#ifndef Q_OS_WIN
        QSKIP("QSettings persistence path is in hotkeyservice_win.cpp only.");
#else
        const QString uniqueCombo = QStringLiteral("Ctrl+Alt+Shift+F23");

        {
            HotkeyService svc;
            const bool ok = svc.setShortcut(uniqueCombo);
            QCOMPARE(ok, true);
            QCOMPARE(svc.shortcut(), uniqueCombo);
        }
        // Force a sync so the second instance sees the value.
        QSettings settings;
        settings.sync();

        {
            HotkeyService svc2;
            QCOMPARE(svc2.shortcut(), uniqueCombo);
        }

        // Restore default so we don't leak the test combo to the next test.
        {
            HotkeyService svc3;
            svc3.setShortcut(QStringLiteral("Ctrl+Alt+A"));
        }
#endif
    }

private:
    QString m_envSkipReason;
};

// QTEST_MAIN (not GUILESS): RegisterHotKey wants a window handle and a
// message pump. QApplication + processEvents() gives us both, matching
// the production main.cpp's QApplication usage.
QTEST_MAIN(TestHotkeyServiceConflicts)
#include "test_hotkeyservice_conflicts.moc"
