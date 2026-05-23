// =============================================================================
// Iter-night-18 pin: TaskViewModel::completeTask must derive the recurrence
// base from BOTH `dueDate` AND `dueDatetime` when advancing a recurring task.
//
// Pre-fix the function consulted only `dueDate`:
//   currentDate = dueDate.isEmpty() ? today() : parse(dueDate);
//
// For a recurring task arriving via sync or import as datetime-only
// (`dueDate=""`, `dueDatetime="2026-05-25T14:00:00"`), `base` defaulted to
// today. `RecurrenceEngine::advance(rule, today)` then computed the next
// occurrence relative to "now" rather than the original due date — so a
// task completed N days late slid N days further than intended on every
// completion, drifting the recurrence schedule out of phase.
//
// Concrete: "every 3 days" task with dueDatetime = May 25, completed on
// May 27 → pre-fix advances to May 30 (today + 3), post-fix advances to
// May 28 (orig + 3).
//
// Post-fix derivation:
//   if (!dueDate.isEmpty())       → parse(dueDate)
//   else if (!dueDatetime.isEmpty()) → fromIsoString(dueDatetime).date()
//   else                          → today (fallback)
// =============================================================================

#include <QtTest>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QString>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

namespace {

// Push a fixed wall-clock-ish date string for stable assertions. We can't
// freeze QDate::currentDate() so the test sets the due date in the past by
// a known offset, then asserts the post-advance date matches "orig + N",
// not "today + N". This works regardless of when the test runs because
// we control both `orig` and the FREQ INTERVAL.

} // anonymous namespace


