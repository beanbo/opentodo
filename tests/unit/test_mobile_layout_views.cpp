// =============================================================================
// test_mobile_layout_views.cpp
// =============================================================================
//
// PURPOSE
// -------
// Pin Q-Android-Audit fix #1.1: for every filterMode that MobileLayout maps
// to a full-page view, the body Loader (`fullPageLoader`) must become active
// and pick the correct .qml source. Before the audit fix the mobile layout
// had no Loader at all and only the top-bar title updated when the user
// tapped Settings / Productivity / Notifications / etc.
//
// We DO NOT try to load MobileLayout.qml under the offscreen renderer at a
// realistic mobile resolution — the wrapper here mimics the same shape used
// by tests/unit/test_views_smoke.cpp and test_calendar_view_smoke.cpp: load
// the QML inside an outer Item that gives it a parent, walk the children to
// find the `fullPageLoader`, then read `active` and `source` for each of the
// 7 filter modes.
//
// SETUP
// -----
// Mirror test_views_smoke.cpp's context-property registration. MobileLayout
// transitively reads more properties than the audited views, but for this
// smoke test we just need the engine to instantiate the wrapper without a
// fatal load failure; the inner views inside the Loader are NOT instantiated
// because we always read fullPageLoader before letting the loader settle
// (active is a binding; source is computed lazily).
//
// WHY THIS FILE EXISTS (RATHER THAN A SLOT IN test_views_smoke.cpp)
// -----------------------------------------------------------------
// test_views_smoke.cpp targets individual views in isolation. This test
// targets the LAYOUT — a different concern. Keeping them in separate
// executables keeps the per-test scope focused on one thing.
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
#include <QDebug>
#include <QVariant>
#include <QUrl>
#include <QMetaEnum>
#include <atomic>
#include <cstdlib>

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "sync/syncengine.h"
#include "model/project.h"
#include "model/task.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/filterviewmodel.h"
#include "viewmodels/searchviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "viewmodels/syncviewmodel.h"
#include "viewmodels/notificationsviewmodel.h"
#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/taskexporter.h"
#include "services/telemetry.h"
// Pick the same NotificationService backend src/main.cpp picks. The
// header-only StubNotificationService has its vtable emitted only when the
// translation unit defining a virtual method is linked; on Windows we use
// WindowsNotificationService (already a real .cpp) instead. Mirrors the
// pattern used by test_desktop_narrow_layout.cpp / test_qml_load_smoke.cpp.
#include "services/notificationservice.h"
#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
#elif defined(Q_OS_ANDROID)
#  include "services/notificationservice_android.h"
#else
#  include "services/notificationservice_stub.h"
#endif
#include "services/reminderscheduler.h"
#include "services/pomodoroservice.h"
#include "services/updatechecker.h"

// -----------------------------------------------------------------------------
// Captured-message handler: mirrors the established pattern.
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

} // namespace


// =============================================================================
// TestMobileLayoutViews
// =============================================================================
class TestMobileLayoutViews : public QObject
{
    Q_OBJECT

private:
    // Engine + ViewModel graph. Built once in initTestCase, reused across slots.
    QObject                *m_scope            = nullptr;
    DataStore              *m_dataStore        = nullptr;
    FileManager            *m_fileManager      = nullptr;
    SyncEngine             *m_syncEngine       = nullptr;
    UndoStack              *m_undoStack        = nullptr;
    TaskViewModel          *m_taskVM           = nullptr;
    ProjectViewModel       *m_projectVM        = nullptr;
    SidebarViewModel       *m_sidebarVM        = nullptr;
    FilterViewModel        *m_filterVM         = nullptr;
    SearchViewModel        *m_searchVM         = nullptr;
    SettingsViewModel      *m_settingsVM       = nullptr;
    KarmaViewModel         *m_karmaVM          = nullptr;
    SyncViewModel          *m_syncVM           = nullptr;
    NotificationsViewModel *m_notificationsVM  = nullptr;
    AIAssistant            *m_aiAssistant      = nullptr;
    TaskExporter           *m_taskExporter     = nullptr;
    Telemetry              *m_telemetry        = nullptr;
    NotificationService    *m_notifService     = nullptr;
    ReminderScheduler      *m_reminderSched    = nullptr;
    PomodoroService        *m_pomodoroService  = nullptr;
    UpdateChecker          *m_updateChecker    = nullptr;
    QQmlApplicationEngine  *m_engine           = nullptr;

