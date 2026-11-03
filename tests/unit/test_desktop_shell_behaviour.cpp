// =============================================================================
// test_desktop_shell_behaviour.cpp
// =============================================================================
//
// Behavioural pins for the desktop QML shell (qml/layouts/DesktopLayout.qml)
// and NotificationsView. Unlike the load smoke tests, each slot drives the
// real ViewModels and checks what the QML did:
//
//   1. The list-mode TaskEditor follows taskViewModel.selectedTask A → B
//      (it used to keep editing A: `task` was only assigned in onLoaded).
//   2. Full-page views that open tasks without an editor of their own
//      (Calendar here) get the layout's full-page editor host; the hidden
//      list-mode editor is not loaded behind them.
//   3. Opening a task while a full-page view with no editor host is shown
//      (AI here) navigates to a view that can show it.
//   4. Ctrl+1..6 navigate (they called the non-invokable
//      setCurrentView()/setFilterMode() and threw a TypeError).
//   5. Shift+T defers the hovered task to tomorrow (it passed a Russian
//      phrase that the default "en" date parser did not understand).
//   6. NotificationsView maps `kind` with Notification::Kind's numbering
//      (2 used to be treated as ReminderFired, shifting every later kind).
//   7. Sidebar "move filter up" swaps with the neighbour even when orders
//      are tied (it used to set order-1, which left ties unchanged).
//   8. The top-bar expand-all / collapse-all token host reaches both the
//      list rows and ProjectView rows (TaskItem.expansionHost).
//
// Shortcuts are triggered by emitting their `activated` signal directly —
// the harness has no focused window for real key events, and what is pinned
// here is the handler body, not Qt's key routing.
//
// Harness shape mirrors test_desktop_narrow_layout.cpp (DesktopLayout inside
// a wrapper Item, real ViewModels as context properties). main.cpp's
// navigationRequested → TaskViewModel wiring is replicated in initTestCase().
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDate>
#include <QSet>
#include <algorithm>
#include <functional>

#include "test_no_teardown_main.h"

#include "storage/datastore.h"
#include "model/filter.h"
#include "model/task.h"
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

#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/taskexporter.h"
#include "services/telemetry.h"

#if defined(Q_OS_WIN)
#  include "services/notificationservice_win.h"
#elif defined(Q_OS_ANDROID)
#  include "services/notificationservice_android.h"
#else
#  include "services/notificationservice_stub.h"
#endif

namespace {

// Depth-first walk over both the QObject tree and the visual item tree:
// Repeater / ListView delegates are not always QObject children of the item
// that visually contains them.
void walkObjects(QObject *root, const std::function<void(QObject *)> &visit)
{
    QSet<QObject *> seen;
    std::function<void(QObject *)> rec = [&](QObject *o) {
        if (!o || seen.contains(o)) return;
        seen.insert(o);
        visit(o);
        const auto kids = o->children();
        for (QObject *c : kids) rec(c);
        if (auto *item = qobject_cast<QQuickItem *>(o)) {
            const auto items = item->childItems();
            for (QQuickItem *c : items) rec(c);
        }
        if (auto *win = qobject_cast<QQuickWindow *>(o))
            rec(win->contentItem());
    };
    rec(root);
}

bool shortcutMatches(QObject *o, const QString &seq)
{
    if (!o->metaObject()->className()
            || QByteArray(o->metaObject()->className()).indexOf("Shortcut") < 0)
        return false;
    if (o->property("sequence").toString() == seq)
        return true;
    const QVariantList seqs = o->property("sequences").toList();
    for (const QVariant &v : seqs)
        if (v.toString() == seq)
            return true;
    return false;
}

void processEventsABit()
{
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

} // namespace

class TestDesktopShellBehaviour : public QObject
{
    Q_OBJECT

private:
    QObject                *m_scope           = nullptr;
    DataStore              *m_dataStore       = nullptr;
    SyncEngine             *m_syncEngine      = nullptr;
    UndoStack              *m_undoStack       = nullptr;
    TaskViewModel          *m_taskVM          = nullptr;
    ProjectViewModel       *m_projectVM       = nullptr;
    SidebarViewModel       *m_sidebarVM       = nullptr;
    FilterViewModel        *m_filterVM        = nullptr;
    SearchViewModel        *m_searchVM        = nullptr;
    SettingsViewModel      *m_settingsVM      = nullptr;
    KarmaViewModel         *m_karmaVM         = nullptr;
    SyncViewModel          *m_syncVM          = nullptr;
    NotificationsViewModel *m_notificationsVM = nullptr;
    AIAssistant            *m_aiAssistant     = nullptr;
    TaskExporter           *m_taskExporter    = nullptr;
    Telemetry              *m_telemetry       = nullptr;
    QObject                *m_notificationService = nullptr;
    QQmlApplicationEngine  *m_engine          = nullptr;

