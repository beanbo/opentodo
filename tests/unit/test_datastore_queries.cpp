#include <QtTest>
#include <QDate>
#include <QDateTime>
#include <QString>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/comment.h"
#include "utils/datetime.h"

/**
 * Edge-case tests for DataStore query methods.
 *
 * Focus: datetime-only tasks, midnight boundaries, soft-deleted handling,
 * search edge cases (case, unicode, whitespace, empty), and the
 * exclude-deleted contract on every query.
 *
 * Note: the API method for child-task lookup is `subtasks(parentId)`
 * (not `subtasksOf`), as confirmed in datastore.h.
 */
class TestDataStoreQueries : public QObject
{
    Q_OBJECT

private:
    // -- Fixture handles --------------------------------------------------
    DataStore *m_ds = nullptr;
    Project   *m_inbox = nullptr;
    Project   *m_p1    = nullptr;
    Project   *m_p2    = nullptr;

    Task *m_taskTodayDate     = nullptr; // dueDate == today
    Task *m_taskTodayDatetime = nullptr; // dueDatetime today, dueDate empty
    Task *m_taskTomorrow      = nullptr; // dueDate == tomorrow
    Task *m_taskYesterday     = nullptr; // dueDate == yesterday (overdue)
    Task *m_taskNoDue         = nullptr; // no dueDate, no dueDatetime
    Task *m_taskDeleted       = nullptr; // soft-deleted
    Task *m_taskCompleted     = nullptr; // completed
    Task *m_taskParent        = nullptr; // has subtask
    Task *m_taskSubtask       = nullptr; // child of m_taskParent

    Comment *m_commentA1 = nullptr; // on m_taskParent
    Comment *m_commentA2 = nullptr; // on m_taskParent — will be deleted
    Comment *m_commentB  = nullptr; // on m_taskTodayDate

    // -- Fixture builder --------------------------------------------------
    void buildFixture()
    {
        m_ds = new DataStore(this);

        // Inbox so addTask("", "") works.
        m_inbox = new Project(m_ds);
        m_inbox->setName(QStringLiteral("Inbox"));
        m_inbox->setIsInbox(true);
        m_ds->addExistingProject(m_inbox);

        m_p1 = m_ds->addProject(QStringLiteral("Project One"));
        m_p2 = m_ds->addProject(QStringLiteral("Project Two"));

        const QDate today     = QDate::currentDate();
        const QDate tomorrow  = today.addDays(1);
        const QDate yesterday = today.addDays(-1);

        // 1) dueDate == today (string format yyyy-MM-dd)
        m_taskTodayDate = m_ds->addTask(QStringLiteral("Buy milk today"), m_p1->id());
        m_taskTodayDate->setDueDate(DateTimeHelper::toDateString(today));

        // 2) dueDatetime today, dueDate empty (datetime-only)
        m_taskTodayDatetime = m_ds->addTask(QStringLiteral("Call mom"), m_p1->id());
        QDateTime dtToday(today, QTime(15, 30));
        m_taskTodayDatetime->setDueDatetime(DateTimeHelper::toIsoString(dtToday));

        // 3) Due tomorrow — must NOT appear in tasksForToday()
        m_taskTomorrow = m_ds->addTask(QStringLiteral("Tomorrow task"), m_p2->id());
        m_taskTomorrow->setDueDate(DateTimeHelper::toDateString(tomorrow));

        // 4) Yesterday (overdue) — documents that tasksForToday() does NOT
        //    include overdue tasks (only those whose dueDate == today).
        m_taskYesterday = m_ds->addTask(QStringLiteral("Forgotten yesterday"), m_p2->id());
        m_taskYesterday->setDueDate(DateTimeHelper::toDateString(yesterday));

        // 5) No due at all
        m_taskNoDue = m_ds->addTask(QStringLiteral("Someday maybe"), m_p1->id());

        // 6) Soft-deleted task (was due today before delete)
        m_taskDeleted = m_ds->addTask(QStringLiteral("Tombstoned today"), m_p1->id());
        m_taskDeleted->setDueDate(DateTimeHelper::toDateString(today));
        m_ds->removeTask(m_taskDeleted->id());

        // 7) Completed task
        m_taskCompleted = m_ds->addTask(QStringLiteral("Already done"), m_p1->id());
        m_taskCompleted->setDueDate(DateTimeHelper::toDateString(today));
        m_taskCompleted->setIsCompleted(true);
        m_taskCompleted->setCompletedAt(QDateTime::currentDateTimeUtc());

        // 8) Parent + subtask
        m_taskParent = m_ds->addTask(QStringLiteral("Parent task"), m_p1->id());
        m_taskSubtask = m_ds->addTask(QStringLiteral("Child task"), m_p1->id());
        m_taskSubtask->setParentId(m_taskParent->id());

        // Comments — two on m_taskParent, one on m_taskTodayDate.
        m_commentA1 = m_ds->addComment(QStringLiteral("First note"),
                                       m_taskParent->id(), QString());
        m_commentA2 = m_ds->addComment(QStringLiteral("To-be-deleted note"),
                                       m_taskParent->id(), QString());
        m_commentB  = m_ds->addComment(QStringLiteral("Other note"),
                                       m_taskTodayDate->id(), QString());

        // Delete one comment so commentsForTask must filter tombstones.
        m_ds->removeComment(m_commentA2->id());
    }

