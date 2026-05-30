#include <QtTest>
#include <QSignalSpy>
#include <QDate>
#include <QString>
#include <QStringList>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/filter.h"
#include "viewmodels/taskviewmodel.h"

// ============================================================================
// Comprehensive tests for TaskViewModel::filterMode and the per-mode visibility
// rules implemented in TaskViewModel::fetchTasks().
//
// Verified behaviours of the production code (NOT the spec wording):
//   * Inbox  -> tasks belonging to the project flagged isInbox()
//              (NOT "tasks with empty projectId" — `tasksForProject(inbox.id())`).
//   * Today  -> tasksForToday() ∪ overdueTasks() (incomplete only).
//   * Upcoming -> incomplete tasks with dueDate >= today (today included).
//   * Project / Board / Calendar -> tasksForProject(currentProjectId).
//   * Label  -> incomplete tasks whose labels contain m_currentFilter
//              (case-insensitive). The label NAME is matched, not the id.
//   * Filter -> evaluates m_currentFilter as a DSL query string against all
//              tasks. (Saved filters are simply Filter entities whose query()
//              text is fed into setCurrentFilter().)
//   * Trash  -> non-task mode; m_tasks is intentionally NOT cleared and the
//              view consumes deletedTasks() / trashCount() instead.
//
// Other invariants checked:
//   * Subtasks (parentId != "") are stripped from every list except Filter.
//   * Selection is NOT auto-cleared on filter mode change (must be cleared
//     explicitly by the caller).
//   * filterModeChanged + countChanged fire on transitions.
// ============================================================================

namespace {

// --------------------------------------------------------------------------
// Fixture: three projects (Inbox, Work, Home), tasks scattered across due
// dates and labels, plus a soft-deleted task and a subtask.
//
//   Inbox project:
//     I1  — no due date, no labels                 (only Inbox-mode visible)
//     I2  — due TODAY                              (Inbox + Today + Upcoming)
//     I3  — due YESTERDAY (overdue)                (Inbox + Today)
//     I4  — due in +3 days, label "urgent"         (Inbox + Upcoming + Label)
//     I5  — completed, due today                   (filtered out by !showCompleted)
//
//   Work project:
//     W1  — no due date                            (Project=work only)
//     W2  — due in +5 days, label "urgent"         (Upcoming + Project=work + Label)
//     W2sub — subtask of W2 (must NEVER appear in non-Filter modes)
//
//   Home project:
//     H1  — no due date                            (Project=home only)
//     H2  — soft-deleted                           (Trash only)
// --------------------------------------------------------------------------
struct Fixture {
    DataStore *store = nullptr;

    QString inboxId;
    QString workId;
    QString homeId;

    QString i1Id, i2Id, i3Id, i4Id, i5Id;
    QString w1Id, w2Id, w2subId;
    QString h1Id, h2Id;

    QString todayStr;
    QString yesterdayStr;
    QString in3Str;
    QString in5Str;

    ~Fixture() { delete store; }
};

static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    Project *work = f->store->addProject(QStringLiteral("Work"));
    f->workId = work->id();

    Project *home = f->store->addProject(QStringLiteral("Home"));
    f->homeId = home->id();

    const QDate today     = QDate::currentDate();
    const QDate yesterday = today.addDays(-1);
    const QDate plus3     = today.addDays(3);
    const QDate plus5     = today.addDays(5);

    const QString fmt = QStringLiteral("yyyy-MM-dd");
    f->todayStr     = today.toString(fmt);
    f->yesterdayStr = yesterday.toString(fmt);
    f->in3Str       = plus3.toString(fmt);
    f->in5Str       = plus5.toString(fmt);

    // --- Inbox tasks ---
    Task *i1 = f->store->addTask(QStringLiteral("I1 plain"), f->inboxId);
    f->i1Id = i1->id();

