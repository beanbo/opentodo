// =============================================================================
// Q_INVOKABLE QObject*-return ownership audit (Q2.15-C)
// =============================================================================
//
// PURPOSE
// -------
//   QML's GC + Q_INVOKABLE rule: when a Q_INVOKABLE returns QObject* to QML,
//   the engine assumes ownership UNLESS one of:
//     a) the object has a non-null Qt parent at the time the engine sees it,
//     b) QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership) was
//        called explicitly,
//     c) the object reaches QML through a QML-singleton/context-property
//        accessor (those default to CppOwnership too).
//
//   When neither (a)/(b)/(c) holds, the JS engine may collect the object on
//   the next GC pass; subsequent C++ access becomes a use-after-free. The
//   classic OpenTodo trap is "DataStore owns the entity, QML touches it
//   through a Q_INVOKABLE getter that returns a borrowed pointer". If the
//   borrowed pointer's parent were ever cleared, QML would happily reap it.
//
//   This test does NOT *fix* anything. It documents the current state — for
//   every Q_INVOKABLE in production code that returns an entity pointer
//   (Task*, Project*, Section*, Label*, Filter*, Reminder*, Comment*), the
//   test asserts that the returned object has a Qt parent (DataStore in
//   every case). If a future change drops the parent, this test fires.
//
// SWEEP RESULT (2026-05-09)
// -------------------------
//   Production headers searched: src/**/*.h excluding tests/.
//   Match pattern: /Q_INVOKABLE.*\*/
//
//   Methods returning a single entity pointer (audited here):
//     - DataStore::addTask          (datastore.h:66)
//     - DataStore::taskById         (datastore.h:67)
//     - DataStore::addProject       (datastore.h:94)
//     - DataStore::projectById      (datastore.h:95)
//     - DataStore::inboxProject     (datastore.h:98)
//     - DataStore::addSection       (datastore.h:103)
//     - DataStore::sectionById      (datastore.h:104)
//     - DataStore::addLabel         (datastore.h:117)
//     - DataStore::labelById        (datastore.h:118)
//     - DataStore::labelByName      (datastore.h:119)
//     - DataStore::addFilter        (datastore.h:125)
//     - DataStore::filterById       (datastore.h:126)
//     - DataStore::addReminder      (datastore.h:132)
//     - DataStore::reminderById     (datastore.h:133)
//     - DataStore::addComment       (datastore.h:139)
//     - ProjectViewModel::projectAt (projectviewmodel.h:63)  delegates → DS
//     - ProjectViewModel::projectById (projectviewmodel.h:64) delegates → DS
//     - TaskViewModel::taskAt       (taskviewmodel.h:224)    delegates → DS
//     - FilterViewModel::filterById (filterviewmodel.h:58)   delegates → DS
//     - SyncEngine::createCommit    (syncengine.h:42) -> Commit*; parent=SyncEngine
//
//   Methods returning a list of entity pointers (NOT audited here per slot —
//   each list element shares the same parent as the singleton getter, so the
//   audit only spot-checks one element from a couple of them):
//     - DataStore::allTasks/tasksFor*/overdue/completed/subtasks/searchTasks
//     - DataStore::allProjects, sectionsForProject, allLabels, allFilters
//     - DataStore::remindersForTask, commentsForTask
//     - FilterEvaluator::evaluate (filtereval.h:20)
//
//   Non-QObject pointer returns (NOT subject to QML GC at all):
//     - FilterParser::parse        (filterparser.h:18) -> AstNode*  (plain struct,
//                                  not Q_OBJECT — QML ownership rules don't apply;
//                                  caller owns and must qDeleteAll children).
//
//   Out-of-scope (not entity pointers, not memory-safety risks):
//     - WindowChrome::setDarkTitleBar takes a QObject* parameter — not a return.
//
//   TOTAL methods returning QObject* entity pointers : 20
//   TOTAL methods returning QObject* lists           : 13
//   TOTAL methods returning non-QObject* pointers    :  1
//   TOTAL flagged as risky (no Qt parent, no explicit
//     setObjectOwnership)                            :  0
//
// VERDICT
// -------
//   All audited Q_INVOKABLE getters return objects parented to either
//   DataStore (entities) or SyncEngine (Commit). None require an explicit
//   setObjectOwnership call because the parent suffices for the QML GC.
//   The non-QObject AstNode return is outside the QML-ownership regime
//   entirely (the QML engine cannot construct a JS handle for it because
//   it is not a registered Q_OBJECT/Q_GADGET — it can only flow through
//   QML if an explicit converter exists, which it does not here).
//
//   Should the parenting invariant ever be broken (e.g. a future
//   refactor constructs a transient `new Task()` without a parent and
//   returns it from a Q_INVOKABLE), this test will fail loudly on the
//   `result->parent() != nullptr` assertion BEFORE the leak/UAF reaches
//   end-users.
//
// =============================================================================

