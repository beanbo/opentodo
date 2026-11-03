#pragma once

#include <QObject>
#include <QDate>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QQmlEngine>

class DataStore;

class KarmaViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("KarmaViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int karmaScore READ karmaScore NOTIFY karmaChanged)
    Q_PROPERTY(QString karmaLevel READ karmaLevel NOTIFY karmaChanged)
    Q_PROPERTY(int dailyStreak READ dailyStreak NOTIFY karmaChanged)
    Q_PROPERTY(int weeklyStreak READ weeklyStreak NOTIFY karmaChanged)
    Q_PROPERTY(int completedToday READ completedToday NOTIFY karmaChanged)
    Q_PROPERTY(int completedThisWeek READ completedThisWeek NOTIFY karmaChanged)
    Q_PROPERTY(int dailyGoal READ dailyGoal NOTIFY karmaChanged)
    Q_PROPERTY(int weeklyGoal READ weeklyGoal NOTIFY karmaChanged)
    Q_PROPERTY(bool dailyGoalMet READ dailyGoalMet NOTIFY karmaChanged)
    Q_PROPERTY(bool weeklyGoalMet READ weeklyGoalMet NOTIFY karmaChanged)
    Q_PROPERTY(QVariantList karmaTrend READ karmaTrend NOTIFY karmaChanged)
    Q_PROPERTY(double dailyProgress READ dailyProgress NOTIFY karmaChanged)
    Q_PROPERTY(double weeklyProgress READ weeklyProgress NOTIFY karmaChanged)
    Q_PROPERTY(int nextMilestone READ nextMilestone NOTIFY karmaChanged)
    Q_PROPERTY(int previousMilestone READ previousMilestone NOTIFY karmaChanged)

public:
    explicit KarmaViewModel(DataStore *store, QObject *parent = nullptr);

    int karmaScore() const;
    QString karmaLevel() const;
    int dailyStreak() const;
    int weeklyStreak() const;
    int completedToday() const;
    int completedThisWeek() const;
    int dailyGoal() const;
    int weeklyGoal() const;
    bool dailyGoalMet() const;
    bool weeklyGoalMet() const;
    QVariantList karmaTrend() const;
    double dailyProgress() const;
    double weeklyProgress() const;
    int nextMilestone() const;
    int previousMilestone() const;

    Q_INVOKABLE void calculateKarma();
    Q_INVOKABLE void onTaskCompleted();
    Q_INVOKABLE void onTaskUncompleted();

    // Resets karma score, level, streaks, milestones, and history. Irreversible.
    Q_INVOKABLE void resetAllKarma();

    // Read accessor for the persisted streak-dedup date (Q2.18-FOLLOWUP).
    // Returns the ISO-8601 "YYYY-MM-DD" string of m_lastStreakIncrementDate,
    // or an empty string when the date is invalid (no streak-bump has fired
    // since QSettings was last cleared). Public so tests can introspect the
    // persistence contract without poking private state.
    Q_INVOKABLE QString lastStreakIncrementDate() const;

    // Returns a QVariantList of {date: "YYYY-MM-DD", count: int} entries
    // for the past `days` calendar days (default 90), inclusive of today.
    // All days in the range are present, including zero-count days.
    Q_INVOKABLE QVariantList completionsByDay(int days = 90) const;

    // Returns a QVariantList of {date: "YYYY-MM-DD", score: int} entries
    // for the past `days` calendar days (default 30), oldest first, today last.
    // score = number of tasks completed on that day (+1 per task, matching
    // onTaskCompleted base scoring).  All days are pre-filled so the line is
    // continuous even on zero-completion days.
    Q_INVOKABLE QVariantList karmaTrendByDay(int days = 30) const;

signals:
    void karmaChanged();
    void achievementUnlocked(const QString &name, const QString &description, int milestone);

private slots:
    void onTasksChanged();

private:
    void updateCompletionCounts();
    // V3: restart the goal-crossing baseline at 0 on a new day / week.
    void rollGoalBaselineToCalendar();
    void updateKarmaLevel();
    QString levelForScore(int score) const;

    // Milestone achievement tracking — persisted in QSettings.
    void loadShownMilestones();
    void saveShownMilestones();
    void checkMilestones(int oldScore, int newScore);

    static const QList<int> s_milestones;

    DataStore *m_store;
    int m_completedToday = 0;
    int m_completedThisWeek = 0;

    // B-3.15 / V3: goal-crossing baseline — the completion counts as of the
    // previous onTaskCompleted/onTaskUncompleted EVENT (not the last store
    // recount, which in the app already includes the completion being
    // handled). Restarted at 0 when the day / week of the baseline is over.
    int m_prevCompletedToday = 0;
    int m_prevCompletedThisWeek = 0;
    QDate m_goalBaselineDate;
    QDate m_goalBaselineWeekStart;

    // First day of the current week per the week_start setting; refreshed by
    // updateCompletionCounts().
    QDate m_weekStartDate;

    // B-3.16: streak dedup — only increment streak once per calendar day.
    QDate m_lastStreakIncrementDate;
    // V3: week (its first day) of the last weekly-streak bump — dedups the
    // bump within a week and detects missed weeks. QSettings-persisted.
    QDate m_lastWeeklyStreakWeekStart;

    // Track which milestone thresholds have already emitted achievementUnlocked.
    // Stored as comma-separated ints under QSettings key "karma/shown_milestones".
    QSet<int> m_shownMilestones;
};