    void resetFixture()
    {
        if (m_ds) { delete m_ds; m_ds = nullptr; }
        m_inbox = nullptr; m_p1 = nullptr; m_p2 = nullptr;
        m_taskTodayDate = m_taskTodayDatetime = m_taskTomorrow = nullptr;
        m_taskYesterday = m_taskNoDue = m_taskDeleted = nullptr;
        m_taskCompleted = m_taskParent = m_taskSubtask = nullptr;
        m_commentA1 = m_commentA2 = m_commentB = nullptr;
    }

    static bool listContains(const QList<Task *> &list, const Task *needle)
    {
        for (const Task *t : list) if (t == needle) return true;
        return false;
    }

    static bool listContains(const QList<Comment *> &list, const Comment *needle)
    {
        for (const Comment *c : list) if (c == needle) return true;
        return false;
    }

private slots:
    void init()    { buildFixture();  }
    void cleanup() { resetFixture();  }

    // ---------------- tasksForToday() ----------------------------------------

    void tasksForToday_includes_dueDate_today()
    {
        const auto list = m_ds->tasksForToday();
        QVERIFY2(listContains(list, m_taskTodayDate),
                 "Task with dueDate == today must appear in tasksForToday().");
    }

    void tasksForToday_includes_datetime_only_today()
    {
        // Documented in DataStore::tasksForToday (B-1.30): include datetime-only
        // tasks whose date component matches today.
        const auto list = m_ds->tasksForToday();
        QVERIFY2(listContains(list, m_taskTodayDatetime),
                 "Datetime-only task with date == today must appear in tasksForToday().");
    }

    void tasksForToday_excludes_completed()
    {
        const auto list = m_ds->tasksForToday();
        QVERIFY2(!listContains(list, m_taskCompleted),
                 "Completed tasks must be excluded from tasksForToday().");
    }

    void tasksForToday_excludes_deleted()
    {
        const auto list = m_ds->tasksForToday();
        QVERIFY2(!listContains(list, m_taskDeleted),
                 "Soft-deleted tasks must be excluded from tasksForToday().");
    }

    void tasksForToday_excludes_tomorrow()
    {
        const auto list = m_ds->tasksForToday();
        QVERIFY2(!listContains(list, m_taskTomorrow),
                 "Tasks due tomorrow must NOT appear in tasksForToday().");
    }

    void tasksForToday_excludes_no_due()
    {
        const auto list = m_ds->tasksForToday();
        QVERIFY2(!listContains(list, m_taskNoDue),
                 "Tasks with no due date must NOT appear in tasksForToday().");
    }

    void tasksForToday_excludes_overdue()
    {
        // Documents the behaviour: tasksForToday() filters strictly by
        // "dueDate == today" — overdue (yesterday) tasks belong to
        // overdueTasks(), not here.
        const auto list = m_ds->tasksForToday();
        QVERIFY2(!listContains(list, m_taskYesterday),
                 "Overdue tasks must NOT be promoted into tasksForToday().");

        // Sanity: they DO appear in overdueTasks().
        const auto overdue = m_ds->overdueTasks();
        QVERIFY2(listContains(overdue, m_taskYesterday),
                 "Overdue task must be returned by overdueTasks().");
    }

    // ---------------- tasksForProject(projectId) -----------------------------

    void tasksForProject_returns_only_top_level_under_project()
    {
        // tasksForProject filters by projectId AND parentId.isEmpty() — i.e.
        // it returns top-level tasks only, NOT subtasks. This is the
        // documented behaviour we lock in here.
        const auto p1Tasks = m_ds->tasksForProject(m_p1->id());

        QVERIFY(listContains(p1Tasks, m_taskTodayDate));
        QVERIFY(listContains(p1Tasks, m_taskTodayDatetime));
        QVERIFY(listContains(p1Tasks, m_taskNoDue));
        QVERIFY(listContains(p1Tasks, m_taskCompleted));
        QVERIFY(listContains(p1Tasks, m_taskParent));

        // The subtask, although under p1, must NOT appear here.
        QVERIFY2(!listContains(p1Tasks, m_taskSubtask),
                 "tasksForProject() must NOT return subtasks (top-level only).");
    }

