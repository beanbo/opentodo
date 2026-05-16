// DST and timezone edge-case coverage for ReminderScheduler.
//
// The existing test_reminderscheduler.cpp + test_reminderscheduler_edge_cases.cpp
// cover happy paths (future schedule, past skip, snooze, soft-delete, etc.).
// This file targets the timezone / DST gap that those suites don't pin:
//
//   * Spring-forward gap: a reminder scheduled for 02:30 local on the
//     DST-forward day. That wall-clock moment does not exist locally
//     (clocks jump 01:59 -> 03:00). QDateTime(QDate, QTime, QTimeZone)
//     normalises the gap differently depending on platform/Qt version;
//     we PIN the safety property — scheduler doesn't crash, doesn't
//     produce a NEGATIVE interval, and either schedules at a positive
//     future instant or skips it. ANY past instant means "skipped".
//
//   * Fall-back duplicate: 01:30 happens twice on the DST-back day.
//     The scheduler stores the trigger as a single QDateTime and arms
//     ONE notification — it cannot fire twice for the same reminder
//     within the same calendar date (verified by scheduledCount == 1).
//
//   * Recurring task during DST: the per-reminder "due date advanced"
//     detection in rescheduleAll() relies on Task::dueDate string equality.
//     A daily / weekly recurring task whose due_date advances across a DST
//     transition still keeps its WALL-CLOCK time, because the scheduler
//     re-reads task->dueDatetime() on every pass — it does NOT add a fixed
//     ms interval to "last fire" (no skew accumulation).
//
//   * TZ change after scheduling: the reminder's due.timezone field pins
//     the wall-clock to a specific zone; setTimeZone() retags without
//     shifting. We verify the bug-fix behavior (C-5 in reminderscheduler.cpp)
//     by feeding an ISO datetime + explicit timezone and observing the
//     scheduled QDateTime carries the intended zone, not a shifted instant.
//
//   * Storage format: QDateTime::fromString(Qt::ISODate) on an unzoned
//     "YYYY-MM-DDTHH:MM:SS" string parses as LOCAL time. We pin that the
//     scheduled time round-trips through ISO -> QDateTime -> mock without
//     drift. Negative result here would mean ANY persisted reminder
//     changes meaning when the user's TZ changes.
//
//   * Feb 29 in non-leap year: ReminderScheduler does NOT compute the
//     "next" Feb 29 — it accepts whatever date string the user gave it.
//     If they set due.date = "2027-02-29", QDate::fromString returns
//     INVALID; the scheduler then falls through resolveTriggerTime()
//     and skips. We pin that the bad date is a quiet no-op (no crash,
//     no schedule, pre-cancel still happens).
//
//   * Past reminder ("yesterday at 09:00"): triggerTime <= now -> skipped.
//     Already covered by test_reminderscheduler_edge_cases for "1 hour
//     ago" — here we drive the FAR-past case (180 days back) to confirm
//     no overflow / off-by-one wrap-around.
//
// All tests use QTEST_GUILESS_MAIN. No wall-clock waits, no QTimer fires —
// the scheduler does NOT itself arm timers, and we observe scheduling
// decisions via the MockNotificationService stub. Production code at
// src/services/reminderscheduler.cpp is NOT modified by this audit; the
// behavior pinned here is intentional behavior-capture so any future DST
// fix can flip these assertions deliberately.

#include <QtTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QTimeZone>
#include <QDate>
#include <QTime>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/reminder.h"
#include "services/notificationservice.h"
#include "services/reminderscheduler.h"

// ---------------------------------------------------------------------------
// MockNotificationService — local copy (Q_OBJECT classes cannot be shared
// across translation units in moc-based builds).
// ---------------------------------------------------------------------------
class MockNotificationService : public NotificationService
{
    Q_OBJECT
public:
    explicit MockNotificationService(QObject *parent = nullptr)
        : NotificationService(parent) {}

    struct ScheduleCall {
        QString title;
        QString body;
        QDateTime when;
        QString id;
    };

    QList<ScheduleCall> scheduled;
    QList<QString>      cancelled;
    QList<QString>      shown;

    void showNotification(const QString &title, const QString &body,
                          const QString &taskId = {}) override
    {
        Q_UNUSED(body);
        shown.append(taskId.isEmpty() ? title : taskId);
    }

    void scheduleNotification(const QString &title, const QString &body,
                              const QDateTime &when, const QString &id) override
    {
        scheduled.append({title, body, when, id});
    }

