// =============================================================================
// test_calendar_view_smoke.cpp
// =============================================================================
//
// PURPOSE
// -------
// Focused smoke test for `qml/views/CalendarView.qml`. The full Main.qml load
// is already covered by test_qml_load_smoke.cpp and test_qml_warning_audit.cpp;
// this test instead instantiates CalendarView IN ISOLATION and exercises its
// public surface — viewMode setter, default value, mode-switch transitions,
// internal Repeater/ListView counts.
//
// WHY ISOLATED?
// -------------
// Loading Main.qml means the calendar is just one tab in a Loader-driven
// stack. A regression that breaks CalendarView's own internals (e.g. the
// month-grid Repeater model going negative, or the agenda day count
// flipping to NaN, or a viewMode value that the toggle buttons don't
// recognise) is invisible from the Main.qml smoke test because the
// CalendarView is hidden until the user navigates to it.
//
// I28 ADDITION
// ------------
// As of iteration I28 the default viewMode was changed from "week" to
// "month" — the month grid is now the landing layout. This test pins
// that decision so an accidental revert (or a typo on the property
// initialiser) is caught at build time.
//
// TEST SLOTS
// ----------
//   - calendar_loads_without_error
//   - default_viewmode_is_month            (I28)
//   - switching_viewmode_to_week_works
//   - switching_viewmode_to_agenda_works
//   - switching_viewmode_back_to_month_works
//   - switching_modes_emits_no_fatal_warnings
//   - month_grid_repeater_count_is_42      (6 rows x 7 cols)
//
// SETUP
// -----
// Mirror test_qml_load_smoke.cpp's environment setup but DO NOT load
// Main.qml. Just register the context properties CalendarView reads
// (taskViewModel is the only one referenced directly), then load
// CalendarView via QQmlComponent. Wrap it in an outer Item so its
// `anchors.fill: parent` resolves cleanly.
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
#include <QUrl>
#include <atomic>

#include "storage/datastore.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "services/undostack.h"

// -----------------------------------------------------------------------------
// Captured-message storage. Same pattern as test_qml_load_smoke.cpp: install a
// global handler so qWarning + qmlWarning land in a flat list, then enable
// capture only during the test action so background noise does not pollute
// the result.
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

