// ---------------------------------------------------------------------------
// test_filterparser_extensions.cpp
//
// Coverage for the FilterParser/FilterEval predicate extensions added in the
// May 2026 quality round (Todoist-parity push):
//
//   1. due / due:N            (HasDue token; N inclusive of today..today+N)
//   2. subtask / !subtask     (Subtask predicate based on parentId)
//   3. view all / all         (ViewAll token; matches every task)
//   4. priority N / N..M      (Priority single + new PriorityRange token)
//   5. label none / no labels (NoLabels predicate; empty labels array)
//   6. created today / week   (CreatedToday + CreatedThisWeek tokens)
//
// Plus:
//   7. combination_with_existing_operators
//   8. existing_filters_unchanged    (smoke; pins old predicate parses)
//
// Constraints (per orchestrator):
//   * QTEST_GUILESS_MAIN.
//   * Unique QSettings org/app namespace.
//   * No raw-string literals R"(...)" anywhere (moc-hostile).
//   * Russian UI strings only — none in this Domain test.
//   * Layer: Domain (registered in tests/unit/CMakeLists.txt).
//
// The test fixture builds a small DataStore + FilterEvaluator so we can
// verify both PARSING (the AST shape / token type) and EVALUATION (filtering
// real Task objects) for every new predicate.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "filter/filterparser.h"
#include "filter/filtereval.h"
#include "filter/tokens.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "utils/datetime.h"

// ---------------------------------------------------------------------------
// Thin helpers
// ---------------------------------------------------------------------------
static AstNode *mustParse(FilterParser &p, const QString &query)
{
    AstNode *ast = p.parse(query);
    if (!ast)
        qWarning() << "parse failed for:" << query << "error:" << p.lastError();
    return ast;
}

static bool matches(FilterEvaluator &eval, const QString &query, Task *task)
{
    FilterParser parser;
    AstNode *ast = parser.parse(query);
    if (!ast) return false;
    const QList<Task *> tasks{task};
    QList<Task *> result = eval.evaluate(ast, tasks);
    delete ast;
    return !result.isEmpty();
}

// ---------------------------------------------------------------------------
class TestFilterParserExtensions : public QObject
{
    Q_OBJECT

private:
    DataStore       *m_store = nullptr;
    FilterEvaluator *m_eval  = nullptr;

    // Tasks designed to exercise the new predicates.
    Task *m_taskInbox        = nullptr; // no due, no labels, p1, no parent
    Task *m_taskDueToday     = nullptr; // due today, p2, label "work"
    Task *m_taskDueIn3Days   = nullptr; // due today+3, p3
    Task *m_taskDueIn10Days  = nullptr; // due today+10, p4
    Task *m_taskNoDate       = nullptr; // no due, no labels, p2
    Task *m_taskOldCreated   = nullptr; // createdAt = 30 days ago, due today
    Task *m_taskSubtask      = nullptr; // parentId set, due today, p1
    Task *m_taskRecurring    = nullptr; // recurring, due today, p2

    QString m_workProjectId;
    QString m_parentTaskId;

private slots:

    void initTestCase()
    {
        // Per-process unique QSettings namespace — keeps this test isolated
        // from the user's real OpenTodo install AND from other parallel tests.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_FilterExt"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_filterparser_extensions"));
    }