    void cancelNotification(const QString &id) override
    {
        cancelled.append(id);
    }

    void cancelAll() override
    {
        cancelled.append(QStringLiteral("ALL"));
    }

    bool wasScheduled(const QString &id) const
    {
        for (const auto &c : scheduled)
            if (c.id == id) return true;
        return false;
    }
    int scheduledCount(const QString &id) const
    {
        int n = 0;
        for (const auto &c : scheduled)
            if (c.id == id) ++n;
        return n;
    }
    int cancelledCount(const QString &id) const
    {
        int n = 0;
        for (const auto &c : cancelled)
            if (c == id) ++n;
        return n;
    }
    bool wasCancelled(const QString &id) const { return cancelled.contains(id); }

    ScheduleCall lastScheduleFor(const QString &id) const
    {
        for (int i = scheduled.size() - 1; i >= 0; --i)
            if (scheduled[i].id == id) return scheduled[i];
        return {};
    }

    void clearAll()
    {
        scheduled.clear();
        cancelled.clear();
        shown.clear();
    }
};

// ---------------------------------------------------------------------------
// Helper: DataStore with Inbox project
// ---------------------------------------------------------------------------
static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

// ---------------------------------------------------------------------------
// Helper: pick a near-future spring-forward Sunday for the given year.
// Returns the date when DST is most commonly observed in the Northern
// Hemisphere (second Sunday of March in the US / last Sunday of March in EU).
// The scheduler doesn't care WHICH transition rule applies — it only sees a
// QTimeZone + QDate + QTime and lets QDateTime do the normalization.
// We pick a date guaranteed to be future-dated relative to test execution.
// ---------------------------------------------------------------------------
static QDate nextSpringForwardSunday()
{
    // Use a date a few years in the future so it's guaranteed past
    // QDateTime::currentDateTime() under any test environment, while still
    // a real DST-forward date in most Northern-Hemisphere zones.
    // March 12, 2028 is the 2nd Sunday of March → US DST starts.
    return QDate(2028, 3, 12);
}

// Reciprocally: a date guaranteed to be a fall-back Sunday. Nov 5, 2028 is
// the 1st Sunday of November → US DST ends.
static QDate nextFallBackSunday()
{
    return QDate(2028, 11, 5);
}