    // Instantiate MobileLayout.qml inside an outer Item with explicit width /
    // height. Returns the wrapper Item; the layout instance is the first child
    // of the Loader inside the wrapper.
    QObject *createWrapper(QString *errOut = nullptr)
    {
        QString qml;
        qml += QStringLiteral("import QtQuick\n");
        qml += QStringLiteral("import QtQuick.Controls.Material\n");
        qml += QStringLiteral("import OpenTodoUI\n");
        qml += QStringLiteral("Item {\n");
        qml += QStringLiteral("    width: 360\n");
        qml += QStringLiteral("    height: 720\n");
        qml += QStringLiteral("    Loader {\n");
        qml += QStringLiteral("        id: layoutLoader\n");
        qml += QStringLiteral("        objectName: \"layoutLoader\"\n");
        qml += QStringLiteral("        anchors.fill: parent\n");
        qml += QStringLiteral("        source: \"qrc:/qt/qml/OpenTodoUI/qml/layouts/MobileLayout.qml\"\n");
        qml += QStringLiteral("    }\n");
        qml += QStringLiteral("}\n");

        QQmlComponent comp(m_engine);
        comp.setData(qml.toUtf8(),
                     QUrl(QStringLiteral("qrc:/test/MobileLayoutWrapper.qml")));
        if (comp.isError()) {
            if (errOut) *errOut = comp.errorString();
            return nullptr;
        }
        QObject *root = comp.create(m_engine->rootContext());
        if (!root) {
            if (errOut) *errOut = QStringLiteral("comp.create() returned null");
            return nullptr;
        }
        // Let the Loader instantiate its source and any deferred bindings.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        return root;
    }

    QObject *layoutFromWrapper(QObject *wrapper) const
    {
        if (!wrapper) return nullptr;
        QObject *loader = wrapper->findChild<QObject*>(QStringLiteral("layoutLoader"));
        if (!loader) return nullptr;
        return loader->property("item").value<QObject*>();
    }

    // Walk the MobileLayout subtree to find the Loader named "fullPageLoader".
    // We declared id: fullPageLoader in the QML but ids alone don't expose
    // children to findChild — we look up by objectName when we can, else
    // fall through to a recursive walk that matches the source URL pattern.
    QObject *findFullPageLoader(QObject *layout) const
    {
        if (!layout) return nullptr;
        // MobileLayout tags the Loader with objectName "fullPageLoader" (QML
        // ids are not exposed as object names). The old heuristic matched on
        // the desktop *View.qml sources and silently picked another Loader
        // once the mobile redesign switched to the M*Screen.qml screens.
        return layout->findChild<QObject *>(QStringLiteral("fullPageLoader"));
    }

private slots:

