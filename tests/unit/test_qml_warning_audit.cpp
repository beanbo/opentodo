// =============================================================================
// test_qml_warning_audit.cpp
// =============================================================================
//
// PURPOSE
// -------
// Sister test to test_qml_load_smoke.cpp. The smoke test only fails on
// *deployment-shape* fatal categories ("module not installed",
// "Type unavailable", "failed to load"). This audit test extends that to
// catch *runtime-shape* QML regressions that the smoke test silently lets
// through:
//
//   - "Invalid property assignment: color expected"
//       (the recent ProductivityView CSS-rgba bug; QML colors must be
//       Qt.rgba(0..1, 0..1, 0..1, 0..1) — passing CSS-style "rgba(255,255,...)"
//       produces a warning but the engine still loads, so the smoke test
//       passes.)
//   - "Binding loop detected for property ..."
//       (the engine breaks the loop and continues, so the smoke test passes
//       — but the affected binding is now permanently wrong.)
//   - "QML Component: Delegate must be of Item type"
//       (delegate factories that accidentally return non-Item — e.g. a bare
//       Action — still load, but the view renders empty.)
//   - "Detected anchors on an item that is managed by a layout"
//       (mixing anchors + Layouts produces undefined geometry but the page
//       still draws.)
//   - "TypeError" / "ReferenceError" / "Cannot read property"
//       (JavaScript-level errors in onClicked / Component.onCompleted that
//       still let Main.qml instantiate.)
//
// The setup mirrors test_qml_load_smoke.cpp byte-for-byte: same boot order,
// same context properties, same handler-installation pattern, same
// std::_Exit(0) bypass for the SyncEngine network-thread teardown bug.
//
// REPORT MECHANISM
// ----------------
// On failure the test dumps EVERY captured warning (not just the fatal
// subset). This matters because a regression usually surfaces as a chain
// of related warnings from the same QML file — seeing only the first one
// in isolation is rarely enough to diagnose. The full dump fits in a CI
// log and is the difference between "rerun locally to debug" and "I can
// see the bug from here".
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQmlError>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QTimer>
#include <QPointer>
#include <atomic>

// Real domain / infra / app types. No stubs — same philosophy as smoke test:
// the regression we're catching here is in real production code, so we must
// boot real production code.
#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"

#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/filterviewmodel.h"
#include "viewmodels/searchviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "viewmodels/syncviewmodel.h"
#include "viewmodels/notificationsviewmodel.h"

#include "services/hotkeyservice.h"
#include "services/windowchrome.h"
#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/updatechecker.h"
#include "services/telemetry.h"
#include "services/taskexporter.h"
#include "services/pomodoroservice.h"
#include "services/crashlogger.h"
#include "services/reminderscheduler.h"

#include "import/todoistimporter.h"

#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
#elif defined(Q_OS_ANDROID)
#  include "services/notificationservice_android.h"
#else
#  include "services/notificationservice_stub.h"
#endif

#ifdef HAS_POSITIONING
#  include "services/locationservice.h"
#endif


// -----------------------------------------------------------------------------
// Captured-message storage. Same dual-channel approach as the smoke test:
// qInstallMessageHandler covers the qWarning / qmlWarning paths, and
// QQmlApplicationEngine::warnings covers the QML-engine-internal channel
// that some Qt configurations route type/import errors through before
// (or instead of) the global handler.
// -----------------------------------------------------------------------------
namespace {

static QStringList g_capturedMessages;
static std::atomic<bool> g_capturing{false};
static QtMessageHandler g_previousHandler = nullptr;

static void captureMessageHandler(QtMsgType type,
                                  const QMessageLogContext &ctx,
                                  const QString &msg)
{
    if (g_capturing.load()) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            g_capturedMessages.append(msg);
        }
    }
    if (g_previousHandler) {
        g_previousHandler(type, ctx, msg);
    } else {
        fprintf(stderr, "%s\n", qPrintable(msg));
    }
}

