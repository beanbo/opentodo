#include "karmaviewmodel.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "utils/settingskeys.h"

#include <QDate>
#include <QJsonArray>
#include <QSettings>
#include <QStringList>

// ── Milestone data ────────────────────────────────────────────────────────────

const QList<int> KarmaViewModel::s_milestones = { 100, 500, 1000, 2500, 5000, 10000 };

static QString milestoneDescription(int m)
{
    switch (m) {
        case 100:   return QStringLiteral("Первая сотня — отличный старт!");
        case 500:   return QStringLiteral("500 очков — ты входишь во вкус.");
        case 1000:  return QStringLiteral("Тысяча — настоящая преданность.");
        case 2500:  return QStringLiteral("2500 — мастер планирования.");
        case 5000:  return QStringLiteral("5000 очков — легенда продуктивности.");
        case 10000: return QStringLiteral("10 000 — ты неудержим!");
        default:    return QStringLiteral("Поздравляем с достижением!");
    }
}

KarmaViewModel::KarmaViewModel(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    loadShownMilestones();

    // B-3.16 persistence (Q2.18-FOLLOWUP): m_lastStreakIncrementDate is the
    // per-calendar-day dedup guard for the streak bump.  If we only kept it
    // in memory, the very first goal-crossing completion after restart would
    // hit the "first ever completion" branch and reset dailyStreak to 1 —
    // even if the user had a 30-day streak yesterday.  Persist via QSettings
    // and restore on construction so the dedup state survives process restart.
    {
        QSettings s;
        const QString stored = s.value(
            QStringLiteral("karma/lastStreakIncrementDate")).toString();
        if (!stored.isEmpty()) {
            // QDate::fromString returns an invalid QDate on garbage input,
            // which is exactly the safe default — see the corrupted-settings
            // test in test_karma_streak_persistence.cpp.
            m_lastStreakIncrementDate = QDate::fromString(stored, Qt::ISODate);
        }
    }

    connect(m_store, &DataStore::tasksChanged, this, &KarmaViewModel::onTasksChanged);
    connect(m_store, &DataStore::settingsChanged, this, &KarmaViewModel::karmaChanged);
    updateCompletionCounts();
}

int KarmaViewModel::karmaScore() const
{
    return m_store->setting(QLatin1String(SettingsKeys::KarmaScore)).toInt();
}

QString KarmaViewModel::karmaLevel() const
{
    return m_store->setting(QLatin1String(SettingsKeys::KarmaLevel)).toString();
}

int KarmaViewModel::dailyStreak() const
{
    return m_store->setting(QLatin1String(SettingsKeys::DailyStreak)).toInt();
}

int KarmaViewModel::weeklyStreak() const
{
    return m_store->setting(QLatin1String(SettingsKeys::WeeklyStreak)).toInt();
}

int KarmaViewModel::completedToday() const { return m_completedToday; }
int KarmaViewModel::completedThisWeek() const { return m_completedThisWeek; }

int KarmaViewModel::dailyGoal() const
{
    return m_store->setting(QLatin1String(SettingsKeys::DailyGoal)).toInt();
}

int KarmaViewModel::weeklyGoal() const
{
    return m_store->setting(QLatin1String(SettingsKeys::WeeklyGoal)).toInt();
}

bool KarmaViewModel::dailyGoalMet() const
{
    return m_completedToday >= dailyGoal();
}

bool KarmaViewModel::weeklyGoalMet() const
{
    return m_completedThisWeek >= weeklyGoal();
}

QVariantList KarmaViewModel::karmaTrend() const
{
    QVariantList result;
    const QJsonArray trendArr = m_store->settings()
        .value(QLatin1String(SettingsKeys::KarmaTrend)).toArray();

    for (const auto &v : trendArr) {
        const QJsonObject point = v.toObject();
        QVariantMap item;
        item[QStringLiteral("date")] = point[QStringLiteral("date")].toString();
        item[QStringLiteral("score")] = point[QStringLiteral("score")].toInt();
        result.append(item);
    }

    return result;
}

