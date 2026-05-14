#include <QtTest>
#include <QSignalSpy>
#include <QDate>

#include "storage/datastore.h"
#include "model/task.h"
#include "viewmodels/taskviewmodel.h"
#include "nlp/recurrenceengine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create a recurring task directly in the DataStore so we have full
// control over dueIsRecurring, dueDate, dueString without going through the
// DateParser (which would overwrite humanString).
// ─────────────────────────────────────────────────────────────────────────────
static Task *addRecurringTask(DataStore *store,
                              const QString &content,
                              const QString &dueDate,   // "yyyy-MM-dd"
                              const QString &dueString, // humanString used by advanceByRecurrence
                              bool isRecurring = true)
{
    Task *task = store->addTask(content, QStringLiteral("inbox"));
    if (!task) return nullptr;
    task->setDueDate(dueDate);
    task->setDueString(dueString);
    task->setDueIsRecurring(isRecurring);
    return task;
}

class TestRecurrence : public QObject
{
    Q_OBJECT

private slots:
    // ── Data-driven: recurring advancement ───────────────────────────────────
    void advance_data()
    {
        QTest::addColumn<QString>("dueString");
        QTest::addColumn<QString>("dueDate");
        QTest::addColumn<QString>("expectedDate");

        // English rules
        QTest::newRow("every day / en")
            << QStringLiteral("Every day")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-16");

        QTest::newRow("every week / en")
            << QStringLiteral("Every week")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-22");

        QTest::newRow("every month / en")
            << QStringLiteral("Every month")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-06-15");

        QTest::newRow("every year / en")
            << QStringLiteral("Every year")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2027-05-15");

        // "every Monday" — dueDate is Wednesday 2026-05-13, next Monday = 2026-05-18
        QTest::newRow("every Monday / en")
            << QStringLiteral("Every monday")
            << QStringLiteral("2026-05-13")
            << QStringLiteral("2026-05-18");

        // every 2 weeks
        QTest::newRow("every 2 weeks / en")
            << QStringLiteral("Every 2 weeks")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-29");

        // Russian rules
        QTest::newRow("каждый день / ru")
            << QString::fromUtf8("Каждый день")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-16");

        QTest::newRow("каждую неделю / ru")
            << QString::fromUtf8("Каждую неделю")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-22");

        QTest::newRow("каждый месяц / ru")
            << QString::fromUtf8("Каждый месяц")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-06-15");

        QTest::newRow("каждый год / ru")
            << QString::fromUtf8("Каждый год")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2027-05-15");

        // "каждый понедельник" — Wednesday 2026-05-13 → next Monday 2026-05-18
        QTest::newRow("каждый понедельник / ru")
            << QString::fromUtf8("Каждый понедельник")
            << QStringLiteral("2026-05-13")
            << QStringLiteral("2026-05-18");

        // every 2 weeks Russian — humanString produced by parseRussian is
        // "Каждые 2 недели"; advanceByRecurrence regex expects "нед" prefix.
        // This row intentionally documents the expected behaviour.
        QTest::newRow("каждые 2 недели / ru")
            << QString::fromUtf8("Каждые 2 недели")
            << QStringLiteral("2026-05-15")
            << QStringLiteral("2026-05-29");
    }

    void advance()
    {
        QFETCH(QString, dueString);
        QFETCH(QString, dueDate);
        QFETCH(QString, expectedDate);

        DataStore store;
        // DataStore needs an inbox project for addTask.
        store.addProject(QStringLiteral("Inbox"));

        TaskViewModel vm(&store);
        // Put the viewmodel in a mode where it won't filter out the task
        // (use "project" mode so it can see tasks from our project).
        vm.setShowCompleted(true);

        Task *task = addRecurringTask(&store, QStringLiteral("Recurring task"),
                                      dueDate, dueString);
        QVERIFY(task != nullptr);
        const QString id = task->id();

        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);

        const bool ok = vm.completeTask(id);
        QVERIFY(ok);

        // Must have emitted taskChanged
        QVERIFY(spy.count() >= 1);

