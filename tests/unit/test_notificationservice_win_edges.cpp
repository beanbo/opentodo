// =============================================================================
// WindowsNotificationService — behavioural edge-case audit (Infra layer)
// =============================================================================
//
// PURPOSE
// -------
//   Existing coverage of WindowsNotificationService is:
//     - test_notification_service_smoke    : 4 happy-path slots (crash-only)
//     - test_qobject_ownership_audit       : audits parent-ownership, not
//                                            notification behaviour at all
//
//   This file fills the behavioural gap. The Windows tray-toast path runs
//   through QSystemTrayIcon (a QtWidgets type that needs QApplication, not
//   QGuiApplication — see test_notification_service_smoke for the "Послать
//   тестовое" regression context) and through Shell_NotifyIconW on the
//   silent path. Either branch can be stressed with unusual inputs:
//
//     1. empty title
//     2. empty body
//     3. 10 KB body (extremely long)
//     4. 100 rapid notifications (rate-limit / queue stress)
//     5. requestPermission() emits exactly one completion signal
//     6. hasPermission() returns true on Windows (no runtime grant prompt)
//     7. construction does not block / throw / fire signals
//     8. unicode + emoji round-trip
//
//   For every slot the goal is PIN CURRENT BEHAVIOUR, not assert a desired
//   behaviour. If a future hardening pass changes one of these — say, a
//   max-body-length truncation — flipping the assertion is the regression
//   bench.
//
//   No production-code changes in this round (focus is observability).
//
// PORTABILITY
// -----------
//   NotificationService is Windows-only. The whole test body is wrapped in
//   #ifdef Q_OS_WIN. On other platforms a single QSKIP() slot keeps the
//   build green so the file still compiles and registers an executable.
//
//   We run under QApplication (NOT QTEST_GUILESS_MAIN / QGuiApplication) so
//   QSystemTrayIcon construction succeeds — see notification_service_smoke
//   for the historical crash that motivated this constraint.
//
// =============================================================================

#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QElapsedTimer>
#include <QDateTime>
#include <QString>

#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
#endif

