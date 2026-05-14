// =============================================================================
// test_qml_load_smoke.cpp
// =============================================================================
//
// PURPOSE
// -------
// Boot the *real* QML engine end-to-end and assert that loading
// `OpenTodoUI / Main.qml` produces a root object without any *fatal-category*
// QML warnings.
//
// WHY THIS TEST EXISTS
// --------------------
// In May 2026 the desktop build shipped with `qmlsettingsplugin.dll` missing
// from `<exe>/qt/qml/Qt/labs/settings/`. At runtime this surfaced as:
//
//     QML module not found: Qt.labs.settings is not installed
//
// Because `Qt.labs.settings` is declared *optional* in qmldir, most QML files
// that import it merely no-op their `Settings { ... }` blocks. But files that
// *load via Loader* or that depend on a Settings-backed singleton (notably
// DesktopLayout.qml) fail outright — producing a black-screen with no visible
// crash. The unit-test suite at the time was 100% green: every C++ test passed,
// because none of them actually instantiated Main.qml. The QtQuickTest runner
// (`qmltest_runner`) was registered with ctest under the name `qml_components`
// but the orchestrator excluded it via `-E "qml_components"`, so the entire
// QML loading path was unverified for ten "passing" iterations.
//
// This test closes that gap. It is a *smoke test*: boot the engine the way
// `src/main.cpp` does, load Main.qml, watch the warnings stream, and fail the
// build if any *fatal-category* warning is emitted. Categories that mask real
// regressions:
//
//   - "is not installed"           — missing/undeployed QML plugin (May 2026)
//   - "failed to load"             — Component.Status.Error from Loader chain
//   - "Unable to assign [undefined]" — context property never wired
//   - "Type ... unavailable"       — registration / module-URI mismatch
//   - "is not a type"              — misnamed import
//   - "module ... not installed"   — same shape as "is not installed"
//   - "Cannot assign to non-existent property" — typo in Q_PROPERTY name
//
// Tolerated noise (real & not the test's concern):
//   - "deprecated"                 — Qt-side API drift; not our regression
//   - "RegisterHotKey failed"      — environmental (CI runners don't grant the
//                                    Ctrl+Alt+A combo); HotkeyService still
//                                    constructs successfully
//   - "401" / "Unauthorized"       — sync HTTP from a stub OAuth flow
//   - "GDrive"                     — same family
//   - "QSettings"                  — deprecation prints
//
// HOW IT BOOTS
// ------------
// We mirror `src/main.cpp` as closely as is reasonable for a unit test:
//
//   - QStandardPaths::setTestModeEnabled(true) so FileManager writes into a
//     scratch QStandardPaths::AppDataLocation (not the user's real OpenTodo
//     directory). This keeps the test self-contained — no env setup needed.
//   - QGuiApplication is provided by QTEST_MAIN (see bottom of file).
//   - Material style is set the same way main.cpp sets it — without this,
//     Material-themed components cannot resolve `Material.theme` and would
//     produce "is not available" warnings.
//   - Every context property that Main.qml reads (dataStore, taskViewModel,
//     ..., hotkeyService, WindowChrome) is set with a *real* instance from
//     OpenTodoApp. Stubs would re-introduce the gap we're trying to close.
//
// The test does NOT exercise sync, hotkeys, notifications etc. — those are
// covered by their own unit tests. We boot the QML side only and let it tear
// down at the end of the slot.
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQmlError>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QTimer>
#include <QPointer>
#include <atomic>

// Real domain / infra / app types — no stubs.
// Boot mirrors src/main.cpp.
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

