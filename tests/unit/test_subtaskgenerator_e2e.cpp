// SubtaskGenerator end-to-end flow tests (no network).
//
// AIAssistant emits subtasksGenerated(QStringList, QString) when a model
// reply has been parsed. The wiring that turns those suggestions into real
// child tasks lives in QML — SubtaskGeneratorDialog.qml's
// onSubtasksGenerated handler iterates the list and calls
// taskViewModel.addSubtask(parentId, stepText) for each non-empty entry
// the user kept checked. There is no C++ wiring for this signal in
// main.cpp; it goes straight to QML.
//
// Because we cannot drive QML in a QTEST_GUILESS_MAIN suite (and don't
// want to), this file replicates the exact QML wiring in C++:
//
//     QObject::connect(&ai, &AIAssistant::subtasksGenerated, this,
//         [&](const QStringList &suggestions, const QString &parentId) {
//             for (const QString &s : suggestions) {
//                 const QString trimmed = s.trimmed();
//                 if (trimmed.isEmpty()) continue;          // QML guard
//                 vm.addSubtask(parentId, trimmed);
//             }
//         });
//
// We then manually emit `subtasksGenerated(...)` on AIAssistant to bypass
// the LLM round-trip and assert on the resulting DataStore state. This
// hits the post-network half of the chain end-to-end without involving a
// real HTTP request.
//
// The test class also documents (per the task brief) that the QML side
// applies a `.trim() !== ""` guard before calling addSubtask, so empty
// suggestions never reach DataStore.

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>

#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "services/aiassistant.h"
#include "viewmodels/taskviewmodel.h"

// Helper: build a DataStore with a minimal Inbox project, parented to
// `parent` so the test's QObject hierarchy cleans everything up on tear-
// down. Mirrors the helper in the existing subtask-generator test files.
static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// Helper: add a task to a DataStore and return it.
static Task *addTask(DataStore *ds, const QString &content,
                     const QString &projectId = QString())
{
    return ds->addTask(content, projectId);
}

class TestSubtaskGeneratorE2E : public QObject
{
    Q_OBJECT

