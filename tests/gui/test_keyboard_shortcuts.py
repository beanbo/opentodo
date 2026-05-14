"""Keyboard shortcut tests:
   - F1 / ? opens hotkeys help dialog (modal verified by luminance change)
   - Search bar can be focused by clicking
   - Source-level checks for Q, +, /, Ctrl+K shortcut wiring

Note: synthesizing single-character keystrokes (Q, +, /) reliably from
pywinauto is fragile when the parent IDE may intercept them. We test
those via source-level wiring + behavioural alternatives (clicking the
search bar, etc.). The behaviour itself is exercised by the QuickAdd
lifecycle and task-action tests which click the QuickAdd directly.
"""
from __future__ import annotations

import time
from pathlib import Path

from conftest import find_plus_glyph, is_accent


REPO = Path(__file__).resolve().parent.parent.parent


def _clear_focus(app):
    """Multiple Esc + click in dead-zone so no widget retains focus."""
    for _ in range(3):
        app.press_key("{ESC}")
        time.sleep(0.15)
    img = app.screenshot()
    app.click_client(800, img.height - 100)
    time.sleep(0.4)


# ── Behavioural tests ───────────────────────────────────────────────────────

def test_f1_opens_hotkeys_help(app, recorder):
    """F1 opens the hotkeys help dialog. Detection: a modal Dialog is
    drawn — Qt fades the underlying content beneath an overlay so the
    average luminance of the centre region changes noticeably."""
    _clear_focus(app)
    pre = app.screenshot()
    pre_lum_arr = pre.crop((300, 150, 1000, 700)).convert("L")
    pre_lum = sum(pre_lum_arr.getdata()) / max(1, pre_lum_arr.width * pre_lum_arr.height)

    app.press_key("{F1}")
    time.sleep(0.6)
    after = app.screenshot()
    recorder("after F1")

    after_lum_arr = after.crop((300, 150, 1000, 700)).convert("L")
    after_lum = sum(after_lum_arr.getdata()) / max(1, after_lum_arr.width * after_lum_arr.height)
    diff = abs(after_lum - pre_lum)
    recorder(f"luminance: {pre_lum:.1f} -> {after_lum:.1f} (diff {diff:.1f})")

    assert diff > 3, (
        f"F1 did not open a modal — luminance unchanged ({diff:.1f})."
    )

    app.press_key("{ESC}")
    time.sleep(0.4)


def test_search_via_click_focuses_input(app, recorder):
    """Clicking the search bar in the top-right of the topbar focuses it
    so subsequent typing shows in the search box."""
    _clear_focus(app)
    pre = app.screenshot()
    search_box = (960, 40, 1170, 78)
    pre_crop = pre.crop(search_box)

    app.click_client(1060, 58)
    time.sleep(0.4)
    app.type_text("searchtext")
    time.sleep(0.5)
    after = app.screenshot()
    recorder("after click + type")

    a = pre_crop.convert("RGB")
    b = after.crop(search_box).convert("RGB")
    changed = sum(
        1 for pa, pb in zip(a.getdata(), b.getdata())
        if abs(pa[0] - pb[0]) + abs(pa[1] - pb[1]) + abs(pa[2] - pb[2]) > 50
    )
    recorder(f"search bar change: {changed} px")
    assert changed > 20, "Clicking the search bar did not focus it."

    app.press_key("{ESC}")
    time.sleep(0.3)


# ── Source-level wiring tests (immune to IDE focus / key-routing) ──────────

def test_q_shortcut_wired():
    """Q opens QuickAdd via focusInput()."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert 'sequence: "Q"' in src, "Q shortcut missing"
    assert "quickAddLoader.item.focusInput()" in src, (
        "Q handler doesn't call focusInput()"
    )


def test_plus_shortcut_wired():
    """'+' also opens QuickAdd."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert 'sequence: "+"' in src, "'+' shortcut missing"


def test_slash_shortcut_wired():
    """'/' focuses the search bar."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert 'sequence: "/"' in src, "'/' shortcut missing"
    assert "focusSearch()" in src, "'/' handler doesn't call focusSearch()"


def test_ctrl_k_shortcut_wired():
    """Ctrl+K opens the command menu palette."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert 'sequence: "Ctrl+K"' in src, "Ctrl+K shortcut missing"
    assert "commandMenuLoader.item.open()" in src
    cm = REPO / "qml" / "components" / "CommandMenu.qml"
    assert cm.exists(), "CommandMenu.qml component is missing"


def test_arrow_navigation_shortcuts_wired():
    """↑ / ↓ navigate through the task list."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert 'sequence: "Up"' in src
    assert 'sequence: "Down"' in src
    assert "navigateSelection" in src


def test_priority_shortcuts_wired():
    """1/2/3/4 keys set priority on the hovered task."""
    src = (REPO / "qml" / "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    for digit in ("1", "2", "3", "4"):
        assert f'sequence: "{digit}"' in src, f"Priority {digit} shortcut missing"
        assert f'updateTask(taskViewModel.hoveredTaskId, "priority", {digit})' in src
