# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

OpenTodo is a Qt 6 / QML (C++20) cross-platform task manager inspired by Todoist, targeting Windows/Linux desktop, Android, and **Web (WASM)**.

## Build

Qt 6.11.0 is the validated version. Required modules: `Core Gui Quick Qml Network Widgets QuickControls2`. Optional: `Positioning` for geofenced reminders.

```
cmake -S . -B build
cmake --build build
```

The QML module URI is `OpenTodoUI`; the entry point is `engine.loadFromModule("OpenTodoUI", "Main")`. Resources use the `RESOURCE_PREFIX "/qt/qml"` (Qt 6.5+ recommended layout) so `Main.qml` lives at `qrc:/qt/qml/OpenTodoUI/qml/Main.qml`.

The QML module sits on the `OpenTodoApp` **library** target (not the executable) because androiddeployqt and Qt's WASM static-link path both resolve module imports through the library's standard QML output directory.

Platform-conditional sources in [CMakeLists.txt](CMakeLists.txt):
- `WIN32` → `notificationservice_win.cpp`, `hotkeyservice_win.cpp`, `dwmapi` link for dark title bar
- `ANDROID` → `notificationservice_android.cpp` plus `hotkeyservice_stub.cpp`; `QT_ANDROID_PACKAGE_SOURCE_DIR=android/` (Java helpers in [android/src/org/opentodo/](android/src/org/opentodo/))
- `EMSCRIPTEN` → `notificationservice_wasm.cpp` plus `hotkeyservice_stub.cpp`; link options add `-sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -lidbfs.js` (IDBFS for state persistence)

For the WASM build see [docs/WASM.md](docs/WASM.md) and [tools/build-wasm.bat](tools/build-wasm.bat). The Qt WASM toolchain needs `QT_CHAINLOAD_TOOLCHAIN_FILE` pointed at the actual emsdk Emscripten.cmake.

The test suite (Qt Test + QtQuickTest) is in [tests/](tests/) — ~170 ctest targets (C++ unit tests covering Domain/Infra/App layers plus QML load/behaviour smoke tests). Run with `ctest --test-dir build` (`build/release` with multi-config generators). Every test source must be registered in [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt) via `opentodo_add_unit_test`.

## Architecture

The app is a single-process Qt app with a strict layered design. Layers communicate via Qt signals/slots; most types are `Q_OBJECT` with `Q_PROPERTY` + `Q_INVOKABLE` so QML can consume them directly.

**Wiring lives in [src/main.cpp](src/main.cpp)** — it constructs one `DataStore`, one `FileManager`, one `SyncEngine`, platform services, all ten ViewModels, and exposes them to QML as `QQmlContext` context properties (`dataStore`, `taskViewModel`, `syncViewModel`, etc.). Despite `QML_SINGLETON` declarations on some classes (e.g. `DataStore`), access from QML uses the context-property names set in `main.cpp` — do not try to `import`-as-singleton.

### Data layer — [src/model/](src/model/), [src/storage/](src/storage/)

- Every model (Task, Project, Section, Label, Filter, Reminder, Comment) has a UUID (`utils/uuid.h`), `created_at`/`updated_at`, and a soft-delete tombstone (`is_deleted`). `toJson()`/`fromJson()` round-trip is authoritative; add new fields in both.
- [DataStore](src/storage/datastore.h) is the in-memory source of truth, holding parallel `QList` + `QMap` index per entity type. Mutations emit both `<entity>Changed()` and the aggregate `dataChanged()` signal.
- [FileManager](src/storage/filemanager.h) listens on `DataStore::dataChanged`, debounces for 1 s, then writes `state.json` atomically via `QSaveFile`. Storage root: `$APPDATA/OpenTodo/` on desktop, `$APPDATA` directly on Android. Pending commits live under `local_log/`.

### Sync layer — [src/sync/](src/sync/)

