#pragma once

// Centralised settings key constants used by DataStore::setting() /
// DataStore::setSetting(). All keys are snake_case to match the JSON field
// names stored in state.json.
namespace SettingsKeys {

// General / UI
inline constexpr auto Theme                = "theme";
inline constexpr auto Language             = "language";
inline constexpr auto StartPage            = "start_page";
inline constexpr auto TimeFormat           = "time_format";
inline constexpr auto DateFormat           = "date_format";
inline constexpr auto WeekStart            = "week_start";
inline constexpr auto NextWeekDay          = "next_week_day";
inline constexpr auto VacationMode        = "vacation_mode";

// Reminders
inline constexpr auto DefaultReminderTime  = "default_reminder_time";
inline constexpr auto AutoReminderOffset   = "auto_reminder_offset";

// Sidebar counts
inline constexpr auto ShowInboxCount       = "show_inbox_count";
inline constexpr auto ShowTodayCount       = "show_today_count";
inline constexpr auto ShowUpcomingCount    = "show_upcoming_count";

// Sync
inline constexpr auto SyncInterval         = "sync_interval";

// Karma / Productivity
inline constexpr auto DailyGoal            = "daily_goal";
inline constexpr auto WeeklyGoal           = "weekly_goal";
inline constexpr auto KarmaScore           = "karma_score";
inline constexpr auto KarmaLevel           = "karma_level";
inline constexpr auto DailyStreak          = "daily_streak";
inline constexpr auto WeeklyStreak         = "weekly_streak";
inline constexpr auto KarmaTrend           = "karma_trend";

// Search
inline constexpr auto RecentSearches       = "recent_searches";

// Permissions / first-run gates
inline constexpr auto NotificationPermissionAsked = "notification_permission_asked";

// Auto-archive
// When > 0, projects whose last task activity is older than this many days
// are automatically archived on startup (if all tasks are completed).
// 0 = feature disabled (default).
inline constexpr auto AutoArchiveOldProjectsDays  = "auto_archive_old_projects_days";

// Task row density: "comfortable" (44 px, default) | "compact" (32 px)
inline constexpr auto TaskDensity                 = "task_density";

// Last-viewed restoration — persisted on aboutToQuit, restored on next startup
// before the QML engine loads. All keys are cleared to empty string when there
// is nothing to restore (e.g. the user closed the app with no task open).
inline constexpr auto LastViewedTaskId = "last_viewed_task_id";
inline constexpr auto LastFilterMode   = "last_filter_mode";   // QMetaEnum key, e.g. "Inbox" / "Today"
inline constexpr auto LastProjectId    = "last_project_id";    // non-empty for Project / Board modes
inline constexpr auto LastFilter       = "last_filter";        // non-empty for Filter / Label modes

} // namespace SettingsKeys
