#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QStringList>
#include <QDateTime>
#include <QVariant>

#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "viewmodels/projectviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: build a DataStore with a minimal Inbox project so tasks can be added.
// ---------------------------------------------------------------------------
static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// ---------------------------------------------------------------------------
// TestProjectViewModelCrud
//
// Comprehensive CRUD + lifecycle coverage for ProjectViewModel that
// complements test_projectviewmodel_defaults.cpp (which covers per-project
// QSettings defaults and the weeklyGoalReached signal).
//
// Scope:
//   - addProject (name only, name+color, name+color+parent)
//   - updateProject (name, color, icon, notes, weeklyGoal, viewStyle, parentId,
//                    cycle rejection, unknown field)
//   - deleteProject (soft-delete, refuses inbox)
//   - archiveProject / unarchiveProject (and showArchived interaction)
//   - setFavorite / setViewStyle convenience wrappers
//   - reorderProject (move, no-op same index, out-of-range)
//   - removeCompletedTasksInProject (only completed tasks, only that project)
//   - taskCountForProject (excludes completed/deleted)
//   - projectById (valid / invalid id)
//   - projectAt (in-range / out-of-range)
//   - childProjects
//   - rowCount / count consistency excluding deletes
//   - Signals: projectAdded, projectChanged, projectRemoved,
//              countChanged on add/delete/reorder
// ---------------------------------------------------------------------------
class TestProjectViewModelCrud : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Throw-away QSettings scope so tests don't pollute real user settings
        // and don't interact with the defaults-suite scope.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_projectviewmodel_crud"));
    }

    void init()
    {
        QSettings s;
        s.remove(QStringLiteral("projects"));
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("projects"));
        s.sync();
    }

    // -----------------------------------------------------------------------
    // addProject — name only — populates name and emits signals.
    // -----------------------------------------------------------------------
    void addProject_name_only_populates_and_signals()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const int initialRows = vm.rowCount();
        QSignalSpy addSpy(&vm, &ProjectViewModel::projectAdded);
        QSignalSpy countSpy(&vm, &ProjectViewModel::countChanged);

        const QString id = vm.addProject(QStringLiteral("Work"));
        QVERIFY(!id.isEmpty());

        // Row count grows by exactly 1; both projectAdded and countChanged fire.
        QCOMPARE(vm.rowCount(), initialRows + 1);
        QCOMPARE(addSpy.count(), 1);
        QCOMPARE(addSpy.takeFirst().at(0).toString(), id);
        QVERIFY(countSpy.count() >= 1);

        Project *p = vm.projectById(id);
        QVERIFY(p);
        QCOMPARE(p->name(), QStringLiteral("Work"));
        // Default color from the addProject signature is "grey".
        QCOMPARE(p->color(), QStringLiteral("grey"));
        QVERIFY(p->parentId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // addProject — name + color — both populated.
    // -----------------------------------------------------------------------
    void addProject_name_and_color_populates_both()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Garden"), QStringLiteral("kale"));
        QVERIFY(!id.isEmpty());

        Project *p = vm.projectById(id);
        QVERIFY(p);
        QCOMPARE(p->name(), QStringLiteral("Garden"));
        QCOMPARE(p->color(), QStringLiteral("kale"));

        delete store;
    }

    // -----------------------------------------------------------------------
    // addProject — with parent — sets parentId.
    // -----------------------------------------------------------------------
    void addProject_with_parent_sets_parentId()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString parent = vm.addProject(QStringLiteral("Parent"));
        const QString child = vm.addProject(QStringLiteral("Child"), QStringLiteral("blue"), parent);
        QVERIFY(!child.isEmpty());

        Project *cp = vm.projectById(child);
        QVERIFY(cp);
        QCOMPARE(cp->parentId(), parent);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(name) — renames; projectChanged fires with the id.
    // (Note: the project-rename API is `updateProject(id, "name", value)`
    // — there is no setProjectName method. Tested via updateProject.)
    // -----------------------------------------------------------------------
    void updateProject_name_renames_and_signals()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Old"));
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);

        QVERIFY(vm.updateProject(id, QStringLiteral("name"), QStringLiteral("New")));
        QCOMPARE(vm.projectById(id)->name(), QStringLiteral("New"));

        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(changedSpy.takeFirst().at(0).toString(), id);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(color) — recolors via the canonical update path.
    // -----------------------------------------------------------------------
    void updateProject_color_updates_color()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        QVERIFY(vm.updateProject(id, QStringLiteral("color"), QStringLiteral("kale")));
        QCOMPARE(vm.projectById(id)->color(), QStringLiteral("kale"));

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(icon) — sets the icon glyph.
    //
    // Project::icon is exposed via Q_PROPERTY but the convenience
    // setProjectIcon name does not exist on ProjectViewModel; mutation goes
    // through updateProject.
    // -----------------------------------------------------------------------
    void updateProject_icon_updates_icon()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        const QString rocket = QString::fromUtf8("\xF0\x9F\x9A\x80"); // U+1F680 ROCKET
        QVERIFY(vm.updateProject(id, QStringLiteral("icon"), rocket));
        QCOMPARE(vm.projectById(id)->icon(), rocket);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(notes) — sets the notes blob.
    // No setProjectNotes wrapper; mutate via updateProject.
    // -----------------------------------------------------------------------
    void updateProject_notes_updates_notes()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        const QString body = QStringLiteral("Sprint goals and notes...");
        QVERIFY(vm.updateProject(id, QStringLiteral("notes"), body));
        QCOMPARE(vm.projectById(id)->notes(), body);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(weeklyGoal) — sets the goal counter.
    // No setWeeklyGoal wrapper; mutate via updateProject.
    // -----------------------------------------------------------------------
    void updateProject_weeklyGoal_updates_value()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        QVERIFY(vm.updateProject(id, QStringLiteral("weeklyGoal"), 5));
        QCOMPARE(vm.projectById(id)->weeklyGoal(), 5);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(viewStyle) via setViewStyle — board / list switch.
    // -----------------------------------------------------------------------
    void setViewStyle_changes_view_style()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Tracker"));
        QCOMPARE(vm.projectById(id)->viewStyle(), QStringLiteral("list")); // default

        QVERIFY(vm.setViewStyle(id, QStringLiteral("board")));
        QCOMPARE(vm.projectById(id)->viewStyle(), QStringLiteral("board"));

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject — unknown field → false, no mutation.
    // -----------------------------------------------------------------------
    void updateProject_unknown_field_returns_false()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);

        QVERIFY(!vm.updateProject(id, QStringLiteral("nonexistent"), 42));
        QCOMPARE(changedSpy.count(), 0);

        delete store;
    }

    // -----------------------------------------------------------------------
    // updateProject(parentId) — rejects an assignment that would create a
    // direct cycle (project parents itself).
    // -----------------------------------------------------------------------
    void updateProject_parentId_rejects_self_cycle()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        // self-parent is the smallest possible cycle.
        QVERIFY(!vm.updateProject(id, QStringLiteral("parentId"), id));
        QVERIFY(vm.projectById(id)->parentId().isEmpty());

        delete store;
    }

    // -----------------------------------------------------------------------
    // deleteProject — soft-deletes (is_deleted=true), removes from rowCount,
    // emits projectRemoved.
    // -----------------------------------------------------------------------
    void deleteProject_soft_deletes_and_excludes_from_rowCount()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("ToRemove"));
        const int rowsAfterAdd = vm.rowCount();

        QSignalSpy removedSpy(&vm, &ProjectViewModel::projectRemoved);

        QVERIFY(vm.deleteProject(id));
        QCOMPARE(vm.rowCount(), rowsAfterAdd - 1);
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.takeFirst().at(0).toString(), id);

        // The Project still exists in the underlying store but is tombstoned;
        // projectById returns the live pointer regardless of the tombstone.
        Project *p = vm.projectById(id);
        QVERIFY(p);
        QVERIFY(p->isDeleted());

        delete store;
    }

    // -----------------------------------------------------------------------
    // deleteProject — refuses to delete the Inbox project.
    // -----------------------------------------------------------------------
    void deleteProject_refuses_inbox()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);

        QVERIFY(!vm.deleteProject(inbox->id()));
        QVERIFY(!inbox->isDeleted());

        delete store;
    }

    // -----------------------------------------------------------------------
    // archiveProject — sets isArchived=true; with showArchived=false (the
    // default) the row leaves the visible list.
    // -----------------------------------------------------------------------
    void archiveProject_excludes_from_default_view()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);
        QVERIFY(!vm.showArchived());

        const QString id = vm.addProject(QStringLiteral("Old project"));
        const int rowsBefore = vm.rowCount();

        QVERIFY(vm.archiveProject(id));
        QVERIFY(vm.projectById(id)->isArchived());
        QCOMPARE(vm.rowCount(), rowsBefore - 1);

        delete store;
    }

    // -----------------------------------------------------------------------
    // archiveProject — with showArchived=true the archived row is included.
    // -----------------------------------------------------------------------
    void archiveProject_included_when_showArchived_true()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Old project"));
        const int rowsBefore = vm.rowCount();

        vm.setShowArchived(true);
        QVERIFY(vm.showArchived());
        QVERIFY(vm.archiveProject(id));

        // Same row count: still visible because showArchived is true.
        QCOMPARE(vm.rowCount(), rowsBefore);

        delete store;
    }

    // -----------------------------------------------------------------------
    // unarchiveProject — restores the archived flag and the row reappears
    // in the default view.
    // -----------------------------------------------------------------------
    void unarchiveProject_restores_visibility()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Park"));
        QVERIFY(vm.archiveProject(id));
        const int rowsArchived = vm.rowCount();

        QVERIFY(vm.unarchiveProject(id));
        QVERIFY(!vm.projectById(id)->isArchived());
        QCOMPARE(vm.rowCount(), rowsArchived + 1);

        delete store;
    }

    // -----------------------------------------------------------------------
    // setFavorite — flips the isFavorite flag.
    // -----------------------------------------------------------------------
    void setFavorite_flips_flag()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Side hustle"));
        QVERIFY(!vm.projectById(id)->isFavorite());

        QVERIFY(vm.setFavorite(id, true));
        QVERIFY(vm.projectById(id)->isFavorite());

        QVERIFY(vm.setFavorite(id, false));
        QVERIFY(!vm.projectById(id)->isFavorite());

        delete store;
    }

    // -----------------------------------------------------------------------
    // reorderProject — moving a project rewrites order across siblings and
    // emits the ordering refresh path. (Note: the production signature is
    // `reorderProject(sourceIndex, targetIndex)`, not `(id, newIndex)`.)
    // -----------------------------------------------------------------------
    void reorderProject_moves_and_renumbers_siblings()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"));
        const QString c = vm.addProject(QStringLiteral("C"));

        // Layout: row 0 = Inbox, rows 1..3 = A, B, C in insertion order.
        QCOMPARE(vm.projectAt(0)->isInbox(), true);
        QCOMPARE(vm.projectAt(1)->id(), a);
        QCOMPARE(vm.projectAt(2)->id(), b);
        QCOMPARE(vm.projectAt(3)->id(), c);

        // Move "A" (row 1) to row 3 → expected order Inbox, B, C, A.
        QVERIFY(vm.reorderProject(1, 3));

        QCOMPARE(vm.projectAt(0)->isInbox(), true);
        QCOMPARE(vm.projectAt(1)->id(), b);
        QCOMPARE(vm.projectAt(2)->id(), c);
        QCOMPARE(vm.projectAt(3)->id(), a);

        // After reorder, non-inbox `order` must be 1..N in the new sequence.
        QCOMPARE(vm.projectById(b)->order(), 1);
        QCOMPARE(vm.projectById(c)->order(), 2);
        QCOMPARE(vm.projectById(a)->order(), 3);

        delete store;
    }

    // -----------------------------------------------------------------------
    // reorderProject — same source and target indices is a no-op.
    // -----------------------------------------------------------------------
    void reorderProject_same_index_is_noop()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"));

        // Snapshot the order field of every visible project.
        QList<int> ordersBefore;
        for (int i = 0; i < vm.rowCount(); ++i)
            ordersBefore.append(vm.projectAt(i)->order());

        QVERIFY(!vm.reorderProject(1, 1));

        // Order field unchanged.
        for (int i = 0; i < vm.rowCount(); ++i)
            QCOMPARE(vm.projectAt(i)->order(), ordersBefore.at(i));

        // Identity unchanged too.
        QCOMPARE(vm.projectAt(1)->id(), a);
        QCOMPARE(vm.projectAt(2)->id(), b);

        delete store;
    }

    // -----------------------------------------------------------------------
    // reorderProject — out-of-range source or target → returns false, no-op.
    // -----------------------------------------------------------------------
    void reorderProject_out_of_range_rejected()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        vm.addProject(QStringLiteral("A"));
        vm.addProject(QStringLiteral("B"));

        const int rows = vm.rowCount();

        // Negative source.
        QVERIFY(!vm.reorderProject(-1, 1));
        // Source past end.
        QVERIFY(!vm.reorderProject(rows + 5, 1));
        // Target past end.
        QVERIFY(!vm.reorderProject(1, rows + 5));
        // Negative target.
        QVERIFY(!vm.reorderProject(1, -3));

        // Row count unchanged after every rejected call.
        QCOMPARE(vm.rowCount(), rows);

        delete store;
    }

    // -----------------------------------------------------------------------
    // removeCompletedTasksInProject — soft-deletes only completed tasks in
    // that project; leaves uncompleted tasks and tasks in other projects alone.
    // -----------------------------------------------------------------------
    void removeCompletedTasksInProject_only_completed_only_that_project()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString a = vm.addProject(QStringLiteral("Project A"));
        const QString b = vm.addProject(QStringLiteral("Project B"));

        // Project A: 2 completed, 1 active.
        Task *aDone1 = store->addTask(QStringLiteral("A done 1"), a);
        Task *aDone2 = store->addTask(QStringLiteral("A done 2"), a);
        Task *aOpen  = store->addTask(QStringLiteral("A open"),   a);
        QVERIFY(aDone1 && aDone2 && aOpen);
        aDone1->setIsCompleted(true);
        aDone2->setIsCompleted(true);

        // Project B: 1 completed — must be untouched.
        Task *bDone = store->addTask(QStringLiteral("B done"), b);
        QVERIFY(bDone);
        bDone->setIsCompleted(true);

        const int removed = vm.removeCompletedTasksInProject(a);
        QCOMPARE(removed, 2);

        // Project A: completed ones tombstoned, active one survives.
        QVERIFY(aDone1->isDeleted());
        QVERIFY(aDone2->isDeleted());
        QVERIFY(!aOpen->isDeleted());

        // Project B's completed task must be entirely untouched.
        QVERIFY(!bDone->isDeleted());

        delete store;
    }

    // -----------------------------------------------------------------------
    // removeCompletedTasksInProject — empty / unknown id → returns 0, no crash.
    // -----------------------------------------------------------------------
    void removeCompletedTasksInProject_unknown_or_empty_id_returns_zero()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        QCOMPARE(vm.removeCompletedTasksInProject(QString()), 0);
        QCOMPARE(vm.removeCompletedTasksInProject(QStringLiteral("does-not-exist")), 0);

        delete store;
    }

    // -----------------------------------------------------------------------
    // taskCountForProject — returns count of NON-completed, NON-deleted tasks.
    // -----------------------------------------------------------------------
    void taskCountForProject_excludes_completed_and_deleted()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        QCOMPARE(vm.taskCountForProject(id), 0);

        Task *t1 = store->addTask(QStringLiteral("Open 1"), id);
        Task *t2 = store->addTask(QStringLiteral("Open 2"), id);
        Task *tDone = store->addTask(QStringLiteral("Done"), id);
        Task *tDel = store->addTask(QStringLiteral("Deleted"), id);
        QVERIFY(t1 && t2 && tDone && tDel);

        tDone->setIsCompleted(true);
        // tasksForProject filters tombstoned tasks out, so deleted ones are
        // already excluded; explicitly mark for clarity.
        store->removeTask(tDel->id());

        QCOMPARE(vm.taskCountForProject(id), 2);

        // Unknown project id → 0.
        QCOMPARE(vm.taskCountForProject(QStringLiteral("nope")), 0);

        delete store;
    }

    // -----------------------------------------------------------------------
    // projectById — valid id → entry; unknown id → null.
    // -----------------------------------------------------------------------
    void projectById_valid_returns_entry_unknown_returns_null()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        Project *p = vm.projectById(id);
        QVERIFY(p);
        QCOMPARE(p->id(), id);

        QVERIFY(vm.projectById(QStringLiteral("does-not-exist")) == nullptr);
        QVERIFY(vm.projectById(QString()) == nullptr);

        delete store;
    }

    // -----------------------------------------------------------------------
    // projectAt — bounds-checking on the visible list.
    // -----------------------------------------------------------------------
    void projectAt_in_range_and_out_of_range()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("P"));
        // visible: row 0 inbox, row 1 = our project.
        QVERIFY(vm.projectAt(1) != nullptr);
        QCOMPARE(vm.projectAt(1)->id(), id);

        QVERIFY(vm.projectAt(-1) == nullptr);
        QVERIFY(vm.projectAt(vm.rowCount()) == nullptr);
        QVERIFY(vm.projectAt(99) == nullptr);

        delete store;
    }

    // -----------------------------------------------------------------------
    // childProjects — returns only direct children of the requested parent.
    // -----------------------------------------------------------------------
    void childProjects_returns_only_direct_children()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString parent = vm.addProject(QStringLiteral("Parent"));
        const QString c1 = vm.addProject(QStringLiteral("Child 1"), QStringLiteral("grey"), parent);
        const QString c2 = vm.addProject(QStringLiteral("Child 2"), QStringLiteral("grey"), parent);
        const QString unrelated = vm.addProject(QStringLiteral("Unrelated"));

        const QVariantList kids = vm.childProjects(parent);
        QCOMPARE(kids.size(), 2);

        // Collect ids returned by childProjects, order is insertion-stable but
        // we don't depend on it — assert membership. childProjects returns
        // QVariantMap entries (see test_childprojects_qml_shape) because Qt
        // 6.11's QML engine can't iterate a QVariantList of QObject*-bearing
        // QVariants reliably.
        QStringList childIds;
        for (const QVariant &v : kids) {
            QVERIFY(v.metaType().id() == QMetaType::QVariantMap);
            childIds << v.toMap().value(QStringLiteral("id")).toString();
        }
        QVERIFY(childIds.contains(c1));
        QVERIFY(childIds.contains(c2));
        QVERIFY(!childIds.contains(unrelated));

        delete store;
    }

    // -----------------------------------------------------------------------
    // rowCount excludes soft-deleted projects.
    // -----------------------------------------------------------------------
    void rowCount_excludes_deleted_projects()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        vm.addProject(QStringLiteral("Keep"));
        const QString gone = vm.addProject(QStringLiteral("Gone"));
        const int rowsAfterAdd = vm.rowCount();

        QVERIFY(vm.deleteProject(gone));
        QCOMPARE(vm.rowCount(), rowsAfterAdd - 1);
        QCOMPARE(vm.count(), vm.rowCount());

        delete store;
    }

    // -----------------------------------------------------------------------
    // Summary signal coverage: deletion of a project must propagate
    // projectRemoved AND keep count() / rowCount() consistent.
    // -----------------------------------------------------------------------
    void deleteProject_signals_and_counts_consistent()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString p1 = vm.addProject(QStringLiteral("p1"));
        const QString p2 = vm.addProject(QStringLiteral("p2"));
        const QString p3 = vm.addProject(QStringLiteral("p3"));

        QSignalSpy removedSpy(&vm, &ProjectViewModel::projectRemoved);
        QSignalSpy countSpy(&vm, &ProjectViewModel::countChanged);

        const int before = vm.rowCount();
        QVERIFY(vm.deleteProject(p2));

        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.takeFirst().at(0).toString(), p2);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(vm.rowCount(), before - 1);

        // p1 and p3 are still findable.
        QVERIFY(vm.projectById(p1) != nullptr);
        QVERIFY(vm.projectById(p3) != nullptr);

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestProjectViewModelCrud)
#include "test_projectviewmodel_crud.moc"
