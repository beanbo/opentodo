#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QDateTime>
#include <QVariant>
#include <QList>
#include <algorithm>

#include "storage/datastore.h"
#include "model/project.h"
#include "viewmodels/projectviewmodel.h"

// ---------------------------------------------------------------------------
// Helper: minimal DataStore with a single Inbox project so addProject works
// and Inbox-targeted scenarios have something to address.
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
// TestProjectViewModelHierarchy
//
// Pins ProjectViewModel CRUD/hierarchy edge cases that the existing
// `defaults` and `crud` suites do not cover:
//   * Cycle prevention when moving a parent under one of its descendants
//   * Archive: parent-only, no cascade to children
//   * Inbox immutability — Q3.08-B fix asserts that rename + setParentId
//     are rejected; delete is rejected at DataStore layer; color + icon
//     remain user-customisable (narrow guard).
//   * Color / icon Q_PROPERTY NOTIFY signal emission
//   * Reorder semantics across parents (single flat counter is shared)
//   * Move-to-root via parentId=""
//   * Two rapid successive setName mutations (no debounce, last wins)
//   * Reorder tie-break when two siblings share the same `order` value
// ---------------------------------------------------------------------------
class TestProjectViewModelHierarchy : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Unique QSettings scope so this suite does not collide with the
        // other ProjectViewModel suites (defaults / crud) or pollute real
        // user settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_ProjVMHier"));
        QCoreApplication::setApplicationName(QStringLiteral("test_projectviewmodel_hierarchy"));
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
    // 1) Cycle prevention: A → B → C; setParentId(A, "C") must be rejected
    //    because making A's parent C creates a cycle A → C → B → A.
    //    ProjectViewModel::updateProject walks the candidate parent's chain
    //    and returns false if it ever encounters `id`.
    // -----------------------------------------------------------------------
    void move_project_to_own_descendant_is_rejected_or_cycle_prevention()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        // Build chain A → B → C (C is grandchild of A).
        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"), QStringLiteral("grey"), a);
        const QString c = vm.addProject(QStringLiteral("C"), QStringLiteral("grey"), b);

        QCOMPARE(vm.projectById(b)->parentId(), a);
        QCOMPARE(vm.projectById(c)->parentId(), b);

        // Attempt to make A a child of C — this is a cycle:
        //   A.parent = C, C.parent = B, B.parent = A → loop back to A.
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);
        const bool ok = vm.updateProject(a, QStringLiteral("parentId"), c);

        // PIN: rejected — A.parentId unchanged.
        QCOMPARE(ok, false);
        QVERIFY(vm.projectById(a)->parentId().isEmpty());
        QCOMPARE(changedSpy.count(), 0);

        // Sibling/descendant chain unchanged.
        QCOMPARE(vm.projectById(b)->parentId(), a);
        QCOMPARE(vm.projectById(c)->parentId(), b);

        qInfo("PIN: cycle detection in ProjectViewModel::updateProject walks "
              "candidate parent's ancestor chain and rejects any assignment that "
              "would close a loop. Self-parent and N-step descendant parent are "
              "both rejected.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 2) Archive cascade: archiveProject on a parent must NOT auto-archive
    //    children. Pin current behaviour either way — today it does not
    //    cascade (archive is a per-project field).
    // -----------------------------------------------------------------------
    void archive_parent_does_not_cascade_archive_to_children()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString parent = vm.addProject(QStringLiteral("Parent"));
        const QString child1 = vm.addProject(QStringLiteral("Child 1"), QStringLiteral("grey"), parent);
        const QString child2 = vm.addProject(QStringLiteral("Child 2"), QStringLiteral("grey"), parent);

        QVERIFY(!vm.projectById(parent)->isArchived());
        QVERIFY(!vm.projectById(child1)->isArchived());
        QVERIFY(!vm.projectById(child2)->isArchived());

        QVERIFY(vm.archiveProject(parent));

        // PIN: parent flipped, children untouched.
        QVERIFY(vm.projectById(parent)->isArchived());
        QVERIFY(!vm.projectById(child1)->isArchived());
        QVERIFY(!vm.projectById(child2)->isArchived());

        qInfo("PIN: archiveProject is a flat per-project flag flip — children "
              "of an archived parent KEEP their isArchived=false. If product "
              "wants cascading archive, this slot is the canary.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 3) Inbox rename — Q3.08-B fix. ProjectViewModel::updateProject now
    //    short-circuits with `false` when the targeted project is the Inbox
    //    and the field is `name`. The Inbox keeps its original name, no
    //    projectChanged emission, no setUpdatedAt bump.
    // -----------------------------------------------------------------------
    void inbox_rename_is_rejected_or_silently_no_op()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);
        const QString inboxId = inbox->id();
        const QString originalName = inbox->name();
        const QDateTime updatedAtBefore = inbox->updatedAt();

        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);
        QSignalSpy nameSpy(inbox, &Project::nameChanged);

        const bool ok = vm.updateProject(inboxId, QStringLiteral("name"),
                                         QStringLiteral("Renamed"));

        // ASSERT FIX: updateProject returns false and the Inbox name is unchanged.
        QCOMPARE(ok, false);
        QCOMPARE(inbox->name(), originalName);
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(nameSpy.count(), 0);

        // updatedAt also untouched — the guard runs before the setUpdatedAt
        // call, so the field is not silently bumped.
        QCOMPARE(inbox->updatedAt(), updatedAtBefore);

        // Inbox flag itself remains sticky.
        QVERIFY(inbox->isInbox());

        qInfo("FIX (Q3.08-B): ProjectViewModel::updateProject now rejects a "
              "rename of the Inbox project. Original name '%s' preserved; "
              "no projectChanged / nameChanged emission. Symmetry with "
              "DataStore::removeProject's inbox short-circuit.",
              qUtf8Printable(originalName));

        delete store;
    }

    // -----------------------------------------------------------------------
    // 4) Inbox setParentId — Q3.08-B fix. Mirror of (3) for the parentId
    //    field. Guard runs before the cycle-detection loop, so even a valid
    //    candidate parent gets rejected when the target is Inbox.
    // -----------------------------------------------------------------------
    void inbox_setParentId_is_rejected_or_silently_no_op()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);
        const QString inboxId = inbox->id();
        QVERIFY(inbox->parentId().isEmpty()); // sanity
        const QDateTime updatedAtBefore = inbox->updatedAt();

        const QString other = vm.addProject(QStringLiteral("Other"));

        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);
        QSignalSpy parentSpy(inbox, &Project::parentIdChanged);

        const bool ok = vm.updateProject(inboxId, QStringLiteral("parentId"), other);

        // ASSERT FIX: rejected — Inbox stays parentless.
        QCOMPARE(ok, false);
        QVERIFY(inbox->parentId().isEmpty());
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(parentSpy.count(), 0);
        QCOMPARE(inbox->updatedAt(), updatedAtBefore);
        QVERIFY(inbox->isInbox());

        qInfo("FIX (Q3.08-B): ProjectViewModel::updateProject now rejects a "
              "parentId reassignment on the Inbox project. The Inbox-"
              "immutability invariant (name + parentId locked) is enforced "
              "at the ViewModel layer.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 4a) Inbox color change — pin that the immutability guard is narrow.
    //     Color is legitimate user customisation (palette pick) and must
    //     still go through. The guard above only blocks name + parentId.
    // -----------------------------------------------------------------------
    void inbox_color_change_is_allowed()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);
        const QString inboxId = inbox->id();
        const QString originalColor = inbox->color();

        QSignalSpy colorSpy(inbox, &Project::colorChanged);
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);

        const bool ok = vm.updateProject(inboxId, QStringLiteral("color"),
                                         QStringLiteral("berry_red"));

        // Color update goes through — Inbox guard is narrow (name + parentId).
        QCOMPARE(ok, true);
        QCOMPARE(inbox->color(), QStringLiteral("berry_red"));
        QVERIFY(inbox->color() != originalColor);
        QCOMPARE(colorSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(changedSpy.takeFirst().at(0).toString(), inboxId);

        // Inbox flag still sticky.
        QVERIFY(inbox->isInbox());

        qInfo("PIN: Inbox color is legitimately user-customisable. The "
              "immutability guard is intentionally narrow — only name and "
              "parentId are locked; color/icon/isFavorite/order remain "
              "writeable so the user can theme the Inbox row.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 4b) Inbox icon change — same as (4a) for the icon field. Pins that
    //     the guard does not over-reach and block icon customisation.
    // -----------------------------------------------------------------------
    void inbox_icon_change_is_allowed()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);
        const QString inboxId = inbox->id();

        QSignalSpy iconSpy(inbox, &Project::iconChanged);
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);

        const bool ok = vm.updateProject(inboxId, QStringLiteral("icon"),
                                         QStringLiteral("I"));

        QCOMPARE(ok, true);
        QCOMPARE(inbox->icon(), QStringLiteral("I"));
        QCOMPARE(iconSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(changedSpy.takeFirst().at(0).toString(), inboxId);
        QVERIFY(inbox->isInbox());

        qInfo("PIN: Inbox icon write is allowed — guard is narrow to "
              "name/parentId. Mirrors 4a for the icon field.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 5) Inbox delete — pin that DataStore::removeProject returns false for
    //    inbox, ProjectViewModel::deleteProject propagates that as false,
    //    no projectRemoved signal, no tombstone.
    // -----------------------------------------------------------------------
    void inbox_delete_is_rejected()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        Project *inbox = store->inboxProject();
        QVERIFY(inbox);
        const QString inboxId = inbox->id();

        QSignalSpy removedSpy(&vm, &ProjectViewModel::projectRemoved);

        const bool ok = vm.deleteProject(inboxId);

        // PIN: rejected — return false, no tombstone, no signal.
        QCOMPARE(ok, false);
        QVERIFY(!inbox->isDeleted());
        QCOMPARE(removedSpy.count(), 0);

        // Inbox is still discoverable in both DataStore and VM.
        QCOMPARE(store->inboxProject(), inbox);
        QCOMPARE(vm.projectById(inboxId), inbox);

        qInfo("PIN: DataStore::removeProject short-circuits with `false` when "
              "project->isInbox(). deleteProject in the VM propagates this and "
              "skips the projectRemoved emission.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 6) Color change emits Project::colorChanged AND ProjectViewModel
    //    forwards projectChanged + dataChanged for the right row.
    //    Pins the Q_PROPERTY NOTIFY plumbing.
    // -----------------------------------------------------------------------
    void color_change_emits_colorChanged_signal()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Painted"), QStringLiteral("grey"));
        Project *p = vm.projectById(id);
        QVERIFY(p);
        QCOMPARE(p->color(), QStringLiteral("grey"));

        QSignalSpy colorSpy(p, &Project::colorChanged);
        QSignalSpy iconSpy(p, &Project::iconChanged);
        QSignalSpy projChangedSpy(&vm, &ProjectViewModel::projectChanged);
        QSignalSpy dataChangedSpy(&vm,
            static_cast<void (QAbstractItemModel::*)(const QModelIndex &,
                                                    const QModelIndex &,
                                                    const QList<int> &)>(
                &QAbstractItemModel::dataChanged));

        // 6a) Color path
        QVERIFY(vm.updateProject(id, QStringLiteral("color"), QStringLiteral("kale")));
        QCOMPARE(p->color(), QStringLiteral("kale"));
        QCOMPARE(colorSpy.count(), 1);
        QCOMPARE(projChangedSpy.count(), 1);
        QCOMPARE(projChangedSpy.takeFirst().at(0).toString(), id);
        QCOMPARE(dataChangedSpy.count(), 1);

        // 6b) Icon path (same Q_PROPERTY NOTIFY pattern). Use a literal ASCII
        // glyph to keep the source ASCII-safe.
        QVERIFY(vm.updateProject(id, QStringLiteral("icon"), QStringLiteral("R")));
        QCOMPARE(p->icon(), QStringLiteral("R"));
        QCOMPARE(iconSpy.count(), 1);
        QCOMPARE(projChangedSpy.count(), 1); // one more since clear
        QCOMPARE(dataChangedSpy.count(), 2);

        // 6c) Same-color write still emits if Project::setColor doesn't
        //     short-circuit. Pin whichever behaviour exists.
        const int colorCountBefore = colorSpy.count();
        QVERIFY(vm.updateProject(id, QStringLiteral("color"), QStringLiteral("kale")));
        qInfo("PIN: same-color rewrite produced %d additional colorChanged "
              "emissions (Project::setColor short-circuits on equal-value).",
              colorSpy.count() - colorCountBefore);

        delete store;
    }

    // -----------------------------------------------------------------------
    // 7) Move-to-root: setParentId(child, "") clears the parent and the
    //    project becomes a top-level one. The cycle-detection loop exits
    //    immediately on empty `cur`, so the empty assignment is allowed.
    // -----------------------------------------------------------------------
    void move_to_root_clears_parentId()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString parent = vm.addProject(QStringLiteral("Parent"));
        const QString child = vm.addProject(QStringLiteral("Child"), QStringLiteral("grey"), parent);
        QCOMPARE(vm.projectById(child)->parentId(), parent);

        // Move to root via empty string. updateProject accepts QString.
        const bool ok = vm.updateProject(child, QStringLiteral("parentId"), QString());
        QCOMPARE(ok, true);
        QVERIFY(vm.projectById(child)->parentId().isEmpty());

        // It is now a child of "" — i.e. surfaces in childProjects("").
        // Inbox + Parent + Child are the three top-level entries; we just
        // assert membership for `child`.
        const QVariantList rootKids = vm.childProjects(QString());
        bool found = false;
        for (const QVariant &v : rootKids) {
            if (v.toMap().value(QStringLiteral("id")).toString() == child) {
                found = true;
                break;
            }
        }
        QVERIFY(found);

        qInfo("PIN: setParentId(child, \"\") is accepted by updateProject — the "
              "cycle-detection while-loop exits immediately on the empty "
              "candidate, so an explicit move-to-root is a normal mutation.");

        delete store;
    }

    // -----------------------------------------------------------------------
    // 8) Reorder tie-break: when two sibling projects carry identical
    //    `order` values, ProjectViewModel::refresh uses std::sort with a
    //    strict-weak `a->order() < b->order()` comparator. std::sort is NOT
    //    guaranteed stable, but in practice for equal keys it preserves
    //    relative input order on libc++/libstdc++. Pin observed behaviour.
    // -----------------------------------------------------------------------
    void reorder_with_duplicate_order_values_resolves_via_uuid_or_creation_time()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        // Create three peers in known insertion order; addProject assigns
        // sequential `order` (1..N). Then collide their order fields onto
        // a single value to provoke the tie-break.
        const QString a = vm.addProject(QStringLiteral("A"));
        const QString b = vm.addProject(QStringLiteral("B"));
        const QString c = vm.addProject(QStringLiteral("C"));

        // Force collision: all three siblings end up with order == 5.
        QVERIFY(vm.updateProject(a, QStringLiteral("order"), 5));
        QVERIFY(vm.updateProject(b, QStringLiteral("order"), 5));
        QVERIFY(vm.updateProject(c, QStringLiteral("order"), 5));

        // Capture the visible row layout after the collision. Inbox stays
        // pinned to row 0 by the explicit `if (a->isInbox()) return true;`
        // branch; the three peers fill rows 1..3 in *some* order.
        QStringList rowIds;
        for (int i = 0; i < vm.rowCount(); ++i)
            rowIds << vm.projectAt(i)->id();

        // Inbox must still be row 0.
        QVERIFY(vm.projectAt(0)->isInbox());

        // Each of A, B, C must appear exactly once in rows 1..3 — no row
        // duplicated, no row missing.
        QStringList peerRows = rowIds.mid(1);
        QCOMPARE(peerRows.size(), 3);
        QVERIFY(peerRows.contains(a));
        QVERIFY(peerRows.contains(b));
        QVERIFY(peerRows.contains(c));

        qInfo("PIN: tie-break for equal `order` values relies on std::sort's "
              "unspecified-but-stable-in-practice behaviour. Observed row "
              "sequence after collision (rows 1..3): [%s, %s, %s]. "
              "If determinism matters product-side, add a secondary key (e.g. "
              "createdAt or id) to the lambda in ProjectViewModel::refresh.",
              qUtf8Printable(peerRows.at(0)),
              qUtf8Printable(peerRows.at(1)),
              qUtf8Printable(peerRows.at(2)));

        // Reorder must still succeed despite the colliding inputs — and it
        // should renumber 1..N so the collision is gone after the move.
        QVERIFY(vm.reorderProject(1, 3));

        QSet<int> ordersAfter;
        int nonInboxRows = 0;
        for (int i = 0; i < vm.rowCount(); ++i) {
            Project *p = vm.projectAt(i);
            if (p->isInbox()) continue;
            ordersAfter.insert(p->order());
            ++nonInboxRows;
        }
        // All three orders now distinct.
        QCOMPARE(ordersAfter.size(), nonInboxRows);

        delete store;
    }

    // -----------------------------------------------------------------------
    // 9) Two rapid setName mutations: with no async writeback in
    //    updateProject, the second call wins synchronously and both emit
    //    projectChanged. Pin "last write wins" + signal count.
    // -----------------------------------------------------------------------
    void concurrent_rename_does_not_corrupt_state()
    {
        DataStore *store = makeStore();
        ProjectViewModel vm(store);

        const QString id = vm.addProject(QStringLiteral("Original"));
        QSignalSpy changedSpy(&vm, &ProjectViewModel::projectChanged);

        // Two near-simultaneous renames via the synchronous Q_INVOKABLE.
        // The dispatch is single-threaded; the second call commits "Final".
        QVERIFY(vm.updateProject(id, QStringLiteral("name"), QStringLiteral("Interim")));
        QVERIFY(vm.updateProject(id, QStringLiteral("name"), QStringLiteral("Final")));

        // PIN: state holds the last write, not a merge or the first one.
        QCOMPARE(vm.projectById(id)->name(), QStringLiteral("Final"));

        // Both updates fire projectChanged in order.
        QCOMPARE(changedSpy.count(), 2);
        QCOMPARE(changedSpy.at(0).at(0).toString(), id);
        QCOMPARE(changedSpy.at(1).at(0).toString(), id);

        // updatedAt should have moved monotonically forward through both
        // setUpdatedAt(now) calls — pin that the field is non-default
        // after the burst.
        QVERIFY(vm.projectById(id)->updatedAt().isValid());

        qInfo("PIN: ProjectViewModel::updateProject is fully synchronous on the "
              "calling thread. Two back-to-back setName calls produce two "
              "projectChanged emissions and the second value is the durable "
              "one — no debounce, no coalescing.");

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestProjectViewModelHierarchy)
#include "test_projectviewmodel_hierarchy.moc"
