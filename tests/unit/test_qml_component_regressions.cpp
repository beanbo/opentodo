// =============================================================================
// test_qml_component_regressions.cpp
// =============================================================================
//
// Behavioural smoke tests for desktop QML components, pinning fixes from the
// global QML-components bug pass. Each slot instantiates one component in
// isolation (inside a wrapper Item) against a real DataStore / TaskViewModel
// and drives it through its JS surface:
//
//   - editorReminders_save_createsReminderWithOffset
//       EditorRemindersSection: Save used to throw (offsetOptions undefined,
//       rem.setMinuteOffset / dataStore.markDataChanged not invokable) — the
//       reminder was half-created and the dialog stayed open.
//   - datePicker_isoQuickButtons_andBindingSurvives
//       DatePicker: emitIsoDates quick buttons yield yyyy-MM-dd; a pick no
//       longer overwrites (and so breaks) the caller's selectedDate binding.
//   - quickAdd_pickerOverride_stillParsesInlineTokens
//       QuickAdd: with a picker override the raw text was stored as the
//       literal title (no NLP) — date / @label / #project were lost.
//   - editorMetaRow_labelEdit_usesCurrentLabels
//       EditorMetaRow: a label edit assigned the _labels binding, so after an
//       in-place change (sync/undo) the next edit wrote stale labels back.
//
// Pattern mirrors test_inapp_banner_smoke.cpp (captured warnings, wrapper
// Item built from a string, OPENTODO_QTEST_MAIN_NO_TEARDOWN).
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDate>
#include <QJSValue>
#include <QQuickItem>
#include <atomic>
#include <cstdio>

#include "storage/datastore.h"
#include "model/project.h"
#include "model/reminder.h"
#include "model/task.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "services/undostack.h"

namespace {

QStringList       g_messages;
std::atomic<bool> g_capturing{false};
QtMessageHandler  g_previousHandler = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_capturing.load() && type != QtDebugMsg && type != QtInfoMsg)
        g_messages.append(msg);
    if (g_previousHandler)
        g_previousHandler(type, ctx, msg);
    else
        std::fprintf(stderr, "%s\n", qPrintable(msg));
}

// JS exceptions / bad assignments surfaced as warnings by the engine.
QStringList scriptErrors()
{
    QStringList out;
    for (const QString &m : std::as_const(g_messages)) {
        if (m.contains(QLatin1String("TypeError"))
            || m.contains(QLatin1String("ReferenceError"))
            || m.contains(QLatin1String("is not a function"))
            || m.contains(QLatin1String("Unable to assign")))
            out << m;
    }
    return out;
}

// First item in the VISUAL tree under `root` whose `prop` equals `value`
// (optionally restricted to objects inheriting `cls`). Repeater delegates and
// Popup content are not QObject children, so findChildren() can't see them;
// a Popup is entered through its contentItem. QML type class names carry
// generated suffixes, so matching goes through properties / C++ bases.
QObject *findByProperty(QObject *root, const char *prop, const QVariant &value,
                        const char *cls = nullptr)
{
    QList<QQuickItem *> stack;
    if (auto *item = qobject_cast<QQuickItem *>(root))
        stack << item;
    else if (auto *content = root->property("contentItem").value<QQuickItem *>())
        stack << content;
    while (!stack.isEmpty()) {
        QQuickItem *it = stack.takeLast();
        if (!cls || it->inherits(cls)) {
            const QVariant v = it->property(prop);
            if (v.isValid() && v.toString() == value.toString())
                return it;
        }
        stack << it->childItems();
    }
    return nullptr;
}

} // namespace

class TestQmlComponentRegressions : public QObject
{
    Q_OBJECT

    QObject               *m_scope     = nullptr;
    DataStore             *m_dataStore = nullptr;
    UndoStack             *m_undoStack = nullptr;
    TaskViewModel         *m_taskVM    = nullptr;
    ProjectViewModel      *m_projectVM = nullptr;
    QQmlApplicationEngine *m_engine    = nullptr;

    QObject *createWrapper(const QString &body, QString *err)
    {
        QString qml;
        qml += QStringLiteral("import QtQuick\n");
        qml += QStringLiteral("import QtQuick.Controls.Material\n");
        qml += QStringLiteral("import OpenTodoUI\n");
        qml += QStringLiteral("Item {\n    width: 900\n    height: 900\n");
        qml += body;
        qml += QStringLiteral("}\n");
        QQmlComponent comp(m_engine);
        comp.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/test/ComponentRegressionWrapper.qml")));
        if (comp.isError()) {
            *err = comp.errorString();
            return nullptr;
        }
        QObject *root = comp.create(m_engine->rootContext());
        if (!root)
            *err = QStringLiteral("create() returned null");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        return root;
    }