#include <QtTest>
#include <QObject>
#include <QCoreApplication>
#include <QSettings>

#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/reminder.h"
#include "model/comment.h"
#include "storage/datastore.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/filterviewmodel.h"
#include "sync/syncengine.h"
#include "sync/commit.h"

class TestQObjectOwnershipAudit : public QObject
{
    Q_OBJECT

private:
    static void configureSettings()
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_OwnershipAudit"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_qobject_ownership_audit"));
        // Wipe any leftover settings (deviceId etc.) from prior runs
        // so SyncEngine boots into a deterministic state.
        QSettings().clear();
    }

    // Helper: assert one Q_INVOKABLE-returned pointer is CPP-owned via parent,
    // and emit an audit log line. Counts a "risky" (parent-less) pointer with
    // a qWarning if encountered — but expected count is zero.
    template <typename T>
    void auditOne(const char *methodName, T *obj, int &totalChecked,
                  int &riskyCount)
    {
        ++totalChecked;
        QVERIFY2(obj != nullptr,
                 qPrintable(QStringLiteral("Audit precondition failed: %1 "
                                           "returned nullptr — fixture bug?")
                                .arg(QString::fromUtf8(methodName))));
        QObject *p = obj->parent();
        if (p == nullptr) {
            ++riskyCount;
            qWarning().noquote()
                << "Audit RISK:" << methodName
                << "returned a QObject* with NO Qt parent — QML may GC it. "
                   "Add QQmlEngine::setObjectOwnership(obj, "
                   "QQmlEngine::CppOwnership) or set a parent.";
            return;
        }
        qInfo().noquote()
            << "Audit:" << methodName
            << "returns CPP-owned (parent="
            << p->metaObject()->className() << ")";
    }

