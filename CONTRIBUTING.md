# Contributing to OpenTodo

Thanks for your interest in contributing. OpenTodo is a Qt 6 / QML / C++20 cross-platform task manager targeting Windows, Linux, Android, and Web (WASM). This document covers how to set up, write code that fits the project, and submit changes.

The project is released under the MIT License — see [LICENSE](LICENSE). By submitting a contribution you agree it will be distributed under those terms.

## How to set up

Required toolchain:

- **Qt 6.11.0** (validated). Modules: `Core Gui Quick Qml Network Widgets QuickControls2`. Optional `Positioning` for geofenced reminders.
- **CMake 3.16 or newer**.
- A **C++20** compiler. On Windows, MinGW 13.1 (shipped with Qt) is the validated path. GCC 11+ / Clang 14+ / MSVC 19.30+ also work.
- For the WASM target, the matching Qt for WebAssembly build and the emsdk version Qt was built against. See [docs/WASM.md](docs/WASM.md).
- For Android, the NDK / Java toolchain configured per the Qt Android docs. See [CMakeLists.txt](CMakeLists.txt) for the platform-conditional sources.

Build and test:

```
git clone <your fork>
cd opentodo
cmake -S . -B build
cmake --build build
ctest --test-dir build/release
```

The QML module URI is `OpenTodoUI` and the entry point is `Main.qml`. Application binaries land in `build/`; on first run, app data is written under `$APPDATA/OpenTodo/` (desktop) or the Android app data dir.

## Code style

- **C++** uses camelCase for identifiers (`taskViewModel`, `applyCommit`). Member variables are unprefixed; getters are noun-form (`task.content()`), setters are `setX(value)`. Header-only helpers live next to their consumers.
- **JSON keys on disk are snake_case** (`project_id`, `is_completed`, `day_order`). This matches the Todoist API shape and must stay consistent for sync interop.
- **QML** files use PascalCase for component names, camelCase for properties. Bind to `ThemeManager.backgroundColor`, `.accentColor`, etc. — do not hardcode color literals. The active theme is read from `settingsViewModel.theme`.
- **i18n**: every user-facing string must go through `tr()` (C++) or `qsTr()` (QML). The project ships translations for English, Russian, German, Ukrainian, and Belarusian in [i18n/](i18n/). When adding strings, run `lupdate` to refresh the `.ts` files.
- **Architecture**: read [CLAUDE.md](CLAUDE.md) before adding anything non-trivial. The data → sync → DSL → viewmodel → QML layering is strict, and cross-layer wiring is centralized in [src/main.cpp](src/main.cpp).
- **No emojis** in source files, commits, or docs unless they are part of bundled UI strings already (e.g. project icons).

## Branch and PR workflow

1. Fork the repository on GitHub.
2. Create a feature branch off `main`. Use a short, descriptive name: `fix/dateparser-dst`, `feat/board-view-drag`, `docs/contributing`.
3. Keep PRs focused — one logical change per PR. Mechanical refactors should be separated from behavior changes.
4. PR title: imperative mood, under 70 characters. `Add WASM IDBFS persistence`, `Fix HLC clamp in Merger`, `Refactor SyncEngine retry loop`. Avoid prefixes like `[WIP]` — open as draft instead.
5. Reference related issues in the PR body (`Closes #123`).
6. Rebase rather than merge `main` into your branch when resolving conflicts.

## Commit messages

- One logical change per commit. Squash trivial fixups before opening the PR.
- First line is imperative and under 72 characters. Body wrapped at 72.
- Reference the affected subsystem when it helps reviewers — `sync:`, `filter:`, `qml:`, `i18n:`, `tests:` are existing prefixes in the log.

## Testing

- Tests live in [tests/](tests/) and use Qt Test plus QtQuickTest. Run everything with `ctest --test-dir build/release`.
- Add unit tests in `tests/unit/` for any new logic in Domain, Infra, or App layers. The existing `test_dateparser_en`, `test_filterparser`, `test_merger`, `test_recurrence` targets are good templates.
- For UI changes: manually verify in both desktop and mobile layouts. The layout switch is keyed on `Qt.platform.os === "android"` — set the env override or run on the actual target.
- For sync changes: exercise the merge path with at least two devices' commit logs. See `test_merger` and the CRDT pinning tests for the established pattern.
- For platform-specific code (`*_win.cpp`, `*_android.cpp`, `*_wasm.cpp`): a stub or guarded test is required so CI on other platforms keeps passing.

## Adding model fields or entity types

Both are checklist operations. Refer to the **Conventions** section of [CLAUDE.md](CLAUDE.md) for the authoritative list. In short:

**New field on an existing entity:**

1. Add `Q_PROPERTY` + getter/setter with change signal on the model class.
2. Update `toJson()` / `fromJson()` (snake_case key).
3. If the field should sync, add a branch in `SyncEngine::applyCommit` and per-field CRDT metadata handling in [Merger](src/sync/merger.cpp).
4. If the UI reads the field, add a role enum entry in the relevant `*ViewModel`.

**New entity type:**

1. Add to `DataStore`: list, index, CRUD methods, `toJson` / `loadFromJson` / `clear`.
2. Add to `Merger::mergeStates` collections list.
3. Add `<entity>_add` / `_update` / `_delete` branches to `SyncEngine::applyCommit`.
4. Emit both the specific `<entity>Changed()` and the aggregate `dataChanged()` signal so `FileManager` autosaves.

**Removing data is never a hard delete.** Set `is_deleted = true`, bump `updated_at`, and let tombstones propagate through sync. All query methods filter tombstones out.

## Translations

Translations are managed via Qt Linguist `.ts` files in [i18n/](i18n/). To add a new language:

1. Copy an existing `opentodo_<lang>.ts` and rename.
2. Register the new locale in the settings UI language picker and the `LanguageManager` mapping.
3. Run `lupdate` to populate strings, translate, then `lrelease` for the `.qm` binaries.
4. Add a test case (or extend an existing one) covering any language-specific NLP rules, e.g. a new `rules_<lang>.cpp` in [src/nlp/](src/nlp/).

## Reporting bugs and requesting features

Use the issue templates under [.github/ISSUE_TEMPLATE/](.github/ISSUE_TEMPLATE/). Bug reports without reproduction steps will usually be closed pending more information.

## Code of conduct

Be respectful, assume good faith, and keep discussion focused on the work. Personal attacks, harassment, or off-topic political fights will get the thread locked.

## Developer Certificate of Origin (DCO)

All commits must be signed off under the [Developer Certificate of Origin 1.1](https://developercertificate.org/). Add the sign-off automatically with:

```
git commit -s -m "Your message"
```

This appends a `Signed-off-by: Your Name <your.email@example.com>` trailer to the commit. The sign-off certifies that you wrote the patch (or otherwise have the right to submit it under the project's MIT License), as described in the DCO text. PRs without DCO sign-off will be asked to amend before merging.

## Questions

Open a discussion or issue on GitHub. There is no Discord, mailing list, or other side channel — keep the conversation on the repo so it stays searchable.
