# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once it reaches a tagged release.

## [Unreleased]

### Added

- Todoist importer pro mode with subtask hierarchy up to depth 5.
- Project hierarchy support in `ProjectViewModel`.
- Markdown task export (`TaskExporter`) with proper edge-case handling.
- Telemetry hooks for edge events.
- Five-language UI coverage: English, Russian, German, Ukrainian, Belarusian.
- App restart button in settings.
- "Drag from anywhere" behavior for task reordering.
- Sync status labels in the sidebar.
- Sidebar chevron double-click to expand a project tree.
- Support page accessible from settings.
- Filter DSL extensions: `created before/after`, `assigned`, recurrence predicates.
- 24 new edge-case tests for `FilterEval`.
- CRDT, use-after-free, and cascade-delete regression tests.
- Drag-handler test coverage and karma persistence audit.
- Bug-report and feature-request issue templates, pull-request template, and contributing guide.

### Changed

- `DataStore` migration bumped to v2.
- Inbox immutability is now symmetric across move and update paths.
- `moveTask` correctly cascades `project_id` and wires `section_id`.
- Hybrid Logical Clock (HLC) is clamped inside the merger to prevent skew.
- `SyncEngine` resilience pass — retry, partial-failure, and lifecycle handling.
- Snapshot writes are now atomic; corruption regression pinned.
- Filter parser performance pinned with regression test.
- Multi-device CRDT scenarios pinned with explicit test cases.
- Pomodoro timer overflow clamped.
- AI prompt-injection input containment with cancel API.
- Sidebar chevron hit-zone enlarged for easier targeting.
- Narrow-window layout tuned for small desktop windows.
- Android audit pass — layout and lifecycle fixes.

### Fixed

- UTC vs local-date mismatch in `created-*` filter tokens.
- Six `DateParser` defects across recurrence and natural-language paths.
- Daylight-saving-time and timezone edge cases in date parsing.
- `applyCommit` infinite-loop on parent-cycle in project hierarchy.
- Four `TaskExporter` defects, including NUL-byte handling in exported strings.
- Five `TodoistImporter` defects (parser `assigned:` token, depth handling, label edges).
- QML dialog binding-loop warnings.
- Sidebar chevron expand state and language-switch persistence.
- Parallel-test flake in `TaskExporter` suite.
- Logging hygiene (noise reduction, redaction of sensitive paths).
- Windows `NotificationService` and `HotkeyService` edge cases (toast lifetime, hotkey re-registration).
- Drive sync resilience under transient network failure.

### Removed

- Accidentally-committed stdout debug file.
- Six obsolete translation entries during `lupdate` refresh.

---

This project does not yet have a tagged release. Once `v0.1.0` ships, prior changes will be moved out of `[Unreleased]` into a dated section here.