Google Drive is the only remote. The model is a commit log on top of a state snapshot:
- A [Commit](src/sync/commit.h) is a list of `SyncOperation {type, entityId, data}` (e.g. `task_add`, `task_update`, `project_delete`) — see the dispatch table in [SyncEngine::applyCommit](src/sync/syncengine.cpp). If you add a new entity type or mutable field, you must extend both this dispatch and [Merger](src/sync/merger.h).
- [SyncEngine](src/sync/syncengine.h) push/pull flow: acquire remote `.lock`, upload each pending commit under `log/<uuid>.json`, write `HEAD` + `state.json`, release lock. In practice nothing in the app calls `createCommit()` yet, so sync is driven by the state snapshot: push and pull download `state.json`, `Merger::mergeStates` it with the local state (union of entities, per-field CRDT), reload only if the result differs, and upload. A push/pull requested while one is running is deferred and re-run once (`isBusy()`). Pending commits persist locally across restarts in `local_log/`. `deviceId` lives in `QSettings` under `sync/device_id`. Inbound commits advance the local HLC via `HybridLogicalClock::update`.
- **CRDT conflict resolution** ([Merger](src/sync/merger.cpp)): per-field LWW Register stamped with a Hybrid Logical Clock ([HLC](src/sync/hlc.h)) instead of whole-object LWW. Per-field metadata lives under `entity["_meta"][fieldName] = {ts, dev}`. Two devices editing different fields concurrently both win their respective fields. The OR-Set in [CrdtMeta](src/sync/crdt_meta.h) handles add-wins for ordered collections (`label_ids`). Tombstones take precedence over ordinary edits; only an explicit restore (`restoreTask` / undo-delete, which stamps `_meta.is_deleted`) whose stamp is strictly newer than the tombstone's revives an entity. Settings merge per key: `DataStore::setSetting` stamps each changed key in a top-level `settings_meta` object (never inside `settings`); keys without stamps fall back to remote-wins. Entities without `_meta` (legacy local files) fall back to the original whole-object LWW.
- **OR-Set re-add semantics**: A re-add after a remove only resurrects the element if its HLC is STRICTLY greater than the remove's HLC. Equal HLC (a rare cross-device tie) ⇒ remove wins. Earlier HLC (concurrent older add) ⇒ remove wins. Within a single device, re-add always wins because the HLC advances monotonically between calls. Across devices with skewed clocks, the result is decided by HLC comparison (with deviceId as final tiebreak on identical physMs+counter). See the doc block in [src/sync/crdt_meta.h](src/sync/crdt_meta.h) above `OrsElement` and the pinning tests `orset_concurrentRemoveThen*` / `orset_localRemoveThenLocalReAdd_resurrects` / `orset_sameHlcTie_deterministicAcrossDeviceIds` in [tests/unit/test_crdt.cpp](tests/unit/test_crdt.cpp).
- [SnapshotManager](src/sync/snapshot.h) compacts the commit log every N (default 100) commits.

### DSL layers — [src/filter/](src/filter/), [src/nlp/](src/nlp/)

- Filter DSL: [FilterParser](src/filter/filterparser.h) is a recursive-descent tokenize→parse producing an `AstNode` tree (`tokens.h`), evaluated against tasks by [FilterEval](src/filter/filtereval.h). Supported operators: `& | ! ( ) ,` plus predicates `today`, `tomorrow`, `overdue`, `no date`, `p1`–`p4`, `#Project`, `@label`, `/Section`, `search:`, `N days`, `before/after <date>`, `recurring`, `!recurring`, `created before/after`, `assigned`, `completed`. Saved user filters live in [Filter](src/model/filter.h).
- Natural-language date parser: [DateParser](src/nlp/dateparser.h) dispatches on language to [rules_en.cpp](src/nlp/rules_en.cpp) or [rules_ru.cpp](src/nlp/rules_ru.cpp) (per-language `DateRuleFunc` pointers). Output is `DateParseResult {date, datetime, isRecurring, recurrenceRule, humanString, valid}`. `TaskViewModel::addTask(..., dueString, ...)` runs this to populate the `DueObject`. To add a language, add a `rules_<lang>.cpp` and wire it into `DateParser::parse`.