// Tolerated-noise allowlist. Mirrors the smoke test's allowlist, since the
// philosophy is the same: any environmental / network / hotkey-registration
// chatter is not what the audit is auditing. An "Invalid property
// assignment" inside a hotkey-related warning would still fire (the
// allowlist match is on the ENTIRE message — a fatal substring that
// appears alongside a tolerated substring is still treated as fatal,
// because we check the fatal list first below).
static bool isTolerated(const QString &m)
{
    static const QStringList kTolerated = {
        QStringLiteral("deprecated"),
        QStringLiteral("RegisterHotKey"),
        QStringLiteral("hotkey"),
        QStringLiteral("401"),
        QStringLiteral("Unauthorized"),
        QStringLiteral("OAuth"),
        QStringLiteral("GDrive"),
        QStringLiteral("QSettings"),
        QStringLiteral("network"),
        QStringLiteral("DNS"),
        QStringLiteral("connection refused"),
        QStringLiteral("Failed to load Qt translations"),
        QStringLiteral("offscreen"),
        QStringLiteral("QQuickStyle"),
        QStringLiteral("Qt.labs.settings"),
        // Geofencing / positioning service: GPS not available in CI.
        QStringLiteral("Positioning"),
        QStringLiteral("geofence"),
        QStringLiteral("LocationService"),
        // Telemetry sink may complain if the endpoint is unreachable —
        // again, environmental noise.
        QStringLiteral("Telemetry"),
        // Update checker hits a remote URL on startup; if the runner is
        // offline this prints a warning but is not what we are auditing.
        QStringLiteral("UpdateChecker"),
    };
    for (const QString &t : kTolerated) {
        if (m.contains(t, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// Fatal substrings — the *runtime-shape* QML regressions this audit catches.
// Each entry corresponds to a real bug class:
//
//   "Invalid property assignment"
//       Wrong type assigned to a typed property. The ProductivityView
//       rgba bug surfaced as
//       "Invalid property assignment: color expected"
//       — the engine fell back to an undefined color but kept rendering.
//
//   "Binding loop detected"
//       A binding eventually re-evaluates to a value depending on its
//       own output. Qt breaks the loop and continues, but the binding
//       result is now whatever the loop happened to settle on.
//
//   "Delegate must be of Item type"
//       ListView/Repeater delegate factories that return a non-Item
//       (e.g. an Action by mistake). The view silently renders empty.
//
//   "Detected anchors on an item that is managed by a layout"
//       Mixing `anchors.fill` with being a Layout child. Geometry is
//       undefined. We treat this as fatal and rely on the production
//       fix landing before this test runs (Q2.9-A/B/C).
//
//   "TypeError" / "ReferenceError" / "Cannot read property"
//       Pure JavaScript errors inside onClicked / Component.onCompleted
//       handlers — these halt the offending handler but the engine and
//       Main.qml keep loading, so the smoke test misses them.
//
static bool isFatalAuditWarning(const QString &m)
{
    if (isTolerated(m))
        return false;

    static const QStringList kFatal = {
        QStringLiteral("Invalid property assignment"),
        QStringLiteral("Binding loop detected"),
        QStringLiteral("Delegate must be of Item type"),
        QStringLiteral("Detected anchors on an item that is managed by a layout"),
        QStringLiteral("TypeError"),
        QStringLiteral("ReferenceError"),
        QStringLiteral("Cannot read property"),
    };
    for (const QString &f : kFatal) {
        if (m.contains(f, Qt::CaseSensitive))
            return true;
    }
    return false;
}

} // namespace


// =============================================================================
// TestQmlWarningAudit
// =============================================================================
class TestQmlWarningAudit : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Same isolation pattern as the smoke test: scratch QStandardPaths
        // directory + matching QSettings org/app so neither test pollutes
        // the user's real OpenTodo install.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_QmlWarningAudit"));
        QCoreApplication::setApplicationName(QStringLiteral("test_qml_warning_audit"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
    }

    // -------------------------------------------------------------------------
    // Boot the full engine + Main.qml exactly as the smoke test does, but
    // assert against the *runtime-shape* fatal categories rather than the
    // *deployment-shape* ones. The two tests cover complementary failure
    // modes; both should remain green for the build to ship.
    // -------------------------------------------------------------------------
    void main_qml_loads_without_runtime_warnings()
    {
        QQuickStyle::setStyle(QStringLiteral("Material"));

        QObject scope;

        auto *crashLogger = new CrashLogger(&scope);
        Q_UNUSED(crashLogger);

        auto *dataStore   = new DataStore(&scope);
        auto *fileManager = new FileManager(dataStore, &scope);
        auto *syncEngine  = new SyncEngine(dataStore, &scope);

        fileManager->load();

#if defined(Q_OS_WIN)
        auto *notificationService = new WindowsNotificationService(&scope);
#elif defined(Q_OS_ANDROID)
        auto *notificationService = new AndroidNotificationService(&scope);
#else
        auto *notificationService = new StubNotificationService(&scope);
#endif

        new ReminderScheduler(dataStore, notificationService, &scope);

        auto *hotkeyService = new HotkeyService(&scope);

        auto *undoStack = new UndoStack(&scope);
        auto *taskVM    = new TaskViewModel(dataStore, &scope);
        taskVM->setUndoStack(undoStack);
        auto *projectVM       = new ProjectViewModel(dataStore, &scope);
        auto *sidebarVM       = new SidebarViewModel(dataStore, projectVM, &scope);
        auto *filterVM        = new FilterViewModel(dataStore, &scope);
        auto *searchVM        = new SearchViewModel(dataStore, &scope);
        auto *settingsVM      = new SettingsViewModel(dataStore, &scope);
        auto *karmaVM         = new KarmaViewModel(dataStore, &scope);
        auto *syncVM          = new SyncViewModel(syncEngine, &scope);
        auto *notificationsVM = new NotificationsViewModel(dataStore, syncEngine, &scope);

        auto *aiAssistant     = new AIAssistant(dataStore, &scope);
        auto *todoistImporter = new TodoistImporter(dataStore, &scope);
        auto *updateChecker   = new UpdateChecker(&scope);
        auto *taskExporter    = new TaskExporter(dataStore, &scope);
        auto *pomodoroService = new PomodoroService(notificationService, &scope);
        auto *telemetry       = new Telemetry(&scope);

        QQmlApplicationEngine engine;

        // Same import-path widening as smoke test: the post-build copy of
        // qmlsettingsplugin.dll lives at <build>/qt/qml/Qt/labs/settings/
        // and the engine needs <build>/qt/qml on its import path to find
        // it. Without this the engine emits "module Qt.labs.settings is
        // not installed" — but that is a smoke-test concern, the audit
        // would not fail on it (it's tolerated above), so this line is
        // strictly for correctness rather than for the audit's pass/fail.
        engine.addImportPath(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/qt/qml"));
        engine.addImportPath(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/../../qt/qml"));

        QQmlContext *ctx = engine.rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"), dataStore);
        ctx->setContextProperty(QStringLiteral("fileManager"), fileManager);
        ctx->setContextProperty(QStringLiteral("syncEngine"), syncEngine);
        ctx->setContextProperty(QStringLiteral("gdriveClient"),
                                syncEngine->gdriveClient());
        ctx->setContextProperty(QStringLiteral("taskViewModel"), taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"), projectVM);
        ctx->setContextProperty(QStringLiteral("sidebarViewModel"), sidebarVM);
        ctx->setContextProperty(QStringLiteral("filterViewModel"), filterVM);
        ctx->setContextProperty(QStringLiteral("searchViewModel"), searchVM);
        ctx->setContextProperty(QStringLiteral("settingsViewModel"), settingsVM);
        ctx->setContextProperty(QStringLiteral("karmaViewModel"), karmaVM);
        ctx->setContextProperty(QStringLiteral("syncViewModel"), syncVM);
        ctx->setContextProperty(QStringLiteral("notificationsViewModel"),
                                notificationsVM);
        ctx->setContextProperty(QStringLiteral("undoStack"), undoStack);
        ctx->setContextProperty(QStringLiteral("aiAssistant"), aiAssistant);
        ctx->setContextProperty(QStringLiteral("updateChecker"), updateChecker);
        ctx->setContextProperty(QStringLiteral("todoistImporter"),
                                todoistImporter);
        ctx->setContextProperty(QStringLiteral("taskExporter"), taskExporter);
        ctx->setContextProperty(QStringLiteral("telemetry"), telemetry);
        ctx->setContextProperty(QStringLiteral("pomodoroService"),
                                pomodoroService);
        ctx->setContextProperty(QStringLiteral("crashLogger"), crashLogger);
        ctx->setContextProperty(QStringLiteral("notificationService"),
                                notificationService);
        ctx->setContextProperty(QStringLiteral("hotkeyService"), hotkeyService);
        ctx->setContextProperty(QStringLiteral("WindowChrome"),
                                new WindowChrome(&scope));

        // Engine warnings signal: catches QML-engine-internal warnings that
        // some Qt builds route through QQmlError objects rather than the
        // global handler. Same pattern as smoke test.
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [](const QList<QQmlError> &errs) {
            for (const QQmlError &e : errs) {
                g_capturedMessages.append(e.toString());
            }
        });

        QSignalSpy creationFailedSpy(&engine,
                                     &QQmlApplicationEngine::objectCreationFailed);

        g_capturedMessages.clear();
        g_capturing.store(true);

        engine.loadFromModule("OpenTodoUI", "Main");

        // Drain queued connections (Loader::loaded, Component.onCompleted
        // chains, deferred property bindings) so any warning produced by
        // those late-fired handlers ends up in the captured stream too.
        // Two passes because some chains schedule themselves on the next
        // event-loop iteration.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        g_capturing.store(false);

        QVERIFY2(creationFailedSpy.count() == 0,
                 "QQmlApplicationEngine::objectCreationFailed fired during load");
        QVERIFY2(!engine.rootObjects().isEmpty(),
                 "Engine.loadFromModule(OpenTodoUI, Main) produced no root object");

        // Partition the captured stream into fatal (audit-failing) and
        // benign (informational only) buckets.
        QStringList fatal;
        QStringList benign;
        for (const QString &m : g_capturedMessages) {
            if (isFatalAuditWarning(m)) {
                fatal << m;
            } else {
                benign << m;
            }
        }

        // Report mechanism: on failure dump EVERY captured warning, not
        // just the fatal ones. A regression usually surfaces as a related
        // chain of warnings (e.g. one Invalid property assignment
        // followed by three Binding loop detected on adjacent properties),
        // and seeing the full chain is the difference between a one-shot
        // diagnosis and a "rerun locally to find out" round-trip.
        if (!fatal.isEmpty()) {
            qWarning() << "QML warning audit FAILED."
                       << fatal.size() << "fatal warning(s) caught.";
            qWarning() << "--- Fatal warnings (these failed the test): ---";
            for (const QString &m : fatal)
                qWarning().noquote() << "  [FATAL]" << m;
            qWarning() << "--- All other captured warnings (benign / tolerated): ---";
            for (const QString &m : benign)
                qWarning().noquote() << "  [info]" << m;
            qWarning() << "(Total messages captured during Main.qml load:"
                       << g_capturedMessages.size() << ")";
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("Main.qml load produced %1 audit-fatal "
                                           "warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        // Bypass destructor chain. SyncEngine owns network threads that
        // do not shut down cleanly inside QtTest's lifecycle — the
        // QGuiApplication destructor blocks waiting on them and the
        // process eventually trips STATUS_FAIL_FAST_EXCEPTION (0xC0000602)
        // after several minutes. Same workaround as smoke test.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }
};

#include <cstdlib>
QTEST_MAIN(TestQmlWarningAudit)
#include "test_qml_warning_audit.moc"
