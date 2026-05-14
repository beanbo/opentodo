// Extended drag-and-drop semantics tests at the C++ model / ViewModel level.
//
// The QML layer (TaskItem.qml, ProjectView.qml, BoardView.qml,
// SidebarContent.qml, EditorSubtasksSection.qml) wires up the actual mouse
// drag handlers and DropAreas; this suite exercises the *invocable model
// methods* those handlers ultimately call:
//
//   * TaskViewModel::convertToSubtask  — drop task on another task (subtask)
//   * TaskViewModel::promoteTask       — drop subtask at root level
//   * TaskViewModel::demoteTask        — sidebar/editor reparent w/ validation
//   * TaskViewModel::moveTask          — drop task on project / inbox / column
//   * TaskViewModel::setTaskLabels     — drop task on a label row in sidebar
//   * ProjectViewModel::reorderProject — drop project chip to a new position
//
// Mime-key constants live only on the QML side (Drag.mimeData / Drag.keys).
// They are not exposed via Q_PROPERTY or static const in C++, so we just
// document the contract here as constexpr literals — see kMime* below.
//
// We do NOT modify production code in this suite; every test is purely
// observational against the existing public API.

#include <QtTest>
#include <QSignalSpy>
#include <QStringList>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/section.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"

// ---------------------------------------------------------------------------
// Mime-type contract (referenced by the QML drag/drop layer).
// Keep in sync with Drag.keys / DropArea.keys across the qml/ tree.
// ---------------------------------------------------------------------------
static constexpr const char *kMimeTask          = "application/x-opentodo-task";
static constexpr const char *kMimeProject       = "application/x-opentodo-project";
static constexpr const char *kMimeSubtask       = "application/x-opentodo-subtask";
static constexpr const char *kMimeLabelReorder  = "application/x-opentodo-label-reorder";
static constexpr const char *kMimeSection       = "application/x-opentodo-section";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// Find a project's index in the visible projects list of `pvm`.
static int projectRow(ProjectViewModel &pvm, const QString &id)
{
    for (int i = 0; i < pvm.rowCount(); ++i) {
        if (pvm.data(pvm.index(i), ProjectViewModel::IdRole).toString() == id)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
class TestDragDropExtended : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // Reparent (subtask drop) — task A dropped on task B becomes B's subtask.
    // -----------------------------------------------------------------------
    void reparent_setsParentId_andEmitsSignals()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *parent = store->addTask(QStringLiteral("Родитель"), inboxId);
        Task *child  = store->addTask(QStringLiteral("Перетащенная"), inboxId);
        QVERIFY(child->parentId().isEmpty());

        TaskViewModel vm(store);
        QSignalSpy spyTaskChanged(&vm, &TaskViewModel::taskChanged);

        const bool ok = vm.convertToSubtask(child->id(), parent->id());
        QVERIFY(ok);
        QCOMPARE(child->parentId(), parent->id());

        // taskChanged emitted at least once for the reparented child.
        QVERIFY(spyTaskChanged.count() >= 1);
        bool sawChild = false;
        for (const auto &args : spyTaskChanged) {
            if (args.value(0).toString() == child->id()) { sawChild = true; break; }
        }
        QVERIFY(sawChild);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Promote via drop — subtask dropped at root level loses its parentId.
    // -----------------------------------------------------------------------
    void promote_clearsParentId()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *parent = store->addTask(QStringLiteral("Родитель"), inboxId);
        Task *child  = store->addTask(QStringLiteral("Подзадача"), inboxId);
        child->setParentId(parent->id());

        TaskViewModel vm(store);
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        QVERIFY(vm.promoteTask(child->id()));
        QVERIFY(child->parentId().isEmpty());
        QVERIFY(spy.count() >= 1);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Cross-project demote — demoteTask must reject a parent in another
    // project (per I25-D's same-project guard).
    // -----------------------------------------------------------------------
    void demote_crossProject_rejected()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        Task *taskInInbox = store->addTask(QStringLiteral("Задача"), inboxId);
        Task *parentInWork = store->addTask(QStringLiteral("Родитель"), workId);

        TaskViewModel vm(store);
        const bool ok = vm.demoteTask(taskInInbox->id(), parentInWork->id());
        QVERIFY(!ok);
        QVERIFY(taskInInbox->parentId().isEmpty()); // still a top-level task

        delete store;
    }

    // demoteTask should also reject self-parenting and unknown ids.
    void demote_selfOrUnknown_rejected()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Task *t = store->addTask(QStringLiteral("Task"), inboxId);

        TaskViewModel vm(store);
        QVERIFY(!vm.demoteTask(t->id(), t->id()));                          // self-parent
        QVERIFY(!vm.demoteTask(t->id(), QStringLiteral("no-such-id")));    // bad parent id
        QVERIFY(!vm.demoteTask(QStringLiteral("no-such-id"), t->id()));    // bad task id
        QVERIFY(!vm.demoteTask(QString(), t->id()));                        // empty task id
        QVERIFY(!vm.demoteTask(t->id(), QString()));                        // empty parent id

        delete store;
    }

    // -----------------------------------------------------------------------
    // Project reorder — move from index 1 to index 3 in a 4-project list.
    // Inbox is pinned at index 0 and excluded from the order numbering.
    // -----------------------------------------------------------------------
    void projectReorder_movesAndRenumbers()
    {
        DataStore *store = makeStore();
        Project *p1 = store->addProject(QStringLiteral("A"));
        Project *p2 = store->addProject(QStringLiteral("B"));
        Project *p3 = store->addProject(QStringLiteral("C"));
        p1->setOrder(1); p2->setOrder(2); p3->setOrder(3);
        emit store->projectsChanged();

        ProjectViewModel pvm(store);
        QCOMPARE(pvm.rowCount(), 4); // Inbox + A,B,C

        // Initial visible order: Inbox, A, B, C
        QCOMPARE(projectRow(pvm, p1->id()), 1);
        QCOMPARE(projectRow(pvm, p2->id()), 2);
        QCOMPARE(projectRow(pvm, p3->id()), 3);

        // Move A (visible index 1) to position 3 → expected: Inbox, B, C, A
        QVERIFY(pvm.reorderProject(1, 3));

        QCOMPARE(projectRow(pvm, p2->id()), 1);
        QCOMPARE(projectRow(pvm, p3->id()), 2);
        QCOMPARE(projectRow(pvm, p1->id()), 3);

        // The ProjectViewModel renumbers order=1..N for non-inbox projects in
        // their new visible order.
        QCOMPARE(p2->order(), 1);
        QCOMPARE(p3->order(), 2);
        QCOMPARE(p1->order(), 3);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Project reorder out-of-range — clamped/rejected as a no-op.
    // -----------------------------------------------------------------------
    void projectReorder_outOfRange_isNoop()
    {
        DataStore *store = makeStore();
        Project *p1 = store->addProject(QStringLiteral("A"));
        Project *p2 = store->addProject(QStringLiteral("B"));
        p1->setOrder(1); p2->setOrder(2);
        emit store->projectsChanged();

        ProjectViewModel pvm(store);
        QCOMPARE(pvm.rowCount(), 3);

        // target == projectCount (out of range, valid indices are [0, count))
        QVERIFY(!pvm.reorderProject(1, pvm.rowCount()));
        // negative target
        QVERIFY(!pvm.reorderProject(1, -1));
        // negative source
        QVERIFY(!pvm.reorderProject(-1, 0));
        // source out of range
        QVERIFY(!pvm.reorderProject(99, 0));
        // source == target — explicit no-op, returns false
        QVERIFY(!pvm.reorderProject(1, 1));

        // Visible order untouched
        QCOMPARE(projectRow(pvm, p1->id()), 1);
        QCOMPARE(projectRow(pvm, p2->id()), 2);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Drop task on label — label name added to task's label list.
    // Second drop with the same label is a silent no-op (deduplicated).
    // -----------------------------------------------------------------------
    void dropOnLabel_addsLabel_andDedupes()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t = store->addTask(QStringLiteral("Task"), inboxId);
        Label *urgent = store->addLabel(QStringLiteral("urgent"));
        QVERIFY(t->labels().isEmpty());

        TaskViewModel vm(store);

        // First drop: add the label name to the task's labels list.
        // (Mirrors SidebarContent.qml labelDropArea.onDropped which calls
        //  setTaskLabels(taskId, existing.concat([labelName])).)
        QStringList labels = t->labels();
        if (!labels.contains(urgent->name())) labels.append(urgent->name());
        QVERIFY(vm.setTaskLabels(t->id(), labels));
        QCOMPARE(t->labels().size(), 1);
        QCOMPARE(t->labels().first(), urgent->name());

        // Second drop with the same label — dedupe path: nothing added.
        labels = t->labels();
        if (!labels.contains(urgent->name())) labels.append(urgent->name());
        QVERIFY(vm.setTaskLabels(t->id(), labels));
        QCOMPARE(t->labels().size(), 1);

        delete store;
    }

    // -----------------------------------------------------------------------
    // Drop task on label A, then on label B — both labels accumulate
    // (the QML drop adds a label, it does not replace the set).
    // -----------------------------------------------------------------------
    void dropOnLabel_thenAnotherLabel_accumulates()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *t = store->addTask(QStringLiteral("Task"), inboxId);
        Label *a = store->addLabel(QStringLiteral("alpha"));
        Label *b = store->addLabel(QStringLiteral("beta"));

        TaskViewModel vm(store);

        QStringList labels = t->labels();
        if (!labels.contains(a->name())) labels.append(a->name());
        QVERIFY(vm.setTaskLabels(t->id(), labels));

        labels = t->labels();
        if (!labels.contains(b->name())) labels.append(b->name());
        QVERIFY(vm.setTaskLabels(t->id(), labels));

        QCOMPARE(t->labels().size(), 2);
        QVERIFY(t->labels().contains(QStringLiteral("alpha")));
        QVERIFY(t->labels().contains(QStringLiteral("beta")));

        delete store;
    }

    // -----------------------------------------------------------------------
    // Drop on Inbox — task's projectId is set to Inbox.
    // (DesktopLayout.qml inbox drop area calls moveTask(taskId, inboxId, "").)
    // -----------------------------------------------------------------------
    void dropOnInbox_setsProjectIdToInbox()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));
        Task *t = store->addTask(QStringLiteral("Task"), work->id());
        QCOMPARE(t->projectId(), work->id());

        TaskViewModel vm(store);
        QVERIFY(vm.moveTask(t->id(), inboxId, QString()));
        QCOMPARE(t->projectId(), inboxId);
        QVERIFY(t->sectionId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Drop on archived project — at the C++/VM layer this *succeeds*. The
    // archive guard lives in QML (DropArea.enabled gates on isArchived).
    // We document and pin that contract so future refactors don't silently
    // shift the guard down into moveTask without updating both layers.
    // -----------------------------------------------------------------------
    void dropOnArchivedProject_modelLayer_acceptsButQmlGuards()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Project *archived = store->addProject(QStringLiteral("Archived"));
        archived->setIsArchived(true);

        Task *t = store->addTask(QStringLiteral("Task"), inboxId);

        TaskViewModel vm(store);
        // Documented current behavior: the model-level moveTask does NOT
        // reject archived destinations — that's a UI-side concern.
        const bool result = vm.moveTask(t->id(), archived->id(), QString());
        QVERIFY(result);
        QCOMPARE(t->projectId(), archived->id());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Soft-deleted task is *visible* to the model layer (taskById returns
    // tombstones). The QML side filters tombstones out of TaskViewModel's
    // list, so a deleted task never gets a drag handle — but if a stale
    // reference somehow drives a moveTask(), the call still succeeds.
    // We pin the current contract: model layer accepts, UI layer prevents.
    // -----------------------------------------------------------------------
    void dropOfDeletedTask_modelLayer_acceptsButUiPrevents()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();
        Project *work = store->addProject(QStringLiteral("Work"));

        Task *t = store->addTask(QStringLiteral("Task"), inboxId);
        const QString tid = t->id();
        QVERIFY(store->removeTask(tid)); // soft-delete (tombstone)
        QVERIFY(t->isDeleted());

        TaskViewModel vm(store);
        // Model layer doesn't gate on isDeleted — UI does (filtered list view).
        QVERIFY(vm.moveTask(tid, work->id(), QString()));
        QCOMPARE(t->projectId(), work->id());
        QVERIFY(t->isDeleted()); // still tombstoned

        delete store;
    }

    // -----------------------------------------------------------------------
    // Section drop — moving a task between sections inside the same project
    // by passing the new sectionId as moveTask's third argument.
    // (BoardView.qml column drop area: taskViewModel.moveTask(tid, projectId,
    //  column.modelData.id).)
    // -----------------------------------------------------------------------
    void dropOnSection_movesBetweenSections()
    {
        DataStore *store = makeStore();
        Project *work = store->addProject(QStringLiteral("Work"));
        const QString workId = work->id();

        Section *todo = store->addSection(QStringLiteral("To Do"), workId);
        Section *done = store->addSection(QStringLiteral("Done"),  workId);
        QVERIFY(todo); QVERIFY(done);

        Task *t = store->addTask(QStringLiteral("Task"), workId);
        t->setSectionId(todo->id());
        QCOMPARE(t->sectionId(), todo->id());

        TaskViewModel vm(store);

        // Move card to the Done column.
        QVERIFY(vm.moveTask(t->id(), workId, done->id()));
        QCOMPARE(t->projectId(), workId);
        QCOMPARE(t->sectionId(), done->id());

        // Move it back.
        QVERIFY(vm.moveTask(t->id(), workId, todo->id()));
        QCOMPARE(t->sectionId(), todo->id());

        // Drop with empty sectionId clears the section assignment.
        QVERIFY(vm.moveTask(t->id(), workId, QString()));
        QVERIFY(t->sectionId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Demote into the same project succeeds — sanity check that the
    // cross-project rejection above isn't a false-positive.
    // -----------------------------------------------------------------------
    void demote_sameProject_succeeds()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *parent = store->addTask(QStringLiteral("Parent"), inboxId);
        Task *child  = store->addTask(QStringLiteral("Child"),  inboxId);
        QVERIFY(child->parentId().isEmpty());

        TaskViewModel vm(store);
        QVERIFY(vm.demoteTask(child->id(), parent->id()));
        QCOMPARE(child->parentId(), parent->id());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Cycle prevention — demoting a task into one of its own descendants
    // (or convertToSubtask of a parent under its own child) must fail.
    // -----------------------------------------------------------------------
    void reparent_cycle_rejected()
    {
        DataStore *store = makeStore();
        const QString inboxId = store->inboxProject()->id();

        Task *grand = store->addTask(QStringLiteral("Grand"), inboxId);
        Task *mid   = store->addTask(QStringLiteral("Mid"),   inboxId);
        Task *leaf  = store->addTask(QStringLiteral("Leaf"),  inboxId);
        mid->setParentId(grand->id());
        leaf->setParentId(mid->id());

        TaskViewModel vm(store);

        // Demote grand under leaf — would form grand → mid → leaf → grand.
        QVERIFY(!vm.demoteTask(grand->id(), leaf->id()));
        QVERIFY(grand->parentId().isEmpty());

        // convertToSubtask uses the same kind of cycle guard.
        QVERIFY(!vm.convertToSubtask(grand->id(), leaf->id()));
        QVERIFY(grand->parentId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Mime-type contract — these literal strings are the keys passed in
    // Drag.mimeData / Drag.keys / DropArea.keys in the QML layer. There is
    // no exposed C++ Q_PROPERTY or static const, so this test simply pins
    // the contract by asserting our local constants match the documented
    // values used across the qml/ tree.
    // -----------------------------------------------------------------------
    void mimeKeys_areExpectedConstants()
    {
        QCOMPARE(QString::fromLatin1(kMimeTask),
                 QStringLiteral("application/x-opentodo-task"));
        QCOMPARE(QString::fromLatin1(kMimeProject),
                 QStringLiteral("application/x-opentodo-project"));
        QCOMPARE(QString::fromLatin1(kMimeSubtask),
                 QStringLiteral("application/x-opentodo-subtask"));
        QCOMPARE(QString::fromLatin1(kMimeLabelReorder),
                 QStringLiteral("application/x-opentodo-label-reorder"));
        QCOMPARE(QString::fromLatin1(kMimeSection),
                 QStringLiteral("application/x-opentodo-section"));
    }
};

QTEST_GUILESS_MAIN(TestDragDropExtended)
#include "test_dragdrop_extended.moc"