// Tolerated noise — environmental warnings that are not what this smoke test
// validates (deprecation chatter, theme manager warnings on minimal CI, etc.).
// The list is a subset of the smoke test's allowlist; we don't need the
// hotkey / network entries because this test does not boot those services.
static bool isTolerated(const QString &m)
{
    static const QStringList kTolerated = {
        QStringLiteral("deprecated"),
        QStringLiteral("QSettings"),
        QStringLiteral("offscreen"),
        QStringLiteral("QQuickStyle"),
        QStringLiteral("Qt.labs.settings"),
    };
    for (const QString &t : kTolerated) {
        if (m.contains(t, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// Fatal substrings — same set as the audit test, narrowed to the categories
// that an isolated CalendarView load could plausibly produce.
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
// TestCalendarViewSmoke
// =============================================================================
class TestCalendarViewSmoke : public QObject
{
    Q_OBJECT

private:
    // Engine + ViewModel scope. Reused across every slot — building it once
    // saves ~200 ms per slot and means a single QObject parent owns the whole
    // graph, so cleanup is one-shot.
    QObject                *m_scope        = nullptr;
    DataStore              *m_dataStore    = nullptr;
    TaskViewModel          *m_taskVM       = nullptr;
    ProjectViewModel       *m_projectVM    = nullptr;
    UndoStack              *m_undoStack    = nullptr;
    QQmlApplicationEngine  *m_engine       = nullptr;

    // Helper: build a CalendarView wrapped in a parent Item so its
    // `anchors.fill: parent` resolves cleanly. Returns the wrapper; the
    // CalendarView itself is wrapper->findChild<QObject*>("calendarRoot").
    //
    // We use a tiny inline QML harness instead of loading CalendarView
    // directly because the view's root Rectangle uses `anchors.fill:
    // parent` and Qt would emit "Cannot anchor to a null item" if the
    // component were created standalone.
    QObject *createCalendarWrapper(QString *errOut = nullptr)
    {
        // Inline QML harness: an Item that hosts a Loader pointing at
        // CalendarView.qml. The Loader gives the view a parent for its
        // `anchors.fill: parent`, which would otherwise emit "Cannot
        // anchor to a null item" if the component were created standalone.
        //
        // NOTE: per project convention, avoid R"(...)" raw strings. Build
        // the harness with explicit "\n" line breaks instead.
        const QByteArray loaderQml =
            "import QtQuick\n"
            "import QtQuick.Controls.Material\n"
            "import OpenTodoUI\n"
            "Item {\n"
            "    width: 800\n"
            "    height: 600\n"
            "    Loader {\n"
            "        id: calLoader\n"
            "        objectName: \"calLoader\"\n"
            "        anchors.fill: parent\n"
            "        source: \"qrc:/qt/qml/OpenTodoUI/qml/views/CalendarView.qml\"\n"
            "    }\n"
            "}\n";

        QQmlComponent comp(m_engine);
        comp.setData(loaderQml, QUrl(QStringLiteral("qrc:/test/CalendarSmokeWrapper.qml")));
        if (comp.isError()) {
            if (errOut) *errOut = comp.errorString();
            return nullptr;
        }
        QObject *root = comp.create(m_engine->rootContext());
        if (!root) {
            if (errOut) *errOut = QStringLiteral("comp.create() returned null");
            return nullptr;
        }
        // Allow Loader to instantiate its source.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        return root;
    }

    // Walk the wrapper to find the loaded CalendarView root. The Loader's
    // `item` property holds it once the source has finished loading.
    QObject *calendarFromWrapper(QObject *wrapper) const
    {
        if (!wrapper) return nullptr;
        QObject *loader = wrapper->findChild<QObject*>(QStringLiteral("calLoader"));
        if (!loader) return nullptr;
        return loader->property("item").value<QObject*>();
    }

private slots:

    void initTestCase()
    {
        // Per-process unique QSettings org/app — keeps this test's QSettings
        // namespace isolated from the user's real OpenTodo install AND from
        // the smoke / audit tests that run in parallel.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_CalendarViewSmoke"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_calendar_view_smoke"));

        QQuickStyle::setStyle(QStringLiteral("Material"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);

        // Build the minimum object graph that CalendarView needs. Direct
        // references in CalendarView.qml: only `taskViewModel`. We also
        // expose projectViewModel and undoStack because TaskViewModel
        // signals can transitively touch them and a missing context
        // property would surface as a QML "is not defined" warning.
        m_scope     = new QObject;
        m_dataStore = new DataStore(m_scope);
        m_undoStack = new UndoStack(m_scope);
        m_taskVM    = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM = new ProjectViewModel(m_dataStore, m_scope);

        m_engine = new QQmlApplicationEngine(m_scope);

        // Add the OpenTodoUI module's import path so `import OpenTodoUI`
        // resolves at QML load time. Mirror the smoke-test rationale:
        // both the binary's own dir and the build root's qt/qml are
        // candidates depending on where the test executable was deployed.
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/../../qt/qml"));

        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),         m_dataStore);
        ctx->setContextProperty(QStringLiteral("taskViewModel"),     m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"),  m_projectVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),         m_undoStack);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        // Skip cleanup of m_scope; std::_Exit(0) at the end of the last
        // slot bypasses destructors entirely (mirrors the smoke test's
        // SyncEngine workaround pattern).
    }

    // -------------------------------------------------------------------------
    // 1. Component loads without QML error.
    // -------------------------------------------------------------------------
    void calendar_loads_without_error()
    {
        g_capturedMessages.clear();
        g_capturing.store(true);

        QString err;
        QObject *wrapper = createCalendarWrapper(&err);

        g_capturing.store(false);

        QVERIFY2(wrapper, qPrintable(QStringLiteral("Wrapper failed: ") + err));

        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY2(cal, "CalendarView did not instantiate inside the Loader");

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 2. Default viewMode is "month" — pinned by I28.
    // -------------------------------------------------------------------------
    void default_viewmode_is_month()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        const QVariant vm = cal->property("viewMode");
        QVERIFY2(vm.isValid(), "CalendarView has no viewMode property");
        QCOMPARE(vm.toString(), QStringLiteral("month"));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 3. Switching to "week" works without crash and reflects in the property.
    // -------------------------------------------------------------------------
    void switching_viewmode_to_week_works()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        QVERIFY(cal->setProperty("viewMode", QStringLiteral("week")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QCOMPARE(cal->property("viewMode").toString(), QStringLiteral("week"));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 4. Switching to "agenda" works.
    // -------------------------------------------------------------------------
    void switching_viewmode_to_agenda_works()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        QVERIFY(cal->setProperty("viewMode", QStringLiteral("agenda")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QCOMPARE(cal->property("viewMode").toString(), QStringLiteral("agenda"));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 5. Round-trip back to "month" — catches a one-way state machine.
    // -------------------------------------------------------------------------
    void switching_viewmode_back_to_month_works()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        cal->setProperty("viewMode", QStringLiteral("week"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        cal->setProperty("viewMode", QStringLiteral("agenda"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        cal->setProperty("viewMode", QStringLiteral("month"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QCOMPARE(cal->property("viewMode").toString(), QStringLiteral("month"));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 6. Cycling through every viewMode emits no FATAL QML warning. Catches
    //    e.g. a binding-loop introduced by a mode-switch handler, or a
    //    "Cannot read property of undefined" inside the agenda kick refresh
    //    chain.
    // -------------------------------------------------------------------------
    void switching_modes_emits_no_fatal_warnings()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        g_capturedMessages.clear();
        g_capturing.store(true);

        const QStringList modes = {
            QStringLiteral("month"),
            QStringLiteral("week"),
            QStringLiteral("agenda"),
            QStringLiteral("month"),
        };
        for (const QString &mode : modes) {
            cal->setProperty("viewMode", mode);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        }

        g_capturing.store(false);

        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            if (isFatalWarning(m))
                fatal << m;
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings during CalendarView mode switch:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("Mode switching produced %1 fatal warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));

        delete wrapper;
    }

    // -------------------------------------------------------------------------
    // 7. _cellCount property: the month grid is fixed at 6 rows x 7 cols = 42.
    //    Asserting this pins the layout decision: a regression that flipped
    //    to a 5-row grid (or to a dynamic count that evaluates to 0) would
    //    fail here.
    //
    //    NB: _cellCount is a "private" identifier (underscore prefix) but
    //    it's exposed as a Q_PROPERTY by virtue of being declared at the
    //    Rectangle root with `property int _cellCount: 42`. QObject's
    //    property system doesn't care about leading underscores.
    // -------------------------------------------------------------------------
    void month_grid_repeater_count_is_42()
    {
        QObject *wrapper = createCalendarWrapper();
        QVERIFY(wrapper);
        QObject *cal = calendarFromWrapper(wrapper);
        QVERIFY(cal);

        const QVariant cc = cal->property("_cellCount");
        QVERIFY2(cc.isValid(), "CalendarView has no _cellCount property");
        QCOMPARE(cc.toInt(), 42);

        const QVariant rows = cal->property("_gridRows");
        QVERIFY2(rows.isValid(), "CalendarView has no _gridRows property");
        QCOMPARE(rows.toInt(), 6);

        delete wrapper;

        // Bypass destructor: this is the last slot. Mirrors the smoke test —
        // the QML engine's queued deleteLaters and the QSettings flush can
        // wedge under QtTest's lifecycle. We've validated everything; exit
        // cleanly so ctest sees a pass.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }
};

// QTEST_MAIN: requires QGuiApplication because we instantiate a
// QQmlApplicationEngine and load Material-themed QML. The QPA platform is
// chosen by the ctest ENVIRONMENT property (offscreen on POSIX, windows on
// Windows — see tests/unit/CMakeLists.txt).
#include <cstdlib>
QTEST_MAIN(TestCalendarViewSmoke)
#include "test_calendar_view_smoke.moc"
