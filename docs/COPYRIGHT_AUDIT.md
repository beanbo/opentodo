# OpenTodo Copyright & Trademark Audit
Last updated: 2026-05-19

---

## TL;DR

Overall risk is **medium-low** for an open-source project, but two issues need attention before any commercial or wide-audience release. The most urgent is the **icon**: a red rounded-square icon with a checkmark is the central Todoist visual identity, and the current SVG is close enough to create confusion. Second priority: **identifier names** (theme name `todoist_light`, QML properties `todoist_red` / `todoistRed`) embed the Todoist trademark inside the codebase and should be renamed. The third item is softer: using "Karma" as a user-visible feature label when Todoist made that term famous in the task-manager niche; renaming it to "Productivity Score" or similar adds distance at low cost. All other concerns are either clearly legitimate (the importer module) or generic and not protectable by Todoist.

---

## 1. Icon / Logo Similarity

### Current design (`resources/icon.svg`)

The SVG draws:
- A 512×512 rounded square (rx/ry = 115, i.e. ~22% corner radius).
- A **red gradient fill** from `#ff7466` (top) to `#db4c3f` (bottom).
- A **270° open ring** (white, `stroke-width=36`), gap in the upper-right.
- A **white bold checkmark** whose tail extends through the gap in the ring.

### Comparison to Todoist's logo

Todoist's logo (as of 2024–2026) is a **red rounded-square icon with a stylised checkmark** — specifically a diagonal check stroke on a red background. The rounded-square + checkmark combination has been Todoist's visual mark across all platforms for many years and is widely recognized as such in the task-manager category.

The current OpenTodo icon:
- Uses the same general **shape** (rounded square).
- Uses the **identical colour** (`#db4c3f`) — Todoist's own registered brand red, literally referenced by name in the source code.
- Draws a **white checkmark** in the same general position (lower-left to upper-right diagonal).
- The open-ring element is a distinguishing addition, but at small sizes (taskbar, dock, notification area) the ring may not be clearly visible and the icon reads as "red square with checkmark."

**Verdict: Medium-High similarity.** At 16×32 px the ring collapses and the icon is indistinguishable from Todoist's. This creates a real risk of trademark infringement by likelihood-of-confusion, especially because:
1. Both products are in the **same category** (task management).
2. The README explicitly positions OpenTodo as an **"альтернатива Todoist"** (Todoist alternative), so a user encountering both in the same context is likely.

### Recommendation

Redesign the icon with at least two of the following changes:
- Change the background colour to something clearly distinct (e.g. blue, green, or a neutral purple — not any shade of red near `#db4c3f`).
- Change the primary symbol from a checkmark to something different (checkbox frame, a stylised O/T lettermark, a clipboard, etc.).
- Remove the checkmark from the foreground entirely, or reposition it so the overall silhouette at small sizes differs.

The open-ring concept ("incomplete circle = open todo") is a good conceptual direction; keeping it as the *primary* visual element with a different background colour would be sufficient to differentiate.

---

## 2. Color Palette

### Palette inventory (`qml/themes/ThemeManager.qml`)

| Key | Display name | `accentColor` | Flagged? |
|---|---|---|---|
| `system` | Системная | (resolves to `todoist_light` or `dark`) | See below |
| **`todoist_light`** | Светлая | `#db4c3f` | **YES — name** |
| `dark` | Тёмная | `#db4c3f` | YES — colour |
| `kale` | Капуста | `#7fba7a` | No |
| `blueberry` | Blueberry | `#6c88f4` | No |
| `lavender` | Lavender | `#b07fd0` | No |
| `raspberry` | Raspberry | `#e06088` | No |
| `graphite` | Graphite | `#7a9cc6` | No |
| `sunflower` | Sunflower | `#e8a820` | No |
| `highcontrast` | (не отображается напрямую) | `#FFFF00` | No |

### Issues