### ViewModels — [src/viewmodels/](src/viewmodels/)

One VM per UI concern: Task, Project, Sidebar, Filter, Search, Settings, Karma, Sync, Notifications, Template. Most derive from `QAbstractListModel` and expose a `Roles` enum (see [taskviewmodel.h](src/viewmodels/taskviewmodel.h) for the canonical pattern — QML uses `model.content`, `model.priority`, etc.). Cross-VM coordination is done by signal→lambda connections in `main.cpp` (e.g. `SidebarViewModel::navigationRequested` → `TaskViewModel::setFilterMode/setCurrentProjectId`; `TaskViewModel::taskChanged` → `KarmaViewModel::onTaskCompleted`).

### Platform services — [src/services/](src/services/)

Each is a cross-platform interface with a platform-suffixed implementation. All are instantiated conditionally in `main.cpp` and exposed as context properties when present:
- `NotificationService` — Windows toasts or Android `NotificationHelper` (via JNI to [android/src/org/opentodo/NotificationHelper.java](android/src/org/opentodo/NotificationHelper.java)).
- `HotkeyService` — desktop-only, registers a global Quick Add hotkey (Ctrl+Alt+A default) via `QAbstractNativeEventFilter`.
- `LocationService` — requires Qt Positioning (`HAS_POSITIONING`); polls `QGeoPositionInfoSource` and fires `enteredGeofence` / `leftGeofence` when the device crosses a registered geofence — used for location-based `Reminder`s.

### UI — [qml/](qml/)

- [qml/Main.qml](qml/Main.qml) sets the Material theme and switches between [layouts/DesktopLayout.qml](qml/layouts/DesktopLayout.qml) and [layouts/MobileLayout.qml](qml/layouts/MobileLayout.qml) via a `Loader` keyed on `Qt.platform.os === "android"`.
- Themes: [ThemeManager.qml](qml/themes/ThemeManager.qml) is a QML singleton (`pragma Singleton`) with eight bundled palettes. UI components bind to `ThemeManager.backgroundColor`, `.accentColor`, etc. — do not hardcode colors. The active theme is read from `settingsViewModel.theme`.
- Views ([qml/views/](qml/views/)) correspond to `filterMode` strings: `inbox`, `today`, `upcoming`, `project`, `board`, `calendar`, `label`, `filter`, plus settings/productivity/notifications/onboarding.
- All QML files must be registered in the `QML_FILES` list in `CMakeLists.txt` — `qt_add_qml_module` does not glob.

## Conventions

- **Adding a model field**: add Q_PROPERTY + setter/getter with change signal, update `toJson`/`fromJson`, add a sync op branch in [SyncEngine::applyCommit](src/sync/syncengine.cpp) if the field should sync, add a role in the relevant ViewModel if the UI reads it.
- **Adding an entity type**: register in `DataStore` (list, index, CRUD, `toJson`/`loadFromJson`/`clear`), add to `Merger::mergeStates` collections list, add `<entity>_add`/`_update`/`_delete` branches to `SyncEngine::applyCommit`, and emit both the specific and aggregate `dataChanged` signal so autosave fires.
- **Removing data**: never hard-delete. Set `is_deleted=true` and bump `updated_at`; tombstones carry through sync and all query methods filter them out. A permanent delete from Trash (`purgeTask`) keeps the tombstone and sets `is_purged=true` (hidden from Trash and from `taskById`), because a hard-erased entity would be resurrected by the next union merge; only `vacuum()` drops old tombstones.
- **JSON field names use snake_case** (`project_id`, `is_completed`, `day_order`) even though C++ uses camelCase. This matches the Todoist API shape on disk.
