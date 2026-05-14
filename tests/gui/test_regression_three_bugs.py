"""Regression tests for the three bugs reported on 2026-05-02:

  1. Settings sections overlap each other (ColumnLayout with raw `height:`
     instead of `Layout.preferredHeight:`).
  2. Quick Add "+" icon and "Добавить задачу" placeholder are not vertically
     centered inside the top bar.
  3. Quick Add input does not respond to mouse clicks (Loader was clipping
     the bottom 8 px of the panel where the TextField hit-area sat).

Run:  pytest tests/gui/test_regression_three_bugs.py -v

The QML scene graph is one OpenGL surface, so we operate at the pixel
level: PIL screenshots + click-by-coordinate. Coordinates are derived
from the screenshot rather than hardcoded — the suite copes with
different DPI / window sizes.
"""
from __future__ import annotations

import time
from pathlib import Path

import pytest
from PIL import Image

from conftest import find_accent_pixels, find_plus_glyph, is_accent


# ---------------------------------------------------------------------------
# Bug 2: vertical centering of "+" inside the QuickAdd row
# ---------------------------------------------------------------------------
def test_bug2_plus_icon_is_vertically_centered(app, recorder):
    """The '+' glyph in the QuickAdd bar should sit in the middle of the
    56-px row, not jammed against the top edge.

    Before the fix QuickAdd's RowLayout used raw `height:` and the inner
    Text element rendered top-aligned; after the fix the Text has
    `anchors.verticalCenter: barHitArea.verticalCenter` so its centroid
    is mid-row.
    """
    img = app.screenshot()
    bbox = find_plus_glyph(img)
    recorder(f"plus glyph bbox: {bbox}")
    assert bbox, "no '+' glyph (accent-colored cluster) found in QuickAdd area"

    x0, y0, x1, y1 = bbox
    glyph_h = y1 - y0
    glyph_w = x1 - x0
    # A real "+" glyph is roughly square (~15-25 px each side). If the
    # bbox is much taller than that we caught polluting accent pixels.
    assert 6 < glyph_h < 22, (
        f"'+' glyph height looks wrong ({glyph_h}px) — search box probably "
        f"caught extra accent regions. bbox={bbox}"
    )
    assert 4 < glyph_w < 32, (
        f"'+' glyph width looks wrong ({glyph_w}px). bbox={bbox}"
    )
    # The glyph should be vertically centered within its 56-px row.
    # When centered, there's ~16-20 px of dark space above the glyph.
    # When top-aligned (the bug), the glyph sits at the top of the row
    # with only ~2-4 px above it.
    row_top_min = max(0, y0 - 30)
    above_strip = img.crop((x0 - 2, row_top_min, x1 + 2, y0)).convert("L")
    above_pixels = list(above_strip.getdata())
    dark_above = sum(1 for p in above_pixels if p < 80)
    total_above = len(above_pixels)
    above_ratio = dark_above / total_above if total_above else 0
    recorder(f"glyph: bbox={bbox}, above_dark_ratio={above_ratio:.2f}")
    assert above_ratio > 0.5 and (y0 - row_top_min) >= 8, (
        f"'+' has too little space above it (ratio={above_ratio:.2f}, "
        f"top_offset={y0 - row_top_min}px). Top-aligned regression."
    )


# ---------------------------------------------------------------------------
# Bug 3: clicking the QuickAdd row should focus the input and expand panel
# ---------------------------------------------------------------------------
def test_bug3_quickadd_click_focuses_input(app, recorder):
    """A click on the placeholder strip must focus the TextField — proven
    by typing afterwards: the typed character must appear in the input
    area (placeholder text gone or content visible).

    Before the fix the Loader's bottomMargin clipped the bottom of
    QuickAdd; clicks landing in that strip were swallowed and the field
    never received focus, so typing went nowhere.
    """
    pre = app.screenshot()
    bbox = find_plus_glyph(pre)
    assert bbox, "no '+' anchor found"
    x0, y0, x1, y1 = bbox
    click_x = x0 + 220   # well inside the placeholder area
    click_y = (y0 + y1) // 2
    recorder(f"click target ({click_x}, {click_y})")

    app.click_client(click_x, click_y)
    time.sleep(0.4)
    # Type a character; if focus landed on the TextField, the character
    # appears in the input (placeholder vanishes, character glyph shows
    # at ~placeholder position).
    app.type_text("Z")
    time.sleep(0.4)
    post = app.screenshot()
    recorder("after click + type Z")

    # Compare the band where text rendering happens (left of '+' through
    # 250 px right). A typed "Z" introduces brand-new bright pixels.
    text_box = (x0 + 20, y0 - 4, x0 + 250, y1 + 6)
    a = pre.crop(text_box).convert("RGB")
    b = post.crop(text_box).convert("RGB")
    changed = sum(
        1 for pa, pb in zip(a.getdata(), b.getdata())
        if abs(pa[0] - pb[0]) + abs(pa[1] - pb[1]) + abs(pa[2] - pb[2]) > 60
    )
    total = a.width * a.height
    change_ratio = changed / total
    recorder(f"text-area change ratio: {change_ratio:.2%} ({changed}/{total} px)")

    assert change_ratio > 0.002, (
        f"typing 'Z' after click did not change the input-area pixels "
        f"(only {change_ratio:.2%} differ). The click did not focus the "
        f"TextField."
    )


