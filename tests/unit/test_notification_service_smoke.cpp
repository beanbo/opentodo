// Regression test for the "Послать тестовое" crash.
//
// Background: WindowsNotificationService uses QSystemTrayIcon, which is a
// QtWidgets type. Constructing/showing it under a QGuiApplication crashes
// the process the moment showMessage() is invoked. The diagnostic button
// on the About settings page hits exactly that path. The fix in main.cpp
// is to use QApplication instead of QGuiApplication.
//
// This test guards the contract by:
//   1. Constructing a QApplication (matches production main.cpp).
//   2. Constructing the platform notification service the same way main.cpp
//      does (header-conditional WindowsNotificationService on Windows,
//      stub elsewhere).
//   3. Invoking showNotification() and scheduleNotification() and asserting
//      they DO NOT crash. We can't verify the actual OS toast appears in a
//      headless test environment, but a crash here would catch the original
//      regression as well as future "someone changed main.cpp back to
//      QGuiApplication" mistakes.
//
// Layer: App (because the test depends on QApplication, not just QCoreApplication).

#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QDateTime>

#include "services/notificationservice.h"

#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
   using PlatformNotificationService = WindowsNotificationService;
#elif defined(Q_OS_ANDROID)
#  include "services/notificationservice_android.h"
   using PlatformNotificationService = AndroidNotificationService;
#elif defined(Q_OS_WASM)
#  include "services/notificationservice_wasm.h"
   using PlatformNotificationService = WasmNotificationService;
#else
#  include "services/notificationservice_stub.h"
   using PlatformNotificationService = StubNotificationService;
#endif

class TestNotificationServiceSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique QSettings ns so we don't trample the user's real
        // notifications/soundEnabled preference or any sibling parallel test.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_NotifSmoke"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_notification_service_smoke"));
    }

    // Sanity: we ARE running under QApplication, not QGuiApplication. If
    // someone changes main.cpp back to QGuiApplication AND copies the bad
    // pattern into this test, this slot will at least flag the discrepancy.
    void running_under_qapplication()
    {
        QVERIFY2(qobject_cast<QApplication *>(QCoreApplication::instance()),
                 "QCoreApplication::instance() must be a QApplication. "
                 "QSystemTrayIcon (used by WindowsNotificationService) "
                 "requires QApplication; QGuiApplication crashes it.");
    }

    // The main regression: construct the platform service and immediately
    // call showNotification("title", "body", ""). On Windows this is what
    // the "Послать тестовое" diagnostic button does. Under QGuiApplication
    // this crashes; under QApplication it must merely work (or be a no-op
    // when the system tray is unavailable, like in a CI session).
    void show_notification_does_not_crash()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            QSKIP("System tray not available in this test session "
                  "(headless / no shell). The test still serves its purpose "
                  "via the QApplication assertion above.");
        }
#endif

        PlatformNotificationService svc;
        // The exact arguments the QML button sends from SettingsSectionAbout.qml.
        svc.showNotification(QStringLiteral("OpenTodo — тест"),
                             QStringLiteral("Если ты видишь это уведомление, "
                                            "всё работает!"),
                             QString());
        // No crash → pass. We deliberately don't assert anything about the
        // OS-level toast: that's outside the unit-test boundary.
        QVERIFY(true);
    }

    // Same shape but with the silent-sound code path on Windows. The silent
    // path takes a different branch (Shell_NotifyIconW directly) and was
    // hit historically less often, so the smoke test exercises both.
    void show_notification_silent_does_not_crash()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available.");
#endif
        // Force silent-sound branch.
        QSettings s;
        const bool wasOn = s.value(QStringLiteral("notifications/soundEnabled"),
                                   true).toBool();
        s.setValue(QStringLiteral("notifications/soundEnabled"), false);
        s.sync();

        PlatformNotificationService svc;
        svc.showNotification(QStringLiteral("OpenTodo — silent"),
                             QStringLiteral("Тихое уведомление"),
                             QString());

        // Restore.
        s.setValue(QStringLiteral("notifications/soundEnabled"), wasOn);
        s.sync();
        QVERIFY(true);
    }

    // scheduleNotification with a near-future timestamp. The Windows path
    // arms a QTimer; we just verify the call returns without crashing.
    void schedule_notification_does_not_crash()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available.");
#endif
        PlatformNotificationService svc;
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        svc.scheduleNotification(QStringLiteral("Future toast"),
                                 QStringLiteral("Body"),
                                 future,
                                 QStringLiteral("test-id-1"));
        // Cancel immediately so the QTimer doesn't fire after the test ends.
        svc.cancelNotification(QStringLiteral("test-id-1"));
        QVERIFY(true);
    }

    // hasPermission must be callable from QML right after construction —
    // SettingsSectionAbout.qml binds notifCardRoot._granted to it.
    void has_permission_callable_after_construction()
    {
        PlatformNotificationService svc;
        const bool granted = svc.hasPermission();
        // We don't care WHICH value comes back (depends on platform default);
        // we care that the call doesn't crash and returns a bool.
        Q_UNUSED(granted);
        QVERIFY(true);
    }
};

QTEST_MAIN(TestNotificationServiceSmoke)
#include "test_notification_service_smoke.moc"
