opentodo_add_unit_test(test_smoke          App)
opentodo_add_unit_test(test_models         Domain)
opentodo_add_unit_test(test_datastore      Infra)
opentodo_add_unit_test(test_filemanager    Infra)
opentodo_add_unit_test(test_dateparser_ru  Domain)
opentodo_add_unit_test(test_dateparser_en  Domain)
opentodo_add_unit_test(test_filterparser   Domain)
opentodo_add_unit_test(test_filterevaluator Infra)
# May 2026: new Todoist-parity predicates (due/due:N, subtask, view all,
# priority N..M, no labels, created today/this week). Test exercises both
# parsing (Domain) and evaluation against a DataStore fixture (needs Infra).
opentodo_add_unit_test(test_filterparser_extensions Infra)
opentodo_add_unit_test(test_merger         Infra)
opentodo_add_unit_test(test_recurrence     App)
opentodo_add_unit_test(test_viewmodels     App)
opentodo_add_unit_test(test_reminderscheduler Infra)
opentodo_add_unit_test(test_datastore_reset   Infra)
opentodo_add_unit_test(test_quickaddparser    Domain)
opentodo_add_unit_test(test_selection         App)
opentodo_add_unit_test(test_subtasks          App)
opentodo_add_unit_test(test_undostack         Infra)
opentodo_add_unit_test(test_label_color       Infra)
opentodo_add_unit_test(test_aiassistant       Infra)
opentodo_add_unit_test(test_subtask_generator Infra)
opentodo_add_unit_test(test_dragdrop          App)
opentodo_add_unit_test(test_updatechecker     Infra)
opentodo_add_unit_test(test_telemetry         Infra)
opentodo_add_unit_test(test_crdt              Infra)
opentodo_add_unit_test(test_syncengine        Infra)

# Quality round (May 2026): coverage for actions added in I18-I27.
opentodo_add_unit_test(test_taskviewmodel_actions     App)
opentodo_add_unit_test(test_pomodoroservice           Infra)
opentodo_add_unit_test(test_projectviewmodel_defaults App)

# Mega integration test — exercises every public Q_INVOKABLE/Q_PROPERTY/signal
# across DataStore, ViewModels, services, sync, NLP, filter DSL, recurrence,
# CRDT merger, HLC, telemetry call sites — in one big sequenced slot. Catches
# integration regressions when feature wiring drifts apart from individual
# unit-test fixtures.
opentodo_add_unit_test(test_mega_integration         App)

# Quality round 2 (May 2026): coverage for previously-untested ViewModels
# and edge cases for filter DSL + recurrence engine.
opentodo_add_unit_test(test_sidebarviewmodel         App)
opentodo_add_unit_test(test_karmaviewmodel           App)
opentodo_add_unit_test(test_searchviewmodel          App)
opentodo_add_unit_test(test_filter_edge_cases        Infra)
opentodo_add_unit_test(test_recurrence_edge_cases    App)

# Quality round 2.2: untested VMs (filter, notifications) + filemanager
# rotation + CRDT merger edge cases.
opentodo_add_unit_test(test_filterviewmodel          App)
opentodo_add_unit_test(test_notificationsviewmodel   App)
opentodo_add_unit_test(test_filemanager_backups      Infra)
opentodo_add_unit_test(test_merger_edge_cases        Infra)

# Quality round 2.3: SettingsViewModel + AIAssistant new methods +
# TaskExporter CSV escaping + QuickAdd parser extensions + HLC edge cases.
opentodo_add_unit_test(test_settingsviewmodel             App)
opentodo_add_unit_test(test_aiassistant_extended          Infra)
opentodo_add_unit_test(test_taskexporter                  Infra)
opentodo_add_unit_test(test_quickaddparser_extensions     Domain)
opentodo_add_unit_test(test_hlc_edge_cases                Infra)

# Quality round 2.4: ReminderScheduler edge cases + DataStore migration +
# extended drag-drop + ProjectViewModel CRUD + Telemetry retry/persistence.
opentodo_add_unit_test(test_reminderscheduler_edge_cases  Infra)
opentodo_add_unit_test(test_datastore_migration           Infra)
opentodo_add_unit_test(test_dragdrop_extended             App)
opentodo_add_unit_test(test_projectviewmodel_crud         App)
# Quality round Q3.07-C: ProjectViewModel hierarchy + immutability edges.
# 9 slots pinning cycle prevention when moving a parent under its own
# descendant, archive cascade (none today — pinned), Inbox immutability
# (SURFACED GAPS: rename/setParentId are silently accepted; only delete
# is rejected — asymmetric), color/icon Q_PROPERTY NOTIFY plumbing,
# reorder tie-break with duplicate `order` values (depends on std::sort
# non-stable contract — surfaced via qInfo), move-to-root via empty
# parentId, and back-to-back setName rename (no debounce — last wins).
opentodo_add_unit_test(test_projectviewmodel_hierarchy    App)
opentodo_add_unit_test(test_telemetry_retry               Infra)

# Quality round Q3.07-B: Telemetry retry-queue + persistence EDGE cases.
# Eight slots complementing test_telemetry (basic state) and
# test_telemetry_retry (anonymous-id lifecycle + PII allowlist). Covers
# disabled-then-re-enabled no-replay, very long string payload, queue
# persistence across restart (NEGATIVE pin — none today), 1000-event offline
# batching, unicode/emoji round-trip, disable-clears-queue (NEGATIVE pin),
# external QSettings change observed by next read, and burst rate limit
# (NEGATIVE pin — none today). qInfo() surfaces three documented gaps:
# in-memory-only queue, no max-queue cap, no rate limiter.
opentodo_add_unit_test(test_telemetry_edges               Infra)

# Quality round 2.5: UndoStack extended (cross-domain LIFO + capacity edge),
# SubtaskGenerator extended (parsing, busy-flag, count clamping), CrashLogger
# (rotation, level prefixes, restore handler), DailyReviewService (custom
# time, disable gate, payload, persistence).
opentodo_add_unit_test(test_undostack_extended            App)
opentodo_add_unit_test(test_subtaskgenerator_extended     Infra)
opentodo_add_unit_test(test_crashlogger                   Infra)
opentodo_add_unit_test(test_dailyreviewservice            Infra)

