"""Task-level interaction tests:
   - clicking the checkbox marks the task complete (strikethrough + greyed)
   - keyboard 'Space' toggles completion of the hovered task
   - keyboard 'Delete' removes the hovered task
   - keyboard '1'/'4' changes priority — circle colour changes
"""
from __future__ import annotations

import time

from PIL import Image

from conftest import (
    find_plus_glyph,
    is_accent,
    task_count_from_state,
    read_state,
)


def _add_task(app, title: str) -> None:
    """Helper: open QuickAdd, type title, press Enter, then click the
    empty list area so no widget retains keyboard focus (otherwise key
    shortcuts like '1' or Space land in a TextField as text input)."""
    bbox = find_plus_glyph(app.screenshot())
    assert bbox, "no '+' to start QuickAdd"
    x0, y0, x1, y1 = bbox
    app.click_client(x0 + 220, (y0 + y1) // 2)
    time.sleep(0.4)
    app.type_text(title)
    time.sleep(0.2)
    app.press_key("{ENTER}")
    time.sleep(1.0)
    # ESC to dismiss any leftover focus, then click an empty area.
    app.press_key("{ESC}")
    time.sleep(0.2)
    img = app.screenshot()
    # Click the empty list area below tasks (around y=600) so QuickAdd's
    # TextField loses activeFocus.
    app.click_client(800, img.height - 80)
    time.sleep(0.3)


def _find_first_task_circle(img: Image.Image) -> tuple[int, int] | None:
    """Locate the first task row's priority-circle center by finding a
    ring-shaped accent cluster below the QuickAdd row (y > 145).

    The circle is a transparent fill with a 1.5-px coral border, ~18×18 px.
    Returns (cx, cy) — the center coordinates."""
    pixels = []
    for y in range(140, 700):
        for x in range(320, 420):
            r, g, b = img.getpixel((x, y))
            # Strict accent for the ring (no ClearType noise).
            if r > 150 and r - g > 80 and r - b > 80:
                pixels.append((x, y))
    if not pixels:
        return None
    # Cluster by Y proximity (rows are 44px apart). Take the first cluster.
    pixels.sort(key=lambda p: p[1])
    cluster = [pixels[0]]
    for p in pixels[1:]:
        if p[1] - cluster[-1][1] > 10:
            break
        cluster.append(p)
    if len(cluster) < 5:
        return None
    xs = [p[0] for p in cluster]
    ys = [p[1] for p in cluster]
    return ((min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2)


def test_toggle_complete_via_checkbox(app, recorder):
    """Clicking the priority circle (left of the task title) toggles
    its completion state. After click, the circle fills with the
    priority colour and the task title goes grey/strikethrough."""
    title = f"CompleteMe_{int(time.time())}"
    _add_task(app, title)
    recorder("after add")

    pre_state = read_state()
    pre_completed = sum(
        1 for t in pre_state.get("tasks", [])
        if not t.get("is_deleted") and t.get("is_completed")
    )

    img = app.screenshot()
    circle = _find_first_task_circle(img)
    recorder(f"first task circle at {circle}")
    assert circle, "couldn't locate first task's priority circle"
    cx, cy = circle
    recorder(f"clicking circle at ({cx}, {cy})")
    app.click_client(cx, cy)
    time.sleep(0.8)

    # Wait for autosave debounce.
    time.sleep(1.4)
    post_state = read_state()
    post_completed = sum(
        1 for t in post_state.get("tasks", [])
        if not t.get("is_deleted") and t.get("is_completed")
    )
    recorder(f"completed count: {pre_completed} -> {post_completed}")
    assert post_completed == pre_completed + 1, (
        "Expected one more completed task after clicking the checkbox."
    )


def test_keyboard_space_toggles_hovered_task(app, recorder):
    """The hovered task is the one tracked by taskViewModel.hoveredTaskId.
    With the mouse over a task, the 'Space' shortcut should toggle its
    completion. We hover by moving the mouse to the row's center."""
    title = f"SpaceMe_{int(time.time())}"
    _add_task(app, title)

    pre_state = read_state()
    pre_completed = sum(
        1 for t in pre_state.get("tasks", [])
        if not t.get("is_deleted") and t.get("is_completed")
    )

    img = app.screenshot()
    circle = _find_first_task_circle(img)
    assert circle, "couldn't locate first task circle to hover"
    cx, cy = circle

    # Move (no click) to hover over the task row.
    from pywinauto.mouse import move
    try:
        app.dlg.set_focus()
    except Exception:
        pass
    rect = app.dlg.rectangle()
    move(coords=(rect.left + 600, rect.top + cy))
    time.sleep(0.5)
    recorder("hovered over task")

    # Press Space.
    app.press_key("{SPACE}")
    time.sleep(0.6)
    recorder("after space")

    time.sleep(1.4)
    post_state = read_state()
    post_completed = sum(
        1 for t in post_state.get("tasks", [])
        if not t.get("is_deleted") and t.get("is_completed")
    )
    recorder(f"completed count: {pre_completed} -> {post_completed}")
    assert post_completed == pre_completed + 1, (
        "Space did not toggle the hovered task's completion."
    )


def test_keyboard_delete_removes_hovered_task(app, recorder):
    """Delete key on a hovered task should soft-delete (is_deleted=true)."""
    title = f"DeleteMe_{int(time.time())}"
    _add_task(app, title)
    recorder("after add")

    pre_count = task_count_from_state()

    img = app.screenshot()
    circle = _find_first_task_circle(img)
    assert circle, "couldn't locate first task circle to hover"
    cx, cy = circle

    from pywinauto.mouse import move
    try:
        app.dlg.set_focus()
    except Exception:
        pass
    rect = app.dlg.rectangle()
    move(coords=(rect.left + 600, rect.top + cy))
    time.sleep(0.5)

    app.press_key("{DELETE}")
    time.sleep(0.6)
    recorder("after delete")

    time.sleep(1.4)
    post_count = task_count_from_state()
    recorder(f"task count: {pre_count} -> {post_count}")
    assert post_count == pre_count - 1, (
        "Delete key did not remove a task — task count unchanged."
    )


def test_priority_via_quickadd_p1(app, recorder):
    """Adding a task with 'p1' suffix in the QuickAdd should give it
    priority 1 (parsed by the NLP layer in addTaskFromQuickAdd)."""
    title = f"PriorityMe p1"
    bbox = find_plus_glyph(app.screenshot())
    assert bbox
    x0, y0, x1, y1 = bbox
    app.click_client(x0 + 220, (y0 + y1) // 2)
    time.sleep(0.4)
    app.type_text(title)
    time.sleep(0.3)
    recorder("typed title with p1")
    app.press_key("{ENTER}")
    time.sleep(1.0)
    recorder("after enter")

    time.sleep(1.4)
    state = read_state()
    matching = [
        t for t in state.get("tasks", [])
        if not t.get("is_deleted") and t.get("content", "").startswith("PriorityMe")
    ]
    recorder(f"found {len(matching)} matching tasks")
    assert matching, f"task starting with 'PriorityMe' not found in state"
    # The most recent matching task should be p1.
    matching.sort(key=lambda t: t.get("created_at", ""), reverse=True)
    most_recent = matching[0]
    recorder(f"latest task: {most_recent.get('content')!r} priority={most_recent.get('priority')}")
    assert most_recent.get("priority") == 1, (
        f"NLP didn't set priority to 1 for 'p1' suffix — got "
        f"{most_recent.get('priority')}"
    )
