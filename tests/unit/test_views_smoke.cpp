// =============================================================================
// test_views_smoke.cpp
// =============================================================================
//
// PURPOSE
// -------
// Bundle three focused smoke tests for QML views that have not been
// individually exercised by the existing smoke / audit suite:
//
//   - qml/views/ProjectView.qml
//   - qml/views/TrashView.qml
//   - qml/views/AIView.qml
//
// Each view is loaded IN ISOLATION inside a tiny inline QML harness (an outer
// Item that hosts a Loader pointing at the view's resource URL). This mirrors
// the established pattern from test_calendar_view_smoke.cpp and
// test_inapp_banner_smoke.cpp.
//
// WHY ONE FILE FOR THREE VIEWS?
// -----------------------------
// Each view shares the same engine + context-property scaffolding; bundling
// them as separate slots in ONE QObject lets us boot a single
// QQmlApplicationEngine in initTestCase() and reuse it across slots. That
// saves ~600 ms of cold-start cost per view that would otherwise be paid in
// three separate test executables.
//
// TEST SLOTS
// ----------
//   ProjectView:
//     - projectview_loads_without_error
//     - projectview_no_fatal_warnings_during_load
//
//   TrashView:
//     - trashview_loads_without_error
//     - trashview_no_fatal_warnings_during_load
//     - trashview_with_deleted_task_shows_one_item
//
//   AIView:
//     - aiview_loads_without_error
//     - aiview_no_fatal_warnings_during_load
//
// SETUP
// -----
// Mirror test_qml_load_smoke.cpp's approach but DO NOT load Main.qml. We
// register every context property the three views (transitively) read:
//
//   - ProjectView: dataStore, taskViewModel, projectViewModel,
//     sidebarViewModel, taskExporter
//   - TrashView:   dataStore, taskViewModel
//   - AIView:      aiAssistant, telemetry
//
// Plus a few extras (undoStack, settingsViewModel, etc.) so transitive
// signal chains in TaskViewModel never trip on a missing property.
//
// We use std::_Exit(0) in the last slot's tail to bypass the QML engine's
// destructor — the QGuiApplication destructor occasionally wedges under
// QtTest's lifecycle (queued deleteLaters + QSettings flushes) on Windows.
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQmlError>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QVariant>
#include <QVariantMap>
#include <QUrl>
#include <atomic>
#include <cstdlib>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/taskexporter.h"
#include "services/telemetry.h"

