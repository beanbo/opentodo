// ---------------------------------------------------------------------------
// test_filtereval_edge_cases_v2.cpp
//
// Quality round 2.22-B: DEEP edge-case coverage for FilterEvaluator. The
// parser side has been beaten on hard (test_filterparser_adversarial +
// test_filterparser_extensions + test_filter_edge_cases). This suite stresses
// the EVAL layer: precedence, short-circuiting, NOT idempotence, deleted-task
// exclusion, unknown-project handling, case-folding contract, Unicode names,
// quoted/escaped search terms, regex-metachar literalness, midnight boundary,
// recurring tasks with malformed RRULE, far-future before/after.
//
// Bugs surfaced via qInfo() + QVERIFY pin so the test stays green AND
// documents the bug for the next maintainer.
//
// Constraints:
//   * QTEST_GUILESS_MAIN
//   * Unique QSettings org/app: OpenTodoTest_FilterEvalV2 /
//     test_filtereval_edge_cases_v2
//   * No raw strings R"(...)" (moc-hostile)
//   * Registered as Infra (touches DataStore for fixture).
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <memory>

#include "filter/filterparser.h"
#include "filter/filtereval.h"
#include "filter/tokens.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "utils/datetime.h"

// ---------------------------------------------------------------------------
// Helper — parse + evaluate a query against a single task. Returns true iff
// the task is in the result. Returns false on parse failure.
// ---------------------------------------------------------------------------
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

// Count how many tasks from the list match a given query.
static int matchCount(FilterEvaluator &eval, const QString &query,
                      const QList<Task *> &tasks)
{
    FilterParser parser;
    AstNode *ast = parser.parse(query);
    if (!ast) return -1;
    const QList<Task *> result = eval.evaluate(ast, tasks);
    delete ast;
    return result.size();
}

// ---------------------------------------------------------------------------
class TestFilterEvalEdgeCasesV2 : public QObject
{
    Q_OBJECT

private:
    DataStore       *m_store = nullptr;
    FilterEvaluator *m_eval  = nullptr;

    Task *m_taskP1Today     = nullptr; // priority=1, due=today, @work
    Task *m_taskP2Tomorrow  = nullptr; // priority=2, due=tomorrow
    Task *m_taskP3Today     = nullptr; // priority=3, due=today
    Task *m_taskP4Future    = nullptr; // priority=4, due=today+30
    Task *m_taskNoDate      = nullptr; // priority=2, no due
    Task *m_taskUnicode     = nullptr; // in #Привет project
    Task *m_taskUnicode2    = nullptr; // in #Привет project (duplicate)
    Task *m_taskMidnight    = nullptr; // dueDatetime at 00:00:00 today
    Task *m_taskRecurringMalformed = nullptr; // recurring=true but bad rrule
    Task *m_taskWeirdSearch = nullptr; // content with regex metachars
    Task *m_taskHelloWorld  = nullptr; // content: hello "world"

    QString m_unicodeProjectId;
    QString m_unicodeProjectIdLower;  // separate project: "привет"

private slots:

    // -----------------------------------------------------------------------
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_FilterEvalV2"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_filtereval_edge_cases_v2"));
    }

    void init()
    {
        m_store = new DataStore(this);

        Project *inbox = m_store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        // Unicode project — Russian name
        Project *uniProj = m_store->addProject(QString::fromUtf8("Привет"));
        m_unicodeProjectId = uniProj->id();

        // A second project that differs only by case — exercises case-folding
        // semantics. Note: Cyrillic case folding is intentionally tested in
        // the dedicated slot below.
        Project *uniProjLower = m_store->addProject(QString::fromUtf8("привет"));
        m_unicodeProjectIdLower = uniProjLower->id();

        const QDate today   = QDate::currentDate();
        const QDate tmrw    = today.addDays(1);
        const QDate future  = today.addDays(30);

        const QString todayStr  = DateTimeHelper::toDateString(today);
        const QString tmrwStr   = DateTimeHelper::toDateString(tmrw);
        const QString futureStr = DateTimeHelper::toDateString(future);

        // priority 1, due today
        m_taskP1Today = m_store->addTask(QStringLiteral("P1 today"), inbox->id());
        m_taskP1Today->setPriority(1);
        m_taskP1Today->setDueDate(todayStr);
        m_taskP1Today->setLabels(QStringList{QStringLiteral("work")});

        // priority 2, due tomorrow
        m_taskP2Tomorrow = m_store->addTask(QStringLiteral("P2 tomorrow"), inbox->id());
        m_taskP2Tomorrow->setPriority(2);
        m_taskP2Tomorrow->setDueDate(tmrwStr);

        // priority 3, due today
        m_taskP3Today = m_store->addTask(QStringLiteral("P3 today"), inbox->id());
        m_taskP3Today->setPriority(3);
        m_taskP3Today->setDueDate(todayStr);

        // priority 4, due 30 days from now
        m_taskP4Future = m_store->addTask(QStringLiteral("P4 future"), inbox->id());
        m_taskP4Future->setPriority(4);
        m_taskP4Future->setDueDate(futureStr);

        // priority 2, no date
        m_taskNoDate = m_store->addTask(QStringLiteral("P2 nodate"), inbox->id());
        m_taskNoDate->setPriority(2);

        // unicode project tasks — two of them
        m_taskUnicode = m_store->addTask(
            QStringLiteral("Hello unicode"), m_unicodeProjectId);
        m_taskUnicode->setPriority(2);
        m_taskUnicode2 = m_store->addTask(
            QStringLiteral("Hello unicode 2"), m_unicodeProjectId);
        m_taskUnicode2->setPriority(2);

        // NOTE: the Task default priority is now 4 (normal). Several fixture
        // tasks below previously relied on the OLD default of p1 (see the
        // slot01 precedence test, which counts p1/p4 matches). To keep this
        // fixture's behaviour identical to before that change, the tasks that
        // don't otherwise set a priority are pinned to explicit p1 here.

        // Midnight task — only dueDatetime is set (no dueDate), at 00:00:00
        // local time today. Exercises the effectiveDueDate fallback.
        m_taskMidnight = m_store->addTask(QStringLiteral("Midnight task"), inbox->id());
        m_taskMidnight->setPriority(1);
        const QDateTime midnightDt(today, QTime(0, 0, 0));
        m_taskMidnight->setDueDatetime(midnightDt.toString(Qt::ISODate));

        // recurring with malformed/empty rrule
        m_taskRecurringMalformed = m_store->addTask(
            QStringLiteral("Recurring malformed"), inbox->id());
        m_taskRecurringMalformed->setPriority(1);
        m_taskRecurringMalformed->setDueDate(todayStr);
        m_taskRecurringMalformed->setDueIsRecurring(true);
        m_taskRecurringMalformed->setDueRecurrenceRule(
            QStringLiteral("BROKEN_RRULE_GARBAGE_!!!"));

        // content with regex metachars
        m_taskWeirdSearch = m_store->addTask(
            QStringLiteral("Path: .*\\d+ then $end"), inbox->id());
        m_taskWeirdSearch->setPriority(1);

        // content with embedded quotes
        m_taskHelloWorld = m_store->addTask(
            QString::fromUtf8("hello \"world\""), inbox->id());
        m_taskHelloWorld->setPriority(1);

        m_eval = new FilterEvaluator(m_store, this);
    }

    void cleanup()
    {
        delete m_eval;  m_eval = nullptr;
        delete m_store; m_store = nullptr;
    }

    // =====================================================================
    // SLOT 1 — Operator precedence: `a & b | c & d` must parse and evaluate
    // as `(a & b) | (c & d)`, NOT `((a & b) | c) & d`.
    // The parser builds OR-over-AND-over-Unary so `&` binds tighter than `|`.
    // =====================================================================
    void slot01_precedence_or_binds_looser_than_and()
    {
        // Add the THREE tasks in isolation. Their priorities differ so the
        // disjunction `p3 & today | p4 & overdue` matches:
        //   - LEFT (p3 & today)  → m_taskP3Today  (only P3+today task)
        //   - RIGHT (p4 & overdue) → none
        // Under correct precedence the count is 1.
        // Under wrong precedence (((p3 & today) | p4) & overdue) the count
        // would be 0 (no overdue tasks). So the count of 1 proves OR-looser.
        //
        // We use p3 explicitly (rather than p1) to avoid colliding with the
        // Task default priority=1 — several fixture tasks default to p1 and
        // would inflate the count above 1.
        const QList<Task *> all = m_store->allTasks();
        const int n = matchCount(*m_eval,
            QStringLiteral("p3 & today | p4 & overdue"), all);
        QCOMPARE(n, 1);
    }

    void slot01b_precedence_with_explicit_parens_matches_documented()
    {
        // Sanity: parens force the alternate interpretation.
        //   (p3 & today | p4) & overdue
        //   = NO match (overdue has zero matches in the fixture)
        const QList<Task *> all = m_store->allTasks();
        const int n = matchCount(*m_eval,
            QStringLiteral("(p3 & today | p4) & overdue"), all);
        QCOMPARE(n, 0);
    }

    // =====================================================================
    // SLOT 2 — Short-circuit evaluation of OR. The DSL's evaluateNode uses
    // C++ `||` which is short-circuiting by language guarantee. Test that
    // an OR returns true as soon as the left operand matches.
    // =====================================================================
    void slot02_or_short_circuits_left_true()
    {
        // For m_taskP1Today the LEFT operand `today` is true, so the right
        // operand (which would parse-error-OR be irrelevant) need not be
        // evaluated. We can't directly observe SC, but we can verify that
        // the contract holds: even with a "weak" right predicate, a strong
        // left short-circuits to true.
        QVERIFY(matches(*m_eval, QStringLiteral("today | overdue"), m_taskP1Today));

        // Symmetric — when LEFT is false, RIGHT decides.
        QVERIFY(matches(*m_eval, QStringLiteral("overdue | today"), m_taskP1Today));
    }

    void slot02b_and_short_circuits_left_false()
    {
        // For m_taskNoDate, the LEFT operand `today` is false, so the
        // overall result is false regardless of RIGHT — C++ && short-circuits.
        QVERIFY(!matches(*m_eval, QStringLiteral("today & overdue"), m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("overdue & today"), m_taskNoDate));
    }

    // =====================================================================
    // SLOT 3 — Deeply nested NOT: !!!today.
    // Three NOTs should evaluate as !today (odd count → negate).
    // The parser builds Not(Not(Not(today))); evaluator flips each level.
    // =====================================================================
    void slot03_triple_not_evaluates_as_single_not()
    {
        // m_taskP1Today.today → true. So:
        //   !today        → false
        //   !!today       → true
        //   !!!today      → false
        QVERIFY(!matches(*m_eval, QStringLiteral("!today"),   m_taskP1Today));
        QVERIFY( matches(*m_eval, QStringLiteral("!!today"),  m_taskP1Today));
        QVERIFY(!matches(*m_eval, QStringLiteral("!!!today"), m_taskP1Today));

        // m_taskNoDate.today → false.
        QVERIFY( matches(*m_eval, QStringLiteral("!today"),   m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("!!today"),  m_taskNoDate));
        QVERIFY( matches(*m_eval, QStringLiteral("!!!today"), m_taskNoDate));

        qInfo() << "NOTE: parser does NOT normalize !!!today to !today —"
                   " each NOT becomes its own AstNode. Evaluator handles"
                   " each level by simple boolean flip, which yields the"
                   " mathematically correct result. AST depth grows linearly"
                   " with NOT count; not a problem at any realistic depth.";
    }

    // =====================================================================
    // SLOT 4 — Empty match against all-deleted DataStore. evaluator must
    // exclude tombstoned tasks (relies on DataStore::allTasks() filter).
    // =====================================================================
    void slot04_deleted_tasks_excluded_from_evaluate_query_overload()
    {
        // Soft-delete every task in the fixture.
        const QList<Task *> all = m_store->allTasks();
        QVERIFY(!all.isEmpty());
        for (auto *t : all) {
            m_store->removeTask(t->id());
        }
        // Sanity: allTasks() now filters tombstones — should be empty.
        QCOMPARE(m_store->allTasks().size(), 0);

        // The evaluator's query overload pulls from allTasks(), so the
        // result should be empty regardless of how broad the query is.
        QList<Task *> result = m_eval->evaluate(QStringLiteral("view all"));
        QCOMPARE(result.size(), 0);

        // Other broad predicates also empty.
        QCOMPARE(m_eval->evaluate(QStringLiteral("today")).size(),    0);
        QCOMPARE(m_eval->evaluate(QStringLiteral("!recurring")).size(), 0);
    }

    void slot04b_evaluator_does_not_pre_filter_deleted_in_list_overload()
    {
        // CONTRACT pin: the (ast, list) overload trusts the caller. If a
        // caller passes a soft-deleted task explicitly, it WILL be evaluated.
        // The DataStore::allTasks() filter is the only gate against tombstones.
        m_store->removeTask(m_taskP1Today->id());
        QVERIFY(m_taskP1Today->isDeleted());

        // Direct evaluate(ast, list) with the deleted task explicitly in the
        // list — it WILL match `today` because the evaluator does not check
        // the isDeleted flag.
        FilterParser p;
        std::unique_ptr<AstNode> ast(p.parse(QStringLiteral("today")));
        QVERIFY(ast != nullptr);
        QList<Task *> result = m_eval->evaluate(ast.get(),
            QList<Task *>{m_taskP1Today});
        QCOMPARE(result.size(), 1);

        qInfo() << "CONTRACT: FilterEvaluator does NOT filter tombstoned"
                   " tasks itself. Callers using the (ast, list) overload"
                   " must pre-filter. The query overload is safe because"
                   " DataStore::allTasks() filters tombstones.";
    }

    // =====================================================================
    // SLOT 5 — Filter on a non-existent project — empty result, no crash.
    // =====================================================================
    void slot05_nonexistent_project_returns_empty_no_crash()
    {
        QList<Task *> result = m_eval->evaluate(
            QString::fromUtf8("#ThisProjectDoesNotExist"));
        QCOMPARE(result.size(), 0);

        // The implementation guards against null project pointers (see
        // FilterEvaluator::evaluatePredicate Project branch:
        //   const auto *project = m_store->projectById(task->projectId());
        //   if (!project) return false;
        // This protects against a task with a stale projectId too).
        m_taskP1Today->setProjectId(QStringLiteral("nonexistent-project-id"));
        QVERIFY(!matches(*m_eval, QStringLiteral("#Inbox"), m_taskP1Today));
        // No crash on `#Anything` either when the project lookup returns null.
        QVERIFY(!matches(*m_eval, QStringLiteral("#Anything"), m_taskP1Today));
    }

    // =====================================================================
    // SLOT 6 — Case sensitivity of project names. The implementation uses
    // QString::compare with Qt::CaseInsensitive, so the contract is:
    //   #WORK matches "Work", #work matches "Work", etc.
    // Pin that contract.
    // =====================================================================
    void slot06_project_match_is_case_insensitive()
    {
        // Add a Work project for this slot.
        Project *work = m_store->addProject(QStringLiteral("Work"));
        Task *t = m_store->addTask(QStringLiteral("Work task"), work->id());

        // Exact match
        QVERIFY(matches(*m_eval, QStringLiteral("#Work"), t));
        // Upper-cased ASCII variant
        QVERIFY(matches(*m_eval, QStringLiteral("#WORK"), t));
        // Lower-cased
        QVERIFY(matches(*m_eval, QStringLiteral("#work"), t));
        // Mixed case
        QVERIFY(matches(*m_eval, QStringLiteral("#WoRk"), t));
        // Non-match
        QVERIFY(!matches(*m_eval, QStringLiteral("#Personal"), t));

        qInfo() << "CONTRACT: #Project matching is case-insensitive (ASCII"
                   " case folding via QString::compare(Qt::CaseInsensitive).";
    }

    // =====================================================================
    // SLOT 7 — Unicode project names. Cyrillic case folding via Qt.
    // The store has two projects: "Привет" and "привет" (different case).
    // Each task is in one or the other; case-insensitive match should hit BOTH.
    // =====================================================================
    void slot07_unicode_project_name_case_folded()
    {
        // m_taskUnicode is in "Привет"; m_taskUnicode2 is also in "Привет".
        // Add one more task in the lowercase "привет" project.
        Task *third = m_store->addTask(
            QStringLiteral("third"), m_unicodeProjectIdLower);

        // Query the unicode name — exact case
        const QList<Task *> all = m_store->allTasks();
        int n = matchCount(*m_eval, QString::fromUtf8("#Привет"), all);
        // Should match all three (two in capitalized + one in lowercase project).
        QCOMPARE(n, 3);

        // Query with all-lowercase Cyrillic — should still match all three.
        n = matchCount(*m_eval, QString::fromUtf8("#привет"), all);
        QCOMPARE(n, 3);

        // Bogus mid-string case mix
        n = matchCount(*m_eval, QString::fromUtf8("#ПрИвЕт"), all);
        QCOMPARE(n, 3);

        // A genuinely different project name must NOT match
        QVERIFY(!matches(*m_eval,
            QString::fromUtf8("#Прощай"), m_taskUnicode));
    }

    // =====================================================================
    // SLOT 8 — search:"hello \"world\"" — embedded escaped quote survival.
    // The tokenizer DOES NOT interpret backslash escapes; it reads search
    // text until a stop char ('|', '&', ',', ')'). Document the contract.
    // =====================================================================
    void slot08_search_with_embedded_quotes_documented()
    {
        // Plain content: "hello \"world\"" — the inner backslashes are part
        // of the C++ literal, the actual runtime string is hello "world".
        QCOMPARE(m_taskHelloWorld->content(),
                 QString::fromUtf8("hello \"world\""));

        // Search for the literal `"world"` substring (case-insensitive).
        // The tokenizer reads everything after "search:" until the next
        // top-level operator. Quotes are NOT special — they pass through
        // as literal characters.
        QVERIFY(matches(*m_eval,
            QStringLiteral("search: \"world\""), m_taskHelloWorld));

        // Without quotes the contains-check still hits.
        QVERIFY(matches(*m_eval,
            QStringLiteral("search: world"), m_taskHelloWorld));

        // Inside a longer phrase
        QVERIFY(matches(*m_eval,
            QStringLiteral("search: hello \"world\""), m_taskHelloWorld));

        qInfo() << "CONTRACT: search: tokenizer does NOT interpret backslash"
                   " escapes. Quotes are literal characters. Stop chars are"
                   " | & , ) — to include those in a search term you cannot.";
    }

    // =====================================================================
    // SLOT 9 — search with regex metachars should match as literal substring,
    // never interpreted as regex.
    // =====================================================================
    void slot09_search_treats_regex_metachars_as_literal()
    {
        // m_taskWeirdSearch content: "Path: .*\\d+ then $end" (literal runtime
        // string: Path: .*\d+ then $end).
        QCOMPARE(m_taskWeirdSearch->content(),
                 QString::fromUtf8("Path: .*\\d+ then $end"));

        // ".*" — if treated as regex, would match everything. As literal,
        // matches only the task whose content contains the literal ".*".
        const QList<Task *> all = m_store->allTasks();
        int n = matchCount(*m_eval, QStringLiteral("search: .*"), all);
        QCOMPARE(n, 1);  // only m_taskWeirdSearch contains the literal ".*"

        // "$end" — if regex, would mean "end of string" + literal "end".
        // As literal, must match only the task whose content contains "$end".
        n = matchCount(*m_eval, QStringLiteral("search: $end"), all);
        QCOMPARE(n, 1);

        // "\\d+" — if regex, would match any digit run. As literal, matches
        // only the task whose content contains the literal "\d+".
        n = matchCount(*m_eval, QStringLiteral("search: \\d+"), all);
        QCOMPARE(n, 1);

        qInfo() << "CONTRACT: search: uses QString::contains (literal,"
                   " case-insensitive). NO regex interpretation, ever.";
    }

    // =====================================================================
    // SLOT 10 — Date predicates at midnight boundary. A task whose only
    // due field is dueDatetime at 00:00:00 today: does it match `today`?
    // The implementation's effectiveDueDate() extracts the date portion
    // of dueDatetime when dueDate is empty, so 00:00:00 today → today.
    // =====================================================================
    void slot10_midnight_boundary_dueDatetime_today_matches_today()
    {
        // m_taskMidnight has only dueDatetime set, at midnight today.
        QVERIFY(m_taskMidnight->dueDate().isEmpty());
        QVERIFY(!m_taskMidnight->dueDatetime().isEmpty());

        // Calendar predicates (today/tomorrow/yesterday) keep date-truncated
        // semantics — a task due "midnight today" lives on today's calendar
        // page regardless of time-of-day.
        QVERIFY(matches(*m_eval, QStringLiteral("today"), m_taskMidnight));
        QVERIFY(!matches(*m_eval, QStringLiteral("tomorrow"), m_taskMidnight));

        // Iter-night-17: `overdue` flipped from date-truncated to TIME-
        // PRECISE comparison for datetime-only tasks. Pre-fix this slot
        // asserted `!overdue` because effectiveDueDate truncated the
        // datetime to its date component and `due < today` was false.
        // Post-fix: dt < now() is what matters. A 00:00:00 datetime is
        // strictly earlier than any non-midnight `now`, so the task IS
        // overdue once the clock crosses midnight.
        //
        // The flake-prone edge case — running the test at exactly
        // 00:00:00 — is mitigated by the test fixture timing (initFixture
        // runs after the QTest harness, which itself takes >0ms to start).
        // Practically every test run lands at a `now` strictly after
        // midnight so `overdue` is reliably true. We assert that here.
        QVERIFY2(matches(*m_eval, QStringLiteral("overdue"), m_taskMidnight),
                 "Iter-night-17: datetime-only task with dt=midnight-today "
                 "is overdue once `now` crosses 00:00:00 — matches "
                 "DataStore::overdueTasks's time-precise behaviour and the "
                 "sibling DateTimeHelper::isOverdue(date, dt) overload.");

        // Same task does NOT match `no date` because dueDatetime is non-empty.
        QVERIFY(!matches(*m_eval, QStringLiteral("no date"), m_taskMidnight));

        qInfo() << "CONTRACT (post iter-night-17): calendar predicates "
                   "(today/tomorrow/yesterday) compare via dt.date(); "
                   "`overdue` compares the full QDateTime to "
                   "QDateTime::currentDateTime(). Mirrors "
                   "DataStore::overdueTasks (datastore.cpp:611-627).";
    }

    void slot10b_midnight_tomorrow_boundary()
    {
        // A task whose dueDatetime is 23:59:59 today still matches `today`.
        Task *t = m_store->addTask(QStringLiteral("Last moment"),
            m_store->inboxProject()->id());
        const QDateTime endOfDay(QDate::currentDate(), QTime(23, 59, 59));
        t->setDueDatetime(endOfDay.toString(Qt::ISODate));

        QVERIFY(matches(*m_eval, QStringLiteral("today"), t));
        QVERIFY(!matches(*m_eval, QStringLiteral("tomorrow"), t));

        // A task whose dueDatetime is 00:00:00 tomorrow matches `tomorrow`,
        // never `today`. This is the boundary contract.
        Task *t2 = m_store->addTask(QStringLiteral("First moment tomorrow"),
            m_store->inboxProject()->id());
        const QDateTime midnightTmrw(QDate::currentDate().addDays(1),
            QTime(0, 0, 0));
        t2->setDueDatetime(midnightTmrw.toString(Qt::ISODate));

        QVERIFY(!matches(*m_eval, QStringLiteral("today"), t2));
        QVERIFY(matches(*m_eval, QStringLiteral("tomorrow"), t2));
    }

    // =====================================================================
    // SLOT 11 — Recurring with malformed RRULE: the `recurring` predicate
    // checks ONLY the dueIsRecurring boolean flag. RRULE content has no
    // bearing on the predicate's truth value.
    // =====================================================================
    void slot11_recurring_predicate_only_checks_flag_not_rrule_validity()
    {
        QVERIFY(m_taskRecurringMalformed->dueIsRecurring());
        QVERIFY(matches(*m_eval, QStringLiteral("recurring"),
                        m_taskRecurringMalformed));
        QVERIFY(!matches(*m_eval, QStringLiteral("!recurring"),
                         m_taskRecurringMalformed));

        qInfo() << "CONTRACT: `recurring` checks task->dueIsRecurring(), NOT"
                   " task->dueRecurrenceRule(). A task flagged recurring with"
                   " a garbage RRULE still matches `recurring`. Validity of"
                   " the RRULE is RecurrenceEngine's problem, not the filter's.";
    }

    // =====================================================================
    // SLOT 12 — before/after with far-future dates. `before 2099-12-31`
    // should match every task that has any due date set (all are < 2099).
    // =====================================================================
    void slot12_before_far_future_matches_all_dated_tasks()
    {
        const QList<Task *> all = m_store->allTasks();
        const int n = matchCount(*m_eval,
            QStringLiteral("before 2099-12-31"), all);

        // All tasks with any due (m_taskP1Today, m_taskP2Tomorrow,
        // m_taskP3Today, m_taskP4Future, m_taskMidnight,
        // m_taskRecurringMalformed) should match. 6 tasks with due dates.
        // Count of dated tasks dynamically:
        int dated = 0;
        for (auto *t : all) {
            if (!t->dueDate().isEmpty() || !t->dueDatetime().isEmpty())
                ++dated;
        }
        QCOMPARE(n, dated);
        QVERIFY(dated >= 6);
    }

    void slot12b_after_far_past_matches_all_dated_tasks()
    {
        const QList<Task *> all = m_store->allTasks();
        const int n = matchCount(*m_eval,
            QStringLiteral("after 1970-01-01"), all);

        int dated = 0;
        for (auto *t : all) {
            if (!t->dueDate().isEmpty() || !t->dueDatetime().isEmpty())
                ++dated;
        }
        QCOMPARE(n, dated);
    }

    void slot12c_before_invalid_date_token_matches_nothing()
    {
        // CONTRACT: resolveDateToken returns invalid QDate for unparseable
        // tokens. The Before/After branch then short-circuits to false via
        // beforeDate.isValid() check.
        QVERIFY(!matches(*m_eval,
            QStringLiteral("before garbage-not-a-date"), m_taskP1Today));
        QVERIFY(!matches(*m_eval,
            QStringLiteral("after garbage-not-a-date"), m_taskP1Today));
    }

    // =====================================================================
    // SLOT 13 — `today` against a task with empty dueDate AND empty
    // dueDatetime must be false (no due → not today).
    // =====================================================================
    void slot13_today_against_no_due_is_false()
    {
        QVERIFY(m_taskNoDate->dueDate().isEmpty());
        QVERIFY(m_taskNoDate->dueDatetime().isEmpty());
        QVERIFY(!matches(*m_eval, QStringLiteral("today"),    m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("tomorrow"), m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("yesterday"), m_taskNoDate));
        QVERIFY(!matches(*m_eval, QStringLiteral("overdue"),  m_taskNoDate));
        QVERIFY(matches(*m_eval,  QStringLiteral("no date"),  m_taskNoDate));
    }

    // =====================================================================
    // SLOT 14 — `overdue` excludes completed tasks. The implementation
    // adds `&& !task->isCompleted()` to the overdue branch.
    // =====================================================================
    void slot14_overdue_excludes_completed_tasks()
    {
        // Create a task that IS in the past AND completed.
        Task *t = m_store->addTask(QStringLiteral("Done yesterday"),
            m_store->inboxProject()->id());
        const QDate yesterday = QDate::currentDate().addDays(-1);
        t->setDueDate(DateTimeHelper::toDateString(yesterday));
        t->setIsCompleted(true);

        QVERIFY(!matches(*m_eval, QStringLiteral("overdue"), t));

        // Sanity: same task without the completed flag IS overdue.
        t->setIsCompleted(false);
        QVERIFY(matches(*m_eval, QStringLiteral("overdue"), t));

        qInfo() << "CONTRACT: `overdue` excludes completed tasks. Logic:"
                   " due.isValid() && due < today && !task->isCompleted().";
    }

    // =====================================================================
    // SLOT 15 — Section separator (comma) acts as OR. Pin that semantic.
    // The parser builds an AstNodeType::SectionSeparator that evaluator
    // treats identically to OR.
    // =====================================================================
    void slot15_comma_acts_as_or_for_evaluation()
    {
        // "p1, p4" → match priority 1 OR priority 4
        QVERIFY( matches(*m_eval, QStringLiteral("p1, p4"), m_taskP1Today));
        QVERIFY(!matches(*m_eval, QStringLiteral("p1, p4"), m_taskP2Tomorrow));
        QVERIFY(!matches(*m_eval, QStringLiteral("p1, p4"), m_taskP3Today));
        QVERIFY( matches(*m_eval, QStringLiteral("p1, p4"), m_taskP4Future));
    }

    // =====================================================================
    // SLOT 16 — Identifier fall-through: an unknown bare word becomes an
    // Identifier predicate that matches via content.contains(case-insensitive).
    // =====================================================================
    void slot16_unknown_identifier_matches_content_substring()
    {
        // m_taskP1Today.content == "P1 today"; "p1" the token becomes a
        // Priority token, NOT an identifier. Use a word that is genuinely
        // unrecognized.
        QVERIFY(matches(*m_eval, QStringLiteral("today"), m_taskP1Today));

        // The literal word "p1" in content of another task should still match
        // via content substring for an Identifier — but "p1" tokenizes as
        // Priority, not Identifier. Let's use a non-keyword word.
        Task *t = m_store->addTask(QStringLiteral("buy groceries"),
            m_store->inboxProject()->id());

        // "groceries" is not a keyword → Identifier → content substring match
        QVERIFY(matches(*m_eval, QStringLiteral("groceries"), t));
        // case-insensitive
        QVERIFY(matches(*m_eval, QStringLiteral("GROCERIES"), t));
        QVERIFY(matches(*m_eval, QStringLiteral("Groceries"), t));

        // Identifier predicate does NOT search description (only content)
        // — pin that contract. Note: the `search:` predicate DOES search both.
        t->setContent(QStringLiteral("groceries"));
        t->setDescription(QStringLiteral("milk eggs"));
        // "eggs" only in description; Identifier predicate must not match.
        QVERIFY(!matches(*m_eval, QStringLiteral("eggs"), t));
        // But `search: eggs` DOES match (it scans both content + description).
        QVERIFY(matches(*m_eval, QStringLiteral("search: eggs"), t));

        qInfo() << "CONTRACT: bare Identifier matches CONTENT only."
                   " search:term matches CONTENT + DESCRIPTION."
                   " This asymmetry is documented but not obvious to users.";
    }

    // =====================================================================
    // SLOT 17 — `view all` predicate matches every task, including those
    // with no due date, no project, and unusual states.
    // =====================================================================
    void slot17_view_all_matches_everything()
    {
        const QList<Task *> all = m_store->allTasks();
        const int n = matchCount(*m_eval, QStringLiteral("view all"), all);
        QCOMPARE(n, all.size());

        // Sanity: `view all & no date` is equivalent to `no date` alone.
        const int nd1 = matchCount(*m_eval, QStringLiteral("no date"), all);
        const int nd2 = matchCount(*m_eval,
            QStringLiteral("view all & no date"), all);
        QCOMPARE(nd1, nd2);
    }

    // =====================================================================
    // SLOT 18 — `assigned` (bare) and `assigned: <name>` both parse and
    // match correctly. F-Q2.24-B FIX: the tokenizer's outer word loop now
    // stops at ':', so the keyword `assigned` is properly recognised and
    // its dedicated branch consumes the optional `:<name>` suffix.
    //
    // Behaviour pinned here:
    //   * `assigned`           → all tasks (default assignee "me").
    //   * `assigned: me`       → all tasks (explicit "me").
    //   * `assigned: alice`    → no tasks (no task is assigned to "alice").
    //
    // Note: the evaluator currently treats every task as "assigned to me"
    // because OpenTodo doesn't yet model multi-user assignees — the
    // `Assigned` predicate matches everything when the assignee is "me".
    // Any other assignee name matches nothing. This is wired-up parser ->
    // evaluator behaviour; the upstream multi-user feature can layer on
    // top later without changing the syntax.
    // =====================================================================
    void slot18_assigned_me_matches_all_others_none()
    {
        const QList<Task *> all = m_store->allTasks();

        // `assigned` (defaults to "me") — matches all. Verified working.
        const int n1 = matchCount(*m_eval, QStringLiteral("assigned"), all);
        QCOMPARE(n1, all.size());

        // `assigned: me` — parses cleanly after F-Q2.24-B and matches all
        // tasks because the evaluator treats every task as assigned to "me"
        // (no multi-user data model yet).
        const int n2 = matchCount(*m_eval, QStringLiteral("assigned: me"), all);
        QCOMPARE(n2, all.size());

        // No-space variant: `assigned:me` — same semantics, also parses.
        const int n2b = matchCount(*m_eval, QStringLiteral("assigned:me"), all);
        QCOMPARE(n2b, all.size());

        // Explicit foreign assignee: parses cleanly but matches no task.
        const int n3 = matchCount(*m_eval,
            QStringLiteral("assigned: alice"), all);
        QCOMPARE(n3, 0);

        qInfo() << "CONTRACT (F-Q2.24-B fix): `assigned` and `assigned: <name>`"
                   " both parse. The tokenizer now stops at ':', so the"
                   " 'assigned' keyword's optional-colon-suffix branch is"
                   " reachable. The evaluator treats every task as 'assigned"
                   " to me' (no multi-user model), so `assigned: me` matches"
                   " all and `assigned: alice` matches none.";
    }

    // =====================================================================
    // SLOT 19 — Priority predicate matches exactly; priority range matches
    // inclusively. Test boundary values (p1, p4, edges).
    // =====================================================================
    void slot19_priority_exact_and_range_inclusive()
    {
        QVERIFY( matches(*m_eval, QStringLiteral("p1"), m_taskP1Today));
        QVERIFY(!matches(*m_eval, QStringLiteral("p1"), m_taskP2Tomorrow));
        QVERIFY(!matches(*m_eval, QStringLiteral("p1"), m_taskP4Future));
        QVERIFY( matches(*m_eval, QStringLiteral("p4"), m_taskP4Future));

        // Range — p1..p2 inclusive
        const QList<Task *> all = m_store->allTasks();
        const int rng12 = matchCount(*m_eval,
            QStringLiteral("priority 1..2"), all);
        // P1, P2 (m_taskP2Tomorrow), P2 (m_taskNoDate),
        // plus unicode tasks p2, plus m_taskRecurringMalformed p1 default.
        int expectedRng12 = 0;
        for (auto *t : all) {
            if (t->priority() >= 1 && t->priority() <= 2)
                ++expectedRng12;
        }
        QCOMPARE(rng12, expectedRng12);
    }

    // =====================================================================
    // SLOT 20 — `no labels` matches tasks with empty label list, AND tasks
    // are still excluded if they have at least one label.
    // =====================================================================
    void slot20_no_labels_matches_empty_label_list()
    {
        // m_taskP1Today has @work
        QVERIFY(!matches(*m_eval, QStringLiteral("no labels"), m_taskP1Today));
        // m_taskP2Tomorrow has no labels
        QVERIFY(matches(*m_eval, QStringLiteral("no labels"), m_taskP2Tomorrow));

        // Empty string in label list is technically a label
        Task *t = m_store->addTask(QStringLiteral("empty label"),
            m_store->inboxProject()->id());
        t->setLabels(QStringList{QString()});
        // Even an empty-string label means labels is NOT empty.
        QVERIFY(!matches(*m_eval, QStringLiteral("no labels"), t));

        qInfo() << "CONTRACT: `no labels` checks task->labels().isEmpty(),"
                   " not the truthiness of each label. A list containing an"
                   " empty string still counts as 'has labels'.";
    }

    // =====================================================================
    // SLOT 21 — Section predicate matches by section name (case-insensitive)
    // AND a task with an empty sectionId never matches any section query.
    // =====================================================================
    void slot21_section_predicate_case_insensitive_empty_id_misses()
    {
        // Create a section and assign a task to it.
        Project *p = m_store->addProject(QStringLiteral("ProjForSec"));
        Section *s = m_store->addSection(QStringLiteral("Backlog"), p->id());

        Task *t = m_store->addTask(QStringLiteral("Sectioned"), p->id());
        t->setSectionId(s->id());

        // exact case
        QVERIFY(matches(*m_eval, QStringLiteral("/Backlog"), t));
        // case-insensitive
        QVERIFY(matches(*m_eval, QStringLiteral("/backlog"), t));
        QVERIFY(matches(*m_eval, QStringLiteral("/BACKLOG"), t));

        // task with empty sectionId never matches any /name
        QVERIFY(m_taskP1Today->sectionId().isEmpty());
        QVERIFY(!matches(*m_eval, QStringLiteral("/Backlog"), m_taskP1Today));
        QVERIFY(!matches(*m_eval, QStringLiteral("/anything"), m_taskP1Today));

        // task with sectionId pointing to a deleted/nonexistent section misses
        t->setSectionId(QStringLiteral("nonexistent-section-id"));
        QVERIFY(!matches(*m_eval, QStringLiteral("/Backlog"), t));
    }

    // =====================================================================
    // SLOT 22 — Label predicate matches via QStringList::contains with case
    // insensitivity. Tasks without any label never match a label query.
    // =====================================================================
    void slot22_label_predicate_case_insensitive()
    {
        // m_taskP1Today has label "work"
        QVERIFY(matches(*m_eval, QStringLiteral("@work"), m_taskP1Today));
        QVERIFY(matches(*m_eval, QStringLiteral("@WORK"), m_taskP1Today));
        QVERIFY(matches(*m_eval, QStringLiteral("@Work"), m_taskP1Today));

        // Wrong label
        QVERIFY(!matches(*m_eval, QStringLiteral("@personal"), m_taskP1Today));

        // No labels
        QVERIFY(!matches(*m_eval, QStringLiteral("@work"), m_taskP2Tomorrow));
    }

    // =====================================================================
    // SLOT 23 — `N days` predicate is inclusive of today and today+(N-1).
    // The fix B-4.46 sets endDate = today + (N-1).
    // =====================================================================
    void slot23_n_days_predicate_inclusive_range()
    {
        // m_taskP2Tomorrow due in 1 day → "1 days" should INCLUDE today only,
        // EXCLUDE tomorrow (endDate = today + 0).
        QVERIFY(!matches(*m_eval, QStringLiteral("1 days"), m_taskP2Tomorrow));
        QVERIFY( matches(*m_eval, QStringLiteral("2 days"), m_taskP2Tomorrow));
        QVERIFY( matches(*m_eval, QStringLiteral("7 days"), m_taskP2Tomorrow));

        // m_taskP1Today due today → "1 days" should match (just today).
        QVERIFY(matches(*m_eval, QStringLiteral("1 days"), m_taskP1Today));

        // P4 future task is 30 days out — "7 days" must miss, "31 days" hit.
        QVERIFY(!matches(*m_eval, QStringLiteral("7 days"), m_taskP4Future));
        QVERIFY( matches(*m_eval, QStringLiteral("31 days"), m_taskP4Future));

        qInfo() << "CONTRACT: `N days` matches due in [today, today+N-1]."
                   " (B-4.46 fix — was previously [today, today+N] which"
                   " over-counted by one.)";
    }

    // =====================================================================
    // SLOT 24 — Compound NOT distributes correctly via De Morgan's logic.
    // Verify !(p1 | p4) is equivalent to !p1 & !p4 (by behavior).
    // =====================================================================
    void slot24_de_morgan_equivalence_holds()
    {
        const QList<Task *> all = m_store->allTasks();

        // !(p1 | p4) — match tasks that are neither p1 nor p4
        const int nNot   = matchCount(*m_eval,
            QStringLiteral("!(p1 | p4)"), all);
        // !p1 & !p4 — same semantics
        const int nBoth = matchCount(*m_eval,
            QStringLiteral("!p1 & !p4"), all);
        QCOMPARE(nNot, nBoth);

        // Direct check: P2 task should match both forms
        QVERIFY( matches(*m_eval, QStringLiteral("!(p1 | p4)"), m_taskP2Tomorrow));
        QVERIFY( matches(*m_eval, QStringLiteral("!p1 & !p4"),   m_taskP2Tomorrow));
        // P1 task should NOT match either form
        QVERIFY(!matches(*m_eval, QStringLiteral("!(p1 | p4)"), m_taskP1Today));
        QVERIFY(!matches(*m_eval, QStringLiteral("!p1 & !p4"),   m_taskP1Today));
    }
};

QTEST_GUILESS_MAIN(TestFilterEvalEdgeCasesV2)
#include "test_filtereval_edge_cases_v2.moc"
