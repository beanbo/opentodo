#include <QtTest>

#include "filter/filterparser.h"
#include "filter/filtereval.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "utils/datetime.h"

// ---------------------------------------------------------------------------
// Thin wrapper: evaluate a query string against a single task.
// Returns true if the task matches, false otherwise (including parse failure).
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

// ---------------------------------------------------------------------------
// Fixture — builds a DataStore with two projects and several tasks that
// cover every predicate we want to exercise.
// ---------------------------------------------------------------------------
class TestFilterEvaluator : public QObject
{
    Q_OBJECT

private:
    DataStore *m_store       = nullptr;
    FilterEvaluator *m_eval  = nullptr;

    // Tasks
    Task *m_taskToday    = nullptr;   // due=today, p1, #Work, @urgent
    Task *m_taskTomorrow = nullptr;   // due=tomorrow, p2
    Task *m_taskOverdue  = nullptr;   // due=yesterday, p2, #Personal
    Task *m_taskNoDate   = nullptr;   // no due date, p3
    Task *m_taskCompleted= nullptr;   // due=today, p4, completed
    Task *m_taskRecurring= nullptr;   // due=today, recurring=true
    Task *m_taskSearch   = nullptr;   // content="buy groceries", no date, p3
    Task *m_taskDeadline = nullptr;   // due=today, deadline=tomorrow, p3

    // Project IDs
    QString m_workProjectId;
    QString m_personalProjectId;

private slots:

    void init()
    {
        m_store = new DataStore(this);

        // Create an inbox project first — DataStore::addTask falls back to
        // inboxProject() when no projectId is given, which crashes if no inbox
        // project has been registered yet.
        Project *inbox = m_store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        // Create projects
        Project *work = m_store->addProject(QStringLiteral("Work"));
        m_workProjectId = work->id();

        Project *personal = m_store->addProject(QStringLiteral("Personal"));
        m_personalProjectId = personal->id();

        const QDate today     = QDate::currentDate();
        const QDate yesterday = today.addDays(-1);
        const QDate tomorrow  = today.addDays(1);
        const QString todayStr    = DateTimeHelper::toDateString(today);
        const QString yesterdayStr= DateTimeHelper::toDateString(yesterday);
        const QString tomorrowStr = DateTimeHelper::toDateString(tomorrow);

        // Task due today, p1, #Work, @urgent
        m_taskToday = m_store->addTask(QStringLiteral("Today task"), m_workProjectId);
        m_taskToday->setDueDate(todayStr);
        m_taskToday->setPriority(1);
        m_taskToday->setLabels(QStringList{QStringLiteral("urgent")});

        // Task due tomorrow, p2
        m_taskTomorrow = m_store->addTask(QStringLiteral("Tomorrow task"), m_workProjectId);
        m_taskTomorrow->setDueDate(tomorrowStr);
        m_taskTomorrow->setPriority(2);

        // Task overdue, p2, #Personal
        m_taskOverdue = m_store->addTask(QStringLiteral("Overdue task"), m_personalProjectId);
        m_taskOverdue->setDueDate(yesterdayStr);
        m_taskOverdue->setPriority(2);

        // Task no date, p3 (use inbox project to avoid null dereference)
        m_taskNoDate = m_store->addTask(QStringLiteral("No date task"), inbox->id());
        m_taskNoDate->setPriority(3);

        // Task completed, due today, p4
        m_taskCompleted = m_store->addTask(QStringLiteral("Completed task"), inbox->id());
        m_taskCompleted->setDueDate(todayStr);
        m_taskCompleted->setPriority(4);
        m_taskCompleted->setIsCompleted(true);

        // Recurring task, due today
        m_taskRecurring = m_store->addTask(QStringLiteral("Recurring task"), inbox->id());
        m_taskRecurring->setDueDate(todayStr);
        m_taskRecurring->setDueIsRecurring(true);
        m_taskRecurring->setPriority(3);

        // Search task: "buy groceries", no date, p3
        m_taskSearch = m_store->addTask(QStringLiteral("buy groceries"), inbox->id());
        m_taskSearch->setPriority(3);

        // Deadline task: due today, deadline tomorrow, p3
        m_taskDeadline = m_store->addTask(QStringLiteral("Has deadline"), inbox->id());
        m_taskDeadline->setDueDate(todayStr);
        m_taskDeadline->setDeadline(tomorrowStr);
        m_taskDeadline->setPriority(3);

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
    // today
    // -----------------------------------------------------------------------
    void today_matchesTodayTask()
    {
        QVERIFY(matches(*m_eval, "today", m_taskToday));
    }

    void today_doesNotMatchOverdue()
    {
        QVERIFY(!matches(*m_eval, "today", m_taskOverdue));
    }

    void today_doesNotMatchNoDate()
    {
        QVERIFY(!matches(*m_eval, "today", m_taskNoDate));
    }

    void today_matchesCompletedDueToday()
    {
        // "today" predicate only checks the date, not completion status
        QVERIFY(matches(*m_eval, "today", m_taskCompleted));
    }

    // -----------------------------------------------------------------------
    // overdue  (must be !completed)
    // -----------------------------------------------------------------------
    void overdue_matchesOverdueTask()
    {
        QVERIFY(matches(*m_eval, "overdue", m_taskOverdue));
    }

    void overdue_doesNotMatchToday()
    {
        QVERIFY(!matches(*m_eval, "overdue", m_taskToday));
    }

    void overdue_doesNotMatchCompleted()
    {
        // completed overdue tasks are excluded by the evaluator
        Task *completedOverdue = new Task(this);
        completedOverdue->setDueDate(DateTimeHelper::toDateString(QDate::currentDate().addDays(-3)));
        completedOverdue->setIsCompleted(true);
        QVERIFY(!matches(*m_eval, "overdue", completedOverdue));
    }

    void overdue_doesNotMatchNoDate()
    {
        QVERIFY(!matches(*m_eval, "overdue", m_taskNoDate));
    }

    // -----------------------------------------------------------------------
    // no date
    // -----------------------------------------------------------------------
    void noDate_matchesTaskWithoutDueDate()
    {
        QVERIFY(matches(*m_eval, "no date", m_taskNoDate));
    }

    void noDate_doesNotMatchTodayTask()
    {
        QVERIFY(!matches(*m_eval, "no date", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // priorities
    // -----------------------------------------------------------------------
    void priority_p1()
    {
        QVERIFY(matches(*m_eval, "p1", m_taskToday));
        QVERIFY(!matches(*m_eval, "p1", m_taskOverdue));
    }

    void priority_p2()
    {
        QVERIFY(matches(*m_eval, "p2", m_taskOverdue));
        QVERIFY(matches(*m_eval, "p2", m_taskTomorrow));
        QVERIFY(!matches(*m_eval, "p2", m_taskToday));
    }

    void priority_p3()
    {
        QVERIFY(matches(*m_eval, "p3", m_taskNoDate));
        QVERIFY(!matches(*m_eval, "p3", m_taskToday));
    }

    void priority_p4()
    {
        QVERIFY(matches(*m_eval, "p4", m_taskCompleted));
        QVERIFY(!matches(*m_eval, "p4", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // #Project — resolved through DataStore
    // -----------------------------------------------------------------------
    void project_matchesWorkTask()
    {
        QVERIFY(matches(*m_eval, "#Work", m_taskToday));
    }

    void project_doesNotMatchWrongProject()
    {
        QVERIFY(!matches(*m_eval, "#Work", m_taskOverdue));   // overdue is #Personal
    }

    void project_matchesPersonalTask()
    {
        QVERIFY(matches(*m_eval, "#Personal", m_taskOverdue));
    }

    void project_caseInsensitive()
    {
        QVERIFY(matches(*m_eval, "#work", m_taskToday));
        QVERIFY(matches(*m_eval, "#WORK", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // @label
    // -----------------------------------------------------------------------
    void label_matchesUrgent()
    {
        QVERIFY(matches(*m_eval, "@urgent", m_taskToday));
    }

    void label_doesNotMatchTaskWithoutLabel()
    {
        QVERIFY(!matches(*m_eval, "@urgent", m_taskOverdue));
    }

    void label_caseInsensitive()
    {
        QVERIFY(matches(*m_eval, "@URGENT", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // recurring / !recurring
    // -----------------------------------------------------------------------
    void recurring_matchesRecurringTask()
    {
        QVERIFY(matches(*m_eval, "recurring", m_taskRecurring));
    }

    void recurring_doesNotMatchNonRecurring()
    {
        QVERIFY(!matches(*m_eval, "recurring", m_taskToday));
    }

    void notRecurring_matchesNonRecurring()
    {
        // "!recurring" is written as the logical NOT of "recurring"
        QVERIFY(matches(*m_eval, "!recurring", m_taskToday));
    }

    void notRecurring_doesNotMatchRecurring()
    {
        QVERIFY(!matches(*m_eval, "!recurring", m_taskRecurring));
    }

    // -----------------------------------------------------------------------
    // completed
    // -----------------------------------------------------------------------
    void completed_matchesCompletedTask()
    {
        QVERIFY(matches(*m_eval, "completed", m_taskCompleted));
    }

    void completed_doesNotMatchActiveTask()
    {
        QVERIFY(!matches(*m_eval, "completed", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // Compound expressions
    // -----------------------------------------------------------------------
    void compound_todayAndP1()
    {
        QVERIFY(matches(*m_eval, "today & p1", m_taskToday));
        QVERIFY(!matches(*m_eval, "today & p1", m_taskTomorrow));
        QVERIFY(!matches(*m_eval, "today & p1", m_taskOverdue));
    }

    void compound_todayOrOverdue()
    {
        QVERIFY(matches(*m_eval, "today | overdue", m_taskToday));
        QVERIFY(matches(*m_eval, "today | overdue", m_taskOverdue));
        QVERIFY(!matches(*m_eval, "today | overdue", m_taskNoDate));
    }

    void compound_notToday()
    {
        QVERIFY(!matches(*m_eval, "!today", m_taskToday));
        QVERIFY(matches(*m_eval, "!today", m_taskNoDate));
        QVERIFY(matches(*m_eval, "!today", m_taskOverdue));
    }

    void compound_groupedOrAndP1()
    {
        // "(today | overdue) & p1"
        // m_taskToday: today + p1 => matches
        QVERIFY(matches(*m_eval, "(today | overdue) & p1", m_taskToday));
        // m_taskOverdue: overdue + p2 => does NOT match (wrong priority)
        QVERIFY(!matches(*m_eval, "(today | overdue) & p1", m_taskOverdue));
        // m_taskNoDate: !today && !overdue => does NOT match
        QVERIFY(!matches(*m_eval, "(today | overdue) & p1", m_taskNoDate));
    }

    void compound_commaActsAsOr()
    {
        // comma is SectionSeparator which evaluates as OR
        QVERIFY(matches(*m_eval, "today, overdue", m_taskToday));
        QVERIFY(matches(*m_eval, "today, overdue", m_taskOverdue));
        QVERIFY(!matches(*m_eval, "today, overdue", m_taskNoDate));
    }

    // -----------------------------------------------------------------------
    // search: keyword
    // -----------------------------------------------------------------------
    void search_findsTaskByContent()
    {
        QVERIFY(matches(*m_eval, "search: groceries", m_taskSearch));
        QVERIFY(!matches(*m_eval, "search: groceries", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // before / after date predicates
    // -----------------------------------------------------------------------
    void before_matchesEarlierDate()
    {
        // m_taskOverdue has date = yesterday; yesterday < 2099-01-01
        QVERIFY(matches(*m_eval, "before 2099-01-01", m_taskOverdue));
    }

    void before_doesNotMatchLaterDate()
    {
        // m_taskOverdue (yesterday) is NOT before 2000-01-01
        QVERIFY(!matches(*m_eval, "before 2000-01-01", m_taskOverdue));
    }

    void after_matchesLaterDate()
    {
        // m_taskTomorrow has due = tomorrow > 2000-01-01
        QVERIFY(matches(*m_eval, "after 2000-01-01", m_taskTomorrow));
    }

    void after_doesNotMatchNoDate()
    {
        QVERIFY(!matches(*m_eval, "after 2000-01-01", m_taskNoDate));
    }

    // -----------------------------------------------------------------------
    // N days
    // -----------------------------------------------------------------------
    void nDays_matchesTodayTask()
    {
        // today is within "5 days" window
        QVERIFY(matches(*m_eval, "5 days", m_taskToday));
    }

    void nDays_matchesTomorrowTask()
    {
        QVERIFY(matches(*m_eval, "5 days", m_taskTomorrow));
    }

    void nDays_doesNotMatchOverdue()
    {
        // overdue is in the past, not within the next 5 days
        QVERIFY(!matches(*m_eval, "5 days", m_taskOverdue));
    }

    void nDays_doesNotMatchNoDate()
    {
        QVERIFY(!matches(*m_eval, "5 days", m_taskNoDate));
    }

    // -----------------------------------------------------------------------
    // created before / after
    // -----------------------------------------------------------------------
    void createdBefore_futureDate()
    {
        // tasks were just created, so they are before any far-future date
        QVERIFY(matches(*m_eval, "created before 2099-01-01", m_taskToday));
    }

    void createdAfter_pastDate()
    {
        QVERIFY(matches(*m_eval, "created after 2000-01-01", m_taskToday));
    }

    // -----------------------------------------------------------------------
    // deadline predicate
    // -----------------------------------------------------------------------
    void deadline_matchesTaskWithDeadline()
    {
        QVERIFY(matches(*m_eval, "deadline", m_taskDeadline));
    }

    void deadline_doesNotMatchTaskWithoutDeadline()
    {
        QVERIFY(!matches(*m_eval, "deadline", m_taskToday));
        QVERIFY(!matches(*m_eval, "deadline", m_taskNoDate));
    }

    void deadline_negationMatchesTaskWithoutDeadline()
    {
        QVERIFY(matches(*m_eval, "!deadline", m_taskToday));
        QVERIFY(matches(*m_eval, "!deadline", m_taskNoDate));
        QVERIFY(!matches(*m_eval, "!deadline", m_taskDeadline));
    }

    void deadline_andTodayComposes()
    {
        // deadline & today should match the deadline task (due=today, deadline=tomorrow)
        QVERIFY(matches(*m_eval, "deadline & today", m_taskDeadline));
        // taskToday has due=today but no deadline => no match
        QVERIFY(!matches(*m_eval, "deadline & today", m_taskToday));
        // taskTomorrow: no deadline => no match
        QVERIFY(!matches(*m_eval, "deadline & today", m_taskTomorrow));
    }

    void deadline_russianForm()
    {
        // "с дедлайном" should evaluate the same as "deadline"
        QVERIFY(matches(*m_eval, QString::fromUtf8("с дедлайном"), m_taskDeadline));
        QVERIFY(!matches(*m_eval, QString::fromUtf8("с дедлайном"), m_taskToday));
    }

    // -----------------------------------------------------------------------
    // evaluate(query) string overload uses the DataStore's task list
    // -----------------------------------------------------------------------
    void stringOverload_returnsMatchingTasks()
    {
        QList<Task *> result = m_eval->evaluate(QStringLiteral("p1"));
        // m_taskToday is p1 and is in the DataStore
        bool found = false;
        for (Task *t : result) {
            if (t->id() == m_taskToday->id()) { found = true; break; }
        }
        QVERIFY(found);
    }

    void stringOverload_invalidQueryReturnsEmpty()
    {
        QList<Task *> result = m_eval->evaluate(QStringLiteral("(unclosed"));
        QVERIFY(result.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFilterEvaluator)
#include "test_filterevaluator.moc"