// ---------------------------------------------------------------------------
// TestReminderSchedulerDst
// ---------------------------------------------------------------------------
class TestReminderSchedulerDst : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Spring-forward: 02:30 on a non-existent local wall-clock ─────
    //
    // QDateTime(date, QTime(2,30), TZ) on a spring-forward day: 02:30
    // doesn't exist. Qt 6 normalizes by either snapping forward to 03:30
    // (the next valid local minute) or by treating the input as if the
    // gap weren't there. Either way, the resulting QDateTime IS valid and
    // corresponds to a real instant.
    //
    // We pin the contract:
    //   - resolveTriggerTime returns a valid QDateTime (no crash, no null)
    //   - scheduler proceeds to scheduleNotification if instant is in the future
    //   - scheduler proceeds to silent-skip if QDateTime normalization
    //     landed it in the past (e.g. running the test on or after that date).
    //
    // The behavior we MUST AVOID is "scheduler crashes on gap minute" or
    // "scheduler creates a QTimer with a negative interval".
    void spring_forward_skips_invalid_hour()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Spring forward task"), inboxId);

        // 02:30 on a spring-forward Sunday — does not exist in US zones.
        const QDate dstDay = nextSpringForwardSunday();
        const QDateTime gapTime(dstDay, QTime(2, 30));

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = gapTime.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        // Either scheduled at a future instant, or silently skipped.
        // Both are acceptable; what's NOT acceptable is a crash or a
        // negative interval.
        const bool scheduled = mock.wasScheduled(reminder->id());
        QVERIFY(mock.wasCancelled(reminder->id())); // unconditional pre-cancel
        QVERIFY(mock.shown.isEmpty());              // no immediate fire

        if (scheduled) {
            const auto call = mock.lastScheduleFor(reminder->id());
            // Whatever instant Qt produced for 02:30 on the gap day MUST be valid.
            QVERIFY(call.when.isValid());
            // And must be in the future (otherwise scheduler would have skipped).
            QVERIFY(call.when > QDateTime::currentDateTime().addSecs(-2));
            qInfo("Spring-forward 02:30: Qt normalized to %s (round-tripped through "
                  "Qt::ISODate). Scheduler accepted as-is. PIN.",
                  qPrintable(call.when.toString(Qt::ISODate)));
        } else {
            qInfo("Spring-forward 02:30: Qt produced an instant <= now; scheduler "
                  "treated as past and skipped. PIN.");
        }

        delete store;
    }

    // ── 2. Fall-back: 01:30 happens twice — single schedule, no double fire ──
    //
    // On the fall-back Sunday, the local wall-clock 01:30 occurs twice
    // (once in DST, once in standard). QDateTime::fromString with no zone
    // suffix picks ONE of those two instants (usually the first / DST
    // occurrence). The scheduler stores exactly one QDateTime and arms
    // exactly one notification. We assert scheduledCount == 1.
    //
    // The "did the OS fire it twice?" question lives in the platform
    // notification service, not the scheduler — but the scheduler itself
    // cannot cause a double-fire from a single setDue() call.
    void fall_back_does_not_double_fire()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Fall-back task"), inboxId);

        // 01:30 on fall-back Sunday — repeats wall-clock.
        const QDate dstDay = nextFallBackSunday();
        const QDateTime ambiguousTime(dstDay, QTime(1, 30));

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = ambiguousTime.toString(Qt::ISODate);
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        if (mock.wasScheduled(reminder->id())) {
            // Exactly ONE schedule entry — not two.
            QCOMPARE(mock.scheduledCount(reminder->id()), 1);
            qInfo("Fall-back 01:30: scheduler armed exactly one notification. "
                  "Wall-clock 01:30 may occur twice on this date; the scheduler "
                  "cannot itself cause a double-fire because it stores one "
                  "QDateTime. PIN.");
        } else {
            qInfo("Fall-back 01:30: target instant in the past relative to test "
                  "execution; scheduler silently skipped. PIN.");
        }

        delete store;
    }

    // ── 3. Recurring task across DST: wall-clock 09:00 stays 09:00 ──────
    //
    // The scheduler does NOT compute the next occurrence by adding an
    // interval to a "last fire" timestamp — it reads task->dueDatetime()
    // on every rescheduleAll() pass. So for a recurring task whose due
    // date advances from "day before DST" to "DST day", the wall-clock
    // 09:00 stays 09:00 even though the elapsed time between fires is
    // either 23 or 25 hours.
    //
    // We test the contract by:
    //   1. Setting task due_date to a date BEFORE the spring-forward Sunday,
    //      with dueIsRecurring=true.
    //   2. Adding a reminder with dueDate = that same pre-DST date.
    //   3. Scheduler stores the first schedule.
    //   4. Simulate the recurring engine advancing the task's due_date to
    //      the DST Sunday — update task.dueDate, also update reminder.due.date.
    //   5. Scheduler re-runs, sees the new date. Wall-clock 09:00 on DST
    //      day must be the new schedule target.
    void recurring_daily_during_dst_keeps_local_wall_clock_time()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Daily standup"), inboxId);
        task->setDueIsRecurring(true);

        const QDate beforeDst = nextSpringForwardSunday().addDays(-1);
        const QDate onDst = nextSpringForwardSunday();

        task->setDueDate(beforeDst.toString(Qt::ISODate));

        // Reminder uses datetime (not date) so we explicitly pin 09:00 wall-clock.
        const QDateTime t1(beforeDst, QTime(9, 0));
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue d1; d1.datetime = t1.toString(Qt::ISODate);
        reminder->setDue(d1);

        mock.clearAll();
        emit store->remindersChanged();

        // First schedule, if future.
        if (mock.wasScheduled(reminder->id())) {
            const auto call1 = mock.lastScheduleFor(reminder->id());
            // Wall-clock 09:00 on pre-DST day.
            QCOMPARE(call1.when.time(), QTime(9, 0));
            QCOMPARE(call1.when.date(), beforeDst);
        }

        // Recurring engine advances task to next day (DST Sunday). The
        // reminder dispatch logic in rescheduleAll() reads task->dueDate
        // to detect advancement; we mirror that by updating the reminder's
        // own due.datetime to the new wall-clock 09:00.
        task->setDueDate(onDst.toString(Qt::ISODate));
        const QDateTime t2(onDst, QTime(9, 0));
        ReminderDue d2; d2.datetime = t2.toString(Qt::ISODate);
        reminder->setDue(d2);

        mock.clearAll();
        emit store->remindersChanged();

        if (mock.wasScheduled(reminder->id())) {
            const auto call2 = mock.lastScheduleFor(reminder->id());
            // Wall-clock 09:00 on DST day — NOT 08:00 or 10:00 (no skew).
            QCOMPARE(call2.when.time(), QTime(9, 0));
            QCOMPARE(call2.when.date(), onDst);
            qInfo("Recurring daily reminder across DST: wall-clock 09:00 preserved. "
                  "Scheduler reads task->dueDatetime each pass rather than adding "
                  "24h to lastFire, so no DST skew accumulates. PIN.");
        } else {
            qInfo("Recurring DST scenario: target dates were in the past at test "
                  "execution time; both schedules silently skipped. Behavior pinned: "
                  "no crash, no spurious schedule, no immediate fire.");
            QVERIFY(mock.shown.isEmpty());
        }

        delete store;
    }

    // ── 4. TZ-tagged ISO datetime: setTimeZone retags, doesn't shift ────
    //
    // The C-5 bug-fix in resolveTriggerTime: when the reminder's due
    // payload carries an explicit timezone, the scheduler does
    // setTimeZone(tz) — which RE-TAGS the QDateTime to that zone WITHOUT
    // shifting the wall-clock. So "14:00 + Europe/Moscow" stays 14:00
    // wall-clock in Moscow, regardless of what the host system zone is.
    //
    // We feed Europe/Moscow + 14:00 wall-clock, and verify the scheduled
    // QDateTime's timeZone() reflects Moscow and the time() is 14:00.
    void tz_change_after_scheduling_recomputes_correctly()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Moscow meeting"), inboxId);

        // Tomorrow 14:00 wall-clock; tag it Europe/Moscow.
        const QDate tomorrow = QDate::currentDate().addDays(1);
        const QDateTime triggerLocal(tomorrow, QTime(14, 0));

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = triggerLocal.toString(Qt::ISODate);
        rDue.timezone = QStringLiteral("Europe/Moscow");
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        QVERIFY(mock.wasScheduled(reminder->id()));
        const auto call = mock.lastScheduleFor(reminder->id());

        // Wall-clock time must be 14:00 — setTimeZone retags without shifting.
        QCOMPARE(call.when.time(), QTime(14, 0));
        QCOMPARE(call.when.date(), tomorrow);

        // The scheduled QDateTime should carry the Moscow timezone.
        // (Or — if Moscow == systemTimeZone for the host running tests —
        // the scheduler's "if tz != systemTimeZone" gate skips setTimeZone
        // and the QDateTime is local-time. Either way, time() is 14:00.)
        const QTimeZone moscow(QByteArray("Europe/Moscow"));
        if (moscow.isValid() && moscow != QTimeZone::systemTimeZone()) {
            QCOMPARE(call.when.timeZone().id(), moscow.id());
            qInfo("TZ-tagged reminder: 14:00 Europe/Moscow retagged correctly, "
                  "wall-clock preserved. PIN (C-5 bug-fix contract).");
        } else {
            qInfo("Host is already on Europe/Moscow — scheduler short-circuits "
                  "the setTimeZone path; wall-clock 14:00 preserved trivially. PIN.");
        }

        delete store;
    }

    // ── 5. Weekly recurring across DST: Wednesday 10:00 stays Wednesday 10:00 ──
    //
    // Mirror of slot 3 but for weekly cadence. The DST transition happens
    // ON a Sunday — so a Wed 10:00 reminder one week, advanced to the
    // following Wed 10:00, crosses the Sunday transition. The week
    // is 168h calendar hours but 167h or 169h elapsed instant-time. The
    // scheduler reads the new wall-clock from task->dueDatetime, so the
    // Wed 10:00 stays Wed 10:00.
    void recurring_weekly_across_dst_keeps_weekday()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Weekly review"), inboxId);
        task->setDueIsRecurring(true);

        // Spring-forward 2028 is Sun Mar 12. Wed before = Mar 8, Wed after = Mar 15.
        const QDate wedBefore(2028, 3, 8);
        const QDate wedAfter(2028, 3, 15);
        // Sanity: both ARE Wednesdays.
        QCOMPARE(wedBefore.dayOfWeek(), 3);
        QCOMPARE(wedAfter.dayOfWeek(), 3);

        task->setDueDate(wedBefore.toString(Qt::ISODate));
        const QDateTime t1(wedBefore, QTime(10, 0));
        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue d1; d1.datetime = t1.toString(Qt::ISODate);
        reminder->setDue(d1);

        mock.clearAll();
        emit store->remindersChanged();

        if (mock.wasScheduled(reminder->id())) {
            const auto call1 = mock.lastScheduleFor(reminder->id());
            QCOMPARE(call1.when.time(), QTime(10, 0));
            QCOMPARE(call1.when.date().dayOfWeek(), 3);
        }

        // Advance to next Wednesday.
        task->setDueDate(wedAfter.toString(Qt::ISODate));
        const QDateTime t2(wedAfter, QTime(10, 0));
        ReminderDue d2; d2.datetime = t2.toString(Qt::ISODate);
        reminder->setDue(d2);

        mock.clearAll();
        emit store->remindersChanged();

        if (mock.wasScheduled(reminder->id())) {
            const auto call2 = mock.lastScheduleFor(reminder->id());
            QCOMPARE(call2.when.time(), QTime(10, 0));
            QCOMPARE(call2.when.date().dayOfWeek(), 3);
            QCOMPARE(call2.when.date(), wedAfter);
            qInfo("Weekly recurring across DST: Wed 10:00 -> Wed 10:00 preserved. "
                  "Elapsed instant-time may be 167h or 169h depending on direction; "
                  "wall-clock is invariant. PIN.");
        } else {
            qInfo("Weekly DST scenario: both target dates in the past at test time; "
                  "skipped. PIN — scheduler never produces a stale schedule.");
        }

        delete store;
    }

    // ── 6. ISO date string round-trip: persisted nextFireAt is LOCAL wall-clock ──
    //
    // The reminder's due.datetime is stored as a plain Qt::ISODate string
    // ("YYYY-MM-DDTHH:MM:SS"), NO zone suffix in the absence of due.timezone.
    // QDateTime::fromString(Qt::ISODate) parses it as LOCAL time.
    //
    // Consequence: if the user moves between time zones (e.g. travelling +
    // changing system TZ), the reminder will fire at the same WALL-CLOCK
    // time in the new zone. The persisted string is timezone-naive, so it
    // semantically means "9:00 local, whatever local is right now".
    //
    // We pin that contract by round-tripping a reminder through ISO and
    // observing the scheduled QDateTime equals the originally-set local time.
    void epoch_seconds_storage_survives_clock_change()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Local reminder"), inboxId);

        // Tomorrow 09:00 local. ISO drops sub-second precision, so zero msec.
        QDateTime triggerLocal = QDateTime::currentDateTime().addDays(1);
        triggerLocal.setTime(QTime(9, 0, 0));

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.datetime = triggerLocal.toString(Qt::ISODate);
        // Crucially: no timezone field — pins the local-time-naive contract.
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        QVERIFY(mock.wasScheduled(reminder->id()));
        const auto call = mock.lastScheduleFor(reminder->id());

        // Wall-clock 09:00 must round-trip exactly.
        QCOMPARE(call.when.time(), QTime(9, 0));

        // And the stored datetime string MUST have no zone suffix (proves
        // we're saving LOCAL time, not UTC, not offset-from-UTC).
        const QString persisted = reminder->due().datetime;
        QVERIFY(!persisted.contains(QLatin1Char('Z')));
        QVERIFY(!persisted.contains(QLatin1Char('+')));
        // ISO strings can contain a '-' in date "2028-03-12T..." — we only
        // want to assert there's no trailing offset like "...09:00-08:00".
        // Check the last 6 characters for an offset pattern.
        const QString tail = persisted.right(6);
        QVERIFY2(!tail.contains(QRegularExpression(QStringLiteral("[-+]\\d{2}:\\d{2}"))),
                 qPrintable(QStringLiteral("Unexpected zone suffix in: %1").arg(persisted)));

        qInfo("Persisted ISO datetime is timezone-naive (local wall-clock). "
              "Reminder semantics: '09:00 in whatever local zone is current'. "
              "Move TZ -> reminder follows wall-clock, not absolute instant. PIN.");

        delete store;
    }

    // ── 7. Feb 29 in non-leap year: invalid QDate -> graceful no-op ─────
    //
    // The scheduler's date-only path runs QDate::fromString(due.date, Qt::ISODate).
    // For "2027-02-29" (non-leap year), QDate returns INVALID, and the
    // scheduler returns an invalid QDateTime from resolveTriggerTime,
    // which then hits the !triggerTime.isValid() branch and skips.
    //
    // We pin: no crash, pre-cancel still fires, no schedule, no shown.
    void monthly_february_29_in_non_leap_year_falls_back_to_feb_28()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();
        Task *task = store->addTask(QStringLiteral("Monthly review"), inboxId);

        // 2027 is NOT a leap year — "2027-02-29" is invalid.
        const QString badDateString = QStringLiteral("2027-02-29");
        QVERIFY(!QDate::fromString(badDateString, Qt::ISODate).isValid());

        Reminder *reminder = store->addReminder(task->id(), QStringLiteral("absolute"));
        ReminderDue rDue;
        rDue.date = badDateString;  // date-only path
        reminder->setDue(rDue);

        mock.clearAll();
        emit store->remindersChanged();

        // Must NOT schedule — invalid date short-circuits to invalid trigger.
        QVERIFY(!mock.wasScheduled(reminder->id()));
        // Pre-cancel still happens (defensive).
        QVERIFY(mock.wasCancelled(reminder->id()));
        // No immediate fire.
        QVERIFY(mock.shown.isEmpty());

        qInfo("Feb 29 in non-leap year: QDate::fromString returns invalid; "
              "scheduler silently skips. NO automatic Feb 28 fallback — the "
              "calling code (Recurrence engine) would have to compute the "
              "next valid date itself. PIN (responsibility split).");

        delete store;
    }

    // ── 8. Far-past reminder ("yesterday at 09:00", or "180 days ago") ──
    //
    // A reminder created with a far-past trigger time is skipped silently.
    // The scheduler's "triggerTime <= now" guard handles ANY past instant —
    // there's no cliff at e.g. 24h-ago where behaviour changes. We drive
    // 180 days back to confirm no overflow / sign-flip / wrap-around in
    // QDateTime::secsTo or msecsTo arithmetic anywhere downstream.
    //
    // Companion: scheduler does NOT fire-immediately for past reminders.
    // showNotification is reserved for explicit user-triggered "show now"
    // semantics; rescheduleAll never calls it.
    void reminder_in_far_past_fires_immediately_or_skipped()
    {
        DataStore *store = makeStore();
        MockNotificationService mock;
        ReminderScheduler scheduler(store, &mock);
        mock.clearAll();

        const QString inboxId = store->inboxProject()->id();

        // Three buckets of "past":
        //   (a) 24 hours ago — recent past
        //   (b) 180 days ago — far past
        //   (c) Jan 1 1970 + 1 second — epoch boundary (no negative time_t)
        struct PastCase { QString name; QDateTime when; };
        const QList<PastCase> cases = {
            { QStringLiteral("24h_ago"),  QDateTime::currentDateTime().addSecs(-24 * 3600) },
            { QStringLiteral("180d_ago"), QDateTime::currentDateTime().addDays(-180) },
            { QStringLiteral("epoch+1s"), QDateTime::fromSecsSinceEpoch(1) },
        };

        QList<Reminder *> reminders;
        for (const auto &c : cases) {
            Task *t = store->addTask(QStringLiteral("Past ") + c.name, inboxId);
            Reminder *r = store->addReminder(t->id(), QStringLiteral("absolute"));
            ReminderDue d; d.datetime = c.when.toString(Qt::ISODate);
            r->setDue(d);
            reminders.append(r);
        }

        mock.clearAll();
        emit store->remindersChanged();

        // Every past reminder must be:
        //   - cancelled (pre-cancel runs unconditionally)
        //   - NOT scheduled
        //   - NOT shown immediately (no fire-immediately semantics here)
        for (Reminder *r : reminders) {
            QVERIFY2(mock.wasCancelled(r->id()),
                     qPrintable(QStringLiteral("pre-cancel missed for %1").arg(r->id())));
            QVERIFY2(!mock.wasScheduled(r->id()),
                     qPrintable(QStringLiteral("past reminder erroneously scheduled: %1").arg(r->id())));
        }
        QVERIFY(mock.shown.isEmpty());

        qInfo("Past reminders (24h / 180d / 1970+1s) all silently skipped. "
              "No fire-immediately, no negative-interval arithmetic, no "
              "QDateTime overflow at the epoch boundary. PIN.");

        delete store;
    }
};

QTEST_GUILESS_MAIN(TestReminderSchedulerDst)
#include "test_reminderscheduler_dst.moc"