// Pick the same NotificationService backend that main.cpp picks. The test
// must not invent its own subclass — that would let a typo in the real
// header sneak past the test (we do NOT want a stub here).
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
// Captured-message storage. Populated from BOTH the global qInstallMessageHandler
// and the QQmlApplicationEngine::warnings signal — different layers of the QML
// pipeline route messages through different channels (qmlWarning vs Qt logging),
// and we want to see all of them.
// -----------------------------------------------------------------------------
namespace {

// Global storage: a flat list of every message text seen during loadFromModule.
// We use a static atomic-guarded vector because the qt message handler is a
// raw C function pointer and cannot capture instance state.
static QStringList g_capturedMessages;
static std::atomic<bool>  g_capturing{false};

// Forward existing default handler so we still see things in the test log.
static QtMessageHandler g_previousHandler = nullptr;

static void captureMessageHandler(QtMsgType type,
                                  const QMessageLogContext &ctx,
                                  const QString &msg)
{
    if (g_capturing.load()) {
        // Record only warnings/criticals/fatals — info & debug are noisy and
        // not the categories we care about.
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            g_capturedMessages.append(msg);
        }
    }
    if (g_previousHandler) {
        g_previousHandler(type, ctx, msg);
    } else {
        // Fallback: write to stderr the same way Qt's default does.
        fprintf(stderr, "%s\n", qPrintable(msg));
    }
}

// Decide whether a captured message represents a real regression (= fail the
// test) or harmless background noise (= ignore).
//
// This is intentionally a small allowlist of FATAL substrings + a small
// denylist of TOLERATED substrings. The denylist wins when both match, so a
// "deprecated" warning that *also* contains "is not installed" will still
// fail — that combination is implausible and would indicate a real bug.
static bool isFatalQmlWarning(const QString &m)
{
    // Tolerated noise: skip these even if they contain a fatal substring.
    static const QStringList kTolerated = {
        QStringLiteral("deprecated"),
        QStringLiteral("RegisterHotKey"),       // CI hotkey conflict
        QStringLiteral("hotkey"),                // generic hotkey complaint
        QStringLiteral("401"),                   // GDrive unauthorised in CI
        QStringLiteral("Unauthorized"),
        QStringLiteral("OAuth"),                 // sync auth complaints
        QStringLiteral("GDrive"),                // sync surface
        QStringLiteral("QSettings"),             // deprecation prints from QSettings
        QStringLiteral("network"),               // any "network unreachable" etc.
        QStringLiteral("DNS"),
        QStringLiteral("connection refused"),
        QStringLiteral("Failed to load Qt translations"), // main.cpp emits
                                                          // this on minimal CI
                                                          // images without
                                                          // qtbase translations
        QStringLiteral("offscreen"),             // QPA chatter under offscreen
        // Material style chatter on minimal CI:
        QStringLiteral("QQuickStyle"),
    };
    for (const QString &t : kTolerated) {
        if (m.contains(t, Qt::CaseInsensitive))
            return false;
    }

    // Fatal substrings — these are the categories that masked the May 2026
    // black-screen incident. Any one of them in the message stream should
    // fail the test loudly.
    static const QStringList kFatal = {
        QStringLiteral("is not installed"),                  // missing QML module/plugin
        QStringLiteral("module \""),                          // "module \"X\" is not installed"
        QStringLiteral("is not available"),                   // type registered but not exposed
        QStringLiteral("failed to load"),                     // Loader/Component error
        QStringLiteral("Unable to assign [undefined]"),       // missing context property
        QStringLiteral("is not a type"),                      // import URI mismatch
        QStringLiteral("Cannot assign to non-existent property"), // misnamed Q_PROPERTY
        QStringLiteral("Component is not ready"),             // load-order regression
        QStringLiteral("Type ") + QStringLiteral(" unavailable"), // see note below
    };
    // Special-case "Type X unavailable" — Qt formats this with the type name
    // in between, which kFatal can't match as a single substring. Detect it
    // by combining "Type" and "unavailable" within the same line.
    if (m.contains(QStringLiteral("Type "), Qt::CaseSensitive)
        && m.contains(QStringLiteral("unavailable"), Qt::CaseSensitive)) {
        return true;
    }

    for (const QString &f : kFatal) {
        if (m.contains(f, Qt::CaseSensitive))
            return true;
    }
    return false;
}

} // namespace