double KarmaViewModel::dailyProgress() const
{
    int goal = dailyGoal();
    if (goal <= 0) return 0.0;
    return qMin(1.0, static_cast<double>(m_completedToday) / goal);
}

double KarmaViewModel::weeklyProgress() const
{
    int goal = weeklyGoal();
    if (goal <= 0) return 0.0;
    return qMin(1.0, static_cast<double>(m_completedThisWeek) / goal);
}

int KarmaViewModel::nextMilestone() const
{
    const int score = karmaScore();
    for (int m : s_milestones) {
        if (score < m) return m;
    }
    return 0; // all milestones exceeded
}

int KarmaViewModel::previousMilestone() const
{
    const int score = karmaScore();
    int last = 0;
    for (int m : s_milestones) {
        if (score >= m) last = m;
        else break;
    }
    return last;
}

void KarmaViewModel::calculateKarma()
{
    updateCompletionCounts();

    int score = karmaScore();

    // Karma scoring rules:
    // +1 per completed task
    // +5 bonus for meeting daily goal
    // +10 bonus for meeting weekly goal
    // -2 per overdue task (daily penalty)

    const auto allTasks = m_store->allTasks();
    int overdueCount = 0;
    const QDate today = QDate::currentDate();
    for (const auto *t : allTasks) {
        if (!t->isCompleted() && !t->dueDate().isEmpty()) {
            QDate d = QDate::fromString(t->dueDate(), QStringLiteral("yyyy-MM-dd"));
            if (d.isValid() && d < today)
                ++overdueCount;
        }
    }

    score -= overdueCount * 2;
    if (score < 0) score = 0;

    m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore), score);
    updateKarmaLevel();

    // Update karma trend
    QJsonArray trendArr = m_store->settings()
        .value(QLatin1String(SettingsKeys::KarmaTrend)).toArray();

    QJsonObject todayPoint;
    todayPoint[QStringLiteral("date")] = today.toString(QStringLiteral("yyyy-MM-dd"));
    todayPoint[QStringLiteral("score")] = score;

    // Replace today's entry if exists, otherwise append
    bool found = false;
    for (int i = 0; i < trendArr.size(); ++i) {
        if (trendArr[i].toObject()[QStringLiteral("date")].toString() ==
            today.toString(QStringLiteral("yyyy-MM-dd"))) {
            trendArr[i] = todayPoint;
            found = true;
            break;
        }
    }
    if (!found)
        trendArr.append(todayPoint);

    // B-3.17: trim to 30 days regardless of whether today's point is new or
    // updated.  Previously the trim was inside `if (!found)` so it never ran
    // on update days, allowing the array to grow without bound.
    while (trendArr.size() > 30)
        trendArr.removeFirst();

    QJsonObject settings = m_store->settings();
    settings[QLatin1String(SettingsKeys::KarmaTrend)] = trendArr;
    m_store->setSettings(settings);

    emit karmaChanged();
}

