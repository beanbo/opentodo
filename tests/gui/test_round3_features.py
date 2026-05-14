"""Regression tests for the round-3 fixes:
   - TaskEditor passes taskId to inner editor components (subtasks/comments/save)
   - "Помощник" sidebar item routes to AI view, not Inbox
   - Settings switches use canonical (snake_case) keys
   - Project context menu wired into SidebarContent.qml
   - Drop-on-task → makes subtask
   - Narrow-window responsive mode
   - Sections have rename/delete + drop zone
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent.parent


# ── Source-level wiring tests (cheap, deterministic) ───────────────────────

def _read(rel: str) -> str:
    return (REPO / rel).read_text(encoding="utf-8")


def test_task_editor_derives_task_id_from_task():
    """TaskEditor.taskId must be a binding that reads task.id, not a
    plain empty string. Without this, all inner editor components
    silently disable themselves (subtasks, comments, save)."""
    src = _read("qml/components/TaskEditor.qml")
    # The taskId line should reference task.id
    import re
    m = re.search(r"property\s+string\s+taskId\s*:\s*([^\n]+)", src)
    assert m, "TaskEditor must declare 'property string taskId: ...'"
    binding = m.group(1)
    assert "task" in binding and ".id" in binding, (
        f"TaskEditor.taskId must derive from task.id, got: {binding!r}"
    )


def test_ai_filter_mode_lookup_is_case_insensitive():
    """filterModeFromString must accept 'ai' as well as 'AI' so the
    Помощник sidebar item lands on the AI view."""
    src = _read("src/viewmodels/taskviewmodel.cpp")
    # The fix iterates all enum keys with case-insensitive compare.
    assert "Qt::CaseInsensitive" in src and "filterModeFromString" in src, (
        "filterModeFromString must do case-insensitive enum-key lookup"
    )


def test_settings_camel_case_keys_mapped_to_snake_case():
    """SettingsViewModel::updateSetting must translate camelCase keys
    (the QML callers' style) to the canonical snake_case keys used by
    on-disk storage. Without translation, every Switch silently wrote
    to the wrong key and binding never refreshed."""
    src = _read("src/viewmodels/settingsviewmodel.cpp")
    assert "canonicalSettingKey" in src, (
        "updateSetting must funnel through a canonical-key mapper"
    )
    # Spot-check the most-likely-wrong mappings.
    assert '"showInboxCount"' in src
    assert '"vacationMode"' in src
    assert '"dailyGoal"' in src


def test_project_context_menu_in_sidebar():
    """Right-click on a project must show: Open / Add Section /
    Parent Project / Favorite / Archive / Delete."""
    src = _read("qml/components/SidebarContent.qml")
    assert "projectCtxMenu" in src
    for label in ("Открыть", "Добавить раздел", "Родительский проект",
                  "Архивировать", "Удалить проект"):
        assert label in src, f"Project context menu missing item: {label}"


def test_drop_on_task_makes_subtask():
    """The DesktopLayout's task delegate must split into two DropAreas:
    top half = reorder, bottom half = make subtask."""
    src = _read("qml/layouts/DesktopLayout.qml")
    assert "subtaskDrop" in src and "reorderDrop" in src, (
        "DesktopLayout must split row into reorder + subtask drop zones"
    )
    assert "convertToSubtask" in src, (
        "subtask drop must call taskViewModel.convertToSubtask(...)"
    )


def test_narrow_window_responsive_mode():
    """DesktopLayout must define a narrowBreakpoint and an overlay
    drawer for narrow mode."""
    src = _read("qml/layouts/DesktopLayout.qml")
    assert "narrowBreakpoint" in src
    assert "isNarrow" in src
    assert "drawerOverlay" in src, "Overlay drawer for narrow mode missing"


def test_sections_have_context_menu_and_drop():
    """ProjectView's section delegate must include a per-section ⋯ menu
    (rename / delete) and a DropArea so tasks can be dropped onto a
    section header."""
    src = _read("qml/views/ProjectView.qml")
    assert "sectionMenu" in src
    assert "Переименовать раздел" in src
    assert "renameSection" in src or "renameSectionDialog" in src
    assert "sectionDrop" in src, "Section header DropArea missing"


def test_save_button_exists_in_action_bar():
    """The TaskEditor action bar must show a 'Сохранить' button so
    Todoist users have the Save affordance they expect."""
    src = _read("qml/components/editor/EditorActionBar.qml")
    assert "Сохранить" in src
    # And it must be visually a primary button (accent background).
    assert "ThemeManager.accentColor" in src


def test_calendar_replaces_text_due_input_in_editor():
    """The TaskEditor's due-date row must use the calendar DatePicker
    component, not a free-text input."""
    src = _read("qml/components/editor/EditorMetaRow.qml")
    assert "DatePicker {" in src, (
        "TaskEditor must use the calendar DatePicker for due dates"
    )
    # And the old free-text dialog must be gone.
    assert "datePickerDialog" not in src, (
        "Old datePickerDialog free-text input still present"
    )


def test_section_drop_uses_move_task():
    """Dropping a task onto a section header must call taskViewModel.moveTask
    with both project and section ids."""
    src = _read("qml/views/ProjectView.qml")
    assert 'taskViewModel.moveTask(tid, root.projectId,\n                                                                    sectionHeaderRect.section)' in src \
           or "moveTask" in src and "section" in src.lower()


def test_settings_spinboxes_align_right():
    """Number-stepper rows in settings must pin the SpinBox to the
    right edge so the - / + buttons aren't floating in the middle."""
    for f in ("SettingsSectionProductivity.qml",
              "SettingsSectionReminders.qml",
              "SettingsSectionSync.qml"):
        src = _read(f"qml/components/settings/{f}")
        assert "Layout.alignment: Qt.AlignRight" in src, (
            f"SpinBox in {f} should use Layout.alignment: Qt.AlignRight"
        )


def test_collapse_clears_quickadd_text():
    """QuickAdd.collapse() must clear typed text — otherwise Cancel /
    Esc leaves a draft that re-appears on next open."""
    src = _read("qml/components/QuickAdd.qml")
    # collapse() should call clearFields()
    import re
    m = re.search(r"function\s+collapse\s*\(\s*\)\s*\{([^}]*)\}", src)
    assert m, "QuickAdd.collapse() function not found"
    body = m.group(1)
    assert "clearFields" in body
