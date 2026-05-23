// =============================================================================
// Iter-night-17 pin: FilterEvaluator's `overdue` token must use TIME-PRECISE
// comparison for datetime-only tasks (dueDate empty, dueDatetime set), not
// the date-truncated comparison the helper effectiveDueDate() returns.
//
// Pre-fix:
//   case TokenType::Overdue:
//       const QDate due = effectiveDueDate(task);  // truncates dt → date
//       return due.isValid() && due < today && !task->isCompleted();
//
// `due < today` is FALSE when dt is "today at 09:00" viewed at 15:00 —
// because both sides truncate to the same calendar day. The filter said
// "not overdue" while DataStore::overdueTasks (datastore.cpp:611-627),
// DateTimeHelper::isOverdue(date, dt) overload (datetime.h:66-73), and
// (since iter-night-16) the sidebar Today badge all correctly flagged it.
//
// Post-fix (matches DataStore::overdueTasks):
//   - dueDate non-empty → calendar-day comparison (`d < today`).
//   - dueDate empty + dueDatetime non-empty → `dt < QDateTime::currentDateTime()`.
//
// Calendar predicates (today/tomorrow/yesterday) keep date-truncated
// semantics — they're naturally calendar-day concepts.
// =============================================================================

#include <QtTest>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QString>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "filter/filtereval.h"
#include "filter/filterparser.h"

namespace {

static QString isoLocal(const QDateTime &dt)
{
    return dt.toString(Qt::ISODate);
}

// Compile the filter expression and evaluate against a single task.
// Mirrors the helper in test_filtereval_edge_cases_v2.cpp: the evaluator's
// public API returns a filtered task list, so we wrap a single-task list
// and check whether it survives.
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

} // anonymous namespace


class TestFilterOverdueDatetimePrecision : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    FilterEvaluator *eval = nullptr;
    QString inboxId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
        eval = new FilterEvaluator(store);
    }

    void cleanup()
    {
        delete eval; eval = nullptr;
        delete store; store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Datetime-only task with TODAY date but PAST time-of-day IS overdue.
    //    This is the headline bug. Pre-fix `due < today` returned false
    //    because both sides truncated to the same day.
    //
    //    We can't hardcode a "past time" without race vs the wall clock,
    //    so set dt = `now - 1 hour` (so it's reliably in the past).
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_datetimeOnly_pastInstantSameDay_IS_overdue()
    {
        const QDateTime past = QDateTime::currentDateTime().addSecs(-3600);
        Task *t = store->addTask(QStringLiteral("Past today"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(past));

        QVERIFY2(matches(*eval, QStringLiteral("overdue"), t),
                 "Iter-night-17: datetime-only task whose datetime is in "
                 "the past must match the `overdue` filter, even if the "
                 "calendar day is still today. Pre-fix the filter used "
                 "date-truncated comparison and reported `not overdue`.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Datetime-only task with TODAY date but FUTURE time-of-day is NOT
    //    overdue. Regression guard against accidentally flipping the
    //    inequality direction.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_datetimeOnly_futureInstantSameDay_isNOT_overdue()
    {
        const QDateTime future = QDateTime::currentDateTime().addSecs(3600);
        Task *t = store->addTask(QStringLiteral("Future today"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(future));

        QVERIFY2(!matches(*eval, QStringLiteral("overdue"), t),
                 "A datetime in the future must NOT match `overdue` even "
                 "if the calendar date is today.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Datetime-only task on a PAST date IS overdue (regression guard for
    //    the calendar-day overdue case via datetime-only storage).
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_datetimeOnly_pastDate_IS_overdue()
    {
        const QDateTime past = QDateTime(QDate::currentDate().addDays(-3),
                                         QTime(12, 0));
        Task *t = store->addTask(QStringLiteral("Yesterday yesterday"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(past));

        QVERIFY(matches(*eval, QStringLiteral("overdue"), t));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Date-only (no datetime) task on a past day IS overdue. Pre-fix
    //    was already correct here; pinned as regression guard.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_dateOnly_pastDay_IS_overdue()
    {
        Task *t = store->addTask(QStringLiteral("Yesterday"), inboxId);
        t->setDueDate(QDate::currentDate().addDays(-1)
                      .toString(QStringLiteral("yyyy-MM-dd")));

        QVERIFY(matches(*eval, QStringLiteral("overdue"), t));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) Date-only task with TODAY date is NOT overdue. Pin the calendar-day
    //    semantic for the date-only path — only datetime-only crosses into
    //    sub-day precision.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_dateOnly_today_isNOT_overdue()
    {
        Task *t = store->addTask(QStringLiteral("Today"), inboxId);
        t->setDueDate(QDate::currentDate()
                      .toString(QStringLiteral("yyyy-MM-dd")));

        QVERIFY2(!matches(*eval, QStringLiteral("overdue"), t),
                 "Date-only task with today's date is NOT overdue — the "
                 "filter compares calendar days. Sub-day precision only "
                 "applies to datetime-only tasks.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6) Completed task is never overdue. Pre-fix and post-fix both must
    //    honor this guard.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_completedTask_isNOT_overdue()
    {
        const QDateTime past = QDateTime::currentDateTime().addSecs(-3600);
        Task *t = store->addTask(QStringLiteral("Done overdue"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(past));
        t->setIsCompleted(true);

        QVERIFY(!matches(*eval, QStringLiteral("overdue"), t));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 7) Task with no due information at all is NOT overdue.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_noDueInfo_isNOT_overdue()
    {
        Task *t = store->addTask(QStringLiteral("Empty"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(QString());

        QVERIFY(!matches(*eval, QStringLiteral("overdue"), t));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 8) Garbage datetime string is NOT overdue (defensive — older
    //    state.json or hand-edited files may carry malformed ISO).
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_garbageDatetime_isNOT_overdue()
    {
        Task *t = store->addTask(QStringLiteral("Garbage"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(QStringLiteral("not-a-date"));

        QVERIFY(!matches(*eval, QStringLiteral("overdue"), t));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9) Consistency pin: the filter `overdue` and DataStore::overdueTasks
    //    must agree on a datetime-only-today-past task. Pre-fix they
    //    disagreed (the filter said NO, the store said YES). Post-fix
    //    both say YES.
    // ─────────────────────────────────────────────────────────────────────────
    void overdue_filterAndDataStore_agreeOnDatetimeOnlyPastInstant()
    {
        const QDateTime past = QDateTime::currentDateTime().addSecs(-1800);
        Task *t = store->addTask(QStringLiteral("Synced disagreement"), inboxId);
        t->setDueDate(QString());
        t->setDueDatetime(isoLocal(past));

        const bool filterSays = matches(*eval, QStringLiteral("overdue"), t);

        bool storeSays = false;
        for (Task *ot : store->overdueTasks()) {
            if (ot == t) { storeSays = true; break; }
        }

        QVERIFY2(filterSays == storeSays,
                 "Iter-night-17: FilterEvaluator's `overdue` and "
                 "DataStore::overdueTasks must agree on every task. "
                 "Pre-fix they disagreed on datetime-only-today-past "
                 "tasks (filter said NO, store said YES) and the user saw "
                 "the same task in the sidebar Today count but missing "
                 "from a saved filter using `overdue`.");
        QVERIFY(filterSays);
        QVERIFY(storeSays);
    }
};

QTEST_GUILESS_MAIN(TestFilterOverdueDatetimePrecision)
#include "test_filter_overdue_datetime_precision.moc"