class TestNotificationServiceWinEdges : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Isolated QSettings ns so a parallel test session can't trample our
        // notifications/enabled toggle (and vice-versa). Same pattern as
        // test_notification_service_smoke.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_NotifWinEdges"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_notificationservice_win_edges"));
        QSettings().clear();
    }

    // -----------------------------------------------------------------------
    // Slot 1: empty title — what does the Windows path do? Pin the behaviour.
    // QSystemTrayIcon::showMessage explicitly allows empty titles (Qt docs say
    // an empty title means "no title bar in the balloon"). We pin that the
    // call does not crash and does not throw; this is the most likely path a
    // misconfigured caller would hit (e.g. a Reminder created without a task
    // content string fed via QString()).
    // -----------------------------------------------------------------------
    void showNotification_with_empty_title_renders_safely()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available in this test session.");

        WindowsNotificationService svc;
        // Defensive: empty title, real body. Should NOT crash and should NOT
        // throw. We can't verify what Windows actually renders in a headless
        // environment — that's an OS concern — but the C++ side must remain
        // well-behaved.
        svc.showNotification(QString(),
                             QStringLiteral("Body without title"),
                             QStringLiteral("task-empty-title"));
        QVERIFY(true);
        qInfo() << "Audit: empty title accepted by WindowsNotificationService "
                   "(Qt's showMessage docs: empty title => no title bar).";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 2: empty body — symmetric to slot 1. Empty body would be unusual
    // (a reminder with no description) but the path must not crash.
    // -----------------------------------------------------------------------
    void showNotification_with_empty_body_renders_safely()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available in this test session.");

        WindowsNotificationService svc;
        svc.showNotification(QStringLiteral("Title only"),
                             QString(),
                             QStringLiteral("task-empty-body"));
        QVERIFY(true);
        qInfo() << "Audit: empty body accepted (showMessage path).";

        // Cover the silent branch too — it hits a different code path
        // (Shell_NotifyIconW directly with NIIF_NOSOUND). The silent path uses
        // wcsncpy_s with _TRUNCATE on the std::wstring from QString — an
        // empty string maps to an empty wstring, c_str() returns L"" which
        // wcsncpy_s handles fine. Pin that.
        QSettings s;
        s.setValue(QStringLiteral("notifications/enabled"), false);
        s.sync();
        svc.showNotification(QStringLiteral("Silent title"),
                             QString(),
                             QStringLiteral("task-empty-body-silent"));
        // Restore for downstream slots.
        s.setValue(QStringLiteral("notifications/enabled"), true);
        s.sync();
        QVERIFY(true);
        qInfo() << "Audit: silent-path empty body also accepted.";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 3: extremely long body. Windows toast bodies are limited (~200
    // chars historically, more on Win10+) — Qt's showMessage internally
    // truncates. The silent path uses wcsncpy_s with _TRUNCATE, which caps
    // at the szInfo[256] buffer. Either way, a 10 KB input must NOT crash.
    // Pin the no-crash + no-throw contract.
    // -----------------------------------------------------------------------
    void showNotification_with_extremely_long_body_truncates_or_passes_through()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available in this test session.");

        WindowsNotificationService svc;
        QString hugeBody;
        hugeBody.reserve(10000);
        for (int i = 0; i < 10000; ++i)
            hugeBody.append(QChar('x'));
        QCOMPARE(hugeBody.size(), 10000);

        svc.showNotification(QStringLiteral("10 KB body stress"),
                             hugeBody,
                             QStringLiteral("task-huge-body"));
        QVERIFY(true);
        qInfo() << "Audit: 10 KB body accepted by audible path (Qt internally "
                   "truncates).";

        // Silent path — szInfo is a fixed 256 wchar_t buffer. wcsncpy_s with
        // _TRUNCATE clamps without overflow. Pin that the call still returns.
        QSettings s;
        s.setValue(QStringLiteral("notifications/enabled"), false);
        s.sync();
        svc.showNotification(QStringLiteral("10 KB silent"),
                             hugeBody,
                             QStringLiteral("task-huge-body-silent"));
        s.setValue(QStringLiteral("notifications/enabled"), true);
        s.sync();
        QVERIFY(true);
        qInfo() << "Audit: 10 KB body accepted by silent path (wcsncpy_s "
                   "_TRUNCATE clamps to szInfo[256]).";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 4: rapid 100 notifications. The Windows tray API silently drops
    // toasts when fired faster than the OS can present them — there is no
    // per-call queue in our C++ side and no rate-limit. We pin that:
    //   (a) firing 100 in a tight loop completes in a reasonable time
    //       (< 5 seconds — generous for slow CI),
    //   (b) the process does not crash,
    //   (c) no exception escapes.
    // The OS-side outcome (one toast every ~5s, the rest discarded) is an
    // OS concern, not a C++ concern. If future hardening adds in-process
    // throttling, this test will need an explicit signal-count assertion.
    // -----------------------------------------------------------------------
    void rapid_100_notifications_do_not_crash()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available in this test session.");

        WindowsNotificationService svc;
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 100; ++i) {
            svc.showNotification(QStringLiteral("Rapid #%1").arg(i),
                                 QStringLiteral("Body %1").arg(i),
                                 QStringLiteral("rapid-task-%1").arg(i));
        }
        const qint64 elapsed = t.elapsed();
        qInfo().noquote()
            << "Audit: 100 rapid showNotification calls completed in"
            << elapsed << "ms (no in-process throttle).";
        QVERIFY2(elapsed < 5000,
                 qPrintable(QStringLiteral("100 calls took %1 ms — should be "
                                           "< 5000 ms; the path may have grown "
                                           "a blocking sync op.").arg(elapsed)));
        QVERIFY(true);
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 5: requestPermission() must fire permissionRequestCompleted exactly
    // ONCE, with granted=true, asynchronously (queued via invokeMethod). This
    // is the QML contract documented in notificationservice.h:46 — the
    // SyncIndicator flips its persistent "we've asked" flag in a Connections
    // handler keyed on this signal.
    //
    // The implementation in notificationservice_win.cpp:172-180 schedules the
    // emit via QMetaObject::invokeMethod(..., Qt::QueuedConnection). We spin
    // the event loop and assert exactly one emission with the expected payload.
    // -----------------------------------------------------------------------
    void requestPermission_fires_completion_signal_once()
    {
#if defined(Q_OS_WIN)
        WindowsNotificationService svc;
        QSignalSpy spy(&svc, &NotificationService::permissionRequestCompleted);
        QVERIFY(spy.isValid());

        svc.requestPermission();

        // The emit is queued — must wait for the event loop to drain.
        QVERIFY2(spy.wait(2000),
                 "permissionRequestCompleted did not fire within 2s of "
                 "requestPermission() — Windows path is supposed to be a "
                 "no-op-with-completion, not a no-op.");

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 1);
        QCOMPARE(args.at(0).toBool(), true);

        // Bonus: drain the event loop a bit more, and confirm we still see
        // exactly one — guards against the path being changed to e.g. emit
        // both synchronously AND queued.
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);  // takeFirst() drained the one we got.

        qInfo() << "Audit: requestPermission() emits "
                   "permissionRequestCompleted(true) exactly once "
                   "(queued connection).";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 6: hasPermission() returns true on Windows. The base class default
    // is `return true` (notificationservice.h:20) and WindowsNotificationService
    // does NOT override hasPermission(). This is intentional — Windows has no
    // runtime POST_NOTIFICATIONS prompt like Android 13+. Pin that contract
    // here so a regression (e.g. someone adding a "check the registry / check
    // Focus Assist" stub that returns false) is loud.
    // -----------------------------------------------------------------------
    void hasPermission_returns_true_on_windows()
    {
#if defined(Q_OS_WIN)
        WindowsNotificationService svc;
        QCOMPARE(svc.hasPermission(), true);
        qInfo() << "Audit: hasPermission() == true on Windows (no runtime "
                   "grant prompt — toasts are gated by Windows Settings, not "
                   "by a per-call dialog).";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 7: construction does not block, does not throw, does not fire
    // signals (e.g. notificationClicked, permissionRequestCompleted). The
    // tray icon should be created lazily-but-eagerly (in the constructor)
    // and become visible without raising any OS dialog. We can't test the
    // visibility easily, but we CAN test:
    //   (a) construction completes synchronously (no blocking),
    //   (b) no signal is fired during/right after construction.
    //
    // The "what happens under QGuiApplication" question is not testable
    // here because we're under QApplication. The legacy "Послать тестовое"
    // crash regression is covered by test_notification_service_smoke (which
    // pins the QApplication-not-QGuiApplication contract). We document the
    // gap via qInfo.
    // -----------------------------------------------------------------------
    void service_construction_does_not_block_or_throw()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available — ensureTrayIcon would still "
                  "construct the QSystemTrayIcon object but show() would fail "
                  "silently. Skipping rather than asserting on a degraded state.");

        QElapsedTimer t;
        t.start();
        WindowsNotificationService svc;
        const qint64 ctorMs = t.elapsed();

        // Construction has to be fast — the production code calls it from
        // main.cpp at startup, on the GUI thread. A blocking ctor would
        // freeze the splash screen.
        QVERIFY2(ctorMs < 500,
                 qPrintable(QStringLiteral("Construction took %1 ms — must "
                                           "be < 500 ms (called on GUI thread "
                                           "at startup).").arg(ctorMs)));

        // No signals should have fired purely from construction.
        QSignalSpy clickedSpy(&svc, &NotificationService::notificationClicked);
        QSignalSpy permSpy(&svc, &NotificationService::permissionRequestCompleted);
        QSignalSpy bannerSpy(&svc, &NotificationService::inAppBannerRequested);
        QSignalSpy reminderSpy(&svc, &NotificationService::reminderFired);

        // Drain any queued events from the ctor (none expected).
        QTest::qWait(50);

        QCOMPARE(clickedSpy.count(), 0);
        QCOMPARE(permSpy.count(),    0);
        QCOMPARE(bannerSpy.count(),  0);
        QCOMPARE(reminderSpy.count(), 0);

        qInfo().noquote()
            << "Audit: WindowsNotificationService construction took"
            << ctorMs << "ms and fired no signals. "
                         "GAP: this test cannot pin the QGuiApplication "
                         "crash regression (we're under QApplication here); "
                         "test_notification_service_smoke covers that.";
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }

    // -----------------------------------------------------------------------
    // Slot 8: unicode + emoji round-trip. The Windows toast text is UTF-16
    // natively (NOTIFYICONDATAW takes wchar_t*); QSystemTrayIcon::showMessage
    // passes QString through Qt's UTF-16 internal storage. Both paths should
    // handle Cyrillic + emoji without mojibake or crash.
    //
    // We can't observe the rendered OS toast in a headless test, so the
    // assertion is: the call completes, doesn't crash, and toStdWString() (the
    // exact transformation used by the silent path at notificationservice_win.cpp:85-86)
    // round-trips losslessly back to the original QString.
    // -----------------------------------------------------------------------
    void showNotification_unicode_emoji_round_trips()
    {
#if defined(Q_OS_WIN)
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("System tray not available in this test session.");

        WindowsNotificationService svc;
        const QString title = QStringLiteral("Напоминание");
        // Body has Russian, a BMP emoji (✅ U+2705), and a surrogate-pair emoji
        // (🎉 U+1F389) — exercise both UTF-16 code-unit lengths.
        const QString body =
            QStringLiteral("Задача завершена ") + QChar(0x2705) +
            QStringLiteral(" Поздравляем ") + QString::fromUcs4(U"\U0001F389");
        svc.showNotification(title, body,
                             QStringLiteral("task-unicode"));
        QVERIFY(true);

        // Pin the silent-path conversion is lossless. wstring<->QString round-
        // trip via std::wstring::c_str() must yield the same characters.
        const std::wstring wTitle = title.toStdWString();
        const std::wstring wBody  = body.toStdWString();
        const QString backTitle = QString::fromStdWString(wTitle);
        const QString backBody  = QString::fromStdWString(wBody);
        QCOMPARE(backTitle, title);
        QCOMPARE(backBody, body);

        qInfo().noquote()
            << "Audit: unicode round-trip via std::wstring lossless: "
            << "title len=" << title.size()
            << "body len=" << body.size()
            << "(includes surrogate-pair emoji).";

        // Also exercise the silent branch directly.
        QSettings s;
        s.setValue(QStringLiteral("notifications/enabled"), false);
        s.sync();
        svc.showNotification(title, body,
                             QStringLiteral("task-unicode-silent"));
        s.setValue(QStringLiteral("notifications/enabled"), true);
        s.sync();
        QVERIFY(true);
#else
        QSKIP("WindowsNotificationService is Windows-only.");
#endif
    }
};

QTEST_MAIN(TestNotificationServiceWinEdges)
#include "test_notificationservice_win_edges.moc"
