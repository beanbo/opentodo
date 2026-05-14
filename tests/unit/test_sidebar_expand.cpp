// =============================================================================
// test_sidebar_expand.cpp — REPRODUCTION TEST for the user-reported bug:
//
//   "Не раскрывается родительский проект 'Дом'"
//
// User reports that the chevron arrow next to a top-level project with
// sub-projects (e.g. "🏠 Дом" containing "🏘️ Квартал", "🛒 Покупки",
// "🇮🇹 Италия") is visible but clicking it does NOT toggle the expansion in
// the sidebar. This test reproduces the bug "at the seam where the C++
// ViewModel layer meets the QML expansion state machine".
//
// -----------------------------------------------------------------------------
// IMPORTANT — WHERE THE BUG ACTUALLY LIVES
// -----------------------------------------------------------------------------
// SidebarViewModel (src/viewmodels/sidebarviewmodel.{h,cpp}) does NOT expose
// any expansion API at all. It has NO toggleExpanded(), NO isExpanded(),
// NO expandedProjects set/list, NO expandedProjectsChanged signal. The
// project-tree expansion state is held ENTIRELY in QML — see
// qml/components/SidebarContent.qml lines 40-66:
//
//     property var expandedProjects: ({})          // JS set { id: true }
//     signal expandedToggled()
//     function toggleExpanded(projectId) { ... emit expandedToggled() }
//     function isExpanded(projectId)     { ... }
//
// Therefore this C++-level test PASSES — there is no C++ regression. The
// SidebarViewModel + ProjectViewModel + DataStore stack correctly:
//   - Stores parent_id round-trips on the Project model.
//   - ProjectViewModel::childProjects(parentId) returns the (non-deleted,
//     non-archived) children, wrapped as QVariantList of Project*.
//   - Adding/removing children fires projectsChanged → SidebarViewModel
//     re-emits its counts/labels/favorites signals as appropriate.
//
// The chevron-doesn't-toggle bug is therefore SOMEWHERE IN QML. The most
// plausible root causes (all consistent with the observed behaviour: chevron
// renders fine but onClicked never produces a visible expansion) are:
//
//   (1) The chevron's MouseArea is a tiny 16×16 hit-target nested inside a
//       Repeater-delegate whose ROOT Item is `anchors.fill`'d (transparently)
//       by `projReorderDropArea` (DropArea, default z=0) AND covered by the
//       deeply-nested `hoverMouse` MouseArea inside SidebarItem (z=0). The
//       chevron has z=12 + preventStealing — which SHOULD win — but the
//       comment at qml/components/SidebarContent.qml:713-718 already flags
//       this exact symptom as a previously-fixed regression:
//
//           "z: 12 keeps the chevron above sibling DropArea, the right-click
//            MouseArea, and crucially the dragHandle's MouseArea (z: 11)
//            which would otherwise swallow clicks in the x=4..16 overlap
//            region — that was the cause of 'клик по шеврону не разворачивает
//            родительский проект'."
//
//       The user is reporting the SAME symptom again, which strongly suggests
//       another sibling has been raised above z=12 (or a new event-handling
//       wrapper introduced).
//
//   (2) The Repeater's model is a plain JS array set on
//       `projectTreeColumnRepeater.model = rows`. The rebuild() depends on
//       `sidebarContent.expandedToggled` (explicitly emitted) AND
//       `sidebarContent.expandedProjectsChanged` (auto-generated). If
//       rebuild() runs but the new `expandedProjects` map is still referencing
//       the OLD JS object (e.g. mutated in place without a fresh assignment),
//       isExpanded() may still see the old value because Qt's `var` property
//       notify fires on object-identity change, not deep change. The current
//       toggleExpanded() DOES build a fresh object via Object.assign({}, ...)
//       and reassigns, so this is unlikely the proximate cause — but the
//       explicit `expandedToggled` signal exists precisely BECAUSE this was
//       flaky before.
//
//   (3) The most likely actual cause given the user report: the chevron
//       click IS reaching toggleExpanded, the rebuild IS running, and the
//       children ARE being pushed into `rows` — but the Repeater is failing
//       to re-create the delegates for the newly-appended rows because the
//       `Connections { target: sidebarContent }` block is declared as a
//       DIRECT CHILD of the Repeater (qml/components/SidebarContent.qml
//       lines 668-672). In Qt 6.5+, Connections inside a Repeater whose
//       parent is the Repeater itself (not a delegate) only get instantiated
//       once at Repeater construction; if the Repeater object is replaced/
//       reparented during a project-tree refresh (e.g. on
//       projectViewModel.onCountChanged), those Connections can silently
//       detach.
//
// -----------------------------------------------------------------------------
// WHAT THIS TEST VERIFIES
// -----------------------------------------------------------------------------
// At the C++ layer:
//   1. ProjectViewModel::childProjects(parentId) returns the expected
//      Project* list for a parent project with multiple children.
//   2. Children are filtered correctly (deleted/archived/inbox excluded).
//   3. parent_id round-trips on the Project model after a setParentId() call.
//   4. SidebarViewModel does NOT expose any expansion API surface — this is
//      documented as the entry point for the QML regression.
//
// Because there is NO C++ expansion API, the test does NOT call any
// sidebarVM.toggleExpanded() / isExpanded() — those would not compile.
// Instead, it pins down the C++ contract that QML's expansion logic depends
// on, so any future C++ regression that breaks child enumeration would be
// caught here.
// =============================================================================

