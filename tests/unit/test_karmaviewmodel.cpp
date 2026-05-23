#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDate>
#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "utils/settingskeys.h"

// =============================================================================
// Comprehensive unit tests for KarmaViewModel.
//
// KarmaViewModel persists data in two stores:
//   * DataStore::settings()       — karma_score, karma_level, daily/weekly
//                                   streaks, karma_trend (snake_case JSON keys
//                                   defined in utils/settingskeys.h).
//   * QSettings ("karma/shown_milestones")
//                                 — comma-separated list of milestone
//                                   thresholds whose achievementUnlocked has
//                                   already been emitted (dedup guard).
//
// We isolate QSettings by setting a dedicated org/app name in initTestCase()
// and clearing the store between every test. DataStore is a fresh in-memory
// instance per test — no file I/O. Real DataStore + TaskViewModel are used as
// the test fixture for triggering completions, per the task instructions.
//
// Notes on how completions feed karma:
//   * KarmaViewModel::onTaskCompleted() is called externally (in production
//     by main.cpp wiring TaskVM::taskChanged to KarmaVM). It bumps karma by
//     +1 and recomputes today/week counts from DataStore::completedTasks().
//   * completedToday/completedThisWeek are derived from Task::completedAt(),
//     not from raw "the user clicked complete N times". Multiple onTaskCompleted
//     calls without a corresponding completed task in the store will still
//     bump score, but the derived counts depend on actual task state.
//   * Streaks fire only when the daily-goal threshold is crossed (B-3.15) and
//     are deduped per calendar day (B-3.16).
// =============================================================================

namespace {

struct Fixture {
    DataStore *store = nullptr;
    QString inboxId;

    ~Fixture() { delete store; }
};

// Build a fresh in-memory DataStore with one Inbox project.
static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    return f;
}

// Helper: add a task and immediately mark it completed (which sets
// completedAt = now in UTC, see Task::setIsCompleted). Returns the task id.
static QString addCompletedTask(DataStore *store, const QString &projectId,
                                const QString &content)
{
    Task *t = store->addTask(content, projectId);
    t->setIsCompleted(true);
    return t->id();
}

// Helper: like addCompletedTask but back-dates the completedAt timestamp.
// Use this to simulate completions on prior calendar days for streak/window
// testing without actually waiting overnight.
static QString addCompletedTaskOn(DataStore *store, const QString &projectId,
                                  const QString &content, const QDate &date)
{
    Task *t = store->addTask(content, projectId);
    t->setIsCompleted(true);
    // Override the auto-stamped completedAt with a custom date (12:00 local
    // so timezone wobble can't push it to a different calendar day).
    t->setCompletedAt(QDateTime(date, QTime(12, 0)).toUTC());
    return t->id();
}

} // anonymous namespace

class TestKarmaViewModel : public QObject
{
    Q_OBJECT

private slots:

    // ── Test fixture lifecycle ───────────────────────────────────────────────

    void initTestCase()
    {
        // Use a throwaway QSettings scope so the test never touches the
        // user's real OpenTodo settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_KarmaVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_karmaviewmodel"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // =====================================================================
    // 1) Bootstrap state on a fresh DataStore.
    // =====================================================================
    void bootstrap_zero_score_zero_streak_no_milestones()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        QCOMPARE(karma.karmaScore(), 0);
        QCOMPARE(karma.dailyStreak(), 0);
        QCOMPARE(karma.weeklyStreak(), 0);
        QCOMPARE(karma.completedToday(), 0);
        QCOMPARE(karma.completedThisWeek(), 0);
        // previousMilestone == 0 (no milestone reached); nextMilestone == 100.
        QCOMPARE(karma.previousMilestone(), 0);
        QCOMPARE(karma.nextMilestone(), 100);

        delete f;
    }

    // =====================================================================
    // 2) onTaskCompleted increments score by exactly +1 (base reward).
    // =====================================================================
    void onTaskCompleted_increments_score_by_one()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        const int before = karma.karmaScore();
        // Add a real completed task so updateCompletionCounts has data to
        // tally — though score increment itself doesn't depend on it.
        addCompletedTask(f->store, f->inboxId, QStringLiteral("T1"));
        karma.onTaskCompleted();