    QString m_taskA;
    QString m_taskB;

    // Wrapper hosting `source` through a Loader (objectName "hostLoader").
    // inWindow: use a visible Window instead of a bare Item — ListView only
    // creates delegates on polish, which needs a window. Caller deletes the
    // returned wrapper.
    QObject *createHost(const QString &source, bool inWindow = false)
    {
        QString qml;
        qml += QStringLiteral("import QtQuick\n");
        qml += QStringLiteral("import QtQuick.Controls.Material\n");
        qml += QStringLiteral("import OpenTodoUI\n");
        qml += inWindow ? QStringLiteral("Window {\n    visible: true\n")
                        : QStringLiteral("Item {\n");
        qml += QStringLiteral("    width: 1280\n");
        qml += QStringLiteral("    height: 800\n");
        qml += QStringLiteral("    Loader {\n");
        qml += QStringLiteral("        objectName: \"hostLoader\"\n");
        qml += QStringLiteral("        anchors.fill: parent\n");
        qml += QStringLiteral("        source: \"") + source + QStringLiteral("\"\n");
        qml += QStringLiteral("    }\n");
        qml += QStringLiteral("}\n");
        QQmlComponent comp(m_engine);
        comp.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/test/ShellHost.qml")));
        if (comp.isError()) {
            qWarning().noquote() << comp.errorString();
            return nullptr;
        }
        QObject *root = comp.create(m_engine->rootContext());
        processEventsABit();
        return root;
    }

    static QObject *hostedItem(QObject *wrapper)
    {
        QObject *loader = nullptr;
        walkObjects(wrapper, [&](QObject *o) {
            if (!loader && o->objectName() == QStringLiteral("hostLoader"))
                loader = o;
        });
        return loader ? loader->property("item").value<QObject *>() : nullptr;
    }

