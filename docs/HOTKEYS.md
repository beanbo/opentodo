# OpenTodo — Keyboard Shortcuts

All shortcuts work when the task list is focused (not in a full-page view such as Settings).

## Task List Navigation

| Key | Action |
|-----|--------|
| `↑` / `↓` | Move selection up / down the task list |
| `Enter` | Open the editor panel for the hovered task |
| `Esc` | Close command palette → close editor → clear selection → clear hover |

## Task Actions

| Key | Action |
|-----|--------|
| `Q` or `+` | Expand the Quick Add bar and focus its text field |
| `Space` | Toggle complete / incomplete on the hovered task |
| `Delete` | Delete the hovered task; or bulk-delete if tasks are multi-selected |
| `1` | Set priority P1 (red) on the hovered task |
| `2` | Set priority P2 (orange) on the hovered task |
| `3` | Set priority P3 (blue) on the hovered task |
| `4` | Set priority P4 (grey / no priority) on the hovered task |

## Multi-Selection (mouse-driven)

| Gesture | Action |
|---------|--------|
| `Ctrl+Click` | Toggle individual task selection |
| `Shift+Click` | Range-select from the first selected task to the clicked task |
| Bulk bar → **Завершить** | Complete all selected tasks |
| Bulk bar → **Удалить** | Delete all selected tasks |
| Bulk bar → **✕** | Clear selection |

## Global / Search

| Key | Action |
|-----|--------|
| `Ctrl+F` or `/` | Focus the search bar |
| `Ctrl+K` | Open the command palette |
| `Ctrl+Z` | Undo last action (TODO: wire to UndoStack) |
| `?` or `F1` | Open the hotkeys help dialog |

## Hover-based shortcuts

The `Space`, `Enter`, `Delete`, and `1`–`4` shortcuts all operate on the
**hovered** task — whichever task the mouse cursor is over, or the task that
was last highlighted with `↑`/`↓` keyboard navigation.

The hovered task is tracked by `taskViewModel.hoveredTaskId`, which is set
automatically by `TaskItem.qml`'s `MouseArea.onContainsMouseChanged` handler.

## Implementation notes

Shortcuts are declared as `Shortcut {}` elements in
`qml/layouts/DesktopLayout.qml`.  The `enabled` guards prevent accidental
activation when a text input, command palette, or full-page view is active.

The `hoveredTaskId` and `selectedTask` properties live on `TaskViewModel`
(C++ side: `src/viewmodels/taskviewmodel.h` / `.cpp`).