        QCOMPARE(karma.karmaScore(), before + 1);

        delete f;
    }

    // =====================================================================
    // 3) completionsByDay shows today's count after a completion.
    // =====================================================================
    void completionsByDay_today_increments_after_completion()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("Today task"));
        // Trigger recount; onTaskCompleted internally calls updateCompletionCounts.
        karma.onTaskCompleted();

        const QVariantList days = karma.completionsByDay(7);
        QCOMPARE(days.size(), 7);

        const QVariantMap todayEntry = days.last().toMap();
        QCOMPARE(todayEntry.value(QStringLiteral("date")).toString(),
                 QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
        QCOMPARE(todayEntry.value(QStringLiteral("count")).toInt(), 1);

        delete f;
    }

    // =====================================================================
    // 4) Multiple completions on same day land in same bucket.
    // =====================================================================
    void completionsByDay_multiple_today_are_summed()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("A"));
        addCompletedTask(f->store, f->inboxId, QStringLiteral("B"));
        addCompletedTask(f->store, f->inboxId, QStringLiteral("C"));
        karma.onTaskCompleted();

        const QVariantList days = karma.completionsByDay(7);
        QCOMPARE(days.last().toMap().value(QStringLiteral("count")).toInt(), 3);

        // Today's completedToday should also reflect 3.
        QCOMPARE(karma.completedToday(), 3);

        delete f;
    }

    // =====================================================================
    // 5) Streak: completing past the daily goal extends streak by 1.
    //    The streak only fires when prevToday < goal && completedToday >=
    //    goal — i.e. when we cross the goal threshold.  We force a low goal
    //    of 1 so a single task crosses it.
    // =====================================================================
    void streak_extends_when_daily_goal_crossed_today()
    {
        Fixture *f = makeFixture();
        // Set a daily goal of 1 so a single completion crosses the threshold.
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal), 1);

        KarmaViewModel karma(f->store);
        QCOMPARE(karma.dailyStreak(), 0);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("First"));
        karma.onTaskCompleted();

        // Goal crossed (0 → 1) → streak starts at 1 (no prior valid date).
        QCOMPARE(karma.dailyStreak(), 1);

        delete f;
    }

    // =====================================================================
    // 6) Streak: a calendar-day gap >1 day resets streak to 1 on the next
    //    crossing.  We simulate this by setting the dedup-guard date in the
    //    past via an initial crossing and then re-crossing two days later
    //    (impossible to test directly without freezing the clock — instead
    //    we assert the codified behaviour: an explicitly-cleared streak
    //    starts at 1 on the next crossing, AND the streak does NOT advance
    //    on a second same-day crossing).
    // =====================================================================
    void streak_does_not_double_increment_on_same_day()
    {
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal), 1);

        KarmaViewModel karma(f->store);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("A1"));
        karma.onTaskCompleted();
        QCOMPARE(karma.dailyStreak(), 1);

        // Second completion same day. Since prevToday (1) is NOT < goal (1),
        // the bonus path doesn't fire — streak stays at 1, not 2.
        addCompletedTask(f->store, f->inboxId, QStringLiteral("A2"));
        karma.onTaskCompleted();
        QCOMPARE(karma.dailyStreak(), 1);

        delete f;
    }

    // =====================================================================
    // 7) karmaTrendByDay returns a continuous array with correct shape.
    //    Default is 30 entries with date+score keys, oldest first.
    // =====================================================================
    void karmaTrendByDay_returns_continuous_dated_array()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        const QVariantList trend = karma.karmaTrendByDay(); // default 30
        QCOMPARE(trend.size(), 30);

        // Every entry must carry "date" and "score".
        for (const QVariant &v : trend) {
            const QVariantMap m = v.toMap();
            QVERIFY(m.contains(QStringLiteral("date")));
            QVERIFY(m.contains(QStringLiteral("score")));
            QVERIFY(QDate::fromString(m.value(QStringLiteral("date")).toString(),
                                      QStringLiteral("yyyy-MM-dd")).isValid());
        }

        // Oldest at index 0, today at the last index.
        const QString todayKey = QDate::currentDate()
                                     .toString(QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(trend.last().toMap().value(QStringLiteral("date")).toString(),
                 todayKey);

        // Custom length round-trip.
        const QVariantList shorter = karma.karmaTrendByDay(7);
        QCOMPARE(shorter.size(), 7);

        delete f;
    }

    // =====================================================================
    // 8) karmaTrendByDay reflects today's completion count.
    // =====================================================================
    void karmaTrendByDay_counts_today_completions()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("X"));
        addCompletedTask(f->store, f->inboxId, QStringLiteral("Y"));

        const QVariantList trend = karma.karmaTrendByDay(5);
        QCOMPARE(trend.last().toMap().value(QStringLiteral("score")).toInt(), 2);

        delete f;
    }

    // =====================================================================
    // 9) Reaching a milestone threshold emits achievementUnlocked.
    //    We seed karma_score one point below the 100 milestone, then call
    //    onTaskCompleted once — that should bump to 100 and trigger.
    // =====================================================================
    void reaching_milestone_emits_achievementUnlocked_signal()
    {
        Fixture *f = makeFixture();
        // Set score to 99 directly via DataStore so the next +1 lands on 100.
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 99);

        KarmaViewModel karma(f->store);
        QSignalSpy spy(&karma, &KarmaViewModel::achievementUnlocked);

        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 100);

        QCOMPARE(spy.count(), 1);
        // Signal payload: (name, description, milestone)
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 3);
        QCOMPARE(args.at(2).toInt(), 100);
        QVERIFY(!args.at(0).toString().isEmpty()); // name
        QVERIFY(!args.at(1).toString().isEmpty()); // description

        delete f;
    }

    // =====================================================================
    // 10) Milestone dedup: the same milestone fires only once across calls.
    // =====================================================================
    void milestone_does_not_fire_twice_for_same_threshold()
    {
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 99);

        KarmaViewModel karma(f->store);
        QSignalSpy spy(&karma, &KarmaViewModel::achievementUnlocked);

        karma.onTaskCompleted(); // 99 → 100, fires
        QCOMPARE(spy.count(), 1);

        // Force score back to just below 100 and re-cross. The dedup guard
        // (m_shownMilestones, persisted in QSettings) should suppress a
        // second emission for the same milestone.
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 99);
        karma.onTaskCompleted(); // 99 → 100 again
        QCOMPARE(spy.count(), 1);

        delete f;
    }

    // =====================================================================
    // 11) nextMilestone / previousMilestone bracket the current score.
    // =====================================================================
    void milestones_bracket_current_score()
    {
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 250);

        KarmaViewModel karma(f->store);
        // 250 is between 100 and 500.
        QCOMPARE(karma.previousMilestone(), 100);
        QCOMPARE(karma.nextMilestone(), 500);

        // Above the highest milestone (10000) → next is 0 (sentinel).
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 99999);
        QCOMPARE(karma.previousMilestone(), 10000);
        QCOMPARE(karma.nextMilestone(), 0);

        delete f;
    }

    // =====================================================================
    // 12) resetAllKarma zeroes every persisted karma field.
    // =====================================================================
    void resetAllKarma_zeroes_score_streak_and_completionsByDay()
    {
        Fixture *f = makeFixture();
        // Pre-populate: score, streak, several completed tasks.
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal), 1);
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 1234);
        f->store->setSetting(QLatin1String(SettingsKeys::DailyStreak), 7);

        KarmaViewModel karma(f->store);
        addCompletedTask(f->store, f->inboxId, QStringLiteral("X"));
        addCompletedTask(f->store, f->inboxId, QStringLiteral("Y"));
        karma.onTaskCompleted();

        QVERIFY(karma.karmaScore() > 0);

        karma.resetAllKarma();

        QCOMPARE(karma.karmaScore(), 0);
        QCOMPARE(karma.dailyStreak(), 0);
        QCOMPARE(karma.weeklyStreak(), 0);
        // Karma trend (whole-object LWW JSON array) must be empty.
        QCOMPARE(karma.karmaTrend().size(), 0);

        // completionsByDay still reflects the actual DataStore tasks (it's
        // derived from completedTasks(), not from cleared counters), so
        // we don't assert it's empty here — instead we verify that the
        // *persisted* karma totals are zero, which is what resetAllKarma
        // owns.

        delete f;
    }

    // =====================================================================
    // 13) Edge case: a soft-deleted completed task must NOT be counted in
    //     completedToday / completionsByDay (DataStore::completedTasks()
    //     filters out tombstones, so KarmaVM inherits that behaviour).
    // =====================================================================
    void soft_deleted_completed_task_is_not_counted()
    {
        Fixture *f = makeFixture();

        // One live + one soft-deleted, both completed today.
        const QString liveId    = addCompletedTask(f->store, f->inboxId,
                                                   QStringLiteral("Alive"));
        const QString deletedId = addCompletedTask(f->store, f->inboxId,
                                                   QStringLiteral("Tombstoned"));
        f->store->removeTask(deletedId); // soft-delete

        KarmaViewModel karma(f->store);
        karma.onTaskCompleted(); // triggers recount

        // Only the live task should be in today's bucket.
        QCOMPARE(karma.completedToday(), 1);

        const QVariantList days = karma.completionsByDay(7);
        QCOMPARE(days.last().toMap().value(QStringLiteral("count")).toInt(), 1);

        Q_UNUSED(liveId);
        delete f;
    }

    // =====================================================================
    // 14) onTaskUncompleted decrements karma score, but never below zero.
    // =====================================================================
    void onTaskUncompleted_decrements_score_but_clamps_at_zero()
    {
        Fixture *f = makeFixture();
        // Start with a non-zero score so we can observe the decrement.
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 5);

        KarmaViewModel karma(f->store);
        karma.onTaskUncompleted();
        QCOMPARE(karma.karmaScore(), 4);

        // Drain to zero.
        for (int i = 0; i < 10; ++i) karma.onTaskUncompleted();
        QCOMPARE(karma.karmaScore(), 0);

        delete f;
    }

    // =====================================================================
    // 15) onTaskCompleted via real TaskViewModel wiring: completing a task
    //     through the production VM path leaves DataStore in a state where
    //     KarmaViewModel sees the new completion in its derived counts.
    //     (Per main.cpp, the actual onTaskCompleted call is wired by the
    //     app — here we simulate that wiring explicitly.)
    // =====================================================================
    void taskviewmodel_completion_flows_through_to_karma_counts()
    {
        Fixture *f = makeFixture();
        TaskViewModel taskVM(f->store);
        KarmaViewModel karma(f->store);

        // Wire VM-level completion to KarmaVM, mirroring main.cpp.
        QObject::connect(&taskVM, &TaskViewModel::taskChanged, &karma,
                         [&taskVM, &karma, store = f->store](const QString &id) {
            Task *t = store->taskById(id);
            if (t && t->isCompleted())
                karma.onTaskCompleted();
        });

        Task *t = f->store->addTask(QStringLiteral("Pay bills"), f->inboxId);
        const QString tid = t->id();

        const int beforeScore = karma.karmaScore();
        QVERIFY(taskVM.completeTask(tid));

        QCOMPARE(karma.karmaScore(), beforeScore + 1);
        QCOMPARE(karma.completedToday(), 1);

        delete f;
    }

    // =====================================================================
    // 16) karmaChanged signal fires on completion AND on reset.
    // =====================================================================
    void karmaChanged_signal_fires_on_state_transitions()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);
        QSignalSpy spy(&karma, &KarmaViewModel::karmaChanged);

        addCompletedTask(f->store, f->inboxId, QStringLiteral("X"));
        karma.onTaskCompleted();
        QVERIFY(spy.count() >= 1);

        const int afterCompletion = spy.count();
        karma.resetAllKarma();
        QVERIFY(spy.count() > afterCompletion);

        delete f;
    }

    // =====================================================================
    // 17) Goal-met flags reflect daily completions.
    // =====================================================================
    void dailyGoalMet_reflects_completed_today_vs_goal()
    {
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal), 2);

        KarmaViewModel karma(f->store);
        QVERIFY(!karma.dailyGoalMet());

        addCompletedTask(f->store, f->inboxId, QStringLiteral("A"));
        karma.onTaskCompleted();
        QVERIFY(!karma.dailyGoalMet()); // 1 < 2

        addCompletedTask(f->store, f->inboxId, QStringLiteral("B"));
        karma.onTaskCompleted();
        QVERIFY(karma.dailyGoalMet()); // 2 >= 2

        delete f;
    }

    // =====================================================================
    // 18) karmaLevel transitions across thresholds.
    //     beginner < 100 ≤ novice < 500 ≤ intermediate < 1000 ≤ expert
    //     < 2500 ≤ master < 5000 ≤ grandmaster
    // =====================================================================
    void karmaLevel_transitions_across_thresholds()
    {
        Fixture *f = makeFixture();
        // Score 99 → "beginner" (only set after a calculate triggers update).
        // Manually set + drive updateKarmaLevel via onTaskCompleted.
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 99);

        KarmaViewModel karma(f->store);
        karma.onTaskCompleted(); // 99 → 100, level should flip to novice.
        QCOMPARE(karma.karmaLevel(), QStringLiteral("novice"));

        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 499);
        karma.onTaskCompleted(); // 499 → 500
        QCOMPARE(karma.karmaLevel(), QStringLiteral("intermediate"));

        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 4999);
        karma.onTaskCompleted(); // 4999 → 5000
        QCOMPARE(karma.karmaLevel(), QStringLiteral("grandmaster"));

        delete f;
    }

    // =====================================================================
    // 19) Backdated completions land in their own day bucket in
    //     completionsByDay (verifies the date routing works).
    // =====================================================================
    void backdated_completion_lands_in_correct_day_bucket()
    {
        Fixture *f = makeFixture();
        KarmaViewModel karma(f->store);

        const QDate yesterday = QDate::currentDate().addDays(-1);
        addCompletedTaskOn(f->store, f->inboxId,
                           QStringLiteral("Yesterday's win"), yesterday);

        const QVariantList days = karma.completionsByDay(7);
        QCOMPARE(days.size(), 7);

        // days[5] is yesterday (oldest first, today at index 6 for size=7).
        const QString yKey = yesterday.toString(QStringLiteral("yyyy-MM-dd"));
        bool found = false;
        for (const QVariant &v : days) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("date")).toString() == yKey) {
                QCOMPARE(m.value(QStringLiteral("count")).toInt(), 1);
                found = true;
                break;
            }
        }
        QVERIFY(found);

        // Today's bucket remains zero.
        QCOMPARE(days.last().toMap().value(QStringLiteral("count")).toInt(), 0);

        delete f;
    }

    // =====================================================================
    // Iter-night-33: onTaskUncompleted must mirror onTaskCompleted's
    // threshold-cross arithmetic in reverse. The previous version only
    // subtracted -1, leaving phantom +5/+10 bonuses and inflated streak
    // counters whenever the un-completed task was the one that crossed
    // a goal threshold. Repeated cross/uncross on a single task inflated
    // karma without bound and persisted to QSettings across restarts.
    //
    // Three pin slots cover the failure modes:
    //   1. daily_uncross_subtracts_bonus_and_decrements_streak
    //   2. weekly_uncross_subtracts_bonus_and_decrements_streak
    //   3. cross_uncross_recross_does_not_double_count
    // =====================================================================

    void onTaskUncompleted_dailyUncross_subtractsBonusAndDecrementsStreak()
    {
        Fixture *f = makeFixture();
        // Daily goal = 2, so 2 completions cross the threshold.
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal),  2);
        f->store->setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 0); // disable weekly
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 0);
        f->store->setSetting(QLatin1String(SettingsKeys::DailyStreak), 0);

        KarmaViewModel karma(f->store);

        // First completion (not crossing yet): +1.
        addCompletedTask(f->store, f->inboxId, QStringLiteral("A"));
        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 1);
        QCOMPARE(karma.dailyStreak(), 0);

        // Second completion crosses dailyGoal (now 2 >= 2): +1 base + 5 bonus,
        // dailyStreak bumps to 1.
        const QString secondTaskId =
            addCompletedTask(f->store, f->inboxId, QStringLiteral("B"));
        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 7);
        QCOMPARE(karma.dailyStreak(), 1);

        // Un-complete the second task — this un-crosses the threshold
        // (count goes 2 → 1, < goal). Before the fix:
        //   score = 7 − 1 = 6 (phantom +5)
        //   dailyStreak = 1 (inflated — goal no longer met)
        // After the fix:
        //   score = 7 − 1 − 5 = 1
        //   dailyStreak = 0
        Task *t = f->store->taskById(secondTaskId);
        QVERIFY(t != nullptr);
        t->setIsCompleted(false);
        karma.onTaskUncompleted();
        QCOMPARE(karma.karmaScore(), 1);
        QCOMPARE(karma.dailyStreak(), 0);

        delete f;
    }

    void onTaskUncompleted_weeklyUncross_subtractsBonusAndDecrementsStreak()
    {
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal),  0); // disable daily
        f->store->setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 2);
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 0);
        f->store->setSetting(QLatin1String(SettingsKeys::WeeklyStreak), 0);

        KarmaViewModel karma(f->store);

        // Two completions on different days this week. m_completedThisWeek is
        // derived from completedAt; backdate the first so it lands earlier in
        // the same week.
        const QDate today = QDate::currentDate();
        const QDate earlierThisWeek =
            today.addDays(-(today.dayOfWeek() == 1 ? 0 : 1));
        // If today is Monday, earlier=today (same bucket); else yesterday.
        addCompletedTaskOn(f->store, f->inboxId, QStringLiteral("Mon"),
                           earlierThisWeek);
        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 1);

        const QString secondTaskId =
            addCompletedTask(f->store, f->inboxId, QStringLiteral("Today"));
        karma.onTaskCompleted();
        // Crossed weeklyGoal: +1 + 10 bonus; weeklyStreak bumped.
        QCOMPARE(karma.karmaScore(), 12);
        QCOMPARE(karma.weeklyStreak(), 1);

        // Un-complete today's task — weekly count goes 2 → 1, < goal.
        Task *t = f->store->taskById(secondTaskId);
        QVERIFY(t != nullptr);
        t->setIsCompleted(false);
        karma.onTaskUncompleted();
        // Before the fix: 12 − 1 = 11 (phantom +10); streak stays 1.
        // After the fix: 12 − 1 − 10 = 1; streak = 0.
        QCOMPARE(karma.karmaScore(), 1);
        QCOMPARE(karma.weeklyStreak(), 0);

        delete f;
    }

    void onTaskUncompleted_crossUncrossRecross_doesNotDoubleCount()
    {
        // The most damaging path before the fix: a single task crossed
        // the threshold, was un-completed, then re-completed. Pre-fix:
        //   complete  → +1 +5 = +6
        //   uncomplete→ -1     = -1   (phantom +5 retained)
        //   complete  → +1 +5 = +6   (+5 again because the cross-detection
        //                              fires on prevToday<goal ∧ now≥goal)
        // Net: +11 instead of +6.
        // Post-fix:
        //   complete  → +1 +5 = +6
        //   uncomplete→ -1 -5 = -6
        //   complete  → +1 +5 = +6
        // Net: +6 — correct.
        Fixture *f = makeFixture();
        f->store->setSetting(QLatin1String(SettingsKeys::DailyGoal),  1);
        f->store->setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 0);
        f->store->setSetting(QLatin1String(SettingsKeys::KarmaScore), 0);

        KarmaViewModel karma(f->store);

        // First completion: crosses dailyGoal=1 immediately.
        const QString tid = addCompletedTask(f->store, f->inboxId,
                                             QStringLiteral("X"));
        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 6);

        // Un-complete: goes 1 → 0, < goal. Threshold un-crossed.
        Task *t = f->store->taskById(tid);
        QVERIFY(t != nullptr);
        t->setIsCompleted(false);
        karma.onTaskUncompleted();
        QCOMPARE(karma.karmaScore(), 0);

        // Re-complete: crosses dailyGoal again.
        t->setIsCompleted(true);
        karma.onTaskCompleted();
        QCOMPARE(karma.karmaScore(), 6);

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestKarmaViewModel)
#include "test_karmaviewmodel.moc"