    static QString isoDate(const QDate &d) { return d.toString(QStringLiteral("yyyy-MM-dd")); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_QmlComponentRegressions"));
        QCoreApplication::setApplicationName(QStringLiteral("test_qml_component_regressions"));
        QQuickStyle::setStyle(QStringLiteral("Material"));
        g_previousHandler = qInstallMessageHandler(&captureHandler);

        m_scope     = new QObject;
        m_dataStore = new DataStore(m_scope);
        // The task due phrases below are Russian ("завтра").
        m_dataStore->setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        m_undoStack = new UndoStack(m_scope);
        m_taskVM    = new TaskViewModel(m_dataStore, m_scope);
        m_taskVM->setUndoStack(m_undoStack);
        m_projectVM = new ProjectViewModel(m_dataStore, m_scope);

        m_engine = new QQmlApplicationEngine(m_scope);
        m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../../qt/qml"));
        QQmlContext *ctx = m_engine->rootContext();
        ctx->setContextProperty(QStringLiteral("dataStore"),        m_dataStore);
        ctx->setContextProperty(QStringLiteral("taskViewModel"),    m_taskVM);
        ctx->setContextProperty(QStringLiteral("projectViewModel"), m_projectVM);
        ctx->setContextProperty(QStringLiteral("undoStack"),        m_undoStack);
    }

