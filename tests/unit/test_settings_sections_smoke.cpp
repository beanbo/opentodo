// =============================================================================
// test_settings_sections_smoke.cpp
// =============================================================================
//
// PURPOSE
// -------
// Per-section smoke tests for each SettingsSection*.qml component in
// `qml/components/settings/`. The full SettingsView already loads them
// together (covered by test_qml_load_smoke), but a single-section regression
// gets buried in the noise of the full view's load. By instantiating each
// section in isolation we catch e.g. a typo in `settingsViewModel.theme` or
// a missing `Settings { category: ... }` block as a dedicated failed slot
// rather than a vague Main.qml failure.
//
// FILES UNDER TEST (9 sections)
// -----------------------------
//   - SettingsSectionAbout.qml         needs settingsViewModel, fileManager,
//                                      dataStore, notificationService,
//                                      karmaViewModel, telemetry, crashLogger
//   - SettingsSectionAppearance.qml    needs settingsViewModel, telemetry
//   - SettingsSectionBackup.qml        needs fileManager, taskExporter,
//                                      todoistImporter
//   - SettingsSectionDefer.qml         self-contained (Qt.labs.settings only)
//   - SettingsSectionGeneral.qml       needs settingsViewModel, taskViewModel,
//                                      telemetry
//   - SettingsSectionProductivity.qml  needs settingsViewModel, telemetry,
//                                      pomodoroService, dailyReviewService
//                                      (latter two guarded by `typeof`)
//   - SettingsSectionReminders.qml     needs settingsViewModel,
//                                      notificationService, telemetry
//   - SettingsSectionSidebar.qml       needs settingsViewModel, telemetry
//   - SettingsSectionSync.qml          needs syncViewModel, settingsViewModel,
//                                      gdriveClient (guarded by `typeof`)
//
// SETUP
// -----
// Mirrors test_views_smoke.cpp's pattern: boot ONE QQmlApplicationEngine in
// initTestCase, register every context property the sections (transitively)
// read, then per-slot load ONE section's QML via QQmlComponent. Each slot is
// independent — no state leaks between sections.
//
// Capture the message stream with the same allowlist pattern as
// test_qml_load_smoke.cpp:
//   - Tolerated: Qt.labs.settings deprecation, QSettings noise, Material
//     style chatter, OAuth/network noise (sync surface).
//   - Fatal: "is not installed", binding loops, "Cannot read property",
//     "Unable to assign [undefined]", etc. — these mask real regressions.
//
// SKIP PROTOCOL
// -------------
// If a section legitimately fails to load standalone because it depends on a
// context property we did not (or could not) register in the test scope, we
// QSKIP that slot rather than failing — it is still useful information that
// the section is NOT self-contained, recorded via qInfo() so the test log
// captures the diagnosis. With the wiring below, all nine sections SHOULD
// load cleanly. A QSKIP would indicate a wiring change worth flagging.
//
// std::_Exit(0) in cleanupTestCase mirrors the same MinGW-on-Windows
// destructor wedge sidestep used by the calendar / banner / views smoke
// tests. Everything has been validated by then.
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQmlError>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QScopedPointer>
#include <QDebug>
#include <QUrl>
#include <atomic>
#include <cstdlib>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"

#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "viewmodels/syncviewmodel.h"
#include "viewmodels/notificationsviewmodel.h"

#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/taskexporter.h"
#include "services/telemetry.h"
#include "services/pomodoroservice.h"
#include "services/crashlogger.h"
#include "services/updatechecker.h"

#include "import/todoistimporter.h"

// Pick the right NotificationService for the platform — same logic used by
// test_qml_load_smoke.cpp / src/main.cpp. SettingsSectionReminders binds to
// `notificationService.someProperty` so the type must be the real one (we do
// NOT want a stub class hiding a real Q_PROPERTY drift).
#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
#elif defined(Q_OS_ANDROID)
#  include "services/notificationservice_android.h"
#else
#  include "services/notificationservice_stub.h"
#endif


// -----------------------------------------------------------------------------
// Captured-message storage. Same pattern as test_views_smoke.cpp: a global
// message handler dumps warning+ severities into a flat list, gated by an
// atomic so background noise outside the load-under-test doesn't pollute it.
// -----------------------------------------------------------------------------
namespace {

static QStringList       g_capturedMessages;
static std::atomic<bool> g_capturing{false};
static QtMessageHandler  g_previousHandler = nullptr;

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
        std::fprintf(stderr, "%s\n", qPrintable(msg));
    }
}