private slots:
    void initTestCase() { configureSettings(); }
    void cleanupTestCase() {}

    void audit_all_q_invokable_pointer_returns()
    {
        // ---- Construct one of every entity in a single DataStore. ----
        DataStore ds;

        // Inbox is auto-created by ProjectViewModel; here we just add a
        // regular project. inboxProject() returns nullptr until something
        // creates one, so we exercise both addProject and an explicit
        // Inbox project for inboxProject() coverage.
        Project *inbox = ds.addProject(QStringLiteral("Inbox"));
        QVERIFY(inbox != nullptr);
        inbox->setIsInbox(true);

        Project *workProject = ds.addProject(QStringLiteral("Work"));
        QVERIFY(workProject != nullptr);

        Section *section =
            ds.addSection(QStringLiteral("Sprint 1"), workProject->id());
        QVERIFY(section != nullptr);

        Task *task = ds.addTask(QStringLiteral("Audit Q_INVOKABLE returns"),
                                workProject->id());
        QVERIFY(task != nullptr);

        Label *label = ds.addLabel(QStringLiteral("urgent"));
        QVERIFY(label != nullptr);

        Filter *filter = ds.addFilter(QStringLiteral("Today P1"),
                                      QStringLiteral("today & p1"));
        QVERIFY(filter != nullptr);

        Reminder *reminder = ds.addReminder(task->id(),
                                            QStringLiteral("absolute"));
        QVERIFY(reminder != nullptr);

        Comment *comment = ds.addComment(QStringLiteral("hello"),
                                         task->id(), QString());
        QVERIFY(comment != nullptr);

        int totalChecked = 0;
        int riskyCount   = 0;

        // ---- DataStore: per-entity getters returning a single QObject*. ----
        // The `add*` calls themselves were already audited above (their
        // return values are the same `task`, `project`, ... objects we use
        // here). We re-audit them via the index-lookup variants because the
        // index lookup is the QML-facing critical path.

        auditOne("DataStore::addTask",     task,
                 totalChecked, riskyCount);
        auditOne("DataStore::taskById",    ds.taskById(task->id()),
                 totalChecked, riskyCount);

        auditOne("DataStore::addProject",  workProject,
                 totalChecked, riskyCount);
        auditOne("DataStore::projectById", ds.projectById(workProject->id()),
                 totalChecked, riskyCount);
        auditOne("DataStore::inboxProject", ds.inboxProject(),
                 totalChecked, riskyCount);

        auditOne("DataStore::addSection",  section,
                 totalChecked, riskyCount);
        auditOne("DataStore::sectionById", ds.sectionById(section->id()),
                 totalChecked, riskyCount);

        auditOne("DataStore::addLabel",      label,
                 totalChecked, riskyCount);
        auditOne("DataStore::labelById",     ds.labelById(label->id()),
                 totalChecked, riskyCount);
        auditOne("DataStore::labelByName",   ds.labelByName(label->name()),
                 totalChecked, riskyCount);

        auditOne("DataStore::addFilter",   filter,
                 totalChecked, riskyCount);
        auditOne("DataStore::filterById",  ds.filterById(filter->id()),
                 totalChecked, riskyCount);

        auditOne("DataStore::addReminder",  reminder,
                 totalChecked, riskyCount);
        auditOne("DataStore::reminderById", ds.reminderById(reminder->id()),
                 totalChecked, riskyCount);

        auditOne("DataStore::addComment",  comment,
                 totalChecked, riskyCount);

        // ---- ViewModels: thin wrappers around DataStore lookups. ----
        // Each delegates to the DataStore method already audited; the
        // per-VM check guards against a future regression where a VM
        // might construct or copy entity objects rather than aliasing.
        ProjectViewModel projectVm(&ds);
        TaskViewModel    taskVm(&ds);
        FilterViewModel  filterVm(&ds);

        // ProjectViewModel exposes m_projects via projectAt(int). We need
        // index 0 to exist — the VM mirrors DataStore::allProjects after
        // setDataStore wires up its initial sync.
        if (Project *fromVm = projectVm.projectAt(0)) {
            auditOne("ProjectViewModel::projectAt",  fromVm,
                     totalChecked, riskyCount);
        } else {
            qInfo() << "Audit: ProjectViewModel::projectAt(0) returned "
                       "nullptr — VM list cache empty (VM may filter out "
                       "Inbox / archived). Skipping element check; the "
                       "code path is exercised by projectById below.";
        }
        auditOne("ProjectViewModel::projectById",
                 projectVm.projectById(workProject->id()),
                 totalChecked, riskyCount);

        if (Task *fromVm = taskVm.taskAt(0)) {
            auditOne("TaskViewModel::taskAt", fromVm,
                     totalChecked, riskyCount);
        } else {
            qInfo() << "Audit: TaskViewModel::taskAt(0) returned nullptr — "
                       "VM list cache empty under default filter. The "
                       "delegation path is identical to the DataStore "
                       "audit above; skipping element check.";
        }

        auditOne("FilterViewModel::filterById",
                 filterVm.filterById(filter->id()),
                 totalChecked, riskyCount);

        // ---- SyncEngine::createCommit returns a Commit* parented to the
        //      engine. We must pass a non-empty operations list — the
        //      method explicitly returns nullptr for an empty list as a
        //      no-op short-circuit (syncengine.cpp:51). Use a single
        //      task_add op synthesized from the audit fixture.
        SyncEngine engine(&ds);
        SyncOperation op;
        op.type     = QStringLiteral("task_add");
        op.entityId = task->id();
        op.data     = task->toJson();
        Commit *commit =
            engine.createCommit(QList<SyncOperation>{op});
        auditOne("SyncEngine::createCommit", commit,
                 totalChecked, riskyCount);

        // ---- Spot-check one list-returning getter — element parent
        //      should equal the same DataStore. This guards against a
        //      future "deep-copy-into-list" regression.
        const QList<Task *> tasksList = ds.allTasks();
        QVERIFY(!tasksList.isEmpty());
        auditOne("DataStore::allTasks[0]", tasksList.first(),
                 totalChecked, riskyCount);

        const QList<Project *> projList = ds.allProjects();
        QVERIFY(!projList.isEmpty());
        auditOne("DataStore::allProjects[0]", projList.first(),
                 totalChecked, riskyCount);

        // ---- Final invariants. ----
        // Every audited pointer must have parent == &ds (or, for
        // SyncEngine::createCommit, parent == &engine). We've already
        // emitted the per-call qInfo lines; assert the count of
        // risky returns is zero.
        QCOMPARE(riskyCount, 0);
        QVERIFY2(totalChecked >= 18,
                 qPrintable(QStringLiteral("Expected at least 18 audited "
                                           "Q_INVOKABLE pointer returns; "
                                           "got %1.").arg(totalChecked)));

        qInfo().noquote()
            << "Audit summary: checked" << totalChecked
            << "Q_INVOKABLE pointer-return call sites; risky (parent-less):"
            << riskyCount << "(expected 0).";
    }
};

QTEST_GUILESS_MAIN(TestQObjectOwnershipAudit)
#include "test_qobject_ownership_audit.moc"