1. **Theme name `todoist_light`** — this key is exposed publicly in two ways:
   - Persisted to `QSettings` (the user's settings file on disk contains the string `"todoist_light"`).
   - Returned by `SettingsViewModel::themeNames()` (a public Q_PROPERTY list).
   - Displayed with a translated label ("Светлая"), so it is not *shown* to users as "todoist_light", but the key appears in source, in build artifacts, and in serialised state files. This is a trademark hygiene problem rather than a user-visible one, but it is unnecessary.
   - **Recommended rename:** `classic_light` or `default_light`.

2. **`dark` palette accent `#db4c3f`** — the dark theme also uses Todoist-red as its accent colour. This is not a naming problem, but if the icon redesign changes the brand colour, the dark palette accent should change too. Low risk on its own.

3. **`system` resolution points to `todoist_light` by name** — code in `ThemeManager.qml` (line 92) and `SettingsSectionAppearance.qml` (line 122) hard-codes `"todoist_light"` as the light-mode target. These references must be updated when the palette is renamed.

4. **Hardcoded `#db4c3f` in 39 places across 16 QML files** — beyond `ThemeManager.qml` itself, the hex literal is scattered in: `SidebarContent.qml`, `DesktopLayout.qml`, `QuickAdd.qml`, `TaskItem.qml`, `CalendarView.qml`, `ProductivityView.qml`, `ProjectView.qml`, and others. These are fallback/default values (e.g. `dotColor: favRow.modelData.color || sidebarContent.todoist_red`). Once the brand colour is changed, these fallback literals should be updated to the new default.

---

## 3. Identifier Hygiene

### Grep counts (source tree, excluding `src/import/` and build directories)

| Identifier | Count | Category | Risk |
|---|---|---|---|
| `todoist_light` | 9 (QML + C++) | Theme key name | Medium — trademark in identifier |
| `todoist_red` | 7 (QML: `SidebarContent.qml`) | Variable name | Medium — trademark in variable |
| `todoistRed` | 3 (QML: `DesktopLayout.qml`, `MobileLayout.qml`) | Variable name | Medium — trademark in variable |
| `todoist` variable in `DatePicker.qml` | 5 | Variable alias for accent colour | Medium |
| `todoist` variable in `QuickAdd.qml` | 7 | Variable alias for accent colour | Medium |
| `"Todoist"` in comments (C++) | ~8 | Code comments | Low — internal documentation |
| `"Todoist"` in user-visible strings | 0 (outside importer UI) | — | OK |
| `TodoistImporter` / `todoist_api_client` etc. | ~200 | **Import module — legitimate** | No concern |

### Non-importer C++ identifiers

| File | Line | Identifier | Context | Recommendation |
|---|---|---|---|---|
| `src/viewmodels/settingsviewmodel.h` | 147 | comment: `"matching the todoist_light/dark palettes"` | Documentation comment | Update after rename |
| `src/viewmodels/settingsviewmodel.cpp` | 171, 405 | Comment + string literal `"todoist_light"` | Palette key reference | Rename |
| `src/services/trayicon.h` | 125 | `QColor("#db4c3f")` default | Badge colour default | Update after brand colour change |
| `src/sync/oauth_client.cpp` | 147 | `h1{color:#db4c3f}` | OAuth redirect page heading colour | Update after brand colour change |
| `src/model/task.h` | 162 | `"#db4c3f"` in comment | Example value in doc comment | Update after brand colour change |
| `src/filter/filtereval.cpp` | 302 | `// Todoist semantics` | Comment | Harmless; can say "OpenTodo semantics" |
| `src/nlp/quickaddparser.h` | 10, 12, 19 | `// Todoist convention`, `// Todoist-style` | Comments | Low risk; rephrase to "OpenTodo convention" |

### QML identifiers to rename

| File | Current name | Recommended name |
|---|---|---|
| `qml/components/SidebarContent.qml` | `todoist_red` | `accentRed` or `defaultAccent` |
| `qml/layouts/DesktopLayout.qml` | `todoistRed` | `accentRed` |
| `qml/layouts/MobileLayout.qml` | `todoistRed` | `accentRed` |
| `qml/components/DatePicker.qml` | `todoist` | `accentColor` |
| `qml/components/QuickAdd.qml` | `todoist` | `accentColor` |
| `qml/themes/ThemeManager.qml` | `todoist_light` palette key | `classic_light` |

---

## 4. UI Patterns

### Sidebar: Inbox / Today / Upcoming / Projects
**Verdict: Generic GTD structure. No concern.** These sections correspond to Getting Things Done's Inbox and horizon model, which predates Todoist by ~15 years. Numerous task managers (OmniFocus, Things, TickTick, Microsoft To Do, etc.) use the same structure. Not protectable by Todoist.

### Priority levels P1–P4 with colour coding
**Verdict: Generic. No concern.** Numerical priority levels are industry-standard (many project tools use P0–P4 or similar). The specific colour scheme (red/orange/blue/grey for P1–P4) is shared across many tools and is not distinctively associated with Todoist.

### Filter DSL with `#Project`, `@label`, `today`, `overdue` syntax
**Verdict: Low concern.** Todoist uses `#` and `@` prefix syntax in its filter language. However, these conventions are borrowed from broader tag/mention culture (IRC, Twitter, Slack) and are also used in Notion, Asana, and other tools. The specific AST/DSL implementation is entirely original.

### Karma points and daily/weekly goals
**Verdict: Medium concern — the word "Karma" specifically.** The gamification concept (points for completed tasks, streaks, daily goals, progress milestones) is generic — used in Habitica, Todoist, TickTick, and many habit apps. However, **Todoist specifically branded this feature "Karma"** and the term is strongly associated with Todoist in the task-manager space.

Current user-visible text (Russian): `qsTr("Карма · %1")`, `qsTr("До %1 кармы:")` — the word "карма" appears in the UI.

The internal class name `KarmaViewModel` and the `QSettings` keys (`karma/…`) are not user-facing and carry no trademark risk, but **renaming the user-visible label** is advisable.

**Recommendation:** Rename the user-visible label from "Карма" to "Продуктивность" or "Очки активности" (Productivity / Activity Score). The `KarmaViewModel` class name and settings keys can stay as-is internally — there is no trademark in internal code symbols.

### Boards (kanban), Calendar, Labels, Filters views
**Verdict: No concern.** These are industry-standard UI paradigms shared across dozens of project-management tools.

### Quick Add hotkey (Ctrl+Alt+A) and natural-language date parsing
**Verdict: No concern.** Functionality, not protectable. Many apps have global quick-add shortcuts and NLP date parsing.

### Google Drive sync
**Verdict: No concern.** Many apps sync to Google Drive.

---

## 5. README / Marketing Copy

### Relevant excerpt (line 7 of `README.md`)

> Кросс-платформенный планировщик задач на Qt 6 / QML / C++20. **Бесплатная альтернатива Todoist** с локальным хранением и Google Drive-синхронизацией.
>
> *(Cross-platform task planner on Qt 6 / QML / C++20. **Free alternative to Todoist** with local storage and Google Drive sync.)*

### Assessment

"Free alternative to Todoist" is a **stronger claim than "inspired by"**. Trademark law in most jurisdictions (US, EU) permits comparative and nominative use of a competitor's name — you can say "alternative to X" — but the phrase needs care:

1. **Nominative use is permissible** when: (a) the mark is used only to identify the product being compared, (b) only as much of the mark is used as necessary, and (c) the use does not suggest sponsorship or endorsement. "Бесплатная альтернатива Todoist" satisfies all three criteria as written.

2. The README does **not** claim to be a drop-in replacement, does not claim compatibility with Todoist's cloud, and does not imply endorsement. The framing is clearly competitive/comparative.

3. **Risk level: Low.** The phrase is legally defensible as nominative use in most jurisdictions. No change strictly required. However, rewording to "inspired by Todoist" or "similar to Todoist" would be even safer and is more accurate for an MIT project that has its own sync, own data model, and own feature set.

**Recommendation:** Change to "вдохновлён Todoist" (inspired by Todoist) or "в стиле Todoist" (in the style of Todoist) to avoid any perception of claiming equivalence.

### CONTRIBUTING.md reference (line 32)
> "JSON keys on disk are snake_case … This matches the Todoist API shape and must stay consistent for sync interop."

**Assessment:** Factual technical documentation. Acceptable.

### .github/ISSUE_TEMPLATE reference
References "Todoist, TickTick, Things" as examples in a feature-request template. Nominative comparative use; clearly fine.

---

## 6. User-Visible Strings

### Grep results: `qsTr(…)` containing "Todoist"

The only occurrences of "Todoist" inside `qsTr()` translation-function calls are in `qml/components/settings/SettingsSectionBackup.qml`:

| Line | String | Context |
|---|---|---|
| 94 | `qsTr("Выбрать файл резервной копии Todoist (.zip)")` | File-picker dialog title |
| 143 | `qsTr("Введите API-токен из Todoist: …")` | Import dialog instructions |
| 152 | `qsTr("API-токен Todoist")` | Text-field placeholder |
| 460 | `qsTr("Импорт из Todoist")` | Section header |
| 513 | `qsTr("Todoist → Настройки → Резервные копии → Скачать")` | Step-by-step instructions |
| 565 | `qsTr("Todoist → Настройки → Интеграции → Разработчикам")` | Step-by-step instructions |

**Assessment: All legitimate.** Every occurrence is inside the Todoist *importer* UI, explaining to the user how to export their data *from* Todoist. Using Todoist's name in this context is necessary and unambiguously nominative.

### Non-importer user-visible strings

The user-visible strings "Карма" / "Карма · %1" / "До %1 кармы:" (in `ProductivityView.qml`) are the only non-importer trademark-adjacent strings. See Section 4 above.

No other user-visible strings contain "Todoist" or any Todoist-brand term.

---

## 7. Third-Party Assets

### Full asset inventory (`resources/`)

| File / path | Type | Origin / Licence |
|---|---|---|
| `resources/icon.svg` | SVG vector icon | Created in-project (this codebase). MIT. |
| `resources/icons/icon_*.png` | PNG renders of `icon.svg` | Generated from `icon.svg` by Qt's `rsvg`/`QSvgRenderer`. MIT. |
| `resources/icons/icon.ico` | Windows multi-resolution icon | Rendered from `icon.svg` in-project. MIT. |
| `resources/windows.rc` | Windows resource file | In-project. MIT. |
| `resources/templates/*.json` | JSON task templates | In-project data files. MIT. |
| `resources/fonts/` | (empty directory) | No fonts bundled. |

**No fonts (`.ttf`, `.otf`, `.woff`), audio files (`.wav`, `.mp3`), or third-party icon sets are present.** All assets in `resources/` were generated by this project.

The `NOTICE` file correctly covers Qt 6 (LGPLv3), zlib (zlib licence), and OpenSSL. No gaps found for assets in `resources/`.

**Verdict: No third-party asset licence concerns.** The fonts directory is empty — the app uses Qt's built-in system font stack without bundling anything.

---

## 8. Action Items (Priority Order)

1. **[HIGH] Redesign the icon.** The current red-rounded-square-plus-checkmark is a medium-high similarity match to Todoist's registered visual identity. Change the background colour away from `#db4c3f`/`#ff7466` red (blue, green, or teal are safe directions) and/or replace the checkmark with a more distinctive mark. This is the single change that most reduces infringement risk. Update all rendered PNGs and the `.ico` after the redesign.

2. **[HIGH] Rename the `todoist_light` theme key** to `classic_light` (or similar) everywhere it appears. This is a trivial find-and-replace touching `ThemeManager.qml`, `SettingsSectionAppearance.qml`, `CommandMenu.qml`, `DesktopLayout.qml`, and `settingsviewmodel.cpp`. Also add a migration path so existing user settings with the old key still resolve correctly (e.g. map `"todoist_light"` → `"classic_light"` in `FileManager`'s settings loader).

3. **[MEDIUM] Rename QML variable identifiers** `todoist_red` (SidebarContent), `todoistRed` (DesktopLayout, MobileLayout), and the local `todoist` aliases in `DatePicker.qml` and `QuickAdd.qml`. These are purely internal, but embedding the Todoist trademark in distributed source code is unnecessary. A global rename to `accentRed` / `accentColor` takes under an hour.

4. **[MEDIUM] Rename user-visible "Карма" label** in `ProductivityView.qml` to "Продуктивность" or "Очки". The internal `KarmaViewModel` class name does not need changing. Only the user-facing `qsTr("Карма · %1")` and `qsTr("До %1 кармы:")` strings are in scope.

5. **[LOW] Update README.md line 7** from "Бесплатная альтернатива Todoist" to "вдохновлён Todoist" (inspired by Todoist). Legally defensible as-is, but the softer phrasing reduces exposure and is more accurate.

6. **[LOW] Audit hardcoded `#db4c3f`** in 39 QML locations (16 files) and replace with `ThemeManager.accentColor` or a named constant. This is a code quality improvement that also ensures consistency when the brand colour changes after the icon redesign.

7. **[LOW] Update in-code comments** referencing "Todoist semantics" / "Todoist convention" in `filtereval.cpp`, `tokens.h`, `quickaddparser.h` to say "OpenTodo semantics" / "OpenTodo convention". No legal risk but demonstrates clean independence from Todoist's brand.

---

## 9. Disclaimer

This audit was conducted by an AI agent reading the source code of the repository at commit `881ff4f`. It is **not legal advice**.

The analysis reflects standard trademark-law principles (nominative use, likelihood of confusion, trade dress) as commonly applied in US and EU jurisdictions, but AI systems can miss context, misread intent, or apply jurisdiction-specific rules incorrectly.

**Before releasing a commercial product or a widely-distributed binary, consult an attorney** with trademark and software experience in your target jurisdiction. This document is a developer-oriented starting checklist, not a legal opinion.