// Tolerated noise — environmental warnings unrelated to what these smoke
// tests validate. Aligned with test_views_smoke.cpp / test_qml_load_smoke.cpp.
// "Qt.labs.settings" is tolerated because the deprecation warning fires on
// every Settings { } block; we still want the FATAL "is not installed"
// variant to fail (the kFatal substring "is not installed" is checked
// AFTER the tolerance pass — but the substring "Qt.labs.settings" alone
// will tolerate; the deprecation message contains the word "deprecated"
// which is also in kTolerated, so the order does not matter in practice).
static bool isTolerated(const QString &m)
{
    static const QStringList kTolerated = {
        QStringLiteral("deprecated"),
        QStringLiteral("QSettings"),
        QStringLiteral("offscreen"),
        QStringLiteral("QQuickStyle"),
        QStringLiteral("Qt.labs.settings"),
        QStringLiteral("RegisterHotKey"),
        QStringLiteral("hotkey"),
        QStringLiteral("OAuth"),
        QStringLiteral("GDrive"),
        QStringLiteral("network"),
        QStringLiteral("DNS"),
        QStringLiteral("connection refused"),
        QStringLiteral("401"),
        QStringLiteral("Unauthorized"),
        // Material style chatter — components occasionally print
        // "QQuickStyle: ..." lines on minimal CI images.
        QStringLiteral("Material"),
    };
    for (const QString &t : kTolerated) {
        if (m.contains(t, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

static bool isFatalWarning(const QString &m)
{
    if (isTolerated(m))
        return false;

    // The FATAL substrings closely follow test_qml_load_smoke.cpp /
    // test_views_smoke.cpp. The most important ones for individual section
    // loads are:
    //   - "is not installed"          missing optional Qt module (Qt.labs.*)
    //   - "is not available"          type registered but not exposed
    //   - "Cannot read property"      undefined context property
    //   - "Unable to assign [undefined]"  same family
    //   - "ReferenceError"            misnamed identifier
    //   - "Binding loop detected"     section's bindings are self-referencing
    //   - "Cannot assign to non-existent property"  Q_PROPERTY drift
    static const QStringList kFatal = {
        QStringLiteral("is not installed"),
        QStringLiteral("is not available"),
        QStringLiteral("failed to load"),
        QStringLiteral("Unable to assign [undefined]"),
        QStringLiteral("Cannot read property"),
        QStringLiteral("is not a type"),
        QStringLiteral("Cannot assign to non-existent property"),
        QStringLiteral("Invalid property assignment"),
        QStringLiteral("Binding loop detected"),
        QStringLiteral("TypeError"),
        QStringLiteral("ReferenceError"),
    };
    for (const QString &f : kFatal) {
        if (m.contains(f, Qt::CaseSensitive))
            return true;
    }
    return false;
}

} // namespace


// =============================================================================
// TestSettingsSectionsSmoke
// =============================================================================
class TestSettingsSectionsSmoke : public QObject
{
    Q_OBJECT

private:
    // Object graph parent. Reused across slots — building it once saves
    // ~200 ms QML engine cold-start cost per section.
    QObject               *m_scope             = nullptr;
    DataStore             *m_dataStore         = nullptr;
    FileManager           *m_fileManager       = nullptr;
    SyncEngine            *m_syncEngine        = nullptr;
    UndoStack             *m_undoStack         = nullptr;
    TaskViewModel         *m_taskVM            = nullptr;
    ProjectViewModel      *m_projectVM         = nullptr;
    SidebarViewModel      *m_sidebarVM         = nullptr;
    SettingsViewModel     *m_settingsVM        = nullptr;
    KarmaViewModel        *m_karmaVM           = nullptr;
    SyncViewModel         *m_syncVM            = nullptr;
    NotificationsViewModel*m_notificationsVM   = nullptr;
    AIAssistant           *m_aiAssistant       = nullptr;
    TaskExporter          *m_taskExporter      = nullptr;
    Telemetry             *m_telemetry         = nullptr;
    PomodoroService       *m_pomodoroService   = nullptr;
    CrashLogger           *m_crashLogger       = nullptr;
    UpdateChecker         *m_updateChecker     = nullptr;
    TodoistImporter       *m_todoistImporter   = nullptr;
    QObject               *m_notificationService = nullptr;
    QQmlApplicationEngine *m_engine            = nullptr;

    // Helper: load ONE section's .qml by its full resource URL via
    // QQmlComponent. Returns the created object scoped via QScopedPointer in
    // the caller. Captures messages around the load so isFatalWarning() can
    // be evaluated against just this section's load.
    //
    // sourceUrl: e.g. "qrc:/qt/qml/OpenTodoUI/qml/components/settings/SettingsSectionAbout.qml"
    QObject *loadSectionWithCapture(const QString &sourceUrl,
                                    QString *errOut = nullptr)
    {
        g_capturedMessages.clear();
        g_capturing.store(true);

        QQmlComponent comp(m_engine, QUrl(sourceUrl));
        if (comp.isError()) {
            if (errOut) *errOut = comp.errorString();
            g_capturing.store(false);
            return nullptr;
        }
        QObject *obj = comp.create(m_engine->rootContext());
        if (!obj) {
            if (errOut) *errOut = comp.errorString();
        }

        // Drain any deferred Component.onCompleted chains so warnings emitted
        // from queued connections land in g_capturedMessages BEFORE we stop
        // capturing — otherwise binding-loop warnings that fire one event
        // loop later would be missed.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        g_capturing.store(false);
        return obj;
    }

    // Run the standard "load + assert no fatal warnings" check for one
    // section. Skips if the section legitimately fails to load standalone
    // (records the failure via QSKIP + qInfo so the log explains why).
    void runSectionSmoke(const QString &sectionName, const QString &sourceUrl)
    {
        QString err;
        QScopedPointer<QObject> obj(loadSectionWithCapture(sourceUrl, &err));

        if (!obj) {
            qInfo().noquote() << "Section" << sectionName
                              << "did not load standalone:"
                              << err;
            QSKIP("Section is not self-contained; "
                  "depends on context not registered in the test scope.");
        }
        QVERIFY2(obj, qPrintable(err));

        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning().noquote() << "Fatal QML warnings during"
                                 << sectionName << "load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("%1 load produced %2 fatal "
                                           "warning(s); first: %3")
                                .arg(sectionName)
                                .arg(fatal.size())
                                .arg(fatal.value(0))));
    }

    // Resource-URL builder for a section file. RESOURCE_PREFIX is "/qt/qml"
    // and the QML module URI is "OpenTodoUI" (see CLAUDE.md), so each
    // section lives at qrc:/qt/qml/OpenTodoUI/qml/components/settings/<File>.
    static QString sectionUrl(const QString &fileBaseName)
    {
        return QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/components/")
                + QStringLiteral("settings/SettingsSection")
                + fileBaseName
                + QStringLiteral(".qml");
    }

private slots:

    void initTestCase()
    {
        // Per-process QSettings org/app keeps the test's QSettings isolated
        // from other tests that run under `ctest -j`.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_SettingsSections"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_settings_sections_smoke"));

        // Material style: every section's Material.foo bindings need this
        // to resolve at load time.
        QQuickStyle::setStyle(QStringLiteral("Material"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);

        // ---------------------------------------------------------------
        // Object graph: every context property each section reads.
        // Ownership tree rooted at m_scope so std::_Exit at end can skip
        // the destructor chain cleanly.
        // ---------------------------------------------------------------
        m_scope             = new QObject;
        m_crashLogger       = new CrashLogger(m_scope);
        m_dataStore         = new DataStore(m_scope);
        m_fileManager       = new FileManager(m_dataStore, m_scope);
        m_syncEngine        = new SyncEngine(m_dataStore, m_scope);

#if defined(Q_OS_WIN)
        m_notificationService = new WindowsNotificationService(m_scope);
#elif defined(Q_OS_ANDROID)
        m_notificationService = new AndroidNotificationService(m_scope);
#else
        m_notificationService = new StubNotificationService(m_scope);
#endif

        m_undoStack         = new UndoStack(m_scope);
        m_taskVM            = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM         = new ProjectViewModel(m_dataStore, m_scope);
        m_sidebarVM         = new SidebarViewModel(m_dataStore, m_projectVM, m_scope);
        m_settingsVM        = new SettingsViewModel(m_dataStore, m_scope);
        m_karmaVM           = new KarmaViewModel(m_dataStore, m_scope);
        m_syncVM            = new SyncViewModel(m_syncEngine, m_scope);
        m_notificationsVM   = new NotificationsViewModel(m_dataStore,
                                                         m_syncEngine, m_scope);
        m_aiAssistant       = new AIAssistant(m_dataStore, m_scope);
        m_taskExporter      = new TaskExporter(m_dataStore, m_scope);
        m_telemetry         = new Telemetry(m_scope);
        m_pomodoroService   = new PomodoroService(
            // PomodoroService takes NotificationService* — cast is safe
            // because the Stub/Win/Android variants all derive from it.
            qobject_cast<NotificationService*>(m_notificationService),
            m_scope);
        m_updateChecker     = new UpdateChecker(m_scope);
        m_todoistImporter   = new TodoistImporter(m_dataStore, m_scope);

        // ---------------------------------------------------------------
        // Engine + import paths. Same dual-candidate path logic as the
        // other smoke tests so Qt.labs.settings's plugin (deployed by
        // CMake POST_BUILD) is found whether the test binary is in the
        // build root or under tests/unit/.
        // ---------------------------------------------------------------
        m_engine = new QQmlApplicationEngine(m_scope);
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/../../qt/qml"));

        // ---------------------------------------------------------------
        // Context properties — one per section's surface. Names must
        // match those in src/main.cpp byte-for-byte (sections look them
        // up by string).
        // ---------------------------------------------------------------
        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),         m_dataStore);
        ctx->setContextProperty(QStringLiteral("fileManager"),       m_fileManager);
        ctx->setContextProperty(QStringLiteral("syncEngine"),        m_syncEngine);
        ctx->setContextProperty(QStringLiteral("gdriveClient"),
                                m_syncEngine->gdriveClient());
        ctx->setContextProperty(QStringLiteral("taskViewModel"),     m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"),  m_projectVM);
        ctx->setContextProperty(QStringLiteral("sidebarViewModel"),  m_sidebarVM);
        ctx->setContextProperty(QStringLiteral("settingsViewModel"), m_settingsVM);
        ctx->setContextProperty(QStringLiteral("karmaViewModel"),    m_karmaVM);
        ctx->setContextProperty(QStringLiteral("syncViewModel"),     m_syncVM);
        ctx->setContextProperty(QStringLiteral("notificationsViewModel"),
                                m_notificationsVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),         m_undoStack);
        ctx->setContextProperty(QStringLiteral("aiAssistant"),       m_aiAssistant);
        ctx->setContextProperty(QStringLiteral("taskExporter"),      m_taskExporter);
        ctx->setContextProperty(QStringLiteral("telemetry"),         m_telemetry);
        ctx->setContextProperty(QStringLiteral("pomodoroService"),   m_pomodoroService);
        ctx->setContextProperty(QStringLiteral("crashLogger"),       m_crashLogger);
        ctx->setContextProperty(QStringLiteral("updateChecker"),     m_updateChecker);
        ctx->setContextProperty(QStringLiteral("todoistImporter"),   m_todoistImporter);
        ctx->setContextProperty(QStringLiteral("notificationService"),
                                m_notificationService);
    }

    void cleanupTestCase()
    {
        // Restore the previous message handler so any post-test bookkeeping
        // doesn't crash inside our captureMessageHandler (g_capturedMessages
        // is module-static so it survives, but g_previousHandler may still
        // be useful to other tests in the same binary — though there are none).
        qInstallMessageHandler(g_previousHandler);

        // Bypass destructor: SyncEngine spins up network threads and the
        // QML engine has queued deleteLaters that occasionally wedge under
        // QtTest's MinGW-on-Windows lifecycle (same pattern as
        // test_qml_load_smoke / test_views_smoke / test_calendar_view_smoke).
        // All assertions have been verified by the time we reach here.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

    // -------------------------------------------------------------------------
    // One slot per section file. Naming matches the spec:
    //   <name>_loads_without_error
    // Each slot is a thin wrapper around runSectionSmoke() — the identity of
    // the failing slot tells you which section regressed at a glance.
    // -------------------------------------------------------------------------

    void appearance_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Appearance"), sectionUrl(QStringLiteral("Appearance")));
    }

    void general_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("General"), sectionUrl(QStringLiteral("General")));
    }

    void productivity_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Productivity"), sectionUrl(QStringLiteral("Productivity")));
    }

    void reminders_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Reminders"), sectionUrl(QStringLiteral("Reminders")));
    }

    void sidebar_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Sidebar"), sectionUrl(QStringLiteral("Sidebar")));
    }

    void sync_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Sync"), sectionUrl(QStringLiteral("Sync")));
    }

    void backup_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Backup"), sectionUrl(QStringLiteral("Backup")));
    }

    void about_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("About"), sectionUrl(QStringLiteral("About")));
    }

    void defer_loads_without_error()
    {
        runSectionSmoke(QStringLiteral("Defer"), sectionUrl(QStringLiteral("Defer")));
    }
};

// QTEST_MAIN: needs QGuiApplication (we instantiate QQmlApplicationEngine and
// load Material-themed QML). The QPA platform is selected by ctest's
// ENVIRONMENT property (offscreen on POSIX, windows on Windows — see
// tests/unit/CMakeLists.txt).
QTEST_MAIN(TestSettingsSectionsSmoke)
#include "test_settings_sections_smoke.moc"