    QObject *createLayout()
    {
        return createHost(QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/layouts/DesktopLayout.qml"));
    }

    static QObject *findShortcut(QObject *root, const QString &seq)
    {
        QObject *found = nullptr;
        walkObjects(root, [&](QObject *o) {
            if (!found && shortcutMatches(o, seq))
                found = o;
        });
        return found;
    }

    static QObject *editorOf(QObject *layout, const char *loaderName)
    {
        QObject *loader = layout->findChild<QObject *>(QString::fromLatin1(loaderName));
        return loader ? loader->property("item").value<QObject *>() : nullptr;
    }

    // Back to a neutral list view with nothing open.
    void resetState()
    {
        m_taskVM->setSelectedTask(nullptr);
        m_taskVM->setHoveredTaskId(QString());
        m_sidebarVM->navigateToInbox();
        processEventsABit();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_DesktopShell"));
        QCoreApplication::setApplicationName(QStringLiteral("test_desktop_shell_behaviour"));
        QQuickStyle::setStyle(QStringLiteral("Material"));

        m_scope           = new QObject;
        m_dataStore       = new DataStore(m_scope);
        m_syncEngine      = new SyncEngine(m_dataStore, m_scope);
        m_undoStack       = new UndoStack(m_scope);
        m_taskVM          = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM       = new ProjectViewModel(m_dataStore, m_scope);
        m_sidebarVM       = new SidebarViewModel(m_dataStore, m_projectVM, m_scope);
        m_filterVM        = new FilterViewModel(m_dataStore, m_scope);
        m_searchVM        = new SearchViewModel(m_dataStore, m_scope);
        m_settingsVM      = new SettingsViewModel(m_dataStore, m_scope);
        m_karmaVM         = new KarmaViewModel(m_dataStore, m_scope);
        m_syncVM          = new SyncViewModel(m_syncEngine, m_scope);
        m_notificationsVM = new NotificationsViewModel(m_dataStore, m_syncEngine, m_scope);
        m_aiAssistant     = new AIAssistant(m_dataStore, m_scope);
        m_taskExporter    = new TaskExporter(m_dataStore, m_scope);
        m_telemetry       = new Telemetry(m_scope);
#if defined(Q_OS_WIN)
        m_notificationService = new WindowsNotificationService(m_scope);
#elif defined(Q_OS_ANDROID)
        m_notificationService = new AndroidNotificationService(m_scope);
#else
        m_notificationService = new StubNotificationService(m_scope);
#endif

        // Same shape as src/main.cpp's navigationRequested wiring.
        TaskViewModel *taskVM = m_taskVM;
        connect(m_sidebarVM, &SidebarViewModel::navigationRequested, taskVM,
                [taskVM](const QString &view, const QString &itemId) {
            taskVM->setFilterMode(TaskViewModel::filterModeFromString(view));
            if (view == QStringLiteral("project"))
                taskVM->setCurrentProjectId(itemId);
        });

        // A bare DataStore has no Inbox (FileManager creates it on first run).
        Project *inbox = m_dataStore->addProject(QStringLiteral("Inbox"));
        QVERIFY(inbox);
        inbox->setIsInbox(true);
        const QString inboxId = inbox->id();
        Task *a = m_dataStore->addTask(QStringLiteral("Task A"), inboxId);
        Task *b = m_dataStore->addTask(QStringLiteral("Task B"), inboxId);
        QVERIFY(a && b);
        m_taskA = a->id();
        m_taskB = b->id();

        m_engine = new QQmlApplicationEngine(m_scope);
        m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../../qt/qml"));

        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),              m_dataStore);
        ctx->setContextProperty(QStringLiteral("syncEngine"),             m_syncEngine);
        ctx->setContextProperty(QStringLiteral("gdriveClient"),           m_syncEngine->gdriveClient());
        ctx->setContextProperty(QStringLiteral("taskViewModel"),          m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"),       m_projectVM);
        ctx->setContextProperty(QStringLiteral("sidebarViewModel"),       m_sidebarVM);
        ctx->setContextProperty(QStringLiteral("filterViewModel"),        m_filterVM);
        ctx->setContextProperty(QStringLiteral("searchViewModel"),        m_searchVM);
        ctx->setContextProperty(QStringLiteral("settingsViewModel"),      m_settingsVM);
        ctx->setContextProperty(QStringLiteral("karmaViewModel"),         m_karmaVM);
        ctx->setContextProperty(QStringLiteral("syncViewModel"),          m_syncVM);
        ctx->setContextProperty(QStringLiteral("notificationsViewModel"), m_notificationsVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),              m_undoStack);
        ctx->setContextProperty(QStringLiteral("aiAssistant"),            m_aiAssistant);
        ctx->setContextProperty(QStringLiteral("taskExporter"),           m_taskExporter);
        ctx->setContextProperty(QStringLiteral("telemetry"),              m_telemetry);
        ctx->setContextProperty(QStringLiteral("notificationService"),    m_notificationService);
        // main.cpp always sets shareManager; ProjectView null-checks it (same
        // as test_views_smoke).
        ctx->setContextProperty(QStringLiteral("shareManager"),           static_cast<QObject *>(nullptr));
    }

