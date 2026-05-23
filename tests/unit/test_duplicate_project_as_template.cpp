// =============================================================================
// Pin test for iter-night-7: duplicateProjectAsTemplate must copy the FULL
// task hierarchy + duplicate sections, with parent_id and section_id remapped
// to the template's own ids.
//
// Before the fix, the function:
//   (1) iterated only `tasksForProject(projectId)` which is top-level-only
//       — every subtask was silently dropped from the template;
//   (2) copied the source project's section_id verbatim — template tasks
//       pointed at sections living in the SOURCE project, so board grouping
//       showed them under foreign section headers and sync propagated the
//       stale FK to peers.
//
// This test pins:
//   1. Top-level + depth-1 subtask + depth-2 grandchild all copied.
//   2. Parent_id is remapped — no template task points at a source-project
//      task id.
//   3. Sections are duplicated with the same names, in the template project.
//   4. Each copied task's section_id points at the TEMPLATE's section, not
//      the source's.
//   5. Cycles in the source data don't hang the BFS walker.
//   6. Source project is left untouched.
//   7. Tombstoned source tasks are skipped.
// =============================================================================

#include <QtTest>
#include <QString>
#include <QSet>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "viewmodels/taskviewmodel.h"

class TestDuplicateProjectAsTemplate : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    QString inboxId;