void KarmaViewModel::onTaskCompleted()
{
    const int oldScore = karmaScore();
    int score = oldScore + 1;
    m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore), score);

    const int prevToday  = m_completedToday;
    const int prevWeekly = m_completedThisWeek;
    updateCompletionCounts();

    const int dGoal = dailyGoal();
    const int wGoal = weeklyGoal();
    const QDate today = QDate::currentDate();

    // B-3.15: fire daily-goal bonus only when crossing the threshold
    // (prev < goal && current >= goal), not on every completion at goal.
    if (dGoal > 0 && prevToday < dGoal && m_completedToday >= dGoal) {
        score += 5;
        m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore), score);

        // B-3.16: increment streak only once per calendar day.
        if (m_lastStreakIncrementDate != today) {
            const QDate expectedYesterday = today.addDays(-1);
            int streak;
            if (m_lastStreakIncrementDate == expectedYesterday) {
                // Consecutive day: extend streak.
                streak = dailyStreak() + 1;
            } else if (!m_lastStreakIncrementDate.isValid()) {
                // First ever completion: start streak at 1.
                streak = 1;
            } else {
                // Gap > 1 day. night iter-147: honour vacation mode. When the
                // user is on vacation, the missed days are intentional time off
                // — do NOT reset the streak; treat the gap as consecutive so a
                // returning user keeps (and extends) their streak. This is the
                // consumer the `vacation_mode` setting was missing (it was a
                // dead toggle before). Without vacation mode, a >1-day gap
                // resets to 1 as before.
                const bool onVacation = m_store->setting(
                    QLatin1String(SettingsKeys::VacationMode)).toBool();
                streak = onVacation ? (dailyStreak() + 1) : 1;
            }
            m_store->setSetting(QLatin1String(SettingsKeys::DailyStreak), streak);
            m_lastStreakIncrementDate = today;

            // Q2.18-FOLLOWUP: persist the dedup date so a restart on the same
            // day doesn't replay the "first ever completion" branch and lose
            // the streak.  See the bug-pinning test in
            // test_karma_streak_persistence.cpp.
            QSettings s;
            s.setValue(QStringLiteral("karma/lastStreakIncrementDate"),
                       m_lastStreakIncrementDate.toString(Qt::ISODate));
        }
    }

    // B-3.15: fire weekly-goal bonus only when crossing the threshold.
    if (wGoal > 0 && prevWeekly < wGoal && m_completedThisWeek >= wGoal) {
        score += 10;
        m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore), score);

        int streak = weeklyStreak() + 1;
        m_store->setSetting(QLatin1String(SettingsKeys::WeeklyStreak), streak);
    }

    updateKarmaLevel();
    checkMilestones(oldScore, karmaScore());
    emit karmaChanged();
}

QString KarmaViewModel::lastStreakIncrementDate() const
{
    // Q2.18-FOLLOWUP test introspection: returns the persisted dedup date as
    // an ISO-8601 string, or empty if the in-memory cache is invalid.
    if (!m_lastStreakIncrementDate.isValid())
        return QString();
    return m_lastStreakIncrementDate.toString(Qt::ISODate);
}

void KarmaViewModel::resetAllKarma()
{
    // Clear milestone tracking (QSettings-backed)
    m_shownMilestones.clear();
    saveShownMilestones();

    // Reset transient streak dedup guard — in memory AND on disk. Without
    // wiping the QSettings entry, the very next KarmaViewModel construction
    // would re-load the stale date and the post-reset behaviour would
    // diverge from "as if installed fresh".
    m_lastStreakIncrementDate = QDate();
    {
        QSettings s;
        s.remove(QStringLiteral("karma/lastStreakIncrementDate"));
    }

    // Reset DataStore-backed karma fields
    m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore),   0);
    m_store->setSetting(QLatin1String(SettingsKeys::KarmaLevel),   levelForScore(0));
    m_store->setSetting(QLatin1String(SettingsKeys::DailyStreak),  0);
    m_store->setSetting(QLatin1String(SettingsKeys::WeeklyStreak), 0);

    // Clear karma trend history
    QJsonObject settings = m_store->settings();
    settings[QLatin1String(SettingsKeys::KarmaTrend)] = QJsonArray{};
    m_store->setSettings(settings);

    emit karmaChanged();
}