    void listEditor_followsSelectedTask()
    {
        resetState();
        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY2(layout, "DesktopLayout did not load");

        m_taskVM->setSelectedTask(m_dataStore->taskById(m_taskA));
        processEventsABit();
        QObject *editor = editorOf(layout, "listTaskEditorLoader");
        QVERIFY2(editor, "list-mode TaskEditor did not load for a selected task");
        QCOMPARE(editor->property("taskId").toString(), m_taskA);

        // Clicking another row while the editor is open must retarget it.
        m_taskVM->setSelectedTask(m_dataStore->taskById(m_taskB));
        processEventsABit();
        editor = editorOf(layout, "listTaskEditorLoader");
        QVERIFY(editor);
        QCOMPARE(editor->property("taskId").toString(), m_taskB);

        delete wrapper;
    }

    void fullPageCalendar_hostsEditor()
    {
        resetState();
        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY(layout);

        m_taskVM->setFilterMode(TaskViewModel::FilterMode::Calendar);
        processEventsABit();
        m_taskVM->openTaskById(m_taskA);
        processEventsABit();

        QObject *editor = editorOf(layout, "fullPageTaskEditorLoader");
        QVERIFY2(editor, "no TaskEditor next to the Calendar view after openTaskById");
        QCOMPARE(editor->property("taskId").toString(), m_taskA);
        // The list-mode editor sits in the hidden task area — not loaded.
        QObject *listLoader = layout->findChild<QObject *>(QStringLiteral("listTaskEditorLoader"));
        QVERIFY(listLoader);
        QCOMPARE(listLoader->property("active").toBool(), false);

        m_taskVM->openTaskById(m_taskB);
        processEventsABit();
        editor = editorOf(layout, "fullPageTaskEditorLoader");
        QVERIFY(editor);
        QCOMPARE(editor->property("taskId").toString(), m_taskB);

        delete wrapper;
    }

    void openTaskFromViewWithoutEditor_navigatesToList()
    {
        resetState();
        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY(layout);

        m_taskVM->setFilterMode(TaskViewModel::FilterMode::AI);
        processEventsABit();
        m_taskVM->openTaskById(m_taskA);   // e.g. an OS notification click
        processEventsABit();

        // Task A lives in the Inbox → the layout jumps there and the list
        // editor shows it.
        QCOMPARE(m_taskVM->filterMode(), TaskViewModel::FilterMode::Inbox);
        QObject *editor = editorOf(layout, "listTaskEditorLoader");
        QVERIFY(editor);
        QCOMPARE(editor->property("taskId").toString(), m_taskA);

        delete wrapper;
    }

    void ctrlNumber_navigates()
    {
        resetState();
        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY(layout);

        QObject *ctrl2 = findShortcut(layout, QStringLiteral("Ctrl+2"));
        QVERIFY2(ctrl2, "Ctrl+2 shortcut not found");
        QVERIFY(QMetaObject::invokeMethod(ctrl2, "activated"));
        processEventsABit();
        QCOMPARE(m_sidebarVM->currentView(), QStringLiteral("today"));
        QCOMPARE(m_taskVM->filterMode(), TaskViewModel::FilterMode::Today);

        QObject *ctrl6 = findShortcut(layout, QStringLiteral("Ctrl+6"));
        QVERIFY(ctrl6);
        QVERIFY(QMetaObject::invokeMethod(ctrl6, "activated"));
        processEventsABit();
        QCOMPARE(m_taskVM->filterMode(), TaskViewModel::FilterMode::Notifications);

        QObject *ctrl1 = findShortcut(layout, QStringLiteral("Ctrl+1"));
        QVERIFY(ctrl1);
        QVERIFY(QMetaObject::invokeMethod(ctrl1, "activated"));
        processEventsABit();
        QCOMPARE(m_taskVM->filterMode(), TaskViewModel::FilterMode::Inbox);

        delete wrapper;
    }

