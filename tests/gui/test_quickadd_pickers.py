"""End-to-end tests for the QuickAdd priority and date pickers.

Regression tests for the bug "Не работает выбор приоритетов" — selecting
a priority from the popup must actually apply when submitting the task,
not silently get dropped.
"""
from __future__ import annotations

import time

from PIL import Image

from conftest import find_plus_glyph, read_state, is_accent


def _open_quickadd(app) -> tuple[int, int, int, int]:
    """Click the QuickAdd row to expand it. Returns the '+' bbox."""
    bbox = find_plus_glyph(app.screenshot())
    assert bbox, "no '+' glyph found"
    x0, y0, x1, y1 = bbox
    app.click_client(x0 + 220, (y0 + y1) // 2)
    time.sleep(0.6)
    return bbox


def test_quickadd_submit_passes_picker_overrides(app, recorder):
    """Source-level regression: QuickAdd.submitTask must send the user's
    explicit picker selections (priority/due/labels) to addTask, not
    silently drop them. Before the fix, only the freeform NLP was used.

    Verifies the fix in qml/components/QuickAdd.qml — when any picker is
    non-default, call addTask with explicit args; otherwise fall through
    to the NLP parser."""
    from pathlib import Path
    qml = (Path(__file__).resolve().parent.parent.parent / "qml" /
           "components" / "QuickAdd.qml").read_text(encoding="utf-8")

    # The submitTask function should branch on picker overrides.
    assert "userOverrode" in qml, (
        "submitTask should detect when the user picked priority/date/labels"
    )
    # And must call addTask with explicit args in that branch — on the
    # NLP-parsed content, not the raw text (addTask takes the content
    # verbatim, so rawText kept "завтра @дом #Работа" in the title and lost
    # the date, label and project as soon as any picker was used).
    override_branch = qml.split("if (userOverrode)")[1].split("} else")[0]
    assert "taskViewModel.previewQuickAdd(rawText)" in override_branch, (
        "the override branch should still run the Quick Add parser on the text"
    )
    assert "taskViewModel.addTask(" in override_branch, (
        "submitTask should call addTask(content, projectId, priority, ...) "
        "when the user explicitly picked options — not addTaskFromQuickAdd"
    )
    # The override branch must include selectedPriority.
    assert "root.selectedPriority" in qml.split("userOverrode")[1].split("} else")[0], (
        "userOverrode branch should pass selectedPriority"
    )


def test_date_picker_is_calendar_widget(app, recorder):
    """Source-level regression: DatePicker must be a true calendar with
    month navigation, day-of-week headers, and a 7-column day grid —
    NOT a plain text input."""
    from pathlib import Path
    qml = (Path(__file__).resolve().parent.parent.parent / "qml" /
           "components" / "DatePicker.qml").read_text(encoding="utf-8")

    # Calendar grid markers
    assert 'columns: 7' in qml, "Day grid must have 7 columns"
    assert 'buildDays(' in qml, "Calendar must build a 42-cell day grid"
    assert "navigateMonth" in qml, "Must have month navigation buttons"
    # Day-of-week headers in Russian
    assert '"Вс"' in qml and '"Сб"' in qml, "Day-of-week headers (Вс..Сб) required"
    # Quick options
    assert '"Сегодня"' in qml and '"Завтра"' in qml, (
        "Quick-pick buttons (Сегодня, Завтра, ...) required"
    )