void KarmaViewModel::onTaskUncompleted()
{
    // Iter-night-33: asymmetric with onTaskCompleted before this fix.
    // onTaskCompleted applies THREE independent score deltas — +1 base,
    // +5 on first crossing of dailyGoal, +10 on first crossing of
    // weeklyGoal — plus deduped streak increments. The previous
    // implementation only subtracted -1, so un-completing the task that
    // triggered a goal crossing left a phantom +5/+10 in the karma
    // score and the streak counter inflated by one per cross→uncross
    // cycle. Repeated cross/uncross on a single task inflated karma
    // without bound; persisted to QSettings so the inflation survived
    // restarts. Now mirror onTaskCompleted's threshold-cross arithmetic
    // in reverse, snapshotting prev counts before the refresh so we
    // can detect the un-crossing.
    const int prevToday  = m_completedToday;
    const int prevWeekly = m_completedThisWeek;
    updateCompletionCounts();

    const int dGoal = dailyGoal();
    const int wGoal = weeklyGoal();

    int score = karmaScore() - 1;

    // Daily threshold un-cross: prev was at/above goal, now below.
    if (dGoal > 0 && prevToday >= dGoal && m_completedToday < dGoal) {
        score -= 5;
        // Decrement the streak counter (floor 0). Symmetric with the
        // +1 increment in onTaskCompleted, though we can't perfectly
        // restore the prior streak value because the "before this
        // crossing" state isn't snapshotted anywhere — but un-crossing
        // means today's goal isn't met any more, so the streak should
        // reflect that.
        const int newStreak = qMax(0, dailyStreak() - 1);
        m_store->setSetting(QLatin1String(SettingsKeys::DailyStreak), newStreak);

        // Wipe the dedup date so a subsequent re-cross today can fire
        // the streak logic again. Without this, m_lastStreakIncrementDate
        // would still equal today and the re-cross would silently NOT
        // bump the streak, permanently losing the increment.
        m_lastStreakIncrementDate = QDate();
        QSettings s;
        s.remove(QStringLiteral("karma/lastStreakIncrementDate"));
    }

    // Weekly threshold un-cross.
    if (wGoal > 0 && prevWeekly >= wGoal && m_completedThisWeek < wGoal) {
        score -= 10;
        const int newStreak = qMax(0, weeklyStreak() - 1);
        m_store->setSetting(QLatin1String(SettingsKeys::WeeklyStreak), newStreak);
    }

    m_store->setSetting(QLatin1String(SettingsKeys::KarmaScore), qMax(0, score));
    emit karmaChanged();
}

void KarmaViewModel::onTasksChanged()
{
    updateCompletionCounts();
    emit karmaChanged();
}

void KarmaViewModel::updateCompletionCounts()
{
    const QDate today = QDate::currentDate();

    // B-3.18: read week-start day from settings (1 = Monday … 7 = Sunday,
    // matching Qt's dayOfWeek() convention).  Default to Monday (1) if unset.
    const int configuredWeekStart = m_store->setting(
        QLatin1String(SettingsKeys::WeekStart)).toInt();
    const int weekStartDay = (configuredWeekStart >= 1 && configuredWeekStart <= 7)
                             ? configuredWeekStart : 1;

    const int dow = today.dayOfWeek(); // 1=Mon … 7=Sun
    // Compute days since the configured week start.
    int daysSinceWeekStart = (dow - weekStartDay + 7) % 7;
    const QDate weekStart = today.addDays(-daysSinceWeekStart);

    m_completedToday = 0;
    m_completedThisWeek = 0;

    const auto tasks = m_store->completedTasks();
    for (const auto *t : tasks) {
        if (!t->completedAt().isValid()) continue;
        const QDate completedDate = t->completedAt().toLocalTime().date();

        if (completedDate == today)
            ++m_completedToday;

        if (completedDate >= weekStart && completedDate <= today)
            ++m_completedThisWeek;
    }

    // Keep previous counts in sync for goal-crossing detection (B-3.15).
    m_prevCompletedToday    = m_completedToday;
    m_prevCompletedThisWeek = m_completedThisWeek;
}

void KarmaViewModel::updateKarmaLevel()
{
    const int score = karmaScore();
    const QString newLevel = levelForScore(score);
    const QString currentLevel = karmaLevel();

    if (newLevel != currentLevel)
        m_store->setSetting(QLatin1String(SettingsKeys::KarmaLevel), newLevel);
}

QString KarmaViewModel::levelForScore(int score) const
{
    if (score >= 5000) return QStringLiteral("grandmaster");
    if (score >= 2500) return QStringLiteral("master");
    if (score >= 1000) return QStringLiteral("expert");
    if (score >= 500) return QStringLiteral("intermediate");
    if (score >= 100) return QStringLiteral("novice");
    return QStringLiteral("beginner");
}