class TestRecurringCompleteDatetimeBase : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    TaskViewModel *vm = nullptr;
    QString inboxId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
        vm = new TaskViewModel(store);
    }

    void cleanup()
    {
        delete vm; vm = nullptr;
        delete store; store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Datetime-only recurring task: advance must use the dueDatetime's
    //    DATE component as base, not today.
    //
    //    Setup: orig = today - 2 days, rule = "every 3 days".
    //    Expected after completion: orig + 3 days = (today - 2) + 3 = today+1.
    //    Pre-fix bug: would advance from today → today + 3.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_datetimeOnly_advancesFromDtDate_notFromToday()
    {
        const QDate orig = QDate::currentDate().addDays(-2);
        const QString id = vm->addTask(QStringLiteral("Datetime-only every 3 days"),
                                       inboxId, 0, QString(), QStringList());
        QVERIFY(!id.isEmpty());
        Task *t = store->taskById(id);
        QVERIFY(t);

        // Datetime-only shape: dueDate empty, dueDatetime set with date+time.
        t->setDueDate(QString());
        t->setDueDatetime(QDateTime(orig, QTime(14, 0)).toString(Qt::ISODate));
        t->setDueString(QStringLiteral("every 3 days at 14:00"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=3"));
        t->setDueIsRecurring(true);

        QVERIFY(vm->completeTask(id));
        QVERIFY2(!t->isCompleted(),
                 "Recurring advance must NOT mark the task completed — it "
                 "rolls the due date forward instead.");

        // Post-fix expected next due = orig + 3 = (today - 2) + 3 = today + 1.
        const QDate expectedNext = orig.addDays(3);
        const QString actualStr = t->dueDate();
        QVERIFY(!actualStr.isEmpty());
        const QDate actual = QDate::fromString(actualStr,
                                                QStringLiteral("yyyy-MM-dd"));
        QVERIFY2(actual == expectedNext,
                 qPrintable(QStringLiteral(
                     "Iter-night-18: expected next due %1 (orig + 3), got "
                     "%2. Pre-fix the function defaulted base to today and "
                     "advanced from today instead of the original "
                     "dueDatetime — every late completion drifted the "
                     "recurrence one extra day forward.")
                     .arg(expectedNext.toString(Qt::ISODate),
                          actual.toString(Qt::ISODate))));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Date-only recurring task (the pre-fix happy path) — must still
    //    advance from dueDate. Regression guard.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_dateOnly_advancesFromDueDate()
    {
        const QDate orig = QDate::currentDate().addDays(-5);
        const QString id = vm->addTask(QStringLiteral("Date-only every 7 days"),
                                       inboxId, 0, QString(), QStringList());
        Task *t = store->taskById(id);
        t->setDueDate(orig.toString(QStringLiteral("yyyy-MM-dd")));
        t->setDueDatetime(QString());
        t->setDueString(QStringLiteral("every 7 days"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=7"));
        t->setDueIsRecurring(true);

        QVERIFY(vm->completeTask(id));
        const QDate expected = orig.addDays(7);
        QCOMPARE(t->dueDate(), expected.toString(QStringLiteral("yyyy-MM-dd")));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Time-of-day preserved across the advance — was already correct
    //    pre-fix via line ~411 (newDt(nextDate, old.time())). Pin so the
    //    iter-night-18 refactor of `base` derivation doesn't accidentally
    //    drop time preservation.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_datetimeOnly_preservesTimeOfDay()
    {
        const QDate orig = QDate::currentDate().addDays(-1);
        const QTime origTime(17, 45);

        const QString id = vm->addTask(QStringLiteral("DT task"), inboxId,
                                       0, QString(), QStringList());
        Task *t = store->taskById(id);
        t->setDueDate(QString());
        t->setDueDatetime(QDateTime(orig, origTime).toString(Qt::ISODate));
        t->setDueString(QStringLiteral("every 2 days at 17:45"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=2"));
        t->setDueIsRecurring(true);

        QVERIFY(vm->completeTask(id));

        const QDateTime dt = QDateTime::fromString(t->dueDatetime(),
                                                    Qt::ISODate);
        QVERIFY(dt.isValid());
        QCOMPARE(dt.time(), origTime);
        QCOMPARE(dt.date(), orig.addDays(2));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Neither dueDate nor dueDatetime → still falls back to today as
    //    base. Regression guard for the empty-due edge.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_emptyDueButRecurring_fallsBackToToday()
    {
        const QString id = vm->addTask(QStringLiteral("No-due recurring"),
                                       inboxId, 0, QString(), QStringList());
        Task *t = store->taskById(id);
        t->setDueDate(QString());
        t->setDueDatetime(QString());
        t->setDueString(QStringLiteral("every 5 days"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=5"));
        t->setDueIsRecurring(true);

        QVERIFY(vm->completeTask(id));
        const QString out = t->dueDate();
        if (out.isEmpty()) {
            // Some advance paths don't write dueDate when input had none.
            // Either way the task must NOT be completed.
            QVERIFY(!t->isCompleted());
            return;
        }
        const QDate expected = QDate::currentDate().addDays(5);
        QCOMPARE(out, expected.toString(QStringLiteral("yyyy-MM-dd")));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) Garbage dueDatetime string → fall back to today. Defensive.
    // ─────────────────────────────────────────────────────────────────────────
    void completeTask_garbageDueDatetimeRecurring_fallsBackToToday()
    {
        const QString id = vm->addTask(QStringLiteral("Garbage dt"),
                                       inboxId, 0, QString(), QStringList());
        Task *t = store->taskById(id);
        t->setDueDate(QString());
        t->setDueDatetime(QStringLiteral("not-an-iso-string"));
        t->setDueString(QStringLiteral("every 4 days"));
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=4"));
        t->setDueIsRecurring(true);

        QVERIFY(vm->completeTask(id));
        const QString out = t->dueDate();
        if (!out.isEmpty()) {
            const QDate expected = QDate::currentDate().addDays(4);
            QCOMPARE(out, expected.toString(QStringLiteral("yyyy-MM-dd")));
        }
    }
};

QTEST_GUILESS_MAIN(TestRecurringCompleteDatetimeBase)
#include "test_recurring_complete_datetime_base.moc"