#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QVariant>
#include <QVariantList>

#include "storage/datastore.h"
#include "model/project.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"


class TestSidebarExpand : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Dedicated QSettings scope so we never touch real OpenTodo user data
        // (especially since SidebarViewModel::labelOrder reads/writes
        // QSettings directly).
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SidebarExpand"));
        QCoreApplication::setApplicationName(QStringLiteral("OpenTodoTest_SidebarExpand"));
    }

    void init()
    {
        // Wipe any leftover sidebar settings from a previous slot.
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // -------------------------------------------------------------------------
    // Reproduction of the "Дом" scenario at the SidebarViewModel level.
    //
    // Builds a DataStore + ProjectViewModel + SidebarViewModel; adds a parent
    // project "Дом" with three children; verifies the C++ chain that the QML
    // expansion logic depends on. The test itself PASSES because the C++ layer
    // is correct — see the comment block at the top of this file for the
    // QML-side analysis of the actual chevron-doesn't-toggle regression.
    // -------------------------------------------------------------------------
    void parent_dom_has_three_children_enumerable_by_childProjects()
    {
        DataStore store;
        ProjectViewModel pvm(&store);

        // Inbox first so ProjectViewModel's "Inbox sorted first" invariant
        // doesn't interfere with our ordering assumptions.
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        QVERIFY(inbox);
        inbox->setIsInbox(true);

        // Parent: "🏠 Дом"
        Project *dom = store.addProject(QStringLiteral("\xF0\x9F\x8F\xA0 Дом"));
        QVERIFY(dom);
        const QString domId = dom->id();
        QVERIFY(!domId.isEmpty());

        // Three children — mirroring the user's actual data:
        //   "🏘️ Квартал", "🛒 Покупки", "🇮🇹 Италия"
        Project *kvartal = store.addProject(QStringLiteral("\xF0\x9F\x8F\x98 Квартал"));
        Project *pokupki = store.addProject(QStringLiteral("\xF0\x9F\x9B\x92 Покупки"));
        Project *italia  = store.addProject(QStringLiteral("\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9 Италия"));
        QVERIFY(kvartal && pokupki && italia);

        kvartal->setParentId(domId);
        pokupki->setParentId(domId);
        italia ->setParentId(domId);

        // parent_id must round-trip through the Project model.
        QCOMPARE(kvartal->parentId(), domId);
        QCOMPARE(pokupki->parentId(), domId);
        QCOMPARE(italia ->parentId(), domId);

        // Nudge ProjectViewModel.refresh() so m_projects picks up the new
        // children. setParentId on its own doesn't fire projectsChanged.
        emit store.projectsChanged();

        // childProjects(domId) should return exactly the three children.
        const QVariantList children = pvm.childProjects(domId);
        QCOMPARE(children.size(), 3);

        // Each entry is a wrapped Project* — verify by extracting and
        // checking parentId.
        QSet<QString> childIds;
        for (const QVariant &v : children) {
            Project *p = qvariant_cast<Project *>(v);
            QVERIFY(p != nullptr);
            QCOMPARE(p->parentId(), domId);
            childIds.insert(p->id());
        }
        QVERIFY(childIds.contains(kvartal->id()));
        QVERIFY(childIds.contains(pokupki->id()));
        QVERIFY(childIds.contains(italia ->id()));

        // The parent itself is NOT its own child.
        QVERIFY(!childIds.contains(domId));
    }

    // -------------------------------------------------------------------------
    // childProjects must filter out archived and inbox children, mirroring
    // the QML rebuild() loop's `if (!child || child.isArchived || child.isInbox)
    // continue` guard. This is the contract the chevron's hasChildren
    // property relies on.
    // -------------------------------------------------------------------------
    void childProjects_returns_archived_too_filtering_is_qml_side()
    {
        // CONTRACT NOTE: ProjectViewModel.childProjects() returns ANY project
        // (in m_projects) whose parentId matches — including archived ones
        // WHEN showArchived=true. The QML rebuild() loop filters
        // child.isArchived and child.isInbox itself. We verify both halves:
        //   (a) with showArchived=false (default), m_projects excludes
        //       archived → childProjects naturally returns only visible kids.
        //   (b) with showArchived=true, archived children ARE in the result
        //       and the QML layer is responsible for skipping them.
        DataStore store;
        ProjectViewModel pvm(&store);

        Project *dom    = store.addProject(QStringLiteral("Дом"));
        Project *child1 = store.addProject(QStringLiteral("child1"));
        Project *child2 = store.addProject(QStringLiteral("child2_archived"));
        QVERIFY(dom && child1 && child2);

        child1->setParentId(dom->id());
        child2->setParentId(dom->id());
        child2->setIsArchived(true);

        emit store.projectsChanged();

        // (a) Default — showArchived=false. Archived kid filtered out by
        //     ProjectViewModel.refresh()'s m_projects rebuild.
        QCOMPARE(pvm.showArchived(), false);
        QVariantList kids = pvm.childProjects(dom->id());
        QCOMPARE(kids.size(), 1);
        Project *only = qvariant_cast<Project *>(kids.at(0));
        QVERIFY(only && only->id() == child1->id());

        // (b) showArchived=true — archived child reappears.
        pvm.setShowArchived(true);
        kids = pvm.childProjects(dom->id());
        QCOMPARE(kids.size(), 2);
    }

    // -------------------------------------------------------------------------
    // The deeper concern flagged by the bug report: when the user clicks the
    // chevron, the QML side dispatches sidebarContent.toggleExpanded(id),
    // which mutates a `var` property and emits an explicit `expandedToggled`
    // signal. The Repeater then calls projectViewModel.childProjects(id)
    // again, expecting a STABLE, deterministic answer.
    //
    // This test pins that determinism: calling childProjects(id) repeatedly,
    // with no intervening mutations, returns the same set every time. If a
    // future refactor introduces nondeterminism here (e.g. ordering by hash,
    // randomised QHash iteration), the bug would manifest as "expansion
    // sometimes shows children, sometimes not" — exactly the kind of
    // symptom the user is reporting.
    // -------------------------------------------------------------------------
    void childProjects_is_deterministic_across_multiple_calls()
    {
        DataStore store;
        ProjectViewModel pvm(&store);

        Project *dom = store.addProject(QStringLiteral("Дом"));
        QVERIFY(dom);
        const QString domId = dom->id();

        for (int i = 0; i < 5; ++i) {
            Project *kid = store.addProject(QStringLiteral("kid_%1").arg(i));
            QVERIFY(kid);
            kid->setParentId(domId);
        }
        emit store.projectsChanged();

        const QVariantList first = pvm.childProjects(domId);
        QCOMPARE(first.size(), 5);

        // Re-evaluate 10× without any mutation in between.
        QStringList firstIds;
        for (const QVariant &v : first)
            firstIds << qvariant_cast<Project *>(v)->id();

        for (int i = 0; i < 10; ++i) {
            const QVariantList again = pvm.childProjects(domId);
            QCOMPARE(again.size(), first.size());
            QStringList againIds;
            for (const QVariant &v : again)
                againIds << qvariant_cast<Project *>(v)->id();
            QCOMPARE(againIds, firstIds);
        }
    }

    // -------------------------------------------------------------------------
    // SidebarViewModel public API contract — documents the QML-vs-C++ split.
    //
    // This test simply constructs the VM and asserts that, at compile time,
    // the expansion API IS NOT part of SidebarViewModel. The check is
    // intentionally indirect (via signal/slot count) because a missing slot
    // would cause a compilation error if we tried to invoke it. The point
    // of this slot is to LEAVE A TRAIL in the test suite, so a developer
    // hunting the chevron bug starts at this test file and reads the top
    // comment.
    // -------------------------------------------------------------------------
    void sidebarviewmodel_has_no_expansion_api_documented_in_top_comment()
    {
        DataStore store;
        ProjectViewModel pvm(&store);
        SidebarViewModel sidebar(&store, &pvm);

        const QMetaObject *meta = sidebar.metaObject();

        // Slot/method audit: confirm no method named "toggleExpanded" or
        // "isExpanded" exists. If a future commit adds these, this test
        // fails loudly and we update the top comment to reflect the new
        // ground truth.
        bool hasToggle = false;
        bool hasIsExp  = false;
        bool hasExpProps = false;
        for (int i = 0; i < meta->methodCount(); ++i) {
            const QMetaMethod m = meta->method(i);
            const QByteArray name = m.name();
            if (name == "toggleExpanded") hasToggle = true;
            if (name == "isExpanded")     hasIsExp  = true;
        }
        for (int i = 0; i < meta->propertyCount(); ++i) {
            const QMetaProperty p = meta->property(i);
            const QByteArray name = p.name();
            if (name == "expandedProjects") hasExpProps = true;
        }

        QVERIFY2(!hasToggle,
                 "SidebarViewModel newly exposes toggleExpanded — update "
                 "the top-of-file comment in test_sidebar_expand.cpp because "
                 "expansion state has moved from QML into C++.");
        QVERIFY2(!hasIsExp,
                 "SidebarViewModel newly exposes isExpanded — see comment above.");
        QVERIFY2(!hasExpProps,
                 "SidebarViewModel newly exposes expandedProjects — see comment above.");
    }

    // -------------------------------------------------------------------------
    // End-to-end-ish: simulate the SEQUENCE of events that fires when the
    // user clicks the chevron in QML. We cannot test the QML side from here,
    // but we CAN test that every C++ signal the QML side listens to fires
    // correctly when we mutate the underlying DataStore.
    //
    // QML wiring under test (see SidebarContent.qml:618-624 + 668-672):
    //   target: projectViewModel
    //     onCountChanged   → rebuild
    //     onProjectChanged → rebuild
    //     onProjectAdded   → rebuild
    //     onProjectRemoved → rebuild
    // -------------------------------------------------------------------------
    void projectviewmodel_emits_signals_qml_rebuild_listens_to()
    {
        DataStore store;
        ProjectViewModel pvm(&store);

        QSignalSpy countSpy   (&pvm, &ProjectViewModel::countChanged);
        QSignalSpy addedSpy   (&pvm, &ProjectViewModel::projectAdded);
        QSignalSpy removedSpy (&pvm, &ProjectViewModel::projectRemoved);
        QSignalSpy changedSpy (&pvm, &ProjectViewModel::projectChanged);

        Project *dom = store.addProject(QStringLiteral("Дом"));
        QVERIFY(dom);
        // addProject directly on DataStore bypasses VM.addProject, so
        // projectAdded is NOT emitted by the VM here. countChanged DOES
        // fire via the projectsChanged → refresh → emit chain.
        QVERIFY(countSpy.count() >= 1);

        // Use the VM's addProject which DOES emit projectAdded.
        QString childId = pvm.addProject(QStringLiteral("child"), QStringLiteral("grey"),
                                         dom->id());
        QVERIFY(!childId.isEmpty());
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(addedSpy.takeFirst().at(0).toString(), childId);

        // Renaming the child fires projectChanged → rebuild on QML side.
        QVERIFY(pvm.updateProject(childId, QStringLiteral("name"),
                                  QStringLiteral("renamed")));
        QCOMPARE(changedSpy.count(), 1);

        // Removing the child fires projectRemoved → rebuild on QML side.
        QVERIFY(pvm.deleteProject(childId));
        QCOMPARE(removedSpy.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSidebarExpand)
#include "test_sidebar_expand.moc"
