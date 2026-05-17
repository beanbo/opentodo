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
// Q2.26-B: accent-insensitive search by default (NFKC + Mn-strip + case fold
// in src/utils/textsearch). When true, search becomes accent-sensitive
// (ё ≠ е, é ≠ e). Default false = "café" matches "café" matches "cafe".
inline constexpr auto SearchAccentSensitive = "search_accent_sensitive";

// Permissions / first-run gates
inline constexpr auto NotificationPermissionAsked = "notification_permission_asked";

// Auto-archive
// When > 0, projects whose last task activity is older than this many days
// are automatically archived on startup (if all tasks are completed).
// 0 = feature disabled (default).
inline constexpr auto AutoArchiveOldProjectsDays  = "auto_archive_old_projects_days";

// Task row density: "comfortable" (44 px, default) | "compact" (32 px)
inline constexpr auto TaskDensity                 = "task_density";

// Windows / Linux / macOS system tray icon. true = show OpenTodo icon in the
// notification area with the context menu (Open / Quick Add / Settings /
// Quit). false = hide it. Default true on desktop. Ignored on Android/WASM
// (no tray surface). Read by main.cpp at startup and on every settingChanged.
inline constexpr auto ShowTrayIcon                = "show_tray_icon";

// Custom accent color override as a "#RRGGBB" hex string. Empty means the
// active theme palette's native accent is used; otherwise this value wins
// across the whole app. Mirrored from the QML SettingsSectionAppearance
// `Settings { category: "appearance" }` block so that C++ services (e.g.
// TrayIconService for the badge tint) can read it without reaching into
// QML, and so that it persists alongside the rest of the user's settings
// in state.json (in addition to the platform QSettings backing of the QML
// Settings element).
inline constexpr auto CustomAccentHex             = "custom_accent_hex";

// Q3.10: backup retention + vacuum policy.
// BackupRetentionCount — max dated backups kept on disk (default 50).
// BackupRetentionDays  — drop dated backups older than this many days
//                        (default 30; safety floor keeps newest one regardless).
// VacuumThresholdBytes — when state.json exceeds this on disk, FileManager
//                        emits vacuumRecommended() so the UI can prompt
//                        (default 10 * 1024 * 1024 = 10 MB).
inline constexpr auto BackupRetentionCount  = "backup_retention_count";
inline constexpr auto BackupRetentionDays   = "backup_retention_days";
inline constexpr auto VacuumThresholdBytes  = "vacuum_threshold_bytes";

// Last-viewed restoration — persisted on aboutToQuit, restored on next startup
// before the QML engine loads. All keys are cleared to empty string when there
// is nothing to restore (e.g. the user closed the app with no task open).
inline constexpr auto LastViewedTaskId = "last_viewed_task_id";
inline constexpr auto LastFilterMode   = "last_filter_mode";   // QMetaEnum key, e.g. "Inbox" / "Today"
inline constexpr auto LastProjectId    = "last_project_id";    // non-empty for Project / Board modes
inline constexpr auto LastFilter       = "last_filter";        // non-empty for Filter / Label modes

} // namespace SettingsKeys
