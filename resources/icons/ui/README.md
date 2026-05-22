# UI icon starter pack — proposal for task 17cea65b

16 stroke-style SVG icons, 24×24 viewBox, `currentColor` so they re-tint with
ThemeManager. Style: stroke-width 1.75, round caps + joins. Designed to look
consistent next to the Material font (RobotoFlex / Inter).

## Files
| File                  | Replaces (current UI char) | Where used                       |
|-----------------------|----------------------------|----------------------------------|
| chevron-right.svg     | ▸                          | sidebar expand, section collapse |
| chevron-down.svg      | ▾                          | sidebar expanded, section open   |
| plus.svg              | +                          | add task / add section buttons   |
| close.svg             | ✕                          | dialog close, banner dismiss     |
| more-horizontal.svg   | ⋯                          | task / section context menu      |
| drag-handle.svg       | ⠿                          | task drag handle                 |
| star.svg              | ☆ outline                  | favourite (not set)              |
| star-filled.svg       | ★                          | favourite (set)                  |
| inbox.svg             | (text)                     | "Входящие" filter mode           |
| calendar.svg          | 📅                         | Today / Upcoming filter modes    |
| check.svg             | ✓                          | task completion checkbox state   |
| pin.svg               | 📌                         | pinned task indicator            |
| search.svg            | 🔍                         | search bar / cmd menu            |
| trash.svg             | 🗑                          | delete buttons                   |
| filter.svg            | (Material i icon)          | saved filters                    |
| label.svg             | (#)                        | label chips                      |

## Usage from QML
```qml
Image {
    source: "qrc:/qt/qml/OpenTodoUI/resources/icons/ui/chevron-right.svg"
    sourceSize.width: 16
    sourceSize.height: 16
    // For accent-coloured icons wrap in ColorOverlay (Qt.labs.Effect) or
    // recolour via a layer.effect with the theme accent.
}
```

For per-icon tinting that follows ThemeManager.accentColor / textColor, a
helper component `qml/components/IconSvg.qml` is the recommended next step —
wraps an Image + ColorOverlay so callers pass `name + color` and don't
touch the qrc path themselves.

## Status
- Starter pack only — does NOT yet replace any usage site in QML.
- Awaiting user review of the visual style. Once approved, the next
  iteration adds an `IconSvg` helper + sweeps the QML tree (~40 usage
  sites for unicode characters and Material icons).
- Add more icons here (project / section / settings / sync / AI / etc) as
  needed once style is locked.