    Task *i2 = f->store->addTask(QStringLiteral("I2 today"), f->inboxId);
    i2->setDueDate(f->todayStr);
    f->i2Id = i2->id();

    Task *i3 = f->store->addTask(QStringLiteral("I3 overdue"), f->inboxId);
    i3->setDueDate(f->yesterdayStr);
    f->i3Id = i3->id();

    Task *i4 = f->store->addTask(QStringLiteral("I4 +3"), f->inboxId);
    i4->setDueDate(f->in3Str);
    i4->setLabels({QStringLiteral("urgent")});
    f->i4Id = i4->id();

    Task *i5 = f->store->addTask(QStringLiteral("I5 done"), f->inboxId);
    i5->setDueDate(f->todayStr);
    i5->setIsCompleted(true);
    f->i5Id = i5->id();

    // --- Work tasks ---
    Task *w1 = f->store->addTask(QStringLiteral("W1 plain"), f->workId);
    f->w1Id = w1->id();

    Task *w2 = f->store->addTask(QStringLiteral("W2 +5"), f->workId);
    w2->setDueDate(f->in5Str);
    w2->setLabels({QStringLiteral("Urgent"), QStringLiteral("review")});
    f->w2Id = w2->id();

    Task *w2sub = f->store->addTask(QStringLiteral("W2 sub"), f->workId);
    w2sub->setParentId(f->w2Id);
    f->w2subId = w2sub->id();

    // --- Home tasks ---
    Task *h1 = f->store->addTask(QStringLiteral("H1 plain"), f->homeId);
    f->h1Id = h1->id();

    Task *h2 = f->store->addTask(QStringLiteral("H2 trash"), f->homeId);
    f->h2Id = h2->id();
    f->store->removeTask(f->h2Id); // soft-delete

    return f;
}

// Collect the IdRole value of every visible row. Stable, predictable, and
// easier to assert against than role-by-role data() reads.
static QStringList visibleIds(TaskViewModel &vm)
{
    QStringList out;
    out.reserve(vm.rowCount());
    for (int i = 0; i < vm.rowCount(); ++i)
        out << vm.data(vm.index(i, 0), TaskViewModel::IdRole).toString();
    return out;
}

} // namespace

class TestTaskViewModelFilter : public QObject
{
    Q_OBJECT

private slots:

    // =====================================================================
    // Per-mode visibility
    // =====================================================================

    void inbox_mode_shows_only_inbox_project_tasks()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        const QStringList ids = visibleIds(vm);

        // I1..I4 belong to Inbox project AND are not completed/deleted/subtasks.
        // I5 is completed -> stripped by the !showCompleted filter.
        QCOMPARE(ids.size(), 4);
        QVERIFY(ids.contains(f->i1Id));
        QVERIFY(ids.contains(f->i2Id));
        QVERIFY(ids.contains(f->i3Id));
        QVERIFY(ids.contains(f->i4Id));

        // Tasks from other projects must NOT appear here.
        QVERIFY(!ids.contains(f->w1Id));
        QVERIFY(!ids.contains(f->w2Id));
        QVERIFY(!ids.contains(f->h1Id));

        // Soft-deleted and subtasks always invisible in Inbox.
        QVERIFY(!ids.contains(f->h2Id));
        QVERIFY(!ids.contains(f->w2subId));
        QVERIFY(!ids.contains(f->i5Id));