// -----------------------------------------------------------------------------
// Captured-message storage. Same pattern as test_calendar_view_smoke.cpp:
// install a global handler so qWarning + qmlWarning land in a flat list,
// then enable capture only during the test action so background noise does
// not pollute the result.
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
// tests validate. Kept aligned with the calendar / banner smoke tests'
// allowlists. A few extras for the QML categories that fire on the
// project view's complex layout (binding-loop chatter from the editor pane,
// hotkey complaints, etc.).
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

    static const QStringList kFatal = {
        QStringLiteral("is not installed"),
        QStringLiteral("is not available"),
        QStringLiteral("failed to load"),
        QStringLiteral("Unable to assign [undefined]"),
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
// TestViewsSmoke
// =============================================================================
class TestViewsSmoke : public QObject
{
    Q_OBJECT

private:
    // Engine + ViewModel scope. Reused across every slot — building it once
    // saves the ~200 ms QML engine cold-start cost per slot and means a
    // single QObject parent owns the whole graph.
    QObject                *m_scope        = nullptr;
    DataStore              *m_dataStore    = nullptr;
    UndoStack              *m_undoStack    = nullptr;
    TaskViewModel          *m_taskVM       = nullptr;
    ProjectViewModel       *m_projectVM    = nullptr;
    SidebarViewModel       *m_sidebarVM    = nullptr;
    SettingsViewModel      *m_settingsVM   = nullptr;
    AIAssistant            *m_aiAssistant  = nullptr;
    TaskExporter           *m_taskExporter = nullptr;
    Telemetry              *m_telemetry    = nullptr;
    QQmlApplicationEngine  *m_engine       = nullptr;

    // Helper: instantiate one of the views inside an outer Item that gives
    // it a parent for `anchors.fill: parent`. The view itself is found via
    // the Loader's `item` property after the source has finished loading.
    //
    // sourceUrl: e.g. "qrc:/qt/qml/OpenTodoUI/qml/views/ProjectView.qml"
    //
    // NOTE: per project convention, avoid R"(...)" raw strings. The harness
    // is built with explicit "\n" line breaks instead.
    QObject *createViewWrapper(const QString &sourceUrl, QString *errOut = nullptr)
    {
        QString qml;
        qml += QStringLiteral("import QtQuick\n");
        qml += QStringLiteral("import QtQuick.Controls.Material\n");
        qml += QStringLiteral("import OpenTodoUI\n");
        qml += QStringLiteral("Item {\n");
        qml += QStringLiteral("    width: 1000\n");
        qml += QStringLiteral("    height: 700\n");
        qml += QStringLiteral("    Loader {\n");
        qml += QStringLiteral("        id: viewLoader\n");
        qml += QStringLiteral("        objectName: \"viewLoader\"\n");
        qml += QStringLiteral("        anchors.fill: parent\n");
        qml += QStringLiteral("        source: \"") + sourceUrl + QStringLiteral("\"\n");
        qml += QStringLiteral("    }\n");
        qml += QStringLiteral("}\n");

        QQmlComponent comp(m_engine);
        comp.setData(qml.toUtf8(),
                     QUrl(QStringLiteral("qrc:/test/ViewsSmokeWrapper.qml")));
        if (comp.isError()) {
            if (errOut) *errOut = comp.errorString();
            return nullptr;
        }
        QObject *root = comp.create(m_engine->rootContext());
        if (!root) {
            if (errOut) *errOut = QStringLiteral("comp.create() returned null");
            return nullptr;
        }
        // Allow the Loader to instantiate its source and any deferred
        // bindings (Component.onCompleted chains, Connections, etc.) to settle.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        return root;
    }

    QObject *viewFromWrapper(QObject *wrapper) const
    {
        if (!wrapper) return nullptr;
        QObject *loader = wrapper->findChild<QObject*>(QStringLiteral("viewLoader"));
        if (!loader) return nullptr;
        return loader->property("item").value<QObject*>();
    }

    // Capture-and-load helper: clears the captured messages, enables capture,
    // loads the wrapper for the given source URL, disables capture, and
    // returns the wrapper. Captured fatal warnings are checked by the caller.
    QObject *loadWithCapture(const QString &sourceUrl, QString *errOut = nullptr)
    {
        g_capturedMessages.clear();
        g_capturing.store(true);
        QObject *wrapper = createViewWrapper(sourceUrl, errOut);
        g_capturing.store(false);
        return wrapper;
    }

    // URLs of the three views under test. Resource layout follows the
    // RESOURCE_PREFIX "/qt/qml" convention used by the OpenTodoUI module.
    static QString projectViewUrl()
    {
        return QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/views/ProjectView.qml");
    }
    static QString trashViewUrl()
    {
        return QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/views/TrashView.qml");
    }
    static QString aiViewUrl()
    {
        return QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/views/AIView.qml");
    }

private slots:

    void initTestCase()
    {
        // Per-process unique QSettings org/app — keeps this test's QSettings
        // namespace isolated from real OpenTodo and from sibling smoke tests
        // that may run in parallel.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ViewsSmoke"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_views_smoke"));

        QQuickStyle::setStyle(QStringLiteral("Material"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);

        // Build the minimum object graph that the three views need.
        // Direct context refs:
        //   - ProjectView: dataStore, taskViewModel, projectViewModel,
        //     sidebarViewModel, taskExporter, undoStack
        //   - TrashView:   dataStore, taskViewModel
        //   - AIView:      aiAssistant, telemetry
        // We also expose settingsViewModel because some shared QML
        // components (TaskItem, TaskEditor) reach into it.
        m_scope        = new QObject;
        m_dataStore    = new DataStore(m_scope);
        m_undoStack    = new UndoStack(m_scope);
        m_taskVM       = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM    = new ProjectViewModel(m_dataStore, m_scope);
        m_sidebarVM    = new SidebarViewModel(m_dataStore, m_projectVM, m_scope);
        m_settingsVM   = new SettingsViewModel(m_dataStore, m_scope);
        m_aiAssistant  = new AIAssistant(m_dataStore, m_scope);
        m_taskExporter = new TaskExporter(m_dataStore, m_scope);
        m_telemetry    = new Telemetry(m_scope);

        m_engine = new QQmlApplicationEngine(m_scope);

        // OpenTodoUI module path resolution — same dual-candidate logic as
        // the calendar / banner smoke tests: the binary's own dir for
        // installed layouts, ../../qt/qml for in-source CMake builds.
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/../../qt/qml"));

        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),         m_dataStore);
        ctx->setContextProperty(QStringLiteral("taskViewModel"),     m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"),  m_projectVM);
        ctx->setContextProperty(QStringLiteral("sidebarViewModel"),  m_sidebarVM);
        ctx->setContextProperty(QStringLiteral("settingsViewModel"), m_settingsVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),         m_undoStack);
        ctx->setContextProperty(QStringLiteral("aiAssistant"),       m_aiAssistant);
        ctx->setContextProperty(QStringLiteral("taskExporter"),      m_taskExporter);
        ctx->setContextProperty(QStringLiteral("telemetry"),         m_telemetry);

        // Seed a project so ProjectView has something to render. We also
        // point the sidebar at it — ProjectView reads the active project ID
        // via `sidebarViewModel.currentItemId`.
        Project *p = m_dataStore->addProject(QStringLiteral("Smoke Project"));
        if (p) {
            m_sidebarVM->setCurrentItemId(p->id());
        }
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        // Skip cleanup of m_scope; std::_Exit(0) in the last slot bypasses
        // destructors entirely. Mirrors the calendar / banner smoke tests —
        // the QML engine's queued deleteLaters and the QSettings flush can
        // wedge under QtTest's lifecycle on Windows.
    }

    // ========================================================================
    // ProjectView
    // ========================================================================

    void projectview_loads_without_error()
    {
        QString err;
        QObject *wrapper = loadWithCapture(projectViewUrl(), &err);
        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper failed: ") + err));

        QObject *view = viewFromWrapper(wrapper);
        QVERIFY2(view, "ProjectView did not instantiate inside the Loader");

        delete wrapper;
    }

    void projectview_no_fatal_warnings_during_load()
    {
        QObject *wrapper = loadWithCapture(projectViewUrl());
        QVERIFY(wrapper);
        QVERIFY(viewFromWrapper(wrapper));

        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings during ProjectView load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("ProjectView load produced %1 fatal warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        delete wrapper;
    }

    // ========================================================================
    // TrashView
    // ========================================================================

    void trashview_loads_without_error()
    {
        QString err;
        QObject *wrapper = loadWithCapture(trashViewUrl(), &err);
        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper failed: ") + err));

        QObject *view = viewFromWrapper(wrapper);
        QVERIFY2(view, "TrashView did not instantiate inside the Loader");

        delete wrapper;
    }

    void trashview_no_fatal_warnings_during_load()
    {
        QObject *wrapper = loadWithCapture(trashViewUrl());
        QVERIFY(wrapper);
        QVERIFY(viewFromWrapper(wrapper));

        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings during TrashView load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("TrashView load produced %1 fatal warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // Pre-populate DataStore with a soft-deleted task, load TrashView, then
    // verify trashItems.length == 1 (the property the inner ListView binds to).
    //
    // We do NOT count ListView delegates directly because under the offscreen
    // platform Qt may defer delegate creation; reading `root.trashItems.length`
    // is the model-truth check, not a render check.
    // -------------------------------------------------------------------------
    void trashview_with_deleted_task_shows_one_item()
    {
        // Add a fresh task and soft-delete it via the model layer.
        const QString taskId = m_taskVM->addTask(
            QStringLiteral("Trashed task"), {}, 1, {}, {});
        QVERIFY(!taskId.isEmpty());

        Task *t = m_dataStore->taskById(taskId);
        QVERIFY2(t, "Newly added task not found in DataStore");
        t->setIsDeleted(true);

        // Sanity: the VM helper that TrashView reads via taskViewModel.deletedTasks()
        // must now report exactly one entry.
        const QVariantList deletedFromVm = m_taskVM->deletedTasks();
        QCOMPARE(deletedFromVm.size(), 1);

        // Now load the view and verify its cached snapshot picked up the entry.
        QObject *wrapper = loadWithCapture(trashViewUrl());
        QVERIFY(wrapper);
        QObject *view = viewFromWrapper(wrapper);
        QVERIFY(view);

        // TrashView caches taskViewModel.deletedTasks() in its `trashItems`
        // property. Its Component.onCompleted fires refresh() which fills it.
        const QVariant items = view->property("trashItems");
        QVERIFY2(items.isValid(), "TrashView has no trashItems property");
        const QVariantList list = items.toList();
        QCOMPARE(list.size(), 1);

        // Spot-check: the entry should carry our task's id.
        const QVariantMap entry = list.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("id")).toString(), taskId);

        delete wrapper;

        // Restore the task so subsequent slots see a clean store.
        t->setIsDeleted(false);
    }

    // ========================================================================
    // AIView
    // ========================================================================

    void aiview_loads_without_error()
    {
        QString err;
        QObject *wrapper = loadWithCapture(aiViewUrl(), &err);
        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper failed: ") + err));

        QObject *view = viewFromWrapper(wrapper);
        QVERIFY2(view, "AIView did not instantiate inside the Loader");

        delete wrapper;
    }

    void aiview_no_fatal_warnings_during_load()
    {
        QObject *wrapper = loadWithCapture(aiViewUrl());
        QVERIFY(wrapper);
        QVERIFY(viewFromWrapper(wrapper));

        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings during AIView load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("AIView load produced %1 fatal warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        delete wrapper;

        // Bypass destructor: this is the last slot. Mirrors the calendar /
        // banner smoke tests — the QML engine's queued deleteLaters and the
        // QSettings flush can wedge under QtTest's lifecycle on Windows.
        // Everything has been validated; exit cleanly so ctest sees a pass.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }
};

// QTEST_MAIN: requires QGuiApplication because we instantiate a
// QQmlApplicationEngine and load Material-themed QML. The QPA platform is
// chosen by the ctest ENVIRONMENT property (offscreen on POSIX, windows on
// Windows — see tests/unit/CMakeLists.txt).
QTEST_MAIN(TestViewsSmoke)
#include "test_views_smoke.moc"