    // -----------------------------------------------------------------------
    // Build a fresh DataStore + FilterEvaluator before each slot.
    // -----------------------------------------------------------------------
    void init()
    {
        m_store = new DataStore(this);

        Project *inbox = m_store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        Project *work = m_store->addProject(QStringLiteral("Work"));
        m_workProjectId = work->id();

        const QDate today      = QDate::currentDate();
        const QString todayStr = DateTimeHelper::toDateString(today);
        const QString in3Str   = DateTimeHelper::toDateString(today.addDays(3));
        const QString in10Str  = DateTimeHelper::toDateString(today.addDays(10));

        // 1. Inbox task: no due, no labels, p1.
        m_taskInbox = m_store->addTask(QStringLiteral("Inbox task"), inbox->id());
        m_taskInbox->setPriority(1);

        // 2. Due today, p2, with one label.
        m_taskDueToday = m_store->addTask(QStringLiteral("Due today"), m_workProjectId);
        m_taskDueToday->setDueDate(todayStr);
        m_taskDueToday->setPriority(2);
        m_taskDueToday->setLabels(QStringList{QStringLiteral("work")});

        // 3. Due today+3, p3.
        m_taskDueIn3Days = m_store->addTask(QStringLiteral("Due in 3 days"), m_workProjectId);
        m_taskDueIn3Days->setDueDate(in3Str);
        m_taskDueIn3Days->setPriority(3);

        // 4. Due today+10, p4.
        m_taskDueIn10Days = m_store->addTask(QStringLiteral("Due in 10 days"), m_workProjectId);
        m_taskDueIn10Days->setDueDate(in10Str);
        m_taskDueIn10Days->setPriority(4);

        // 5. No date task.
        m_taskNoDate = m_store->addTask(QStringLiteral("No date task"), inbox->id());
        m_taskNoDate->setPriority(2);

        // 6. Old created task — backdate createdAt to 30 days ago.
        m_taskOldCreated = m_store->addTask(QStringLiteral("Old task"), inbox->id());
        m_taskOldCreated->setDueDate(todayStr);
        m_taskOldCreated->setCreatedAt(
            QDateTime::currentDateTime().addDays(-30));

        // 7. Subtask: has parentId.
        Task *parent = m_store->addTask(QStringLiteral("Parent task"), inbox->id());
        m_parentTaskId = parent->id();
        m_taskSubtask = m_store->addTask(QStringLiteral("Subtask"), inbox->id());
        m_taskSubtask->setParentId(m_parentTaskId);
        m_taskSubtask->setDueDate(todayStr);
        m_taskSubtask->setPriority(1);

        // 8. Recurring task — used by the combination test.
        m_taskRecurring = m_store->addTask(QStringLiteral("Recurring"), inbox->id());
        m_taskRecurring->setDueDate(todayStr);
        m_taskRecurring->setDueIsRecurring(true);
        m_taskRecurring->setPriority(2);

        m_eval = new FilterEvaluator(m_store, this);
    }

    void cleanup()
    {
        delete m_eval;
        m_eval = nullptr;
        delete m_store;
        m_store = nullptr;
    }

    // -----------------------------------------------------------------------
    // SLOT 1 — due / due:N
    // -----------------------------------------------------------------------
    void due_predicate_parses_and_evaluates()
    {
        FilterParser p;

        // a) Bare "due" — token type and value.
        std::unique_ptr<AstNode> astBare(mustParse(p, QStringLiteral("due")));
        QVERIFY(astBare != nullptr);
        QCOMPARE((int)astBare->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astBare->token.type, (int)TokenType::HasDue);
        QCOMPARE(astBare->token.value.toInt(), -1);

        // b) "due: 7" with space.
        std::unique_ptr<AstNode> ast7sp(mustParse(p, QStringLiteral("due: 7")));
        QVERIFY(ast7sp != nullptr);
        QCOMPARE((int)ast7sp->token.type, (int)TokenType::HasDue);
        QCOMPARE(ast7sp->token.value.toInt(), 7);

        // c) "due:7" no-space form.
        std::unique_ptr<AstNode> ast7(mustParse(p, QStringLiteral("due:7")));
        QVERIFY(ast7 != nullptr);
        QCOMPARE((int)ast7->token.type, (int)TokenType::HasDue);
        QCOMPARE(ast7->token.value.toInt(), 7);

        // Evaluation:
        // "due" → matches every task that has any due date.
        QVERIFY(matches(*m_eval, QStringLiteral("due"), m_taskDueToday));
        QVERIFY(matches(*m_eval, QStringLiteral("due"), m_taskDueIn10Days));
        QVERIFY(!matches(*m_eval, QStringLiteral("due"), m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("due"), m_taskInbox));

        // "due:7" → today..today+7 inclusive.
        QVERIFY(matches(*m_eval, QStringLiteral("due:7"), m_taskDueToday));
        QVERIFY(matches(*m_eval, QStringLiteral("due:7"), m_taskDueIn3Days));
        QVERIFY(!matches(*m_eval, QStringLiteral("due:7"), m_taskDueIn10Days));
        QVERIFY(!matches(*m_eval, QStringLiteral("due:7"), m_taskNoDate));

        // "due:0" → only today.
        QVERIFY(matches(*m_eval, QStringLiteral("due:0"), m_taskDueToday));
        QVERIFY(!matches(*m_eval, QStringLiteral("due:0"), m_taskDueIn3Days));
    }

    // -----------------------------------------------------------------------
    // SLOT 2 — subtask / !subtask
    // -----------------------------------------------------------------------
    void subtask_predicate_parses_and_evaluates()
    {
        FilterParser p;

        std::unique_ptr<AstNode> ast(mustParse(p, QStringLiteral("subtask")));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->token.type, (int)TokenType::Subtask);