    void initTestCase()
    {
        // Per-test isolated org/app namespace.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_MobileLayoutViews"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_mobile_layout_views"));

        QQuickStyle::setStyle(QStringLiteral("Material"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);

        m_scope            = new QObject;
        m_dataStore        = new DataStore(m_scope);
        m_fileManager      = new FileManager(m_dataStore, m_scope);
        // SyncViewModel's constructor unconditionally connects to its
        // SyncEngine pointer and calls gdriveClient() — passing nullptr
        // segfaults. Construct a real engine; it never reaches the network
        // because no GDrive credentials are configured in test mode.
        m_syncEngine       = new SyncEngine(m_dataStore, m_scope);
        m_undoStack        = new UndoStack(m_scope);
        m_taskVM           = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM        = new ProjectViewModel(m_dataStore, m_scope);
        m_sidebarVM        = new SidebarViewModel(m_dataStore, m_projectVM, m_scope);
        m_filterVM         = new FilterViewModel(m_dataStore, m_scope);
        m_searchVM         = new SearchViewModel(m_dataStore, m_scope);
        m_settingsVM       = new SettingsViewModel(m_dataStore, m_scope);
        m_karmaVM          = new KarmaViewModel(m_dataStore, m_scope);
#if defined(Q_OS_WIN)
        m_notifService     = new WindowsNotificationService(m_scope);
#elif defined(Q_OS_ANDROID)
        m_notifService     = new AndroidNotificationService(m_scope);
#else
        m_notifService     = new StubNotificationService(m_scope);
#endif
        m_reminderSched    = new ReminderScheduler(m_dataStore,
                                                   m_notifService, m_scope);
        Q_UNUSED(m_reminderSched);
        m_pomodoroService  = new PomodoroService(m_notifService, m_scope);
        m_updateChecker    = new UpdateChecker(m_scope);
        m_notificationsVM  = new NotificationsViewModel(m_dataStore,
                                                       m_syncEngine, m_scope);
        m_syncVM           = new SyncViewModel(m_syncEngine, m_scope);
        m_aiAssistant      = new AIAssistant(m_dataStore, m_scope);
        m_taskExporter     = new TaskExporter(m_dataStore, m_scope);
        m_telemetry        = new Telemetry(m_scope);

        m_engine = new QQmlApplicationEngine(m_scope);

        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/../../qt/qml"));

        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),             m_dataStore);
        ctx->setContextProperty(QStringLiteral("fileManager"),           m_fileManager);
        ctx->setContextProperty(QStringLiteral("taskViewModel"),         m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"),      m_projectVM);
        ctx->setContextProperty(QStringLiteral("sidebarViewModel"),      m_sidebarVM);
        ctx->setContextProperty(QStringLiteral("filterViewModel"),       m_filterVM);
        ctx->setContextProperty(QStringLiteral("searchViewModel"),       m_searchVM);
        ctx->setContextProperty(QStringLiteral("settingsViewModel"),     m_settingsVM);
        ctx->setContextProperty(QStringLiteral("karmaViewModel"),        m_karmaVM);
        ctx->setContextProperty(QStringLiteral("syncViewModel"),         m_syncVM);
        ctx->setContextProperty(QStringLiteral("notificationsViewModel"), m_notificationsVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),             m_undoStack);
        ctx->setContextProperty(QStringLiteral("aiAssistant"),           m_aiAssistant);
        ctx->setContextProperty(QStringLiteral("taskExporter"),          m_taskExporter);
        ctx->setContextProperty(QStringLiteral("telemetry"),             m_telemetry);
        ctx->setContextProperty(QStringLiteral("notificationService"),   m_notifService);
        ctx->setContextProperty(QStringLiteral("pomodoroService"),       m_pomodoroService);
        ctx->setContextProperty(QStringLiteral("updateChecker"),         m_updateChecker);