    void shiftT_defersHoveredTaskToTomorrow()
    {
        resetState();
        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY(layout);

        m_taskVM->setHoveredTaskId(m_taskA);
        QObject *shiftT = findShortcut(layout, QStringLiteral("Shift+T"));
        QVERIFY2(shiftT, "Shift+T shortcut not found");
        QVERIFY(QMetaObject::invokeMethod(shiftT, "activated"));
        processEventsABit();

        Task *a = m_dataStore->taskById(m_taskA);
        QVERIFY(a);
        QCOMPARE(a->dueDate(), QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd")));

        delete wrapper;
    }

    void sidebarFilterMoveUp_reordersEvenWithTiedOrders()
    {
        resetState();
        // Three filters sharing order 0 (legacy data / earlier ±1 ties):
        // "move up" used to set order-1 → still tied/unchanged.
        const QString f1 = m_filterVM->addFilter(QStringLiteral("F1"), QStringLiteral("today"));
        const QString f2 = m_filterVM->addFilter(QStringLiteral("F2"), QStringLiteral("p1"));
        const QString f3 = m_filterVM->addFilter(QStringLiteral("F3"), QStringLiteral("overdue"));
        QVERIFY(!f1.isEmpty() && !f2.isEmpty() && !f3.isEmpty());
        for (const QString &id : {f1, f2, f3})
            QVERIFY(m_filterVM->updateFilter(id, QStringLiteral("order"), 0));

        QObject *wrapper = createLayout();
        QVERIFY(wrapper);
        QObject *layout = hostedItem(wrapper);
        QVERIFY(layout);

        QObject *sidebar = nullptr;
        walkObjects(layout, [&](QObject *o) {
            if (!sidebar && o->metaObject()->indexOfMethod("_moveFilter(QVariant,QVariant)") >= 0)
                sidebar = o;
        });
        QVERIFY2(sidebar, "SidebarContent (_moveFilter) not found");

        auto visibleOrder = [this]() {
            QList<Filter *> fs = m_dataStore->allFilters();
            std::stable_sort(fs.begin(), fs.end(),
                             [](const Filter *a, const Filter *b) { return a->order() < b->order(); });
            QStringList ids;
            for (const Filter *f : fs) ids << f->id();
            return ids;
        };
        // Whatever order the view shows now, "move up" on row 1 must swap
        // rows 0 and 1 and leave unique orders behind.
        processEventsABit();
        const QStringList before = [&]() {
            // The view's row order = FilterViewModel's (sorted by order).
            QStringList ids;
            for (int i = 0; i < m_filterVM->rowCount(); ++i)
                ids << m_filterVM->data(m_filterVM->index(i), FilterViewModel::IdRole).toString();
            return ids;
        }();
        QCOMPARE(before.size(), 3);

        QVERIFY(QMetaObject::invokeMethod(sidebar, "_moveFilter",
                                          Q_ARG(QVariant, QVariant(1)),
                                          Q_ARG(QVariant, QVariant(-1))));
        processEventsABit();

        const QStringList after = visibleOrder();
        QCOMPARE(after, (QStringList{before.at(1), before.at(0), before.at(2)}));
        QSet<int> orders;
        for (const Filter *f : m_dataStore->allFilters()) orders.insert(f->order());
        QCOMPARE(orders.size(), 3);

        delete wrapper;
        for (const QString &id : {f1, f2, f3})
            m_filterVM->deleteFilter(id);
    }

    // Top-bar expand-all / collapse-all bump tokens on ONE host object; list
    // rows and ProjectView rows must both receive it as `expansionHost`.
    void expandCollapseAll_reachesListAndProjectRows()
    {
        resetState();
        Project *inbox = m_dataStore->inboxProject();
        Task *listParent = m_dataStore->addTask(QStringLiteral("List parent"),
                                                inbox ? inbox->id() : QString());
        QVERIFY(listParent);
        QVERIFY(!m_taskVM->addSubtask(listParent->id(), QStringLiteral("List child")).isEmpty());

        Project *work = m_dataStore->addProject(QStringLiteral("Work"));
        QVERIFY(work);
        Task *projParent = m_dataStore->addTask(QStringLiteral("Project parent"), work->id());
        QVERIFY(projParent);
        QVERIFY(!m_taskVM->addSubtask(projParent->id(), QStringLiteral("Project child")).isEmpty());

        // ListView delegates only exist once polished → host in a window.
        QObject *wrapper = createHost(
            QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/layouts/DesktopLayout.qml"), /*inWindow=*/true);
        QVERIFY(wrapper);
        if (auto *win = qobject_cast<QQuickWindow *>(wrapper))
            QVERIFY(QTest::qWaitForWindowExposed(win));
        m_sidebarVM->navigateToInbox();
        processEventsABit();

        auto findRow = [&](const QString &taskId) -> QObject * {
            QObject *found = nullptr;
            walkObjects(wrapper, [&](QObject *o) {
                if (!found && o->property("expansionHost").isValid()
                        && o->property("id").toString() == taskId)
                    found = o;
            });
            return found;
        };
        auto bump = [](QObject *host, const char *token) {
            host->setProperty(token, host->property(token).toInt() + 1);
        };

        QObject *listRow = findRow(listParent->id());
        QVERIFY2(listRow, "list TaskItem for the parent task not found");
        QObject *host = listRow->property("expansionHost").value<QObject *>();
        QVERIFY2(host, "list TaskItem has no expansionHost");

        bump(host, "collapseAllToken");
        processEventsABit();
        QCOMPARE(listRow->property("expanded").toBool(), false);
        bump(host, "expandAllToken");
        processEventsABit();
        QCOMPARE(listRow->property("expanded").toBool(), true);

        m_sidebarVM->navigateToProject(work->id());
        processEventsABit();
        QObject *projRow = findRow(projParent->id());
        QVERIFY2(projRow, "ProjectView TaskItem for the parent task not found");
        QCOMPARE(projRow->property("expansionHost").value<QObject *>(), host);

        bump(host, "collapseAllToken");
        processEventsABit();
        QCOMPARE(projRow->property("expanded").toBool(), false);
        bump(host, "expandAllToken");
        processEventsABit();
        QCOMPARE(projRow->property("expanded").toBool(), true);
        bump(host, "collapseAllToken");   // leave persisted state collapsed
        processEventsABit();

        delete wrapper;
    }

    void notificationsView_kindIconsFollowEnum()
    {
        // AchievementUnlocked == 4, ReminderFired == 5 (Notification::Kind).
        m_notificationsVM->clear();
        m_notificationsVM->onAchievementUnlocked(QStringLiteral("Milestone"), QStringLiteral("desc"), 100);
        m_notificationsVM->onReminderFired(m_taskA, QStringLiteral("Task A"), QStringLiteral("body"));

        QObject *wrapper = createHost(QStringLiteral("qrc:/qt/qml/OpenTodoUI/qml/views/NotificationsView.qml"),
                                      /*inWindow=*/true);
        QVERIFY(wrapper);
        QVERIFY2(hostedItem(wrapper), "NotificationsView did not load");
        if (auto *win = qobject_cast<QQuickWindow *>(wrapper))
            QVERIFY(QTest::qWaitForWindowExposed(win));
        processEventsABit();

        QHash<int, QString> iconByKind;
        walkObjects(wrapper, [&](QObject *o) {
            const QVariant icon = o->property("kindIcon");
            const QVariant kind = o->property("kind");
            if (icon.isValid() && kind.isValid())
                iconByKind.insert(kind.toInt(), icon.toString());
        });
        QVERIFY2(iconByKind.contains(Notification::AchievementUnlocked),
                 "no delegate for the achievement notification");
        QVERIFY2(iconByKind.contains(Notification::ReminderFired),
                 "no delegate for the reminder notification");
        QCOMPARE(iconByKind.value(Notification::AchievementUnlocked), QStringLiteral("star"));
        QCOMPARE(iconByKind.value(Notification::ReminderFired), QStringLiteral("bell"));

        delete wrapper;
        m_notificationsVM->clear();
    }
};

OPENTODO_QTEST_MAIN_NO_TEARDOWN(TestDesktopShellBehaviour)
#include "test_desktop_shell_behaviour.moc"