        delete f;
    }

    void today_mode_shows_due_today_and_overdue_incomplete()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Today);

        const QStringList ids = visibleIds(vm);

        // I2 (today), I3 (overdue) — both incomplete. I5 is completed today
        // and must be filtered out.
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains(f->i2Id));
        QVERIFY(ids.contains(f->i3Id));
        QVERIFY(!ids.contains(f->i5Id));
        QVERIFY(!ids.contains(f->i1Id));
        QVERIFY(!ids.contains(f->i4Id));
        QVERIFY(!ids.contains(f->w2Id));

        delete f;
    }

    void upcoming_mode_includes_today_and_future_incomplete()
    {
        // The production rule is `dueDate >= today` (today is included),
        // not "next 7 days excluding today" — assert the actual behaviour.
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);

        const QStringList ids = visibleIds(vm);

        // I2 (today), I4 (+3), W2 (+5). I3 is in the past => excluded.
        // I5 is today but completed => excluded.
        QCOMPARE(ids.size(), 3);
        QVERIFY(ids.contains(f->i2Id));
        QVERIFY(ids.contains(f->i4Id));
        QVERIFY(ids.contains(f->w2Id));
        QVERIFY(!ids.contains(f->i3Id));
        QVERIFY(!ids.contains(f->i5Id));

        delete f;
    }

    // night iter-114 (B-1.30): a datetime-only task — empty dueDate but a set
    // dueDatetime — must still appear in Upcoming AND the global Calendar.
    // fetchTasks() previously keyed Upcoming/Calendar off dueDate alone, so a
    // task scheduled e.g. "tomorrow 15:00" was silently dropped from both views.
    void upcoming_and_calendar_include_datetime_only_tasks()
    {
        DataStore store;
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        const QString tomorrow =
            QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd"));

        // Plain dueDate control (tomorrow) — confirms Upcoming works at all.
        Task *plain = store.addTask(QStringLiteral("plain +1"), inbox->id());
        plain->setDueDate(tomorrow);

        // Datetime-only future task: dueDatetime set, dueDate left EMPTY.
        Task *dt = store.addTask(QStringLiteral("datetime +1 15:00"), inbox->id());
        dt->setDueDatetime(tomorrow + QStringLiteral("T15:00:00"));
        QVERIFY2(dt->dueDate().isEmpty(),
                 "precondition: datetime-only task must have empty dueDate");

        TaskViewModel vm(&store);

        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);
        const QStringList up = visibleIds(vm);
        QVERIFY2(up.contains(dt->id()),
                 "datetime-only task must appear in Upcoming (keyed off dueDatetime)");
        QVERIFY(up.contains(plain->id()));

        vm.setFilterMode(TaskViewModel::FilterMode::Calendar);
        const QStringList cal = visibleIds(vm);
        QVERIFY2(cal.contains(dt->id()),
                 "datetime-only task must appear in the global Calendar");
        QVERIFY(cal.contains(plain->id()));
    }

    // night iter-115: when sorting by date, a datetime-only task (empty dueDate,
    // set dueDatetime) must interleave chronologically by its datetime's date —
    // not get dumped at the end with truly-undated tasks. Pre-fix the comparator
    // keyed off dueDate alone, so "in 2 days 15:00" sorted AFTER "in 10 days".
    void date_sort_orders_datetime_only_by_its_date()
    {
        DataStore store;
        Project *proj = store.addProject(QStringLiteral("P"));

        const QDate today = QDate::currentDate();
        const QString fmt = QStringLiteral("yyyy-MM-dd");

        // "later" — plain dueDate, +10 days.
        Task *later = store.addTask(QStringLiteral("later +10"), proj->id());
        later->setDueDate(today.addDays(10).toString(fmt));

        // "earlier" — datetime-only, +2 days 15:00 (no plain dueDate).
        Task *earlier = store.addTask(QStringLiteral("earlier +2 dt"), proj->id());
        earlier->setDueDatetime(today.addDays(2).toString(fmt) + QStringLiteral("T15:00:00"));
        QVERIFY(earlier->dueDate().isEmpty());

        TaskViewModel vm(&store);
        vm.setSortBy(QStringLiteral("date"));
        vm.setFilterMode(TaskViewModel::FilterMode::Project);
        vm.setCurrentProjectId(proj->id());

        const QStringList ids = visibleIds(vm);
        const int idxEarlier = ids.indexOf(earlier->id());
        const int idxLater   = ids.indexOf(later->id());
        QVERIFY2(idxEarlier >= 0 && idxLater >= 0,
                 "both tasks must be visible in the project");
        QVERIFY2(idxEarlier < idxLater,
                 "datetime-only task (earlier date) must sort before the later "
                 "plain-dueDate task when sorting by date");
    }

    void project_mode_with_currentProjectId_shows_only_that_project()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Project);
        vm.setCurrentProjectId(f->workId);

        const QStringList ids = visibleIds(vm);

        // Work has W1 + W2 visible (W2sub is a subtask -> stripped).
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains(f->w1Id));
        QVERIFY(ids.contains(f->w2Id));
        QVERIFY(!ids.contains(f->w2subId));
        QVERIFY(!ids.contains(f->i1Id));
        QVERIFY(!ids.contains(f->h1Id));

        delete f;
    }

    void label_mode_shows_only_tasks_carrying_that_label_case_insensitive()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Label);
        // I4 has "urgent" lowercase, W2 has "Urgent" capitalised — both should
        // match thanks to the case-insensitive comparison in fetchTasks().
        vm.setCurrentFilter(QStringLiteral("urgent"));

        const QStringList ids = visibleIds(vm);

        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains(f->i4Id));
        QVERIFY(ids.contains(f->w2Id));
        QVERIFY(!ids.contains(f->i1Id));
        QVERIFY(!ids.contains(f->w1Id));

        delete f;
    }

    void filter_mode_evaluates_dsl_query_from_currentFilter()
    {
        Fixture *f = makeFixture();

        // Persist a saved Filter entity to mirror how the UI wires this:
        // the saved filter's query() string is what gets fed into
        // setCurrentFilter() — the Filter entity's id is metadata only.
        Filter *saved = f->store->addFilter(QStringLiteral("Important"),
                                            QStringLiteral("@urgent"));
        QVERIFY(saved != nullptr);

        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Filter);
        vm.setCurrentFilter(saved->query());

        const QStringList ids = visibleIds(vm);

        // The DSL "@urgent" matches I4 and W2 (both carry the label).
        QVERIFY(ids.contains(f->i4Id));
        QVERIFY(ids.contains(f->w2Id));
        QVERIFY(!ids.contains(f->i1Id));
        QVERIFY(!ids.contains(f->w1Id));
        QVERIFY(!ids.contains(f->h2Id)); // soft-deleted -> excluded everywhere
        QCOMPARE(ids.size(), 2);

        delete f;
    }

    void trash_mode_does_not_clear_task_list_and_uses_deletedTasks()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Populate m_tasks via Inbox first so we can verify Trash leaves it
        // alone.  This is the documented "do NOT clear m_tasks" path in
        // TaskViewModel::refresh() so navigating back is instant.
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const int beforeRows = vm.rowCount();
        QVERIFY(beforeRows > 0);

        vm.setFilterMode(TaskViewModel::FilterMode::Trash);

        // rowCount() is the QAbstractListModel view onto m_tasks — Trash mode
        // intentionally leaves it untouched.
        QCOMPARE(vm.rowCount(), beforeRows);

        // The actual Trash data flows through deletedTasks() / trashCount().
        QCOMPARE(vm.trashCount(), 1);
        const QVariantList del = vm.deletedTasks();
        QCOMPARE(del.size(), 1);
        QCOMPARE(del.first().toMap().value(QStringLiteral("id")).toString(), f->h2Id);

        delete f;
    }

    // =====================================================================
    // Mode transitions
    // =====================================================================

    void switching_mode_changes_rowCount()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const int inboxRows = vm.rowCount();
        QCOMPARE(inboxRows, 4);

        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QCOMPARE(vm.rowCount(), 2);

        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);
        QCOMPARE(vm.rowCount(), 3);

        delete f;
    }

    void switching_mode_emits_filterModeChanged_and_countChanged()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        // Drain initial signals (constructor + early refresh()).
        QSignalSpy modeSpy(&vm,  &TaskViewModel::filterModeChanged);
        QSignalSpy countSpy(&vm, &TaskViewModel::countChanged);

        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        // Inbox -> Inbox is a no-op (no signal expected).
        QCOMPARE(modeSpy.count(), 0);

        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QCOMPARE(modeSpy.count(), 1);
        QVERIFY(countSpy.count() >= 1); // at least one count update on real transitions

        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);
        QCOMPARE(modeSpy.count(), 2);

        delete f;
    }

    void switching_mode_does_not_auto_clear_selection()
    {
        // Documented behaviour: setFilterMode does NOT call clearSelection().
        // Callers (sidebar nav handler in main.cpp) must clear selection
        // explicitly when they want it reset. This test pins that contract
        // so future refactors can't silently change it.
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);

        vm.selectTask(f->i1Id);
        vm.selectTask(f->i2Id);
        QCOMPARE(vm.selectionCount(), 2);

        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QCOMPARE(vm.selectionCount(), 2);
        QVERIFY(vm.isSelected(f->i1Id));
        QVERIFY(vm.isSelected(f->i2Id));

        // Explicit caller-side reset still works after the transition.
        vm.clearSelection();
        QCOMPARE(vm.selectionCount(), 0);

        delete f;
    }

    void switching_back_to_previous_mode_restores_the_list()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);

        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        const QStringList beforeIds = visibleIds(vm);
        QVERIFY(!beforeIds.isEmpty());

        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QVERIFY(visibleIds(vm) != beforeIds);

        // Round-trip back. fetchTasks() is deterministic for an unchanged
        // store, so the same id-set must come back. (Order may differ if
        // sort criteria change — we don't here — so compare as a sorted set.)
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        QStringList afterIds = visibleIds(vm);

        QStringList a = beforeIds; a.sort();
        QStringList b = afterIds;  b.sort();
        QCOMPARE(b, a);

        delete f;
    }

    // =====================================================================
    // Edge cases
    // =====================================================================

    void project_mode_with_empty_currentProjectId_yields_empty_list()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Project);
        vm.setCurrentProjectId(QString());

        QCOMPARE(vm.rowCount(), 0);

        delete f;
    }

    void project_mode_with_unknown_projectId_yields_empty_list()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Project);
        vm.setCurrentProjectId(QStringLiteral("non-existent-project-id"));

        QCOMPARE(vm.rowCount(), 0);

        delete f;
    }

    void label_mode_with_unknown_label_yields_empty_list()
    {
        Fixture *f = makeFixture();
        TaskViewModel vm(f->store);
        vm.setFilterMode(TaskViewModel::FilterMode::Label);
        vm.setCurrentFilter(QStringLiteral("nonexistent-label"));

        QCOMPARE(vm.rowCount(), 0);

        delete f;
    }

    void board_and_calendar_share_project_mode_visibility()
    {
        // CLAUDE.md: "Board and Calendar show the same tasks as Project mode;
        // rendering differences are handled by the QML views."  Pin that
        // contract — if it ever changes, the regression should surface here.
        Fixture *f = makeFixture();

        TaskViewModel vm(f->store);
        vm.setCurrentProjectId(f->workId);

        vm.setFilterMode(TaskViewModel::FilterMode::Project);
        QStringList projIds = visibleIds(vm);
        projIds.sort();

        vm.setFilterMode(TaskViewModel::FilterMode::Board);
        QStringList boardIds = visibleIds(vm);
        boardIds.sort();

        vm.setFilterMode(TaskViewModel::FilterMode::Calendar);
        QStringList calIds = visibleIds(vm);
        calIds.sort();

        QCOMPARE(boardIds, projIds);
        QCOMPARE(calIds,   projIds);

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestTaskViewModelFilter)
#include "test_taskviewmodel_filter.moc"