    void tasksForProject_excludes_deleted()
    {
        const auto p1Tasks = m_ds->tasksForProject(m_p1->id());
        QVERIFY2(!listContains(p1Tasks, m_taskDeleted),
                 "tasksForProject() must exclude soft-deleted tasks.");
    }

    void tasksForProject_unknown_id_returns_empty()
    {
        QCOMPARE(m_ds->tasksForProject(QStringLiteral("does-not-exist")).size(), 0);
    }

    void tasksForProject_empty_id_returns_empty()
    {
        // No task has projectId == "" once an inbox exists, so empty id should
        // return an empty list (not "all tasks").
        QCOMPARE(m_ds->tasksForProject(QString()).size(), 0);
    }

    // ---------------- completedTasks() ---------------------------------------

    void completedTasks_returns_only_completed()
    {
        const auto list = m_ds->completedTasks();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first(), m_taskCompleted);
    }

    void completedTasks_excludes_deleted()
    {
        // Mark the parent task completed AND deleted — it must not appear.
        m_taskParent->setIsCompleted(true);
        m_taskParent->setCompletedAt(QDateTime::currentDateTimeUtc());
        m_ds->removeTask(m_taskParent->id());

        const auto list = m_ds->completedTasks();
        QVERIFY2(!listContains(list, m_taskParent),
                 "completedTasks() must exclude soft-deleted tasks even when completed.");
        // m_taskCompleted still there.
        QVERIFY(listContains(list, m_taskCompleted));
    }

    void completedTasks_returns_each_completed_task_once()
    {
        // Add a second completed task and verify both come back.
        Task *another = m_ds->addTask(QStringLiteral("Also done"), m_p2->id());
        another->setIsCompleted(true);
        another->setCompletedAt(QDateTime::currentDateTimeUtc().addSecs(-3600));

        const auto list = m_ds->completedTasks();
        QCOMPARE(list.size(), 2);
        QVERIFY(listContains(list, m_taskCompleted));
        QVERIFY(listContains(list, another));
        // The current implementation does NOT sort by completedAt — it iterates
        // m_tasks in insertion order. We only assert membership, not order.
    }

    // ---------------- searchTasks(query) -------------------------------------

    void searchTasks_matches_substring()
    {
        const auto list = m_ds->searchTasks(QStringLiteral("milk"));
        QVERIFY(listContains(list, m_taskTodayDate)); // "Buy milk today"
    }

    void searchTasks_is_case_insensitive()
    {
        const auto upper = m_ds->searchTasks(QStringLiteral("MILK"));
        const auto mixed = m_ds->searchTasks(QStringLiteral("Milk"));
        QVERIFY(listContains(upper, m_taskTodayDate));
        QVERIFY(listContains(mixed, m_taskTodayDate));
    }

    void searchTasks_matches_description()
    {
        m_taskNoDue->setDescription(QStringLiteral("Hidden keyword: rutabaga"));
        const auto list = m_ds->searchTasks(QStringLiteral("rutabaga"));
        QVERIFY2(listContains(list, m_taskNoDue),
                 "searchTasks() must match against task description, not only content.");
    }

    void searchTasks_excludes_deleted()
    {
        // m_taskDeleted's content was "Tombstoned today" — it must not match.
        const auto list = m_ds->searchTasks(QStringLiteral("Tombstoned"));
        QVERIFY2(!listContains(list, m_taskDeleted),
                 "searchTasks() must exclude soft-deleted tasks.");
        QCOMPARE(list.size(), 0);
    }

    void searchTasks_empty_query_returns_empty()
    {
        // Documents the behaviour: empty query short-circuits to empty list,
        // not "all tasks".
        QCOMPARE(m_ds->searchTasks(QString()).size(), 0);
    }

    void searchTasks_whitespace_only_query()
    {
        // A whitespace string is not empty, so it bypasses the empty-query
        // short-circuit and falls into a substring search for " ". Most task
        // contents contain a space, so this typically returns many results.
        // We assert: result is a strict subset of allTasks() (no tombstones).
        const auto list = m_ds->searchTasks(QStringLiteral("   "));
        for (const Task *t : list) {
            QVERIFY2(!t->isDeleted(),
                     "searchTasks() must never include tombstones, even for "
                     "a whitespace-only query.");
        }
    }

    void searchTasks_unicode_query()
    {
        // Add a task with Cyrillic content and search for it.
        Task *cyr = m_ds->addTask(QString::fromUtf8("\xD0\xA1\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB4\xD0\xBE\xD0\xBC\xD0\xB0\xD1\x88\xD0\xBA\xD1\x83"),
                                  m_p1->id()); // "Сделать домашку"
        const QString query = QString::fromUtf8("\xD0\xB4\xD0\xBE\xD0\xBC\xD0\xB0\xD1\x88"); // "домаш"
        const auto list = m_ds->searchTasks(query);
        QVERIFY2(listContains(list, cyr),
                 "searchTasks() must support unicode (Cyrillic) substring matching.");
    }

    // ---------------- allTasks() vs allTasksIncludingDeleted() ---------------

    void allTasks_excludes_deleted()
    {
        const auto live = m_ds->allTasks();
        QVERIFY(!listContains(live, m_taskDeleted));
    }

    void allTasksIncludingDeleted_includes_deleted()
    {
        const auto everything = m_ds->allTasksIncludingDeleted();
        QVERIFY2(listContains(everything, m_taskDeleted),
                 "allTasksIncludingDeleted() must surface soft-deleted tasks.");
    }

    void allTasks_count_smaller_than_includingDeleted_when_tombstones_exist()
    {
        const int live = m_ds->allTasks().size();
        const int all  = m_ds->allTasksIncludingDeleted().size();
        QVERIFY2(all > live,
                 "When tombstones exist, allTasksIncludingDeleted() must "
                 "have strictly more entries than allTasks().");
        QCOMPARE(all - live, 1); // exactly one task was deleted in fixture
    }

    void allTasks_count_equals_includingDeleted_when_no_tombstones()
    {
        // Build a fresh, tombstone-free store.
        DataStore ds;
        Project *inbox = new Project(&ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds.addExistingProject(inbox);

        ds.addTask(QStringLiteral("A"), QString());
        ds.addTask(QStringLiteral("B"), QString());
        ds.addTask(QStringLiteral("C"), QString());

        QCOMPARE(ds.allTasks().size(), ds.allTasksIncludingDeleted().size());
    }

    // ---------------- subtasks(parentId) -------------------------------------

    void subtasks_returns_direct_children()
    {
        const auto kids = m_ds->subtasks(m_taskParent->id());
        QCOMPARE(kids.size(), 1);
        QCOMPARE(kids.first(), m_taskSubtask);
    }

    void subtasks_excludes_deleted_children()
    {
        // Add another subtask and immediately tombstone it.
        Task *gone = m_ds->addTask(QStringLiteral("Doomed child"), m_p1->id());
        gone->setParentId(m_taskParent->id());
        m_ds->removeTask(gone->id());

        const auto kids = m_ds->subtasks(m_taskParent->id());
        QCOMPARE(kids.size(), 1);
        QVERIFY(!listContains(kids, gone));
        QVERIFY(listContains(kids, m_taskSubtask));
    }

    void subtasks_empty_when_no_children()
    {
        QCOMPARE(m_ds->subtasks(m_taskNoDue->id()).size(), 0);
        QCOMPARE(m_ds->subtasks(QStringLiteral("nonexistent-parent")).size(), 0);
    }

    // ---------------- commentsForTask(taskId) --------------------------------

    void commentsForTask_returns_tied_comments()
    {
        const auto list = m_ds->commentsForTask(m_taskParent->id());
        QCOMPARE(list.size(), 1);                       // m_commentA2 tombstoned
        QCOMPARE(list.first(), m_commentA1);
    }

    void commentsForTask_excludes_deleted_comments()
    {
        // Verify directly: m_commentA2 is deleted in fixture.
        QVERIFY(m_commentA2->isDeleted());
        const auto list = m_ds->commentsForTask(m_taskParent->id());
        QVERIFY(!listContains(list, m_commentA2));
    }

    void commentsForTask_unknown_taskId_returns_empty()
    {
        QCOMPARE(m_ds->commentsForTask(QStringLiteral("ghost-task")).size(), 0);
    }

    void commentsForTask_only_returns_comments_for_that_task()
    {
        const auto listA = m_ds->commentsForTask(m_taskParent->id());
        const auto listB = m_ds->commentsForTask(m_taskTodayDate->id());

        QVERIFY(!listContains(listA, m_commentB));
        QVERIFY( listContains(listB, m_commentB));
    }
};

QTEST_GUILESS_MAIN(TestDataStoreQueries)
#include "test_datastore_queries.moc"