def test_bug3b_quickadd_has_focus_input_function(app, recorder):
    """QuickAdd must expose a `focusInput()` method so the EmptyState
    button and hotkeys can programmatically focus the input. Before the
    fix, callers used `.forceActiveFocus()` on the Rectangle which is
    a no-op (Rectangles aren't focusable); only the inner TextField
    needs the focus."""
    qml = (Path(__file__).resolve().parent.parent.parent / "qml" /
           "components" / "QuickAdd.qml").read_text(encoding="utf-8")
    assert "function focusInput()" in qml, (
        "QuickAdd.qml is missing the public focusInput() method"
    )
    # And the empty-state caller must use it (not the broken pattern).
    desktop = (Path(__file__).resolve().parent.parent.parent / "qml" /
               "layouts" / "DesktopLayout.qml").read_text(encoding="utf-8")
    assert ".focusInput()" in desktop, (
        "DesktopLayout.qml's EmptyState button still uses the broken "
        "forceActiveFocus() instead of focusInput()"
    )


# ---------------------------------------------------------------------------
# Bug 1: opening Settings produces a non-overlapping layout
# ---------------------------------------------------------------------------


def test_bug1_settings_section_root_uses_implicit_height(app, recorder):
    """The OUTER Rectangle of every SettingsSection*.qml must use
    `implicitHeight:` (or Layout.preferredHeight) — not raw `height:`.

    A ColumnLayout reads `Layout.*` and `implicitHeight` from its
    children. If a child sets only `height:`, the layout sees
    implicitHeight=0 and stacks every section at y=0 → they overlap
    visibly. This bit us a SECOND time after the inner-row fix (bug 1
    original) because the SECTION ROOT was still using `height:`.
    """
    import re
    settings_dir = Path(__file__).resolve().parent.parent.parent / \
        "qml" / "components" / "settings"
    bad = []
    for f in settings_dir.glob("SettingsSection*.qml"):
        text = f.read_text(encoding="utf-8")
        # Pattern: a top-level `height: <expression>` directly inside the
        # root Rectangle. Look for the line that mentions sectionHeader+
        # sectionContent.implicitHeight — that's the buggy collapsed-state
        # height that has to live on implicitHeight, not height.
        if re.search(
                r"^\s+height:\s*sectionHeader\.height\s*\+\s*\(",
                text, re.MULTILINE):
            bad.append(f.name)
    recorder(f"checked {len(list(settings_dir.glob('SettingsSection*.qml')))} sections")
    assert not bad, (
        f"these section files still use raw `height:` on the root "
        f"Rectangle — sections will overlap inside the ColumnLayout: {bad}"
    )


def test_bug1_inner_settings_rows_use_layout_preferred_height(app, recorder):
    """Inside each SettingsSection ColumnLayout, the per-setting Rectangles
    must use `Layout.preferredHeight:`, not raw `height:`. This was the
    ORIGINAL bug 1 — keep guarding it."""
    import re
    settings_dir = Path(__file__).resolve().parent.parent.parent / \
        "qml" / "components" / "settings"
    bad_files = []
    for f in settings_dir.glob("*.qml"):
        text = f.read_text(encoding="utf-8")
        # Find any `Layout.fillWidth: true` followed shortly by raw
        # `height: NN` — that's the regression pattern.
        if re.search(r"Layout\.fillWidth:\s*true\s*\n\s+height:\s*\d+",
                     text):
            bad_files.append(f.name)

    recorder(f"scanned {len(list(settings_dir.glob('*.qml')))} settings files")
    assert not bad_files, (
        f"these settings files still use bare `height: N` instead of "
        f"`Layout.preferredHeight: N` inside ColumnLayout — sections "
        f"will overlap: {bad_files}"
    )