    // The wiring under test, replicated from
    // SubtaskGeneratorDialog.qml's onSubtasksGenerated handler.
    // Hooks the AIAssistant signal to TaskViewModel::addSubtask.
    //
    // Returns a QObject so callers can disconnect on teardown — we just
    // disconnect the AIAssistant's signal directly via QObject::disconnect.
    static void wireDialogHandler(AIAssistant *ai, TaskViewModel *vm)
    {
        QObject::connect(ai, &AIAssistant::subtasksGenerated, vm,
            [vm](const QStringList &suggestions, const QString &parentId) {
                // QML applies item.stepText.trim() !== "" before each call.
                // We replicate that here so the test mirrors the real path.
                for (const QString &s : suggestions) {
                    const QString trimmed = s.trimmed();
                    if (trimmed.isEmpty())
                        continue;
                    vm->addSubtask(parentId, trimmed);
                }
            });
    }

private slots:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SubtaskGenE2E"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_subtaskgenerator_e2e"));
    }

    // ─── Happy path: 3 suggestions create 3 child tasks ────────────────────

    void emit_three_suggestions_creates_three_subtasks()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent task"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        // Sanity: no subtasks before the AI fires.
        QCOMPARE(store->subtasks(parentId).size(), 0);

        const QStringList suggestions = {
            QStringLiteral("Шаг один"),
            QStringLiteral("Шаг два"),
            QStringLiteral("Шаг три"),
        };
        emit ai.subtasksGenerated(suggestions, parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 3);
    }

    // ─── Inheritance: child tasks inherit projectId from parent ────────────

    void child_tasks_inherit_projectId_from_parent()
    {
        DataStore *store = makeStore(this);
        // Parent lives in the inbox by default.
        Task *parent = addTask(store, QStringLiteral("Parent in inbox"));
        const QString parentId        = parent->id();
        const QString parentProjectId = parent->projectId();
        QVERIFY(!parentProjectId.isEmpty());

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        const QStringList suggestions = {
            QStringLiteral("Подзадача A"),
            QStringLiteral("Подзадача B"),
        };
        emit ai.subtasksGenerated(suggestions, parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 2);
        for (Task *kid : kids) {
            QCOMPARE(kid->projectId(), parentProjectId);
            QCOMPARE(kid->parentId(), parentId);
            // Reasonable defaults: not completed, not deleted, default p4 normal.
            QVERIFY(!kid->isCompleted());
            QVERIFY(!kid->isDeleted());
            QCOMPARE(kid->priority(), 4);
        }
    }

    // ─── Empty list is a no-op (no spurious child tasks) ───────────────────

    void empty_suggestions_creates_no_subtasks()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        const int totalBefore = store->allTasks().size();
        emit ai.subtasksGenerated(QStringList{}, parentId);

        QCOMPARE(store->subtasks(parentId).size(), 0);
        // Total task count unchanged — no orphans, no extras.
        QCOMPARE(store->allTasks().size(), totalBefore);
    }

    // ─── Unknown parentId: addSubtask returns "" → no child tasks created
    //     and no orphans (no crash). ──────────────────────────────────────

    void unknown_parentId_creates_no_subtasks_no_crash()
    {
        DataStore *store = makeStore(this);
        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        const int totalBefore = store->allTasks().size();

        // bogus parent id — addSubtask must return "" and create nothing.
        const QString bogus = QStringLiteral("nonexistent-uuid-xxx");
        const QStringList suggestions = {
            QStringLiteral("Шаг один"),
            QStringLiteral("Шаг два"),
        };
        emit ai.subtasksGenerated(suggestions, bogus);

        // The unknown-parent guard short-circuits in
        // TaskViewModel::addSubtask, so nothing was added to DataStore.
        QCOMPARE(store->allTasks().size(), totalBefore);
        QCOMPARE(store->subtasks(bogus).size(), 0);
    }

    // ─── Existing subtasks are preserved; new ones are appended ────────────

    void existing_subtasks_preserved_when_new_batch_added()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);

        // Seed two pre-existing children manually (i.e. as if the user had
        // added them before invoking the AI generator).
        const QString existing1 =
            vm.addSubtask(parentId, QStringLiteral("Существующая 1"));
        const QString existing2 =
            vm.addSubtask(parentId, QStringLiteral("Существующая 2"));
        QVERIFY(!existing1.isEmpty());
        QVERIFY(!existing2.isEmpty());
        QCOMPARE(store->subtasks(parentId).size(), 2);

        // Now wire the AI handler and emit two AI-generated suggestions.
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        const QStringList suggestions = {
            QStringLiteral("AI шаг 1"),
            QStringLiteral("AI шаг 2"),
        };
        emit ai.subtasksGenerated(suggestions, parentId);

        // 2 existing + 2 new = 4 children total, none replaced.
        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 4);

        // Both original ids are still present.
        QStringList ids;
        for (Task *k : kids) ids << k->id();
        QVERIFY(ids.contains(existing1));
        QVERIFY(ids.contains(existing2));
    }

    // ─── Order: AI-generated subtasks are appended at the end ─────────────

    void ai_subtasks_appended_after_existing()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);

        const QString existing =
            vm.addSubtask(parentId, QStringLiteral("Existing"));
        QVERIFY(!existing.isEmpty());

        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        emit ai.subtasksGenerated(
            QStringList{QStringLiteral("AI A"), QStringLiteral("AI B")},
            parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 3);

        // DataStore::addTask appends to m_tasks and assigns order = active
        // task count, so the AI-generated tasks are added strictly after
        // the existing one. Verify by sorting by `order` ascending — the
        // existing child must be first.
        QList<Task *> sorted = kids;
        std::sort(sorted.begin(), sorted.end(),
                  [](Task *a, Task *b) { return a->order() < b->order(); });
        QCOMPARE(sorted.first()->id(), existing);

        // The two AI-generated tasks must follow with strictly larger
        // order values than the existing one.
        QVERIFY(sorted.at(1)->order() > sorted.at(0)->order());
        QVERIFY(sorted.at(2)->order() > sorted.at(1)->order());

        // Their content matches the AI emissions, in emission order.
        QCOMPARE(sorted.at(1)->content(), QStringLiteral("AI A"));
        QCOMPARE(sorted.at(2)->content(), QStringLiteral("AI B"));
    }

    // ─── Content fidelity: literal string from AI reaches DataStore ────────
    //
    // Per the QML handler, the only transform applied between the AI
    // signal and addSubtask is `.trim()`. This pins that contract: tabs
    // and multi-word phrases including punctuation, mixed scripts, and
    // emoji-like sequences round-trip verbatim modulo leading/trailing
    // whitespace. addSubtask itself also calls .trimmed() (defense in
    // depth), so trimmed-equal content arrives unmodified.

    void ai_subtask_content_passes_through_verbatim()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        // Mix of: punctuation, an embedded quote, a unicode arrow, and a
        // multi-word Russian step. Nothing here should be escaped or
        // mangled by the wiring chain.
        const QString step1 =
            QStringLiteral("Создать ТЗ — один документ.");
        const QString step2 =
            QStringLiteral("\"Цитата\" внутри строки");
        const QString step3 =
            QStringLiteral("Шаг → следующий шаг");

        emit ai.subtasksGenerated(QStringList{step1, step2, step3}, parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 3);

        // Order in DataStore matches insertion order (addTask appends).
        QList<Task *> sorted = kids;
        std::sort(sorted.begin(), sorted.end(),
                  [](Task *a, Task *b) { return a->order() < b->order(); });

        QCOMPARE(sorted.at(0)->content(), step1);
        QCOMPARE(sorted.at(1)->content(), step2);
        QCOMPARE(sorted.at(2)->content(), step3);
    }

    // ─── Empty / whitespace-only entries do NOT reach DataStore ────────────
    //
    // The QML handler skips entries with item.stepText.trim() === "".
    // We mirror that in wireDialogHandler — verify here that an emission
    // mixing real and blank entries only creates real children.

    void blank_suggestion_strings_are_filtered_before_addSubtask()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        // 5 entries; only 2 are non-blank after trim → only 2 child
        // tasks should be created.
        const QStringList suggestions = {
            QStringLiteral("Реальный шаг 1"),
            QStringLiteral(""),
            QStringLiteral("   "),
            QStringLiteral("\t\n"),
            QStringLiteral("Реальный шаг 2"),
        };
        emit ai.subtasksGenerated(suggestions, parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 2);

        QStringList contents;
        for (Task *k : kids) contents << k->content();
        QVERIFY(contents.contains(QStringLiteral("Реальный шаг 1")));
        QVERIFY(contents.contains(QStringLiteral("Реальный шаг 2")));
    }

    // ─── Defense in depth: even if a blank slipped past the QML guard,
    //     TaskViewModel::addSubtask itself rejects blank content. Verify
    //     by calling addSubtask directly (no AI wiring) — this pins the
    //     second-line guarantee. ───────────────────────────────────────

    void addSubtask_rejects_blank_content_directly()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent"));
        const QString parentId = parent->id();

        TaskViewModel vm(store);
        const int totalBefore = store->allTasks().size();

        QVERIFY(vm.addSubtask(parentId, QStringLiteral("")).isEmpty());
        QVERIFY(vm.addSubtask(parentId, QStringLiteral("   ")).isEmpty());
        QVERIFY(vm.addSubtask(parentId, QStringLiteral("\t\n")).isEmpty());

        // Total task count unchanged.
        QCOMPARE(store->allTasks().size(), totalBefore);
        QCOMPARE(store->subtasks(parentId).size(), 0);
    }

    // ─── Multiple parents in flight — each emission targets only its own
    //     parent, never the other. ────────────────────────────────────────

    void each_emission_targets_its_own_parent_only()
    {
        DataStore *store = makeStore(this);
        Task *parentA = addTask(store, QStringLiteral("Parent A"));
        Task *parentB = addTask(store, QStringLiteral("Parent B"));
        const QString idA = parentA->id();
        const QString idB = parentB->id();

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        emit ai.subtasksGenerated(
            QStringList{QStringLiteral("A1"), QStringLiteral("A2")}, idA);
        emit ai.subtasksGenerated(
            QStringList{QStringLiteral("B1")}, idB);

        QCOMPARE(store->subtasks(idA).size(), 2);
        QCOMPARE(store->subtasks(idB).size(), 1);

        // Cross-check: A's children carry parent = idA, B's child carries
        // parent = idB.
        for (Task *t : store->subtasks(idA))
            QCOMPARE(t->parentId(), idA);
        QCOMPARE(store->subtasks(idB).first()->parentId(), idB);
        QCOMPARE(store->subtasks(idB).first()->content(), QStringLiteral("B1"));
    }

    // ─── Section inheritance: addSubtask copies parent->sectionId() too.
    //     Pin that contract here — relevant when the user generates
    //     subtasks for a task that lives inside a section, not just the
    //     project root. ──────────────────────────────────────────────────

    void child_tasks_inherit_sectionId_from_parent()
    {
        DataStore *store = makeStore(this);
        Task *parent = addTask(store, QStringLiteral("Parent in section"));
        const QString parentId = parent->id();
        // Set a synthetic sectionId on the parent. The DataStore doesn't
        // care whether the section actually exists for this contract —
        // TaskViewModel::addSubtask copies the literal string.
        parent->setSectionId(QStringLiteral("section-xyz"));

        TaskViewModel vm(store);
        AIAssistant ai(store);
        wireDialogHandler(&ai, &vm);

        emit ai.subtasksGenerated(
            QStringList{QStringLiteral("Шаг X")}, parentId);

        const QList<Task *> kids = store->subtasks(parentId);
        QCOMPARE(kids.size(), 1);
        QCOMPARE(kids.first()->sectionId(), QStringLiteral("section-xyz"));
    }
};

QTEST_GUILESS_MAIN(TestSubtaskGeneratorE2E)
#include "test_subtaskgenerator_e2e.moc"