# Quality round 2.6: UpdateChecker semver/JSON/error-paths, DateParser
# round-trip invariants (parse(format(x)) == x), TodoistImporter API token
# + CSV escaping + ZIP fixtures, drag-drop mime data construction edges.
opentodo_add_unit_test(test_updatechecker_extended        Infra)
opentodo_add_unit_test(test_dateparser_invariants         Domain)
opentodo_add_unit_test(test_todoist_importer_extended     Infra)
opentodo_add_unit_test(test_dragdrop_mime                 App)

# Quality round 2.7: DataStore query edges (datetime-only today, soft-delete
# filter), FileManager 1s autosave debounce, Section/Comment/Reminder
# model + CRDT meta + DataStore ops.
opentodo_add_unit_test(test_datastore_queries             Infra)
opentodo_add_unit_test(test_filemanager_autosave          Infra)
opentodo_add_unit_test(test_section_model                 Infra)
opentodo_add_unit_test(test_comment_extended              Infra)
opentodo_add_unit_test(test_reminder_model                Infra)

# Quality round 2.8: Label color/edge cases + SnapshotManager + TaskViewModel
# filterMode coverage + SubtaskGenerator end-to-end via the QML wiring shape.
opentodo_add_unit_test(test_label_extended                Infra)
opentodo_add_unit_test(test_snapshot_manager              Infra)
opentodo_add_unit_test(test_taskviewmodel_filter          App)
opentodo_add_unit_test(test_subtaskgenerator_e2e          App)