        // Task must NOT be marked completed (recurring — advances instead)
        Task *after = store.taskById(id);
        QVERIFY(after != nullptr);
        QVERIFY(!after->isCompleted());
        QCOMPARE(after->dueDate(), expectedDate);
    }

    // ── Non-recurring task: completeTask sets isCompleted = true ─────────────
    void nonRecurring_completed()
    {
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);
        vm.setShowCompleted(true);

        Task *task = addRecurringTask(&store, QStringLiteral("Normal task"),
                                      QStringLiteral("2026-05-15"),
                                      QStringLiteral("May 15"),
                                      /*isRecurring=*/false);
        QVERIFY(task != nullptr);
        const QString id  = task->id();
        const QString due = task->dueDate();

        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);
        QVERIFY(vm.completeTask(id));
        QVERIFY(spy.count() >= 1);

        Task *after = store.taskById(id);
        QVERIFY(after->isCompleted());
        // Due date must not change
        QCOMPARE(after->dueDate(), due);
    }

    // ── Recurring task with no dueDate: uses today as base, task advances ────
    void recurring_noDueDate_advancesFromToday()
    {
        // When dueDate is empty, completeTask falls back to QDate::currentDate()
        // as the base. If the rule is valid, the task advances — it is NOT marked
        // completed. The new dueDate is today+1 for "Every day".
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);
        vm.setShowCompleted(true);

        Task *task = store.addTask(QStringLiteral("No due recurring"), QStringLiteral("inbox"));
        QVERIFY(task != nullptr);
        task->setDueString(QStringLiteral("Every day"));
        task->setDueIsRecurring(true);
        // dueDate intentionally left empty

        const QString id = task->id();
        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);
        QVERIFY(vm.completeTask(id));
        QVERIFY(spy.count() >= 1);

        Task *after = store.taskById(id);
        QVERIFY(after != nullptr);
        // Task advances from today → not marked completed
        QVERIFY(!after->isCompleted());
        // New dueDate must be tomorrow
        const QString expectedDate =
            QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(after->dueDate(), expectedDate);
    }

    // ── Signal: taskChanged emitted on complete ──────────────────────────────
    void signal_taskChanged_emittedOnComplete()
    {
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);

        Task *task = addRecurringTask(&store, QStringLiteral("Signal test"),
                                      QStringLiteral("2026-05-10"),
                                      QStringLiteral("Every week"));
        const QString id = task->id();

        QSignalSpy spy(&vm, &TaskViewModel::taskChanged);
        vm.completeTask(id);

        QVERIFY(!spy.isEmpty());
        // The signal must carry the correct task id
        bool found = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toString() == id) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // ── Signal: dataChanged emitted on DataStore after recurring complete ─────
    void signal_dataChanged_emittedOnRecurringComplete()
    {
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);

        Task *task = addRecurringTask(&store, QStringLiteral("DC signal test"),
                                      QStringLiteral("2026-05-10"),
                                      QStringLiteral("Every month"));
        const QString id = task->id();

        QSignalSpy storeSpy(&store, &DataStore::dataChanged);
        vm.completeTask(id);

        QVERIFY(storeSpy.count() >= 1);
    }

    // ── Repeated completion of a recurring task advances multiple times ───────
    void recurring_multipleAdvances()
    {
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);
        vm.setShowCompleted(true);

        Task *task = addRecurringTask(&store, QStringLiteral("Multi advance"),
                                      QStringLiteral("2026-05-01"),
                                      QStringLiteral("Every day"));
        const QString id = task->id();

        vm.completeTask(id);
        QCOMPARE(store.taskById(id)->dueDate(), QStringLiteral("2026-05-02"));
        QVERIFY(!store.taskById(id)->isCompleted());

        vm.completeTask(id);
        QCOMPARE(store.taskById(id)->dueDate(), QStringLiteral("2026-05-03"));
        QVERIFY(!store.taskById(id)->isCompleted());

        vm.completeTask(id);
        QCOMPARE(store.taskById(id)->dueDate(), QStringLiteral("2026-05-04"));
    }

    // ── Unknown recurrence rule falls through to standard completion ──────────
    void unknownRule_fallsThrough()
    {
        DataStore store;
        store.addProject(QStringLiteral("Inbox"));
        TaskViewModel vm(&store);
        vm.setShowCompleted(true);

        Task *task = addRecurringTask(&store, QStringLiteral("Unknown rule"),
                                      QStringLiteral("2026-05-15"),
                                      QStringLiteral("some unknown rule"));
        const QString id = task->id();

        vm.completeTask(id);

        Task *after = store.taskById(id);
        // RecurrenceEngine::advance returns invalid date → marks completed
        QVERIFY(after->isCompleted());
        // Due date should be unchanged
        QCOMPARE(after->dueDate(), QStringLiteral("2026-05-15"));
    }

    // ── Direct RecurrenceEngine::advance unit tests ───────────────────────────

    void test_advance_every_day()
    {
        QDate from(2026, 5, 15);
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("every day"), from),
                 QDate(2026, 5, 16));
    }

    void test_advance_every_week()
    {
        QDate from(2026, 5, 15);
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("every week"), from),
                 QDate(2026, 5, 22));
    }

    void test_advance_every_monday_from_wednesday()
    {
        // Wednesday 2026-05-13 → next Monday is 2026-05-18
        QDate from(2026, 5, 13); // Wednesday
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("every monday"), from),
                 QDate(2026, 5, 18));
    }

    void test_advance_kazhduyu_nedelu()
    {
        // Russian: "каждую неделю"
        QDate from(2026, 5, 15);
        QCOMPARE(RecurrenceEngine::advance(QString::fromUtf8("каждую неделю"), from),
                 QDate(2026, 5, 22));
    }

    void test_invalid_rule_returns_invalid_date()
    {
        QVERIFY(!RecurrenceEngine::advance(
            QStringLiteral("absolute nonsense"), QDate(2026, 5, 15)).isValid());
    }

    void test_advance_every_2_weeks()
    {
        QDate from(2026, 5, 15);
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("every 2 weeks"), from),
                 QDate(2026, 5, 29));
    }

    void test_advance_kazhdye_3_dni_ru()
    {
        // Russian: "Каждые 3 дня" — every 3 days
        QDate from(2026, 5, 10);
        QCOMPARE(RecurrenceEngine::advance(QString::fromUtf8("каждые 3 дня"), from),
                 QDate(2026, 5, 13));
    }

    void test_advance_every_weekday_skips_weekend()
    {
        // Friday 2026-05-15 → skip Sat/Sun → Monday 2026-05-18
        QDate from(2026, 5, 15); // Friday
        QCOMPARE(RecurrenceEngine::advance(QStringLiteral("every weekday"), from),
                 QDate(2026, 5, 18));
    }

    void test_advance_invalid_fromDate()
    {
        QVERIFY(!RecurrenceEngine::advance(QStringLiteral("every day"), QDate()).isValid());
    }
};

QTEST_GUILESS_MAIN(TestRecurrence)
#include "test_recurrence.moc"