// ── Milestone helpers ─────────────────────────────────────────────────────────

void KarmaViewModel::loadShownMilestones()
{
    QSettings s;
    const QStringList parts = s.value(QStringLiteral("karma/shown_milestones"))
                                .toString()
                                .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (ok)
            m_shownMilestones.insert(v);
    }
}

void KarmaViewModel::saveShownMilestones()
{
    QSettings s;
    QStringList list;
    list.reserve(m_shownMilestones.size());
    for (int m : std::as_const(m_shownMilestones))
        list.append(QString::number(m));
    s.setValue(QStringLiteral("karma/shown_milestones"), list.join(QLatin1Char(',')));
}

void KarmaViewModel::checkMilestones(int oldScore, int newScore)
{
    for (int m : s_milestones) {
        if (oldScore < m && newScore >= m && !m_shownMilestones.contains(m)) {
            m_shownMilestones.insert(m);
            saveShownMilestones();
            const QString name = QStringLiteral("%1 кармы!").arg(m);
            const QString desc = milestoneDescription(m);
            emit achievementUnlocked(name, desc, m);
        }
    }
}

// ── Completion history ────────────────────────────────────────────────────────

QVariantList KarmaViewModel::karmaTrendByDay(int days) const
{
    // Build a map from local date string ("YYYY-MM-DD") to karma earned that day.
    // Scoring: +1 per completed task, matching the base score in onTaskCompleted.
    QMap<QString, int> scores;
    const QDate today = QDate::currentDate();

    // Pre-populate every day in the range with zero so the chart always has a
    // continuous line.  Index 0 = oldest day, index (days-1) = today.
    for (int i = days - 1; i >= 0; --i) {
        const QString key = today.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        scores.insert(key, 0);
    }

    const QDate firstDay = today.addDays(-(days - 1));

    const auto tasks = m_store->completedTasks();
    for (const auto *t : tasks) {
        if (!t->completedAt().isValid()) continue;
        const QDate completedDate = t->completedAt().toLocalTime().date();
        if (completedDate < firstDay || completedDate > today) continue;
        const QString key = completedDate.toString(QStringLiteral("yyyy-MM-dd"));
        scores[key] += 1;   // +1 per task, matching onTaskCompleted base score
    }

    // Convert to QVariantList ordered oldest → newest.
    QVariantList result;
    result.reserve(days);
    for (int i = days - 1; i >= 0; --i) {
        const QString key = today.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        QVariantMap entry;
        entry[QStringLiteral("date")]  = key;
        entry[QStringLiteral("score")] = scores.value(key, 0);
        result.append(entry);
    }
    return result;
}

QVariantList KarmaViewModel::completionsByDay(int days) const
{
    // Build a map from local date string ("YYYY-MM-DD") to completion count.
    QMap<QString, int> counts;
    const QDate today = QDate::currentDate();

    // Pre-populate every day in the range with zero so the QML layer never
    // has to fill gaps. Index 0 = oldest day, index (days-1) = today.
    for (int i = days - 1; i >= 0; --i) {
        const QString key = today.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        counts.insert(key, 0);
    }

    const QDate firstDay = today.addDays(-(days - 1));

    // Tally completions that fall within the window.
    const auto tasks = m_store->completedTasks();
    for (const auto *t : tasks) {
        if (!t->completedAt().isValid()) continue;
        const QDate completedDate = t->completedAt().toLocalTime().date();
        if (completedDate < firstDay || completedDate > today) continue;
        const QString key = completedDate.toString(QStringLiteral("yyyy-MM-dd"));
        counts[key] += 1;
    }

    // Convert to QVariantList ordered from oldest to newest.
    QVariantList result;
    result.reserve(days);
    for (int i = days - 1; i >= 0; --i) {
        const QString key = today.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        QVariantMap entry;
        entry[QStringLiteral("date")]  = key;
        entry[QStringLiteral("count")] = counts.value(key, 0);
        result.append(entry);
    }
    return result;
}