        // Seed at least one project so MobileLayout's Project view branch is
        // also valid if we ever extend the test.
        Project *p = m_dataStore->addProject(QStringLiteral("Smoke Project"));
        if (p) {
            m_sidebarVM->setCurrentItemId(p->id());
        }
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        // Skip object cleanup — see test_views_smoke.cpp's rationale.
    }

    // -------------------------------------------------------------------------
    // mobilelayout_loads
    //
    // Sanity check: MobileLayout.qml instantiates at all under the test
    // engine. Without this the rest of the slots are meaningless.
    // -------------------------------------------------------------------------
    void mobilelayout_loads()
    {
        QString err;
        QObject *wrapper = createWrapper(&err);
        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper load failed: ") + err));

        QObject *layout = layoutFromWrapper(wrapper);
        QVERIFY2(layout, "MobileLayout did not instantiate inside the Loader");

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // each_full_page_filter_mode_picks_correct_source
    //
    // The core regression pin. For each of the seven filter modes mapped to
    // a full-page view, we:
    //   1. Set taskViewModel.filterMode on the view model.
    //   2. Instantiate the wrapper (fresh per mode so bindings re-evaluate).
    //   3. Find the fullPageLoader inside the MobileLayout subtree.
    //   4. Assert its `source` ends with the expected filename.
    //
    // We DO NOT assert sourceComponent because the QML uses `source:` (string),
    // not `sourceComponent:`. Loader exposes both; for string sources, the
    // Loader emits onLoaded after instantiating the inner item. We don't
    // need to wait for the inner item to load — the source binding is
    // evaluated immediately when filterMode changes, before the inner
    // component's tree is built.
    // -------------------------------------------------------------------------
    void each_full_page_filter_mode_picks_correct_source()
    {
        struct ModeExpectation {
            TaskViewModel::FilterMode mode;
            QString                    filename;
        };

        const ModeExpectation table[] = {
            { TaskViewModel::FilterMode::Settings,      QStringLiteral("MSettingsScreen.qml") },
            { TaskViewModel::FilterMode::Productivity,  QStringLiteral("MProductivityScreen.qml") },
            { TaskViewModel::FilterMode::Notifications, QStringLiteral("MNotificationsScreen.qml") },
            { TaskViewModel::FilterMode::AI,            QStringLiteral("MAIScreen.qml") },
            { TaskViewModel::FilterMode::Board,         QStringLiteral("MBoardScreen.qml") },
            { TaskViewModel::FilterMode::Calendar,      QStringLiteral("MCalendarScreen.qml") },
            { TaskViewModel::FilterMode::Trash,         QStringLiteral("MTrashScreen.qml") },
        };

        // Per-mode fresh wrapper. Re-using a single wrapper is technically
        // possible (the Loader's source binding does re-evaluate), but a
        // fresh wrapper guarantees no leftover deferred bindings from the
        // previous mode taint the captured `source` we read.
        for (const ModeExpectation &row : table) {
            const QString modeName =
                QMetaEnum::fromType<TaskViewModel::FilterMode>()
                    .valueToKey(static_cast<int>(row.mode));

            m_taskVM->setFilterMode(row.mode);

            QString err;
            QObject *wrapper = createWrapper(&err);
            QVERIFY2(wrapper,
                     qPrintable(QStringLiteral("Wrapper load failed for mode %1: %2")
                                    .arg(modeName).arg(err)));

            QObject *layout = layoutFromWrapper(wrapper);
            QVERIFY2(layout,
                     qPrintable(QStringLiteral("MobileLayout did not instantiate for mode %1")
                                    .arg(modeName)));

            QObject *fullPageLoader = findFullPageLoader(layout);
            QVERIFY2(fullPageLoader,
                     qPrintable(QStringLiteral("fullPageLoader not found in MobileLayout subtree (mode %1)")
                                    .arg(modeName)));

            // The Loader's `active` property must be true for any full-page
            // filter mode. (Inbox / Today / etc. flip it back to false; those
            // are exercised implicitly by the wrapper's initial state.)
            const QVariant activeVar = fullPageLoader->property("active");
            QVERIFY2(activeVar.isValid(),
                     qPrintable(QStringLiteral("fullPageLoader has no `active` property (mode %1)")
                                    .arg(modeName)));
            QVERIFY2(activeVar.toBool(),
                     qPrintable(QStringLiteral("fullPageLoader.active is false in mode %1 (expected true)")
                                    .arg(modeName)));

            const QString source = fullPageLoader->property("source").toUrl().toString();
            QVERIFY2(source.contains(row.filename),
                     qPrintable(QStringLiteral("fullPageLoader.source for mode %1 was \"%2\"; expected to contain \"%3\"")
                                    .arg(modeName).arg(source).arg(row.filename)));

            delete wrapper;
        }
    }

    // -------------------------------------------------------------------------
    // inbox_mode_keeps_full_page_loader_inactive
    //
    // The negative case: when filterMode is set to Inbox (the home / default),
    // the body Loader must NOT be active so the task list renders. This pins
    // the gate that hides the inbox content when in full-page view; if the
    // gate is removed the loader would always be active and the inbox
    // wouldn't render.
    // -------------------------------------------------------------------------
    void inbox_mode_keeps_full_page_loader_inactive()
    {
        m_taskVM->setFilterMode(TaskViewModel::FilterMode::Inbox);

        QString err;
        QObject *wrapper = createWrapper(&err);
        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper load failed: ") + err));

        QObject *layout = layoutFromWrapper(wrapper);
        QVERIFY(layout);

        QObject *fullPageLoader = findFullPageLoader(layout);
        QVERIFY2(fullPageLoader, "fullPageLoader not found in MobileLayout subtree");

        const QVariant activeVar = fullPageLoader->property("active");
        QVERIFY(activeVar.isValid());
        QVERIFY2(!activeVar.toBool(),
                 "fullPageLoader.active was true on Inbox view; expected false");

        const QString source = fullPageLoader->property("source").toUrl().toString();
        // When inactive the source binding still evaluates to "" (the switch's
        // default branch). Loader does not instantiate the inner item, but
        // the property is observable.
        QVERIFY2(source.isEmpty(),
                 qPrintable(QStringLiteral("fullPageLoader.source on Inbox was \"%1\"; expected empty")
                                .arg(source)));

        delete wrapper;
    }
};

// QTEST_MAIN: requires QGuiApplication for the Material-themed Qml engine.
#include "test_no_teardown_main.h"
OPENTODO_QTEST_MAIN_NO_TEARDOWN(TestMobileLayoutViews)
#include "test_mobile_layout_views.moc"