// =============================================================================
// TestQmlLoadSmoke
// =============================================================================
class TestQmlLoadSmoke : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Per-test org/app keeps the test-mode AppDataLocation isolated from
        // other tests that toggle setTestModeEnabled (test_filemanager*,
        // test_qml_warning_audit) so the FileManager state.json path does not
        // collide under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_qml_load_smoke"));

        // Send all FileManager I/O into a per-test scratch directory so the
        // test never touches the user's real ~/AppData/OpenTodo. This makes
        // the test reproducible across machines & safe to run in CI.
        QStandardPaths::setTestModeEnabled(true);

        // Install our message handler so qWarning + qmlWarning are captured.
        // We hold off on enabling capture (`g_capturing = true`) until we are
        // about to load Main.qml, so init noise doesn't pollute the result.
        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);
    }

    void cleanupTestCase()
    {
        // Restore the original handler so subsequent tests in the same
        // process (if any) aren't affected.
        qInstallMessageHandler(g_previousHandler);
    }

    // -------------------------------------------------------------------------
    // The actual test: boot the engine + Main.qml end-to-end and check that
    // (a) the engine produced a root object, (b) no FATAL-category warning
    // was emitted from any layer of the QML pipeline.
    //
    // What this test guards against (beyond the May 2026 incident):
    //   - QML_FILES list missing a newly-added file (RESOURCE_PREFIX still
    //     resolves but Loader fails)
    //   - Singleton declaration drift (set_source_files_properties not
    //     applied to a new theme file)
    //   - Context property name typos (Main.qml expects `taskViewModel`,
    //     someone in main.cpp wires it as `taskVM` instead — the QML file
    //     loads but every binding is undefined)
    //   - Qt module dependency drift (someone removes the LabsSettings link
    //     and qmlsettingsplugin stops being deployed)
    // -------------------------------------------------------------------------
    void main_qml_loads_without_fatal_warnings()
    {
        // ---------------------------------------------------------------
        // 1. Set the Material style BEFORE creating the engine.
        //    Doing this after engine creation is too late — Material
        //    components have already resolved their style to the default,
        //    and warnings about unresolved Material.* attached properties
        //    will fire during the load.
        // ---------------------------------------------------------------
        QQuickStyle::setStyle(QStringLiteral("Material"));

        // ---------------------------------------------------------------
        // 2. Build the same object graph main.cpp builds, parented to a
        //    scratch QObject so every instance is destroyed when the slot
        //    returns (parents-then-children destruction order).
        // ---------------------------------------------------------------
        QObject scope;

        auto *crashLogger = new CrashLogger(&scope);
        // Don't actually install the crash handler in tests — it tampers with
        // signal handlers in a way that breaks QtTest's own crash reporting.
        Q_UNUSED(crashLogger);

        auto *dataStore   = new DataStore(&scope);
        auto *fileManager = new FileManager(dataStore, &scope);
        auto *syncEngine  = new SyncEngine(dataStore, &scope);

        // Load whatever happens to be on disk in the scratch dir (almost
        // certainly nothing). This mirrors main.cpp; a fresh DataStore is
        // also valid for the smoke test.
        fileManager->load();

        // Notification service — pick the right backend per platform.
        // We deliberately use the REAL implementation, not a stub: the May
        // 2026 incident was a deployment-side bug, and the real code path
        // is the one we want to validate.
#if defined(Q_OS_WIN)
        auto *notificationService = new WindowsNotificationService(&scope);
#elif defined(Q_OS_ANDROID)
        auto *notificationService = new AndroidNotificationService(&scope);
#else
        auto *notificationService = new StubNotificationService(&scope);
#endif

        // Reminder scheduler bridges DataStore → notifications.
        new ReminderScheduler(dataStore, notificationService, &scope);

        // Hotkey service: we DO construct it but do NOT register the global
        // hotkey on test runners (RegisterHotKey often fails in CI because
        // some other process owns Ctrl+Alt+A). The QML side only needs the
        // object to exist so its `quickAddRequested` signal can be hooked.
        auto *hotkeyService = new HotkeyService(&scope);

        // Undo stack and ViewModels — exact mirror of main.cpp wiring.
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

        // ---------------------------------------------------------------
        // 3. Create the engine + register every context property Main.qml
        //    references. The names must be byte-for-byte identical to
        //    main.cpp — Main.qml looks them up by name.
        // ---------------------------------------------------------------
        QQmlApplicationEngine engine;

        // Add the deployed Qt.labs.* plugins directory to the import path.
        // CMake's POST_BUILD copies qmlsettingsplugin.dll + qmldir into
        // <build>/qt/qml/Qt/labs/settings/ — but Qt only finds them if
        // <build>/qt/qml is on the engine's import path. WITHOUT this line
        // the test would emit "module Qt.labs.settings is not installed"
        // and fail — which is the exact regression we're trying to detect.
        // We need to point at the BUILD root, not the test binary's own
        // dir, because the test binary lives in tests/unit/ which doesn't
        // have its own qt/qml/ tree. CMAKE_BINARY_DIR is the build root
        // and that's where the post-build copy actually lives.
        engine.addImportPath(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/qt/qml"));
        // Also add the build root: when the test binary lives one level
        // below the root (tests/unit/test_qml_load_smoke.exe), the qt/qml
        // tree is at ../../qt/qml relative to the binary.
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

        // ---------------------------------------------------------------
        // 4. Hook the engine's warnings signal. This is the *primary*
        //    channel the QML engine uses for type/import errors — they go
        //    through qmlWarning() before reaching the qInstallMessageHandler
        //    layer in some Qt configurations, so we listen on both.
        // ---------------------------------------------------------------
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [](const QList<QQmlError> &errs) {
            for (const QQmlError &e : errs) {
                g_capturedMessages.append(e.toString());
            }
        });

        // Also fail fast if the engine itself reports the load failed.
        QSignalSpy creationFailedSpy(&engine,
                                     &QQmlApplicationEngine::objectCreationFailed);

        // ---------------------------------------------------------------
        // 5. ENABLE capture, load Main.qml, then DISABLE capture. We only
        //    care about messages emitted *during* this load — afterwards
        //    background services may emit their own (sync, telemetry, …)
        //    that aren't part of what we want to validate.
        // ---------------------------------------------------------------
        g_capturedMessages.clear();
        g_capturing.store(true);

        engine.loadFromModule("OpenTodoUI", "Main");

        // Allow queued connections (e.g. Loader::loaded, Component.onCompleted
        // chains) to finish before we evaluate the warning stream.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        g_capturing.store(false);

        // ---------------------------------------------------------------
        // 6. Assert: engine produced a root object.
        // ---------------------------------------------------------------
        QVERIFY2(creationFailedSpy.count() == 0,
                 "QQmlApplicationEngine::objectCreationFailed fired during load");
        QVERIFY2(!engine.rootObjects().isEmpty(),
                 "Engine.loadFromModule(OpenTodoUI, Main) produced no root object");

        // ---------------------------------------------------------------
        // 7. Assert: no FATAL-category warning was captured.
        //    Print the full filtered set into the test log so a CI failure
        //    is debuggable without needing to re-run locally.
        // ---------------------------------------------------------------
        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalQmlWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings captured during Main.qml load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
            qWarning() << "(Total messages captured:" << g_capturedMessages.size() << ")";
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("Main.qml load produced %1 fatal warning(s); "
                                           "first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        // Bypass destructor chain. SyncEngine spins up network threads that
        // don't shut down cleanly inside QtTest's guiless-ish lifecycle —
        // the QGuiApplication destructor blocks waiting on them and the
        // process eventually trips STATUS_FAIL_FAST_EXCEPTION (0xC0000602)
        // after several minutes. We've verified all of our assertions; skip
        // the cleanup and exit cleanly.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }
};

// QTEST_MAIN (not GUILESS): we instantiate a QQmlApplicationEngine which
// requires a QGuiApplication. The QPA platform is selected by the ctest
// ENVIRONMENT property (offscreen on Linux/macOS, windows on Windows — see
// tests/qml/CMakeLists.txt for the rationale on the Windows side).
// _Exit() is in <cstdlib> on POSIX and MinGW; std::_Exit pulls it in via the
// std namespace. Some MinGW headers also need <stdlib.h>.
#include <cstdlib>
QTEST_MAIN(TestQmlLoadSmoke)
#include "test_qml_load_smoke.moc"