# Quality round 2.9: stricter QML warning audit (catches Invalid property
# assignment, Binding loop, Delegate type mismatch, Layout-anchors warnings).
# Same env setup as test_qml_load_smoke.
opentodo_add_unit_test(test_qml_warning_audit             App)
target_link_libraries(test_qml_warning_audit PRIVATE OpenTodoAppplugin)
set_tests_properties(test_qml_warning_audit PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_qml_warning_audit>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Pin the production _meta double-wrap quirk (silent CRDT degradation; fix
# requires schema migration).
opentodo_add_unit_test(test_meta_double_wrap_pin          Infra)

# Pin per-field CRDT stamp keys against Task::toJson — catches iter-2-style
# drift where a mutator stamps a key that doesn't exist as a real JSON field
# (label_ids vs labels, due_date vs due, duration_amount vs duration, etc.).
opentodo_add_unit_test(test_taskvm_field_keys_pin         App)

# iter-night-4: DataStore::removeTask cascade depth + restoreTask symmetry +
# pre-existing-cycle defence. Was depth-1; grandchildren reminders kept
# firing OS-toasts for invisible tasks.
opentodo_add_unit_test(test_remove_task_deep_cascade      Infra)

# iter-night-6: SearchViewModel must include project-level comments
# (taskId="", projectId=p) in global search. Previously only task-level
# comments were searched, so Todoist-imported project notes were invisible.
opentodo_add_unit_test(test_search_project_level_comments App)

# iter-night-7: duplicateProjectAsTemplate must copy the full task hierarchy
# (subtasks at any depth) AND duplicate sections, remapping parent_id and
# section_id through old→new id maps. Previously subtasks were silently
# dropped and section_id pointed at sections in the source project.
opentodo_add_unit_test(test_duplicate_project_as_template App)

# iter-night-8: NotificationsViewModel::scanOverdueTasks cleanup must release
# m_overdueTaskIds for ANY task that is no longer overdue right now (not just
# date-cleared / completed / deleted). Past→future→past round-trips must fire
# a fresh "Просроченная задача" notification.
opentodo_add_unit_test(test_overdue_notification_round_trip App)

# Quality round 2.10: Q_GADGET value types + TaskViewModel selection state
# machine + HotkeyService (Win32 RegisterHotKey + native event filter).
opentodo_add_unit_test(test_q_gadgets                     Infra)
opentodo_add_unit_test(test_taskviewmodel_selection       App)
opentodo_add_unit_test(test_hotkeyservice                 App)

# Quality round 2.27: HotkeyService EDGE cases — pins behaviour not covered by
# test_hotkeyservice (basic surface) or test_hotkeyservice_conflicts (rebind +
# conflict probe). Seven slots: same-combo double-register idempotency,
# runtime-rebind OS-level release of the old combo, weird-input rejection
# (whitespace, embedded NUL, Win+J reserved), construction without a top-level
# window (filter installs on QCoreApplication path), disable/re-enable toggle,
# accepted modifier matrix (Ctrl/Alt/Shift/Meta/Win/Super aliases + no-mod
# rejection + case-insensitivity), and shortcutChanged signal vocabulary +
# idempotence. Uses QTEST_MAIN (QApplication) and an isolated QSettings
# namespace so parallel ctest runs don't conflict with the existing suites.
opentodo_add_unit_test(test_hotkeyservice_edges           App)

# Quality round 2.11: post-_meta-fix verification — field-level CRDT for
# all non-Task entity types, DataStore meta helpers + migration robustness,
# subtask hierarchy edge cases (multi-level promote/demote + cycles).
opentodo_add_unit_test(test_merger_field_level_per_entity Infra)
opentodo_add_unit_test(test_datastore_meta_helpers        Infra)
opentodo_add_unit_test(test_subtask_hierarchy_edge        App)

# Quality round 2.26: multi-device CRDT interleaving — 3/4-device conflict
# simulation against direct Merger::mergeStates calls. Complements the
# 2-device pairs covered by test_crdt / test_merger / test_merger_edge_cases /
# test_merger_field_level_per_entity / test_hlc_edge_cases. Nine slots pin
# current behaviour for: disjoint per-device field edits (each wins its own
# field), HLC-ordered writes through three devices, four-device OR-Set union,
# add-remove-readd OR-Set asymmetry (re-add only resurrects if HLC STRICTLY >
# remove HLC), tombstone absorbing later edits across three devices,
# physical-clock skew clamping lives in HLC::update not Merger, kanban
# concurrent section drag, schema-agnostic unknown-field forward-compat at
# Merger layer, and year-2099 HLC acceptance (no Merger-side clamping).
opentodo_add_unit_test(test_crdt_multidevice              Infra)

# Quality round 2.12: parallel-test flake fix (per-test org/app names) +
# SyncEngine state machine (offline) + FileManager error paths +
# SettingsViewModel reactivity + CalendarView QML smoke.
opentodo_add_unit_test(test_syncengine_state              Infra)
opentodo_add_unit_test(test_filemanager_error_paths       Infra)
opentodo_add_unit_test(test_settingsviewmodel_reactivity  App)
# i18n round (May 2026): coverage for canRestartApp() / restartApp() — the
# Q_INVOKABLE pair backing the "Restart" button on the language-change toast
# (SettingsSectionLanguage.qml). Six slots cover the desktop-true contract,
# idempotence, Q_INVOKABLE meta-object visibility for both methods (so a
# future drop of the macro can't silently break the QML caller), invocability
# via QMetaObject without actually invoking (since real invocation spawns a
# detached process), and distinct-meta-index sanity.
opentodo_add_unit_test(test_settingsviewmodel_restart     App)
opentodo_add_unit_test(test_calendar_view_smoke           App)
target_link_libraries(test_calendar_view_smoke PRIVATE OpenTodoAppplugin)
set_tests_properties(test_calendar_view_smoke PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_calendar_view_smoke>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Quality round 2.13: QuickAdd recurrence presets, InAppBanner smoke,
# full task lifecycle integration, settings export/import round-trip.
opentodo_add_unit_test(test_quickadd_recurrence_presets   App)
opentodo_add_unit_test(test_inapp_banner_smoke            App)
target_link_libraries(test_inapp_banner_smoke PRIVATE OpenTodoAppplugin)
set_tests_properties(test_inapp_banner_smoke PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_inapp_banner_smoke>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
opentodo_add_unit_test(test_task_lifecycle                App)
opentodo_add_unit_test(test_settings_export_import        App)

# Quality round 2.14: ProjectView/TrashView/AIView smoke (3 in one) +
# recurrence engine advance() chain & extreme edges.
opentodo_add_unit_test(test_views_smoke                   App)
target_link_libraries(test_views_smoke PRIVATE OpenTodoAppplugin)
set_tests_properties(test_views_smoke PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_views_smoke>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Q-Android-Audit #1.1: MobileLayout fullPageLoader smoke — pins the production
# fix for "tapping Settings/Productivity/etc on mobile only updates the top-bar
# title". For each of the 7 full-page filter modes the test instantiates
# MobileLayout.qml inside a wrapper Loader and asserts the inner fullPageLoader
# is `active: true` with `source` ending in the matching .qml filename. A
# negative slot pins that Inbox keeps the loader inactive so the task list
# renders. Same env shape as test_views_smoke / test_calendar_view_smoke (needs
# the OpenTodoUI plugin and the offscreen Qt platform plugin).
opentodo_add_unit_test(test_mobile_layout_views           App)
target_link_libraries(test_mobile_layout_views PRIVATE OpenTodoAppplugin)
set_tests_properties(test_mobile_layout_views PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_mobile_layout_views>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Drag-from-anywhere contract for TaskItem rows. Pins that a press+drag gesture
# at ANY point on a TaskItem row activates dragActive (the public observable
# property that gates Drag.active + the closed-hand cursor + the ⋮⋮ glyph fade)
# — not just clicks on the right-edge ⋮⋮ handle. Probes four x-coordinates at
# mid-row height (left+10 chevron region, centre, width-50 chip row, width-10
# right-edge handle) using QTest::mousePress/mouseMove/mouseRelease into a
# QQuickView so the events bypass the OS event queue. Guards against three
# regression classes: removing the rowDragHandler at TaskItem.qml ~line 223,
# changing its target away from null (which would reparent the visible
# delegate), and raising dragThreshold above ~10 px. Same env shape as
# test_views_smoke (needs the OpenTodoUI plugin + offscreen Qt platform).
opentodo_add_unit_test(test_taskitem_drag_handler         App)
target_link_libraries(test_taskitem_drag_handler PRIVATE OpenTodoAppplugin)
set_tests_properties(test_taskitem_drag_handler PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_taskitem_drag_handler>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Q-Loop B (May 2026): regression tests for two C++ bugs.
#   Bug A — TaskViewModel::m_selectedTask was a raw QObject* that dangled
#           after DataStore::clear() (sync pull / Todoist import / reset all).
#           Fix made it QPointer<QObject> + explicit clear in aboutToReset.
#           QTEST_MAIN because the test exercises the QML-facing property
#           contract under a QGuiApplication-style harness.
#   Bug B — ReminderScheduler::rescheduleAll filtered tombstoned reminders
#           BEFORE the cancelNotification() call, so the QTimer armed by an
#           earlier scheduleNotification kept ticking and fired a toast for
#           a task that no longer existed. Fix walks
#           DataStore::allRemindersIncludingDeleted() up-front and cancels
#           every is_deleted reminder. QTEST_GUILESS_MAIN — no GUI needed.
opentodo_add_unit_test(test_selectedtask_uaf              App)
opentodo_add_unit_test(test_reminder_cascade_cancel       Infra)
opentodo_add_unit_test(test_recurrence_advance_chain      Domain)

# Quality round 2.15: AchievementUnlocked wiring (in main.cpp + VM) +
# Pomodoro full N-cycle session integration. (Q2.15-B/C/E hit rate limit.)
opentodo_add_unit_test(test_pomodoro_full_session         Infra)

# Quality round 2.15-C: Q_INVOKABLE QObject*-return ownership audit.
# Documents that every Q_INVOKABLE in production headers returning a single
# entity pointer (Task*, Project*, Section*, Label*, Filter*, Reminder*,
# Comment*) returns an object with a Qt parent — preventing QML GC from
# reaping a borrowed pointer and causing a use-after-free in C++.
opentodo_add_unit_test(test_qobject_ownership_audit       App)

# GUI smoke test — boots the full QML engine and asserts no critical
# load failures. Catches the May 2026 black-screen incident class
# (qmlsettingsplugin not deployed → DesktopLayout fails → blank window
# despite green build + green non-GUI tests).
opentodo_add_unit_test(test_qml_load_smoke            App)
# Link the QML module's plugin target so the engine registers
# "OpenTodoUI" at startup; without this the engine fails with
# "No module named OpenTodoUI found" before reaching any assertion.
target_link_libraries(test_qml_load_smoke PRIVATE OpenTodoAppplugin)
# Override the WORKING_DIRECTORY: the smoke test loads the QML engine and
# the offscreen platform plugin, which transitively pulls in Qt6Quick /
# Qt6Qml / mingw runtime DLLs. Those live next to the executable in
# build/release/ — running from there ensures Windows DLL search resolves
# them all. The default WD (tests/unit/) leaves the offscreen plugin
# unable to load its mingw dependencies and the test exits before reaching
# main(). Other unit tests don't trip this because they're guiless.
set_tests_properties(test_qml_load_smoke PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_qml_load_smoke>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Quality round 2.16: NLP parser robustness — DoS-length inputs, NUL bytes,
# RTL+LTR scripts, NFC/NFD combining-diacritic equivalence, year overflow,
# emoji-only "dates", mixed RU/EN expressions. Bugs surfaced via qInfo()
# rather than QFAIL so the suite still passes if rules_ru has known quirks.
opentodo_add_unit_test(test_nlp_robustness                Domain)

# DateParser robustness v2 — behavioural pinning for tricky RU/EN phrases
# the existing dateparser suites haven't covered. 23 slots covering
# compound expressions ("через 2 недели в понедельник", "in 2 weeks on
# monday"), unsupported fortnightly forms ("каждый второй вторник",
# "every other tuesday"), bare nouns ("по будням", "weekdays", "в выходные",
# "weekends"), monthly-anchored-to-day-1 ("первого числа каждого месяца",
# "first day of each month"), invalid dates ("31 февраля", "feb 31"),
# out-of-range times ("завтра в 25:00", "tomorrow at 25:00"), word-only
# times ("yesterday at noon"), capitalization variants and whitespace
# tolerance. Pins 14 surface bugs via qInfo() + assertions on current
# (sometimes wrong) behaviour; flipping the assertions when production
# rules are fixed will be the regression bench.
opentodo_add_unit_test(test_dateparser_robustness_v2      App)

# Quality round 2.18 follow-up: QuickAddParser DEEPER adversarial coverage —
# token-grammar boundary cases that test_nlp_robustness misses (sigil at
# token boundary, repeated priorities, multi-# inbox-less Todoist convention,
# project name with embedded space, label with hyphen/dot/slash, escaped
# sigils, lone-sigil empty tokens, mid-sentence priority, 3 KB single token,
# 25 labels, pipe-as-content, mixed-case priority, recurrence + token
# interleaving). 15 slots, no production-code edits.
opentodo_add_unit_test(test_quickaddparser_adversarial    Domain)

# Quality round 2.16: schema_version downgrade — what happens when an
# older build loads a state.json written by a NEWER build (schema_version >
# CURRENT). Pins the "soft warning + best-effort load + downgrade-on-save"
# contract observed in DataStore::loadFromJson / toJson.
opentodo_add_unit_test(test_schema_downgrade              Infra)

# Quality round 2.16: SettingsSection*.qml smoke loader — boots each of the
# 9 SettingsSection components in isolation through QQmlComponent and
# verifies no critical load errors. Catches the ITER1 black-screen class
# where a bad import in one section can cascade and break the entire
# settings panel. Requires the OpenTodoUI QML plugin like other GUI smokes.
opentodo_add_unit_test(test_settings_sections_smoke       App)
target_link_libraries(test_settings_sections_smoke PRIVATE OpenTodoAppplugin)
set_tests_properties(test_settings_sections_smoke PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_settings_sections_smoke>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Quality round 2.17-A: TaskExporter ↔ TodoistImporter round-trip property
# test. Since the two paths don't share a wire format, the suite verifies
# round-trip via JSON export+reimport (lossless) and Todoist CSV-via-ZIP
# (lossy by design — drops _meta, tombstones, original UUIDs, dayOrder,
# isPinned; inverts priority). Pins which fields survive each path.
opentodo_add_unit_test(test_export_import_roundtrip       Infra)

# TaskExporter robustness — adversarial edge cases beyond the basic CSV
# escaping covered by test_taskexporter (round 2.3). Drives bare CR / CRLF
# inside content (RFC 4180 violation), embedded NUL bytes, 10 KB single
# task, deep parent chains (CSV/JSON keep them, MD silently drops),
# all-empty entity, NFC vs NFD survival, filename collision in same
# second, format consistency across CSV/MD/JSON for the same input,
# RTL + bidi-override content. 16 slots; pins three confirmed bugs
# (bare-CR-not-quoted, MD-drops-subtasks, MD-CRLF-stray-CR-in-description)
# that are surfaced via assertions so a future fix produces a visible
# test-suite signal.
# Source file is kept NUL-free: NUL bytes in test payloads are constructed
# at runtime via QChar(QChar::Null) (see the withNul() helper in slot 2),
# so AUTOMOC can scan the file and emit the required .moc.
opentodo_add_unit_test(test_taskexporter_robustness       Infra)

# Quality round Q3.07-B: TaskExporter MARKDOWN-format edge cases.
# Companion to test_taskexporter (basic) + test_taskexporter_robustness
# (general adversarial). This suite drills exclusively into exportToMarkdown
# / renderMdTaskRecursive for payloads that resemble Markdown syntax to a
# downstream CommonMark renderer.
#
# Nine slots covering: `*` and `_` (bold/italic emphasis vectors), triple-
# backtick code-block opener (defanged by the [\r\n]+ collapse in
# renderMdTaskRecursive — single-line form survives though), `[text](url)`
# link syntax pass-through (no URL encoding, no entity rewrite), title
# brackets including the adversarial `[x]` prefix that could be mis-read as
# a checked checkbox, HTML entity preservation (no decode, no double-encode),
# 3-level nested subtask indentation (depth * 2 for the checkbox, depth * 2
# + 2 for the description blockquote, ordering invariant: subtree completes
# before next root renders), filename collision uniquifier exercised at 99
# back-to-back exports inside a temp dir, and emoji round-trip via UTF-8
# byte sequences (BMP + supplementary plane + ZWJ sequence — plus the
# exporter's own 📅 and ⚑ glyphs).
#
# Suite-level contract: NO production-code changes. Every assertion pins
# current observed behaviour; flipping it when a fix lands is the
# regression-bench. Per-suite QSettings org/app via initTestCase keeps the
# test isolated from other taskexporter suites under `ctest -j`.
opentodo_add_unit_test(test_taskexporter_md_edges         Infra)

# IcsExporter — RFC 5545 VCALENDAR / VTODO serialisation for calendar-app
# subscriptions (Apple Calendar / Google Calendar / Outlook). 19 slots cover
# envelope shape, per-task VTODO emission, UID stability across re-exports,
# PRIORITY 1/3/5/9 mapping, STATUS COMPLETED/NEEDS-ACTION, DUE date-only vs
# datetime, RRULE for "every day"/"every monday"/"every 2 weeks"/"every
# weekday"/explicit RRULE pass-through, description fallback for
# unrecognised recurrence, text escaping (comma/semicolon/backslash/newline),
# CRLF line endings, 75-octet line folding with multi-byte UTF-8 safety,
# QSaveFile round-trip, file:// URL handling for the Q_INVOKABLE entry
# point, and an integration scenario combining all the above.
opentodo_add_unit_test(test_icsexporter                   Infra)

# Quality round 2.17-B: FilterParser adversarial / fuzz — 50-deep parens,
# 1000-OR chains, NUL bytes, mixed Unicode, unmatched brackets, garbage
# tokens. Confirmed: parser is genuinely robust — no crashes, no infinite
# loops, no stack overflow. Pins the "Empty query" lastError contract and
# documents that !!!!today is NOT canonicalised.
opentodo_add_unit_test(test_filterparser_adversarial      Domain)

# FilterParser perf + pathological inputs (extension of the adversarial
# suite). 9 slots covering 100/1000 nested parens, 10kB OR/AND chains,
# 1000 NOT chain, alternating-operator precedence stress, mid-chain garbage
# token, empty substrings, and unicode (Cyrillic + emoji) predicate names.
# Pins behaviour with timing budgets sized for slow CI VMs (50ms / 500ms).
# Notable pinned contract: bare identifiers (garbagetoken) silently parse as
# Identifier predicates and fall through to content-substring matching at
# eval time — a future tightening of this would surface as a regression here.
opentodo_add_unit_test(test_filterparser_perf             Domain)

# Quality round 2.22-B: DEEP edge-case coverage for FilterEvaluator (eval
# side, complementing the parser-side adversarial suite). 24 slots covering
# operator precedence, OR/AND short-circuit, triple-NOT, tombstone exclusion,
# nonexistent #Project, case-folding contract (ASCII + Cyrillic), regex
# metachars treated as literal, midnight boundary for dueDatetime-only tasks,
# `recurring` predicate ignoring RRULE validity, far-future before/after,
# `view all`, `assigned: me`, priority ranges, `no labels`, section name
# case-folding, `N days` inclusive range, De Morgan equivalence. Pins
# documented contracts via qInfo where the behavior is non-obvious.
opentodo_add_unit_test(test_filtereval_edge_cases_v2      Infra)

# Quality round 2.21: SearchViewModel DEEP edge-case coverage (App layer).
# 15 slots covering whitespace-vs-empty divergence, Cyrillic case folding,
# NFC/NFD diacritic non-equivalence, indexed-entity scope (sections + filters
# + reminders are NOT indexed — pinned), description-only matches, tombstone
# exclusion across all four indexed entity types, 10 KB DoS-class query,
# FilterParser operators passed verbatim, two-tier persistence (DataStore
# settings + QSettings last_query), no-debounce contract (debounce lives in
# QML), result-map shape vs MobileLayout/SearchBar bindings, mass-result no
# truncation, removeRecentSearch missing-query no-op-but-emits, live refresh
# drops tombstones. Pins five behavioural quirks via qInfo so a deliberate
# fix produces a visible test signal. No production-code edits.
opentodo_add_unit_test(test_searchviewmodel_edge_cases    App)

# Quality round Q3.07-A: RecurrenceEngine.advance() corner cases that the
# happy-path + edge_cases + advance_chain suites don't reach. 12 slots:
# BYMONTHDAY=31 in Feb (skip-on-missing), Feb 29 in non-leap year, BYDAY=-1FR
# (last Friday), weekly without BYDAY, INTERVAL=0/INT_MAX, UNTIL in past,
# COUNT=0, unknown FREQ, BYSETPOS, DTSTART with Z suffix. Behaviour-pin only;
# any surfaced engine bug is a separate work item.
opentodo_add_unit_test(test_recurrence_edges_v2           App)

# Quality round Q3.07: subtask hierarchy at depth >= 5 + cross-project move
# + cycle attempt at depth 5 + soft-delete cascade depth boundary. Sister
# of test_subtasks (depth-1) and test_subtask_hierarchy_edge (depth ~3).
# Nine slots, all App layer:
#   1. depth-5 chain — subtasks() returns DIRECT children only.
#   2. promote at depth 3 — descendants stay attached to the promoted node.
#   3. demote root under an existing subtask — succeeds at API level
#      (candidateParents hides it, but demoteTask itself accepts it).
#   4. moveTask across projects — pin that subtree projectId does NOT
#      cascade (gap vs Todoist parity; parent-child split across projects).
#   5. depth-5 cycle attempt — demoteTask BFS rejects.
#   6/7. complete-parent / subtask-of-completed-parent — no cascade in
#      either direction.
#   8. softdelete cascade — depth-1 only, grandchild C orphaned.
#   9. subtasks(<fresh-uuid>) — empty; subtasks("") — all top-level.
# Behaviour-pin only; no production-code changes.
opentodo_add_unit_test(test_subtask_deep_hierarchy        App)

# Quality round Q3.07: DataStore::loadFromJson migration edge cases that the
# existing test_datastore_migration + test_schema_downgrade suites don't
# cover. 9 slots: missing/null top-level entity keys, extra unknown top-level
# keys (forward-compat drop on resave), missing required id (no UUID
# fallback — collides in index), invalid UUID format (opaque-string contract),
# orphan parent_id / orphan label name (no cross-entity validation), settings
# dict forward-compat, embedded NUL bytes (constructed at runtime via
# QChar(QChar::Null) — source stays NUL-free for AUTOMOC), and 1 MB content
# string (no upper bound enforced on string fields — documented gap).
# Behaviour-pin only; no production-code changes.
opentodo_add_unit_test(test_datastore_migration_v2        Infra)

# Quality round 2.22-E: SyncEngine deep lifecycle coverage. Push during
# pending pull, snapshot compaction during sync, commit ordering (load-order
# is ALPHABETIC UUID, not creation-order — SURFACED BUG), HEAD upload
# atomicity (bool return ignored — SURFACED BUG), empty push/pull, deviceId
# stability across QSettings wipe, lastSyncTime gating on failure branches,
# happy-path signal sequence. Uses DriveApi::setNetworkManagerForTesting +
# file-scope MockNAM for canned HTTP responses. Pins behavioural quirks via
# qInfo so a fix produces a visible signal.
#
opentodo_add_unit_test(test_syncengine_lifecycle          Infra)

# Quality round 2.17-C: Karma day-rollover and streak edge cases. SURFACED
# TWO REAL PRODUCTION BUGS (documented via qInfo, slots stay green):
#   1. m_lastStreakIncrementDate is in-memory only — restart wipes streak
#      on first completion.
#   2. karmaScore double-counts on toggle (uncomplete → recomplete adds
#      +2) while completedToday only counts +1 — karma is farmable.
opentodo_add_unit_test(test_karma_rollover                App)

# Quality round 2.17-D: SnapshotManager compaction equivalence property
# test. Replay-A == Replay-B-then-compact == Load-snapshot-then-tail. SURFACED
# CRITICAL CRDT BUG: SyncEngine::applyCommit silently drops inbound
# per-field _meta in every *_update branch, degrading per-field LWW to
# whole-object LWW for inbound updates. (See Q2.18-FOLLOWUP.) Plus
# secondary atomicity gap in snapshot.cpp (deleteFile loop continues on
# failure but resetCommitCount runs unconditionally).
opentodo_add_unit_test(test_snapshot_compaction_equivalence Infra)

# SnapshotManager corruption-resilience pinning. Eight slots covering
# the on-disk snapshot blob (state.json) being garbage / truncated /
# missing / half-written, forward-compat against unknown entity types,
# orphan-tombstone preservation through loadFromJson, the >= boundary
# in shouldCompact() at exact threshold, and the atomicity contract on
# the early-exit failure branches (no client / no auth — counter and
# log files preserved). Complements test_snapshot_compaction_equivalence
# (replay equivalence) and test_snapshot_manager (pure threshold logic).
opentodo_add_unit_test(test_snapshot_corruption           Infra)

# Quality round 2.18-A: regression coverage for the Q2.17-D CRDT bug. Pins
# that SyncEngine::applyCommit's *_update branches now push inbound _meta
# back into DataStore::m_*Meta via applyInbound<Entity>Meta, and that
# markTaskModified is no longer called for inbound updates (which would
# clobber the inbound updated_at stamp with the local HLC). Six slots cover
# task + project + section + label + filter + reminder + comment update
# branches, signal-count contract, and the "trust the wire" semantics.
opentodo_add_unit_test(test_applycommit_meta_preservation Infra)

# Quality round 2.17-E: SyncEngine resilience under failure paths
# (offline, fake-token 401, repeated push, corrupt local commit, no-auth
# pull, fake-auth pull). Pending commits stay on disk across all six —
# canary holds. Documented gaps: no engine-level authError signal,
# loadPendingCommitsFromDisk accepts JSON missing required fields
# (weak validation), push/pull not virtual (DI blocked).
opentodo_add_unit_test(test_syncengine_resilience         Infra)

# Quality round Q3.01-B: SyncEngine PULL-side corruption resilience.
# Seven slots driving pull() through deliberately corrupted remote payloads
# via DriveApi::setNetworkManagerForTesting + a file-scope MockNAM (same
# pattern as test_syncengine_lifecycle / test_gdrive_resilience).
#
# Coverage:
#   1. Corrupted state.json body (truncated JSON) — B-2.42 guard fires,
#      status=Error, local data preserved.
#   2. state.json valid but missing "tasks" key ({}) — same B-2.42 guard
#      behaviour; documented as defensive but somewhat surprising for the
#      legitimately-empty-remote case.
#   3. 1 MB state.json round-trip succeeds (proxy for the 100 MB scenario);
#      DOCUMENTED GAP: no upper bound enforced by DriveApi::downloadFile or
#      SyncEngine — peer could push huge payload.
#   4. One malformed commit file alongside a valid one — the bad commit is
#      silently SKIPPED (syncengine.cpp:198-207 `if (!doc.isObject())
#      continue`), good neighbour applied, pull reaches Synced. DOCUMENTED
#      DATA-LOSS RISK: operations in the bad commit are now never applied.
#   5. HEAD pointing at a commit absent from /log/ — pull does NOT abort;
#      walks the empty log, applies state.json, stores dangling HEAD as
#      m_headCommitId. DOCUMENTED GAP: no consistency check between HEAD
#      and log contents.
#   6. Commit with mixed valid + unknown operation types — applyCommit's
#      else branch (syncengine.cpp:634-636) logs + skips the unknown op,
#      valid op in the same commit is applied normally.
#   7. Pull creating a 3-cycle parent_id chain (A→C→B→A via task_add +
#      task_update) — applyCommit does NOT validate; cycle is allowed.
#      Safety net: DataStore::subtasks is non-recursive, so reading the
#      structure doesn't crash. DOCUMENTED GAP: any caller that walks
#      ancestors transitively must defend against the cycle.
#
# Suite-level contract: NO production sync code modified. Assertions pin
# current behaviour; flipping them when a fix lands is the regression-bench.
opentodo_add_unit_test(test_syncengine_resilience_v2      Infra)

# REGRESSION TEST for the "Не раскрывается родительский проект 'Дом'" bug
# (user-reported 2026-05-12). Documents the QML-vs-C++ split: SidebarViewModel
# exposes NO expansion API; the QML chevron + expandedProjects map is the only
# state, and the chevron-doesn't-toggle regression lives in QML, not C++.
# Pins the C++ contract that the QML rebuild() depends on.
opentodo_add_unit_test(test_sidebar_expand                App)

# Quality round 2.19-D: AIAssistant safety audit — huge content clamping,
# garbage response handling, cancel race, log leakage, prompt-injection role
# preservation, empty-input fast-fail. Uses a custom QNetworkAccessManager
# installed via setNetworkManagerForTesting() to intercept outbound HTTP.
opentodo_add_unit_test(test_aiassistant_safety            Infra)

# ShareManager + uploadPublicFile happy paths and failure modes. Mocks the
# Drive HTTP layer via DriveApi::setNetworkManagerForTesting (MockNAM —
# same pattern as test_gdrive_resilience). Eight slots covering happy
# paths, rollback on permission failure, unauthenticated/unknown-id quiet
# rejection, revoke round-trip, and webViewLink fallback URL synthesis.
opentodo_add_unit_test(test_sharemanager                  Infra)

# Quality round 2.19-E: GDriveClient / DriveApi resilience audit. Uses a
# MockNAM (file-scope QNetworkAccessManager subclass) installed via
# DriveApi::setNetworkManagerForTesting() to simulate token refresh failure,
# 403/413 quota, 5xx without retry, network drop mid-upload, 404 file not
# found, recreated root folder, peer-held .lock, clock skew on lock
# freshness, and a perf observation for 10 MB state.json upload. Documents
# multiple gaps (no 5xx backoff, no resumable upload, lock cache staleness)
# via qInfo for the next maintainer.
opentodo_add_unit_test(test_gdrive_resilience             Infra)

# Quality round 2.18: BoardView (kanban) drag-drop edge cases. Twelve slots
# covering cross-section move, cross-project drop, "(Без раздела)" pseudo-
# column drop, empty-column round-trip with order/dayOrder invariant,
# same-section noop signal/undo contract, unknown-id quiet rejection, bulk
# multi-select section-clearing quirk, rename-section dialog round-trip
# (surfaced the BoardView dataStore.updateSection → renameSection typo fix
# in this branch), delete-section idempotence, tombstone exclusion from
# columns, repeated-drop dedupe, drag-cancel zero-call invariant.
opentodo_add_unit_test(test_board_dragdrop_edges          App)

# REGRESSION TEST for the "Послать тестовое" Windows-toast crash
# (reported by user 2026-05-09). Root cause was main.cpp using
# QGuiApplication while WindowsNotificationService uses QSystemTrayIcon
# (a QtWidgets type that requires QApplication). Constructing the tray
# icon under QGuiApplication crashed on the first showMessage() call.
# This test uses QTEST_MAIN (not QTEST_GUILESS_MAIN) so it runs under a
# real QApplication and would have caught the regression. Pins the
# QApplication-not-QGuiApplication contract going forward.
opentodo_add_unit_test(test_notification_service_smoke    App)

# WindowsNotificationService behavioural edge-cases (Infra layer). Eight
# slots pinning current behaviour for empty title/body, 10 KB body, 100
# rapid notifications, hasPermission()/requestPermission() Windows contract
# (no runtime grant prompt + queued completion signal fires exactly once),
# zero-signal construction, and unicode + emoji round-trip via std::wstring.
# Same QApplication-not-QGuiApplication harness as test_notification_service_smoke
# (QSystemTrayIcon needs QApplication). Non-Windows builds compile to a
# single QSKIP("Windows-only") slot — see #ifdef Q_OS_WIN guard at file scope.
opentodo_add_unit_test(test_notificationservice_win_edges Infra)

# Quality round 2.19: HotkeyService conflict detection + rebinding +
# modifier-only rejection. New surface: setShortcut(QString) (live re-bind),
# wouldConflict(QString) (probe + release), isValidShortcut(QString),
# registrationFailed(shortcut, reason) (2-arg signal). Tests use QTEST_MAIN
# under QApplication because RegisterHotKey needs a window handle.
opentodo_add_unit_test(test_hotkeyservice_conflicts       App)

# Quality round 2.19-A: deep-dive edge cases for three services that had
# only superficial coverage — PomodoroService (crash recovery, mid-session
# settings change, suspend/resume, custom-duration extremes),
# DailyReviewService (DST spring-forward / fall-back, mid-session reschedule,
# clock skew, first-run defaults), UpdateChecker (network down without
# infinite retry, malformed JSON variants, version-compare edges,
# rate-limit / re-entrancy protections).
opentodo_add_unit_test(test_pomodoro_edges                Infra)
opentodo_add_unit_test(test_dailyreview_dst               Infra)
opentodo_add_unit_test(test_updatechecker_resilience      Infra)

# DST / timezone audit for ReminderScheduler (sister to test_dailyreview_dst).
# Eight slots covering: spring-forward 02:30 wall-clock gap, fall-back 01:30
# ambiguous wall-clock (no scheduler-induced double-fire), recurring daily
# reminder across DST (wall-clock 09:00 preserved — scheduler reads
# task->dueDatetime each pass, never adds 24h to lastFire), TZ-tagged ISO
# datetime (setTimeZone retags without shifting — C-5 bug-fix contract),
# weekly recurring across DST (Wed 10:00 stays Wed 10:00), ISO storage is
# timezone-naive local wall-clock (TZ change moves the reminder), Feb 29 in
# non-leap year (invalid QDate -> silent skip, no auto Feb 28 fallback), and
# far-past reminders (24h / 180d / epoch+1s all silently skipped, no negative
# arithmetic). Pins behavior — no production-code changes in this round.
opentodo_add_unit_test(test_reminderscheduler_dst         Infra)

# Narrow-width DesktopLayout responsive-collapse pinning. Boots a
# QQmlApplicationEngine, mounts DesktopLayout.qml inside a wrapper Item at
# four representative widths (200 / 500 / 800 / 1200 px), and verifies the
# sidebar collapses below the 800 px breakpoint while the hamburger button
# remains reachable and the main content stays visible. Catches a future
# regression of the "sidebar consumes the whole window" class.
# Needs the OpenTodoUI plugin and the deployed Qt.labs.settings module on
# the import path — same env shape as test_views_smoke / test_calendar_view_smoke.
opentodo_add_unit_test(test_desktop_narrow_layout         App)
target_link_libraries(test_desktop_narrow_layout PRIVATE OpenTodoAppplugin)
set_tests_properties(test_desktop_narrow_layout PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_desktop_narrow_layout>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Pins the QVariantMap shape of ProjectViewModel::childProjects() — the
# QML side iterates this list as a JS array (`.length`, `[i].id`,
# `[i].isArchived`). Returning QVariant::fromValue(Project*) entries
# (the original shape) silently broke that iteration on Qt 6.11: every
# project's `hasChildren` evaluated false, the chevron stayed hidden,
# and clicking expand never reached the toggle path. Test catches the
# regression early.
opentodo_add_unit_test(test_childprojects_qml_shape       App)

# WindowChrome + ThemeManager regression coverage.
#
# WindowChrome contract test (Infra layer): structural pinning for the Win32
# dark-title-bar helper. Verifies the Q_INVOKABLE surface is stable, nullptr
# inputs are quiet no-ops, the DWMWA_USE_IMMERSIVE_DARK_MODE constant is the
# Win10 1809+ canonical value (20), and the dwmapi link path resolves at
# runtime via a real QWindow. QTEST_GUILESS_MAIN — no QML engine needed.
opentodo_add_unit_test(test_windowchrome_contract         Infra)
#
# ThemeManager switch test (App layer): boots a QQmlApplicationEngine, loads
# the ThemeManager singleton via `import OpenTodoUI`, iterates every bundled
# palette, verifies the live colour properties update reactively, the
# corresponding NOTIFY signals fire, custom accent override wins, and theme
# persistence survives a DataStore toJson + loadFromJson round-trip. QTEST_MAIN
# (needs QGuiApplication for the QML engine + Material style). Linked against
# the OpenTodoApp QML plugin so OpenTodoUI registers at startup.
opentodo_add_unit_test(test_thememanager_switch           App)
target_link_libraries(test_thememanager_switch PRIVATE OpenTodoAppplugin)
set_tests_properties(test_thememanager_switch PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_thememanager_switch>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Quality round 2.20: TodoistImporter ROBUSTNESS — adversarial coverage for
# edge cases the basic + extended suites miss. Twelve slots covering malformed
# ZIPs (truncated, tiny garbage, unsupported compression method, 200-entry
# stress), unusual encodings (CP-1251 fallback, UTF-16LE BOM), degenerate CSV
# bodies (whitespace, BOM-only, no header), invalid priority values (0, 5, -1,
# "high"/"low"), date-format variants (MM/DD/YYYY, DD.MM.YYYY, natural-language),
# edge-content section names (Cyrillic, emoji, 220 chars), label/project color
# mapping, deep subtask chain (5 levels), image-attachment marker preservation,
# and re-run dedup. Surfaces three real bugs (priority-0 collapse to p4,
# non-ISO date silently downgraded, CSV re-run duplicates because createdAt
# is set to now()) via qInfo() + pinned assertions.
opentodo_add_unit_test(test_todoist_importer_robustness   Infra)

# Quality round Q3.07: TodoistImporter PRO-feature edges. Nine slots pinning
# behaviour for Todoist Pro corners not covered by the basic / extended /
# robustness suites: (1) TYPE=section row creates Section entity but does NOT
# wire task.sectionId on following tasks — documented gap; (2) TYPE=note rows
# attach as Comments to preceding task; (3) multi-CSV ZIP imports all projects
# disjoint; (4) DATE="каждый день" + DATE_LANG=ru sets dueIsRecurring=true but
# loses the FREQ=DAILY RRULE because TodoistDue has no recurrenceRule member —
# documented gap; (5) INDENT 1→2→3 builds A → B → C parent chain; (6) header-
# only CSV creates Project, zero Tasks; (7) duplicate CONTENT in one CSV
# coalesces via in-import fingerprint dedup (documented trade-off vs. spec —
# the slot pins the current "collapses to one" behaviour); (8) named palette
# (berry_red / lavender / olive_green / …) maps documented hex values via
# mapTodoistColor but CSV-path projects always land grey (no colour column in
# CSV export); (9) UTF-8 BOM at byte 0 strips cleanly via parseBytes BOM
# branch. Surfaces 3 gaps (section-id, dueRecurrenceRule, in-csv dedup) via
# qInfo + pinned assertions, no production-code edits.
opentodo_add_unit_test(test_todoist_importer_pro          Infra)

# Q2.26-B: Unicode NFC/NFD normalization for search. Covers the pure
# TextSearch helper (NFKC + Mn-strip + case fold) plus SearchViewModel and
# FilterEvaluator `search:` integration. Acceptance contract: "café" finds
# "café", "Москва" finds "москва", ё matches е under default accent-
# insensitive mode, "fiance" finds "ﬁancé" via NFKC ligature fold, toggle
# via SettingsKeys::SearchAccentSensitive.
opentodo_add_unit_test(test_textsearch                    App)

# Todoist import tests — needs fixture ZIP next to the binary.
qt_add_executable(test_todoist_import test_todoist_import.cpp)
target_link_libraries(test_todoist_import PRIVATE OpenTodoInfra Qt6::Test)
target_compile_definitions(test_todoist_import PRIVATE
    TODOIST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/todoist_sample"
)
add_test(NAME test_todoist_import COMMAND test_todoist_import)
set_tests_properties(test_todoist_import PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
# Copy fixture ZIPs alongside the test binary so the build-dir path works too.
add_custom_command(TARGET test_todoist_import POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:test_todoist_import>/fixtures/todoist_sample
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/todoist_sample/Todoist Backup 2024-01-01.zip"
        "$<TARGET_FILE_DIR:test_todoist_import>/fixtures/todoist_sample/Todoist Backup 2024-01-01.zip"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/todoist_sample/user_backup.zip"
        "$<TARGET_FILE_DIR:test_todoist_import>/fixtures/todoist_sample/user_backup.zip"
    COMMENT "Copying Todoist fixture ZIPs to test output dir"
)
