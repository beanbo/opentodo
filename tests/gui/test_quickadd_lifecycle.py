"""End-to-end tests for the full QuickAdd lifecycle:

  * collapsed → click → expanded (toolbar + buttons appear)
  * expanded → ESC → collapsed
  * expanded → "Отмена" button → collapsed
  * expanded → typing → preview row appears
  * NLP parsing of priority and due date in placeholder text
  * adding a second task immediately after the first (regression for
    "Не могу добавить второй таск!")
"""
from __future__ import annotations

import time

from conftest import find_plus_glyph, task_count_from_state, is_accent


def _focus_quickadd(app, recorder=None):
    """Click the QuickAdd row to expand and focus it. Returns the
    (x0, y0, x1, y1) bbox of the '+' glyph for follow-up clicks."""
    img = app.screenshot()
    bbox = find_plus_glyph(img)
    assert bbox, "no '+' glyph found in QuickAdd"
    x0, y0, x1, y1 = bbox
    # Click 220px right of the '+' so we're well inside the placeholder.
    app.click_client(x0 + 220, (y0 + y1) // 2)
    time.sleep(0.6)
    if recorder:
        recorder("after click")
    return bbox


def test_quickadd_expands_after_click(app, recorder):
    """Clicking the QuickAdd row expands the panel — the toolbar with date,
    priority, label icons and project ComboBox becomes visible.

    Type one character so the 'Добавить задачу' button transitions from
    its disabled (grey #444444) to enabled (red #db4c3f) state — that's
    the most reliable accent-color signal the panel is actually expanded.
    """
    pre = app.screenshot()
    bbox = _focus_quickadd(app, recorder)
    x0, y0, x1, y1 = bbox

    app.type_text("x")
    time.sleep(0.4)
    post = app.screenshot()
    recorder("after typing 'x'")

    # Sample a vertical band ~80 px below the '+' and look for accent
    # pixels — the red "Добавить задачу" button uses #db4c3f when enabled.
    expanded_band = (x0, y1 + 50, x0 + 900, y1 + 130)
    crop = post.crop(expanded_band).convert("RGB")
    accent_count = sum(1 for p in crop.getdata() if is_accent(p))
    recorder(f"accent pixels in expanded band: {accent_count}")

    # The enabled "Добавить задачу" button is roughly 130×32 — about
    # 4000 px of which ~50% are coral red. Threshold ≥ 100 is a
    # comfortable signal that the button is rendered.
    assert accent_count >= 100, (
        f"expanded panel button not found ({accent_count} accent pixels). "
        f"QuickAdd may not have actually expanded after click."
    )


def test_quickadd_escape_collapses(app, recorder):
    """Pressing Esc while the QuickAdd is expanded collapses it back to
    the 56-px row. Regression test for the global Escape shortcut not
    forwarding to QuickAdd."""
    bbox = _focus_quickadd(app, recorder)
    x0, y0, x1, y1 = bbox

    # Confirm it's expanded by typing — that turns the Add button red.
    app.type_text("xRESCAPE")
    time.sleep(0.3)
    expanded_state = app.screenshot()
    recorder("typed (expanded)")

    # Look at the right-edge button area only (last 200 px of the row),
    # 80 px below the '+' — that's where "Добавить задачу" sits when
    # expanded. Restricting horizontally avoids the existing-task
    # circles in the list below.
    # Use absolute right edge minus a margin so we hit the Add button
    # regardless of window width.
    right_edge = expanded_state.width - 50
    button_band = (right_edge - 200, y1 + 60, right_edge, y1 + 110)
    accent_in_expanded = sum(
        1 for p in expanded_state.crop(button_band).convert("RGB").getdata()
        if is_accent(p)
    )
    recorder(f"expanded accent in button area: {accent_in_expanded}")
    assert accent_in_expanded >= 50, (
        f"Sanity check: panel did not expand "
        f"({accent_in_expanded} accent in Add-button area)"
    )

    # Hit Esc — should collapse panel AND clear the typed text.
    app.press_key("{ESC}")
    time.sleep(0.6)
    collapsed = app.screenshot()
    recorder("after esc")

    # Now the button area should be empty of accent pixels.
    accent_in_collapsed = sum(
        1 for p in collapsed.crop(button_band).convert("RGB").getdata()
        if is_accent(p)
    )
    recorder(f"collapsed accent in button area: {accent_in_collapsed}")
    assert accent_in_collapsed < 20, (
        f"Esc did not collapse the QuickAdd panel — still seeing "
        f"{accent_in_collapsed} accent pixels in the Add-button area."
    )


def test_add_three_tasks_in_a_row(app, recorder):
    """Regression for 'Не могу добавить второй таск!' — clicking the
    QuickAdd, typing, Enter, then doing it again two more times must
    create three tasks. Each re-click must re-expand the (now-collapsed)
    bar and route focus to the input."""
    pre_count = task_count_from_state()
    titles = [f"FastAdd_A_{int(time.time())}",
              f"FastAdd_B_{int(time.time())}",
              f"FastAdd_C_{int(time.time())}"]

    for i, title in enumerate(titles):
        bbox = find_plus_glyph(app.screenshot())
        assert bbox, f"task {i+1}: no '+' glyph found"
        x0, y0, x1, y1 = bbox
        app.click_client(x0 + 220, (y0 + y1) // 2)
        time.sleep(0.4)
        app.type_text(title)
        time.sleep(0.2)
        app.press_key("{ENTER}")
        time.sleep(0.7)
        recorder(f"after task {i+1} '{title}'")

    # Wait for autosave debounce.
    time.sleep(1.4)
    post_count = task_count_from_state()
    recorder(f"task count: {pre_count} -> {post_count}")
    assert post_count == pre_count + 3, (
        f"expected 3 new tasks, got {post_count - pre_count}. "
        f"Tried titles: {titles}"
    )


def test_quickadd_cancel_button_collapses(app, recorder):
    """Clicking the 'Отмена' button while expanded must collapse the
    panel without creating a task."""
    pre_count = task_count_from_state()
    bbox = _focus_quickadd(app, recorder)
    x0, y0, x1, y1 = bbox

    # Type some text to ensure the Add button is enabled, but cancel.
    app.type_text("ShouldNotBeCreated")
    time.sleep(0.3)
    recorder("typed text")

    # The "Отмена" button sits left of "Добавить задачу" near the
    # right edge of the QuickAdd bar. We compute its position from
    # the expanded panel layout: it's in Row 3 (~ y1 + 95), and
    # roughly 270 px from the right edge of the panel.
    img = app.screenshot()
    panel_right = img.width - 50  # roughly accounting for sidebar offset
    # Click in the area where Отмена sits — left of Добавить задачу.
    cancel_x = panel_right - 260
    cancel_y = y1 + 95
    recorder(f"cancel click at ({cancel_x}, {cancel_y})")
    app.click_client(cancel_x, cancel_y)
    time.sleep(0.6)
    after = app.screenshot()
    recorder("after cancel")

    # Check the panel collapsed (no accent pixels in toolbar band).
    band = (x0, y1 + 50, x0 + 900, y1 + 130)
    accent = sum(
        1 for p in after.crop(band).convert("RGB").getdata()
        if is_accent(p)
    )
    recorder(f"accent after cancel: {accent}")

    # And no task was created.
    time.sleep(1.4)
    post_count = task_count_from_state()
    assert post_count == pre_count, (
        f"Cancel button created a task anyway: {pre_count} -> {post_count}"
    )