    void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }

    // ── C1 ──────────────────────────────────────────────────────────────────
    void editorReminders_save_createsReminderWithOffset()
    {
        const QString taskId = m_taskVM->addTask(QStringLiteral("Reminder host"));
        QVERIFY(!taskId.isEmpty());

        g_messages.clear();
        g_capturing.store(true);
        QString err;
        QObject *wrapper = createWrapper(
            QStringLiteral("    EditorRemindersSection { objectName: \"sec\"; width: 400; taskId: \"%1\" }\n")
                .arg(taskId), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *sec = wrapper->findChild<QObject *>(QStringLiteral("sec"));
        QVERIFY(sec);
        QObject *dlg = sec->findChild<QObject *>(QStringLiteral("addReminderDialog"));
        QVERIFY2(dlg, "addReminderDialog not found");

        // offsetOptions must resolve on the Dialog (it was undefined there).
        QVariant opts = dlg->property("offsetOptions");
        if (opts.metaType() == QMetaType::fromType<QJSValue>())
            opts = opts.value<QJSValue>().toVariant();
        QCOMPARE(opts.toList().size(), 8);

        dlg->setProperty("activeTab", 0);
        dlg->setProperty("pickedDate", QStringLiteral("2030-01-15"));
        dlg->setProperty("pickedTime", QStringLiteral("09:30"));
        dlg->setProperty("minuteOffsetIndex", 2);   // "За 10 минут"
        QVERIFY(dlg->property("canSave").toBool());
        QVERIFY(QMetaObject::invokeMethod(dlg, "save"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));

        const auto rems = m_dataStore->remindersForTask(taskId);
        QCOMPARE(rems.size(), 1);
        QCOMPARE(rems.first()->minuteOffset(), 10);
        QVERIFY2(rems.first()->due().datetime.startsWith(QLatin1String("2030-01-15T09:30")),
                 qPrintable(rems.first()->due().datetime));

        QObject *rep = sec->findChild<QObject *>(QStringLiteral("reminderRepeater"));
        QVERIFY(rep);
        QCOMPARE(rep->property("count").toInt(), 1);

        delete wrapper;
    }

    // ── C3 / C23 ────────────────────────────────────────────────────────────
    void datePicker_isoQuickButtons_andBindingSurvives()
    {
        g_messages.clear();
        g_capturing.store(true);
        QString err;
        QObject *wrapper = createWrapper(QStringLiteral(
            "    property string sel: \"2030-01-01\"\n"
            "    DatePicker { objectName: \"iso\"; emitIsoDates: true; selectedDate: parent.sel }\n"
            "    DatePicker { objectName: \"nlp\" }\n"), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *iso = wrapper->findChild<QObject *>(QStringLiteral("iso"));
        QObject *nlp = wrapper->findChild<QObject *>(QStringLiteral("nlp"));
        QVERIFY(iso && nlp);

        const QString tomorrowLabel = QStringLiteral("Завтра");
        QObject *isoBtn = findByProperty(iso, "text", tomorrowLabel, "QQuickAbstractButton");
        QObject *nlpBtn = findByProperty(nlp, "text", tomorrowLabel, "QQuickAbstractButton");
        QVERIFY2(isoBtn && nlpBtn, "quick 'Завтра' buttons not found");

        QSignalSpy isoSpy(iso, SIGNAL(dateSelected(QString)));
        QSignalSpy nlpSpy(nlp, SIGNAL(dateSelected(QString)));
        QVERIFY(QMetaObject::invokeMethod(isoBtn, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(nlpBtn, "clicked"));
        QCOMPARE(isoSpy.count(), 1);
        QCOMPARE(nlpSpy.count(), 1);
        QCOMPARE(isoSpy.at(0).at(0).toString(), isoDate(QDate::currentDate().addDays(1)));
        QCOMPARE(nlpSpy.at(0).at(0).toString(), QStringLiteral("tomorrow"));

        // The pick must not have replaced the caller's binding.
        wrapper->setProperty("sel", QStringLiteral("2031-02-02"));
        QCOMPARE(iso->property("selectedDate").toString(), QStringLiteral("2031-02-02"));
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        delete wrapper;
    }

    // ── C4 / C24 ────────────────────────────────────────────────────────────
    void quickAdd_pickerOverride_stillParsesInlineTokens()
    {
        Project *work = m_dataStore->addProject(QStringLiteral("Работа"));
        QVERIFY(work);

        g_messages.clear();
        g_capturing.store(true);
        QString err;
        QObject *wrapper = createWrapper(
            QStringLiteral("    QuickAdd { objectName: \"qa\"; width: 700 }\n"), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *qa = wrapper->findChild<QObject *>(QStringLiteral("qa"));
        QVERIFY(qa);
        QObject *input = qa->findChild<QObject *>(QStringLiteral("quickAddInput"));
        QVERIFY(input);

        input->setProperty("text", QStringLiteral("Позвонить маме завтра @дом #Работа"));
        qa->setProperty("selectedPriority", 1);          // P1 via the picker
        QVERIFY(QMetaObject::invokeMethod(qa, "submitTask"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));

        Task *created = nullptr;
        for (Task *t : m_dataStore->allTasks()) {
            if (t->content().startsWith(QStringLiteral("Позвонить маме")))
                created = t;
        }
        QVERIFY2(created, "task was not created");
        QCOMPARE(created->content(), QStringLiteral("Позвонить маме"));
        QCOMPARE(created->priority(), 1);
        QCOMPARE(created->projectId(), work->id());
        QVERIFY2(created->labels().contains(QStringLiteral("дом")),
                 qPrintable(created->labels().join(QLatin1Char(','))));
        QCOMPARE(created->dueDate(), isoDate(QDate::currentDate().addDays(1)));

        delete wrapper;
    }

    // An explicit P4 pick must beat an inline "p1" in the text: 4 is also the
    // "not chosen" default, so the override branch has to track the pick
    // itself (PR review finding on the C4 fix).
    void quickAdd_explicitP4Pick_beatsInlinePriority()
    {
        g_messages.clear();
        g_capturing.store(true);
        QString err;
        QObject *wrapper = createWrapper(
            QStringLiteral("    QuickAdd { objectName: \"qa\"; width: 700 }\n"), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *qa = wrapper->findChild<QObject *>(QStringLiteral("qa"));
        QVERIFY(qa);
        QObject *input = qa->findChild<QObject *>(QStringLiteral("quickAddInput"));
        QVERIFY(input);
        QObject *picker = qa->findChild<QObject *>(QStringLiteral("quickAddPriorityPicker"));
        QVERIFY(picker);

        input->setProperty("text", QStringLiteral("Explicit four p1"));
        // Go through the real picker signal → QuickAdd handler wiring.
        QVERIFY(QMetaObject::invokeMethod(picker, "prioritySelected", Q_ARG(int, 4)));
        QCOMPARE(qa->property("_priorityPicked").toBool(), true);
        qa->setProperty("selectedDueString", QStringLiteral("tomorrow"));
        QVERIFY(QMetaObject::invokeMethod(qa, "submitTask"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));

        Task *created = nullptr;
        for (Task *t : m_dataStore->allTasks()) {
            if (t->content().startsWith(QStringLiteral("Explicit four")))
                created = t;
        }
        QVERIFY2(created, "task was not created");
        QCOMPARE(created->priority(), 4);
        QCOMPARE(created->dueDate(), isoDate(QDate::currentDate().addDays(1)));
        QCOMPARE(qa->property("_priorityPicked").toBool(), false);   // reset after submit

        delete wrapper;
    }

    // ── C2 ──────────────────────────────────────────────────────────────────
    void editorMetaRow_labelEdit_usesCurrentLabels()
    {
        const QString taskId = m_taskVM->addTask(QStringLiteral("Labels host"), {}, 4, {},
                                                 {QStringLiteral("a"), QStringLiteral("b")});
        Task *task = m_dataStore->taskById(taskId);
        QVERIFY(task);

        g_messages.clear();
        g_capturing.store(true);
        QString err;
        QObject *wrapper = createWrapper(
            QStringLiteral("    EditorMetaRow { objectName: \"meta\"; width: 500 }\n"), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *meta = wrapper->findChild<QObject *>(QStringLiteral("meta"));
        QVERIFY(meta);
        meta->setProperty("taskId", taskId);
        meta->setProperty("task", QVariant::fromValue<QObject *>(task));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        auto removeChip = [&](const QString &name) -> bool {
            QObject *chip = findByProperty(meta, "labelName", name);
            if (!chip) return false;
            const bool ok = QMetaObject::invokeMethod(chip, "removed");
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            return ok;
        };

        QVERIFY2(removeChip(QStringLiteral("a")), "chip 'a' not found");
        QCOMPARE(task->labels(), QStringList{QStringLiteral("b")});

        // In-place change from elsewhere (sync / undo) …
        task->setLabels({QStringLiteral("b"), QStringLiteral("c")});
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        // … the next edit must start from the CURRENT labels, not a stale copy.
        QVERIFY2(removeChip(QStringLiteral("b")), "chip 'b' not found after in-place change");
        QCOMPARE(task->labels(), QStringList{QStringLiteral("c")});
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        delete wrapper;
    }

    // ── M5 ──────────────────────────────────────────────────────────────────
    // The mobile editor's reminder picker must manage a real (relative)
    // Reminder entity instead of a bare one-shot notification.
    void mobileEditor_reminderPicker_managesReminderEntity()
    {
        const QString taskId = m_taskVM->addTask(QStringLiteral("Mobile reminder host"), {}, 4,
                                                 QStringLiteral("завтра"));
        Task *task = m_dataStore->taskById(taskId);
        QVERIFY(task);
        m_taskVM->setSelectedTask(task);

        QString err;
        QObject *wrapper = createWrapper(
            QStringLiteral("    MEditorScreen { objectName: \"med\"; anchors.fill: parent }\n"), &err);
        QVERIFY2(wrapper, qPrintable(err));
        QObject *med = wrapper->findChild<QObject *>(QStringLiteral("med"));
        QVERIFY(med);
        QCOMPARE(med->property("_reminderId").toString(), QStringLiteral("Без напоминания"));

        g_messages.clear();
        g_capturing.store(true);
        QVERIFY(QMetaObject::invokeMethod(med, "_applyReminderChoice",
                                          Q_ARG(QVariant, QStringLiteral("За 1 час"))));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        auto rems = m_dataStore->remindersForTask(taskId);
        QCOMPARE(rems.size(), 1);
        QCOMPARE(rems.first()->type(), QStringLiteral("relative"));
        QCOMPARE(rems.first()->minuteOffset(), 60);
        // The label is derived from the entity (it used to reset on reopen).
        QCOMPARE(med->property("_reminderId").toString(), QStringLiteral("За 1 час"));

        // Re-picking updates the same reminder rather than adding another.
        QVERIFY(QMetaObject::invokeMethod(med, "_applyReminderChoice",
                                          Q_ARG(QVariant, QStringLiteral("За 10 минут"))));
        rems = m_dataStore->remindersForTask(taskId);
        QCOMPARE(rems.size(), 1);
        QCOMPARE(rems.first()->minuteOffset(), 10);

        // "Без напоминания" really removes it.
        QVERIFY(QMetaObject::invokeMethod(med, "_applyReminderChoice",
                                          Q_ARG(QVariant, QStringLiteral("Без напоминания"))));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCOMPARE(m_dataStore->remindersForTask(taskId).size(), 0);
        QCOMPARE(med->property("_reminderId").toString(), QStringLiteral("Без напоминания"));
        g_capturing.store(false);

        const QStringList errors = scriptErrors();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        m_taskVM->setSelectedTask(nullptr);
        delete wrapper;
    }
};

#include "test_no_teardown_main.h"
OPENTODO_QTEST_MAIN_NO_TEARDOWN(TestQmlComponentRegressions)
#include "test_qml_component_regressions.moc"