        // !subtask should parse as Not(Subtask).
        std::unique_ptr<AstNode> astNot(mustParse(p, QStringLiteral("!subtask")));
        QVERIFY(astNot != nullptr);
        QCOMPARE((int)astNot->type, (int)AstNodeType::Not);
        QCOMPARE(astNot->children.size(), 1);
        QCOMPARE((int)astNot->children[0]->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astNot->children[0]->token.type, (int)TokenType::Subtask);

        // Evaluation: only the task we explicitly marked as a subtask matches.
        QVERIFY(matches(*m_eval, QStringLiteral("subtask"), m_taskSubtask));
        QVERIFY(!matches(*m_eval, QStringLiteral("subtask"), m_taskInbox));
        QVERIFY(!matches(*m_eval, QStringLiteral("subtask"), m_taskDueToday));

        // !subtask: matches non-subtasks.
        QVERIFY(matches(*m_eval, QStringLiteral("!subtask"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("!subtask"), m_taskDueToday));
        QVERIFY(!matches(*m_eval, QStringLiteral("!subtask"), m_taskSubtask));
    }

    // -----------------------------------------------------------------------
    // SLOT 3 — view all / all
    // -----------------------------------------------------------------------
    void view_all_predicate_parses_and_evaluates()
    {
        FilterParser p;

        // "view all" — multi-word.
        std::unique_ptr<AstNode> astVA(mustParse(p, QStringLiteral("view all")));
        QVERIFY(astVA != nullptr);
        QCOMPARE((int)astVA->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astVA->token.type, (int)TokenType::ViewAll);

        // Bare "all" — single-word synonym.
        std::unique_ptr<AstNode> astAll(mustParse(p, QStringLiteral("all")));
        QVERIFY(astAll != nullptr);
        QCOMPARE((int)astAll->token.type, (int)TokenType::ViewAll);

        // Evaluation: ViewAll matches every task.
        QVERIFY(matches(*m_eval, QStringLiteral("view all"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("view all"), m_taskDueToday));
        QVERIFY(matches(*m_eval, QStringLiteral("view all"), m_taskNoDate));
        QVERIFY(matches(*m_eval, QStringLiteral("view all"), m_taskSubtask));

        // String overload: returns every non-deleted task in the store.
        QList<Task *> all = m_eval->evaluate(QStringLiteral("view all"));
        QCOMPARE(all.size(), m_store->allTasks().size());
    }

    // -----------------------------------------------------------------------
    // SLOT 4 — priority N / priority N..M
    // -----------------------------------------------------------------------
    void priority_keyword_parses_and_evaluates()
    {
        FilterParser p;

        // a) "priority 1" — single value, equivalent to "p1".
        std::unique_ptr<AstNode> astP1(mustParse(p, QStringLiteral("priority 1")));
        QVERIFY(astP1 != nullptr);
        QCOMPARE((int)astP1->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astP1->token.type, (int)TokenType::Priority);
        QCOMPARE(astP1->token.value.toInt(), 1);

        // b) "priority 2..4" — range.
        std::unique_ptr<AstNode> astRng(mustParse(p, QStringLiteral("priority 2..4")));
        QVERIFY(astRng != nullptr);
        QCOMPARE((int)astRng->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astRng->token.type, (int)TokenType::PriorityRange);
        const QVariantList rng = astRng->token.value.toList();
        QCOMPARE(rng.size(), 2);
        QCOMPARE(rng[0].toInt(), 2);
        QCOMPARE(rng[1].toInt(), 4);

        // Evaluation: "priority 1" matches p1 only.
        QVERIFY(matches(*m_eval, QStringLiteral("priority 1"), m_taskInbox));        // p1
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 1"), m_taskDueToday));    // p2
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 1"), m_taskDueIn3Days));  // p3

        // "priority 2..4" matches p2..p4.
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 2..4"), m_taskInbox));        // p1
        QVERIFY(matches(*m_eval, QStringLiteral("priority 2..4"), m_taskDueToday));      // p2
        QVERIFY(matches(*m_eval, QStringLiteral("priority 2..4"), m_taskDueIn3Days));    // p3
        QVERIFY(matches(*m_eval, QStringLiteral("priority 2..4"), m_taskDueIn10Days));   // p4

        // "priority 3..3" — degenerate single-element range, exactly p3.
        QVERIFY(matches(*m_eval, QStringLiteral("priority 3..3"), m_taskDueIn3Days));
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 3..3"), m_taskDueToday));
    }

    // -----------------------------------------------------------------------
    // SLOT 5 — label none / no labels
    // -----------------------------------------------------------------------
    void no_labels_predicate_parses_and_evaluates()
    {
        FilterParser p;

        std::unique_ptr<AstNode> astA(mustParse(p, QStringLiteral("no labels")));
        QVERIFY(astA != nullptr);
        QCOMPARE((int)astA->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astA->token.type, (int)TokenType::NoLabels);

        // "no label" (singular) should also parse to the same token.
        std::unique_ptr<AstNode> astB(mustParse(p, QStringLiteral("no label")));
        QVERIFY(astB != nullptr);
        QCOMPARE((int)astB->token.type, (int)TokenType::NoLabels);

        // Evaluation: matches tasks with empty labels list.
        QVERIFY(matches(*m_eval, QStringLiteral("no labels"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("no labels"), m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("no labels"), m_taskDueToday));
    }

    // -----------------------------------------------------------------------
    // SLOT 6 — created today / created this week
    // -----------------------------------------------------------------------
    void created_today_and_this_week_parse_and_evaluate()
    {
        FilterParser p;

        std::unique_ptr<AstNode> astT(mustParse(p, QStringLiteral("created today")));
        QVERIFY(astT != nullptr);
        QCOMPARE((int)astT->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astT->token.type, (int)TokenType::CreatedToday);

        std::unique_ptr<AstNode> astW(mustParse(p, QStringLiteral("created this week")));
        QVERIFY(astW != nullptr);
        QCOMPARE((int)astW->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)astW->token.type, (int)TokenType::CreatedThisWeek);

        // Evaluation: tasks created in init() were just created => today.
        QVERIFY(matches(*m_eval, QStringLiteral("created today"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("created today"), m_taskDueToday));

        // m_taskOldCreated was backdated 30 days ago => not today, not this week.
        QVERIFY(!matches(*m_eval, QStringLiteral("created today"), m_taskOldCreated));
        QVERIFY(!matches(*m_eval, QStringLiteral("created this week"), m_taskOldCreated));

        // The fresh tasks fall within "this week" (current ISO week).
        QVERIFY(matches(*m_eval, QStringLiteral("created this week"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("created this week"), m_taskDueToday));
    }

    // -----------------------------------------------------------------------
    // SLOT 7 — combination with existing operators
    // -----------------------------------------------------------------------
    void combination_with_existing_operators()
    {
        FilterParser p;

        // a) "due:7 & p1" — intersection of "within 7 days" and p1.
        std::unique_ptr<AstNode> astA(mustParse(p, QStringLiteral("due:7 & p1")));
        QVERIFY(astA != nullptr);
        QCOMPARE((int)astA->type, (int)AstNodeType::And);
        // m_taskSubtask: due today, p1 -> matches.
        QVERIFY(matches(*m_eval, QStringLiteral("due:7 & p1"), m_taskSubtask));
        // m_taskDueToday: due today, p2 -> doesn't match (wrong priority).
        QVERIFY(!matches(*m_eval, QStringLiteral("due:7 & p1"), m_taskDueToday));
        // m_taskInbox: p1 but no due -> doesn't match.
        QVERIFY(!matches(*m_eval, QStringLiteral("due:7 & p1"), m_taskInbox));

        // b) "subtask & today" — combine new + existing predicates.
        std::unique_ptr<AstNode> astB(mustParse(p, QStringLiteral("subtask & today")));
        QVERIFY(astB != nullptr);
        QCOMPARE((int)astB->type, (int)AstNodeType::And);
        // m_taskSubtask: parentId set, due today -> matches.
        QVERIFY(matches(*m_eval, QStringLiteral("subtask & today"), m_taskSubtask));
        // m_taskDueToday: due today but not a subtask -> doesn't match.
        QVERIFY(!matches(*m_eval, QStringLiteral("subtask & today"), m_taskDueToday));

        // c) "priority 1..2 & !recurring" — range + negation of existing.
        std::unique_ptr<AstNode> astC(mustParse(p, QStringLiteral("priority 1..2 & !recurring")));
        QVERIFY(astC != nullptr);
        QCOMPARE((int)astC->type, (int)AstNodeType::And);
        // m_taskInbox: p1, not recurring -> matches.
        QVERIFY(matches(*m_eval, QStringLiteral("priority 1..2 & !recurring"), m_taskInbox));
        // m_taskDueToday: p2, not recurring -> matches.
        QVERIFY(matches(*m_eval, QStringLiteral("priority 1..2 & !recurring"), m_taskDueToday));
        // m_taskRecurring: p2, recurring -> excluded by !recurring.
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 1..2 & !recurring"), m_taskRecurring));
        // m_taskDueIn3Days: p3, not recurring -> excluded by range.
        QVERIFY(!matches(*m_eval, QStringLiteral("priority 1..2 & !recurring"), m_taskDueIn3Days));

        // d) "view all & p1" — ViewAll narrowed by another predicate.
        QVERIFY(matches(*m_eval, QStringLiteral("view all & p1"), m_taskInbox));
        QVERIFY(matches(*m_eval, QStringLiteral("view all & p1"), m_taskSubtask));
        QVERIFY(!matches(*m_eval, QStringLiteral("view all & p1"), m_taskDueToday));
    }

    // -----------------------------------------------------------------------
    // SLOT 8 — existing filters unchanged: every previously-supported
    // single-predicate parses to the SAME AST shape and token type as before.
    // -----------------------------------------------------------------------
    void existing_filters_unchanged_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("expectedTokenType");

        QTest::newRow("today")            << QStringLiteral("today")
                                          << (int)TokenType::Today;
        QTest::newRow("tomorrow")         << QStringLiteral("tomorrow")
                                          << (int)TokenType::Tomorrow;
        QTest::newRow("yesterday")        << QStringLiteral("yesterday")
                                          << (int)TokenType::Yesterday;
        QTest::newRow("overdue")          << QStringLiteral("overdue")
                                          << (int)TokenType::Overdue;
        QTest::newRow("no date")          << QStringLiteral("no date")
                                          << (int)TokenType::NoDate;
        QTest::newRow("p1")               << QStringLiteral("p1")
                                          << (int)TokenType::Priority;
        QTest::newRow("p4")               << QStringLiteral("p4")
                                          << (int)TokenType::Priority;
        QTest::newRow("project")          << QStringLiteral("#Work")
                                          << (int)TokenType::Project;
        QTest::newRow("label")            << QStringLiteral("@urgent")
                                          << (int)TokenType::Label;
        QTest::newRow("section")          << QStringLiteral("/backlog")
                                          << (int)TokenType::Section;
        QTest::newRow("search")           << QStringLiteral("search: foo")
                                          << (int)TokenType::Search;
        QTest::newRow("days")             << QStringLiteral("5 days")
                                          << (int)TokenType::Days;
        QTest::newRow("before date")      << QStringLiteral("before 2026-12-01")
                                          << (int)TokenType::Before;
        QTest::newRow("after date")       << QStringLiteral("after 2025-01-01")
                                          << (int)TokenType::After;
        QTest::newRow("recurring")        << QStringLiteral("recurring")
                                          << (int)TokenType::Recurring;
        QTest::newRow("deadline")         << QStringLiteral("deadline")
                                          << (int)TokenType::HasDeadline;
        QTest::newRow("completed")        << QStringLiteral("completed")
                                          << (int)TokenType::Completed;
        QTest::newRow("created before")   << QStringLiteral("created before 2025-01-01")
                                          << (int)TokenType::CreatedBefore;
        QTest::newRow("created after")    << QStringLiteral("created after 2025-01-01")
                                          << (int)TokenType::CreatedAfter;
        QTest::newRow("assigned")         << QStringLiteral("assigned")
                                          << (int)TokenType::Assigned;
        QTest::newRow("not recurring")    << QStringLiteral("!recurring")
                                          << (int)TokenType::NotRecurring;
    }

    void existing_filters_unchanged()
    {
        QFETCH(QString, query);
        QFETCH(int, expectedTokenType);

        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, query));
        QVERIFY2(ast != nullptr,
            qPrintable(QStringLiteral("parse failed for '%1': %2")
                .arg(query, p.lastError())));

        // The "!recurring" case is a Predicate (NotRecurring is a leaf token,
        // not a Not(...) wrapper) — that contract must NOT change.
        QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->token.type, expectedTokenType);

        // Operator-precedence sanity on a compound that pre-dated this change:
        // "today | p1 & overdue" must still parse as Or(today, And(p1, overdue)).
        FilterParser p2;
        std::unique_ptr<AstNode> compound(mustParse(p2, QStringLiteral("today | p1 & overdue")));
        QVERIFY(compound != nullptr);
        QCOMPARE((int)compound->type, (int)AstNodeType::Or);
        QCOMPARE((int)compound->children[0]->token.type, (int)TokenType::Today);
        QCOMPARE((int)compound->children[1]->type, (int)AstNodeType::And);
    }
};

QTEST_GUILESS_MAIN(TestFilterParserExtensions)
#include "test_filterparser_extensions.moc"