private slots:
    void init() {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
    }
    void cleanup() { delete store; store = nullptr; }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Deep subtree (3 levels) survives the duplication.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_copiesDepth3Subtree()
    {
        Project *src = store->addProject(QStringLiteral("Onboarding"));
        Task *a = store->addTask(QStringLiteral("Install IDE"), src->id());
        Task *b = store->addTask(QStringLiteral("Install Git plugin"), src->id());
        Task *c = store->addTask(QStringLiteral("Configure Git plugin"), src->id());
        b->setParentId(a->id());
        c->setParentId(b->id());

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        // Three tasks live in the template, none in the source's id space.
        int templateTaskCount = 0;
        QSet<QString> templateContents;
        for (Task *t : store->allTasks()) {
            if (t->projectId() == tplId) {
                templateTaskCount++;
                templateContents.insert(t->content());
            }
        }
        QCOMPARE(templateTaskCount, 3);
        QVERIFY(templateContents.contains(QStringLiteral("Install IDE")));
        QVERIFY2(templateContents.contains(QStringLiteral("Install Git plugin")),
                 "Depth-1 subtask must be copied (was silently dropped).");
        QVERIFY2(templateContents.contains(QStringLiteral("Configure Git plugin")),
                 "Depth-2 grandchild must be copied.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) parent_id is remapped through the new task ids.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_remapsParentIdToTemplate()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Task *root = store->addTask(QStringLiteral("Root"), src->id());
        Task *child = store->addTask(QStringLiteral("Child"), src->id());
        child->setParentId(root->id());

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        // Find the template's root + child by content.
        Task *tplRoot = nullptr;
        Task *tplChild = nullptr;
        for (Task *t : store->allTasks()) {
            if (t->projectId() != tplId) continue;
            if (t->content() == QStringLiteral("Root"))  tplRoot = t;
            if (t->content() == QStringLiteral("Child")) tplChild = t;
        }
        QVERIFY(tplRoot != nullptr);
        QVERIFY(tplChild != nullptr);

        // No template task should point at a source-project task.
        QVERIFY2(tplChild->parentId() != root->id(),
                 "Template child must NOT inherit the source root's id — "
                 "that would make sync cascade-delete the template's child "
                 "whenever the source root is removed.");
        QCOMPARE(tplChild->parentId(), tplRoot->id());
        QVERIFY(tplRoot->parentId().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Sections are duplicated, with same names, in the template.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_duplicatesSections()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Section *secA = store->addSection(QStringLiteral("Setup"), src->id());
        Section *secB = store->addSection(QStringLiteral("Tests"), src->id());
        Q_UNUSED(secA);
        Q_UNUSED(secB);

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        const auto tplSections = store->sectionsForProject(tplId);
        QCOMPARE(tplSections.size(), 2);
        QSet<QString> names;
        for (Section *s : tplSections)
            names.insert(s->name());
        QVERIFY(names.contains(QStringLiteral("Setup")));
        QVERIFY(names.contains(QStringLiteral("Tests")));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) section_id on copied tasks points at the TEMPLATE's section.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_remapsSectionIdToTemplate()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Section *secA = store->addSection(QStringLiteral("Setup"), src->id());
        Task *t = store->addTask(QStringLiteral("Install IDE"), src->id());
        t->setSectionId(secA->id());

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        // Template's section + task.
        Section *tplSec = nullptr;
        for (Section *s : store->sectionsForProject(tplId)) {
            if (s->name() == QStringLiteral("Setup")) { tplSec = s; break; }
        }
        QVERIFY(tplSec != nullptr);

        Task *tplTask = nullptr;
        for (Task *tk : store->allTasks()) {
            if (tk->projectId() == tplId
                && tk->content() == QStringLiteral("Install IDE")) {
                tplTask = tk;
                break;
            }
        }
        QVERIFY(tplTask != nullptr);

        QVERIFY2(tplTask->sectionId() != secA->id(),
                 "Template task must NOT keep the source section id — that "
                 "stale FK propagates through sync and groups the template "
                 "task under a section in the source project.");
        QCOMPARE(tplTask->sectionId(), tplSec->id());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) Pre-existing parent_id cycle in source data does NOT hang the BFS.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_handlesPreExistingCycle()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Task *a = store->addTask(QStringLiteral("A"), src->id());
        Task *b = store->addTask(QStringLiteral("B"), src->id());
        a->setParentId(b->id());
        b->setParentId(a->id());

        TaskViewModel vm(store);
        // If the BFS hangs this never returns; QtTest will time out.
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        // Neither A nor B has parent_id empty in the source, so neither
        // qualifies as a BFS seed (tasksForProject filters parent_id
        // empty). Result: the template is created but has zero tasks
        // because nothing could be safely topologically copied. That's
        // the defensive fall-through, not a crash.
        int tplCount = 0;
        for (Task *t : store->allTasks()) {
            if (t->projectId() == tplId) tplCount++;
        }
        QCOMPARE(tplCount, 0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6) Source project is not mutated by the duplication.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_leavesSourceUntouched()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Section *sec = store->addSection(QStringLiteral("Setup"), src->id());
        Task *a = store->addTask(QStringLiteral("A"), src->id());
        Task *b = store->addTask(QStringLiteral("B"), src->id());
        a->setSectionId(sec->id());
        b->setParentId(a->id());

        const QString origAId = a->id();
        const QString origBId = b->id();
        const QString origSecId = sec->id();
        const QString origAParent = a->parentId();
        const QString origBParent = b->parentId();
        const QString origASection = a->sectionId();

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        // Re-fetch and verify the source side is byte-for-byte unchanged.
        Task *aAfter = store->taskById(origAId);
        Task *bAfter = store->taskById(origBId);
        Section *secAfter = store->sectionById(origSecId);
        QVERIFY(aAfter && !aAfter->isDeleted());
        QVERIFY(bAfter && !bAfter->isDeleted());
        QVERIFY(secAfter && !secAfter->isDeleted());
        QCOMPARE(aAfter->parentId(),  origAParent);
        QCOMPARE(bAfter->parentId(),  origBParent);
        QCOMPARE(aAfter->sectionId(), origASection);
        QCOMPARE(aAfter->projectId(), src->id());
        QCOMPARE(bAfter->projectId(), src->id());
        QCOMPARE(secAfter->projectId(), src->id());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 7) Tombstoned source tasks are not copied into the template.
    // ─────────────────────────────────────────────────────────────────────────
    void duplicate_skipsTombstonedSourceTasks()
    {
        Project *src = store->addProject(QStringLiteral("Proj"));
        Task *alive = store->addTask(QStringLiteral("Alive"), src->id());
        Task *dead  = store->addTask(QStringLiteral("Dead"), src->id());
        Q_UNUSED(alive);
        store->removeTask(dead->id());
        QVERIFY(dead->isDeleted());

        TaskViewModel vm(store);
        const QString tplId = vm.duplicateProjectAsTemplate(src->id());
        QVERIFY(!tplId.isEmpty());

        QSet<QString> tplContents;
        for (Task *t : store->allTasks()) {
            if (t->projectId() == tplId) tplContents.insert(t->content());
        }
        QCOMPARE(tplContents.size(), 1);
        QVERIFY(tplContents.contains(QStringLiteral("Alive")));
        QVERIFY(!tplContents.contains(QStringLiteral("Dead")));
    }
};

QTEST_GUILESS_MAIN(TestDuplicateProjectAsTemplate)
#include "test_duplicate_project_as_template.moc"
