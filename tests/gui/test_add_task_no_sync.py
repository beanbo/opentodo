"""Smoke test: a task can be added with NO Google Drive sync configured.

User claim: 'Без синхронизации с гугл драйв у нас что-ли не работает
добавление задач???' — verifies that addTaskFromQuickAdd → DataStore.addTask
runs without requiring any sync setup.

Also verifies the underlying data layer: the new task must end up in
state.json, not just in the visible UI.
"""
from __future__ import annotations

import time

from PIL import Image

from conftest import find_plus_glyph, task_count_from_state


def test_quickadd_typing_enter_creates_task(app, recorder):
    """Click QuickAdd, type a task name, press Enter, then check that the
    empty-state placeholder disappears (= task was added) AND state.json
    grew by one task."""
    pre_count = task_count_from_state()
    pre = app.screenshot()
    bbox = find_plus_glyph(pre)
    assert bbox, "no '+' anchor"
    x0, y0, x1, y1 = bbox

    # Click in the placeholder area to focus the TextField.
    click_x = x0 + 220
    click_y = (y0 + y1) // 2
    app.click_client(click_x, click_y)
    time.sleep(0.4)
    recorder("after click")

    # Type a unique task title.
    task_title = f"GuiTestTask_{int(time.time())}"
    app.type_text(task_title)
    time.sleep(0.3)
    recorder("after typing")

    # Submit via Enter.
    app.press_key("{ENTER}")
    time.sleep(0.6)   # autosave debounce + redraw
    recorder("after Enter")

    # Wait for FileManager debounce (1s) so we can read state.json.
    time.sleep(1.4)

    post = app.screenshot()
    recorder("final")

    # The empty-state graphic (large green check + "Задач нет") should
    # be gone if the task was added. Detect by sampling a band where the
    # green check sits — coordinates derived from the ~410 px center of
    # a 1296×839 window. The green check is ~80×80 px.
    cx, cy = post.width // 2 + 50, post.height // 2 - 30
    box = (cx - 80, cy - 80, cx + 80, cy + 40)
    crop = post.crop(box).convert("RGB")
    green_count = sum(
        1 for p in crop.getdata()
        if p[1] > 120 and p[1] > p[0] + 30 and p[1] > p[2] + 30
    )
    recorder(f"green pixels in empty-state area: {green_count}")
    assert green_count < 200, (
        f"empty-state is still visible ({green_count} green check pixels) — "
        f"task '{task_title}' was probably NOT added."
    )

    # Oracle: state.json should have grown by exactly one task.
    post_count = task_count_from_state()
    recorder(f"task count: {pre_count} -> {post_count}")
    assert post_count == pre_count + 1, (
        f"task count did not grow by 1: {pre_count} -> {post_count}. "
        f"Either the autosave didn't fire or the write was lost."
    )
