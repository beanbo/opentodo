"""GUI test fixtures.

Approach mirrors the RAC pywinauto/recorder pattern:
  - one running OpenTodo.exe instance per test session,
  - per-test screenshot recording to docx,
  - cached HWND on the page object so no Win32 re-resolves later.

OpenTodo's QML scene graph is a single OpenGL surface, so we cannot drill
into individual buttons via pywinauto (no Win32 children exist). Tests
work at the window level: send mouse clicks at known coordinates, take
PIL screenshots of the client area, and assert on pixel sampling /
rendered text from cropped regions.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from datetime import datetime
from pathlib import Path
from typing import Iterator

import pytest
from PIL import Image, ImageGrab
from docx import Document
from docx.shared import Inches
from pywinauto import Application

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
ARTIFACTS = ROOT / "tests" / "gui" / "_artifacts"
ARTIFACTS.mkdir(exist_ok=True)

# Where OpenTodo persists its state. Same on every Windows install.
APPDATA = Path(os.environ.get("APPDATA", "")) / "OpenTodo" / "OpenTodo" / "OpenTodo"
STATE_JSON = APPDATA / "state.json"
LOCAL_LOG_DIR = APPDATA / "local_log"


# ─────────────────────────────────────────────────────────────────────────────
# Page object: OpenTodo main window
# ─────────────────────────────────────────────────────────────────────────────
class OpenTodoApp:
    """Cached HWND wrapper around the running OpenTodo window."""

    def __init__(self, dlg):
        self.dlg = dlg
        try:
            self.hwnd = dlg.handle
        except Exception:
            self.hwnd = None
        self.rect = dlg.rectangle()

    @property
    def client_box(self):
        """(left, top, right, bottom) of the client area for ImageGrab.bbox."""
        r = self.rect
        return (r.left, r.top, r.right, r.bottom)

    def screenshot(self) -> Image.Image:
        """PIL screenshot of the window client area. Forces OpenTodo to
        the foreground first — pytest's terminal can hide it between
        actions and ImageGrab captures whatever is on screen at the
        absolute coordinates."""
        try:
            self.dlg.set_focus()
        except Exception:
            pass
        time.sleep(0.15)
        # Re-read the rect each time — the window may have been moved.
        self.rect = self.dlg.rectangle()
        return ImageGrab.grab(bbox=self.client_box, all_screens=True)

    def click_client(self, x: int, y: int):
        """Click at (x, y) relative to the client window's top-left.
        Brings the window to the foreground first so the click does not
        leak into whatever window happens to be on top."""
        from pywinauto.mouse import click
        try:
            self.dlg.set_focus()
        except Exception:
            pass
        time.sleep(0.15)
        # Re-read the window rect — it may have moved/resized since init.
        self.rect = self.dlg.rectangle()
        screen_x = self.rect.left + x
        screen_y = self.rect.top + y
        click(coords=(screen_x, screen_y))

    def double_click_client(self, x: int, y: int):
        from pywinauto.mouse import double_click
        try:
            self.dlg.set_focus()
        except Exception:
            pass
        time.sleep(0.15)
        self.rect = self.dlg.rectangle()
        double_click(coords=(self.rect.left + x, self.rect.top + y))

    def right_click_client(self, x: int, y: int):
        from pywinauto.mouse import right_click
        try:
            self.dlg.set_focus()
        except Exception:
            pass
        time.sleep(0.15)
        self.rect = self.dlg.rectangle()
        right_click(coords=(self.rect.left + x, self.rect.top + y))

    def type_text(self, text: str):
        from pywinauto.keyboard import send_keys
        send_keys(text, with_spaces=True, with_tabs=True, with_newlines=False)

    def press_key(self, key: str):
        """Send a key like '{ENTER}', '{ESC}', '{TAB}'."""
        from pywinauto.keyboard import send_keys
        send_keys(key)

    def width(self) -> int:
        return self.rect.right - self.rect.left

    def height(self) -> int:
        return self.rect.bottom - self.rect.top


# ─────────────────────────────────────────────────────────────────────────────
# State helpers
# ─────────────────────────────────────────────────────────────────────────────
def reset_state():
    """Wipe state.json + local_log so the next launch starts clean.
    Safe to call between sessions but never while OpenTodo.exe is running
    (it would just rewrite the file from memory)."""
    if STATE_JSON.exists():
        STATE_JSON.unlink()
    if LOCAL_LOG_DIR.exists():
        shutil.rmtree(LOCAL_LOG_DIR, ignore_errors=True)


def read_state() -> dict:
    if not STATE_JSON.exists():
        return {}
    return json.loads(STATE_JSON.read_text(encoding="utf-8"))


# ─────────────────────────────────────────────────────────────────────────────
# Session: one running OpenTodo across all tests
# ─────────────────────────────────────────────────────────────────────────────
@pytest.fixture(scope="session")
def app() -> Iterator[OpenTodoApp]:
    if not EXE.exists():
        pytest.skip(f"OpenTodo.exe not built at {EXE}")

    # Kill any leftover instances first, THEN reset state — we don't want
    # a still-running process to flush its in-memory state right back to
    # disk after we deleted it.
    subprocess.run(["taskkill", "/F", "/IM", "OpenTodo.exe"],
                   capture_output=True, check=False)
    time.sleep(0.5)
    reset_state()

    proc = subprocess.Popen([str(EXE)], cwd=str(EXE.parent))
    time.sleep(3.5)  # initial QML compilation

    pwa = Application(backend="win32").connect(process=proc.pid)
    dlg = pwa.window(title_re="OpenTodo")
    dlg.wait("visible", timeout=15)
    dlg.set_focus()
    time.sleep(0.4)

    app = OpenTodoApp(dlg)

    # The telemetry prompt was removed, but keep an Esc just in case any
    # other modal pops on first run (update check, etc.).
    time.sleep(0.4)
    app.press_key("{ESC}")
    time.sleep(0.3)

    yield app

    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()


@pytest.fixture(autouse=True)
def reset_between_tests(app):
    """Best-effort reset of UI state between tests:
      1. Press Esc several times (collapse QuickAdd, close menus, deselect).
      2. Make sure the sidebar is visible (toggle if not).
      3. Click the top "Входящие" sidebar row to land on Inbox.
    """
    yield
    try:
        # 3× Esc: covers QuickAdd-expanded, dialog-open, selection-active.
        for _ in range(3):
            app.press_key("{ESC}")
            time.sleep(0.15)

        # Detect sidebar visibility — when collapsed, the leftmost 280px
        # contains main-content background, not a sidebar surface. Easy
        # heuristic: any accent-colored pixel in the leftmost 280×600 px
        # (Inbox highlight or any active strip) means sidebar is visible.
        img = app.screenshot()
        sidebar_region = img.crop((0, 30, 280, 600)).convert("RGB")
        accent_visible = any(
            r > 90 and r > g + 30 and r > b + 30 and g < 130 and b < 130
            for (r, g, b) in sidebar_region.getdata()
        )
        if not accent_visible:
            # Sidebar is hidden — click the hamburger to restore. When
            # collapsed, hamburger sits near the left edge of the topbar.
            app.click_client(48, 56)
            time.sleep(0.5)

        # Now navigate back to Inbox (top sidebar row).
        app.click_client(100, 57)
        time.sleep(0.4)
    except Exception:
        pass


# ─────────────────────────────────────────────────────────────────────────────
# Per-test docx recorder (RAC-style)
# ─────────────────────────────────────────────────────────────────────────────
@pytest.fixture
def recorder(request, app: OpenTodoApp):
    """Captures screenshots before/after the test, embeds them in a docx
    keyed on the test's nodeid, with the test docstring as italic intro."""
    test_name = request.node.name
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    docx_path = ARTIFACTS / f"{test_name}_{ts}.docx"

    doc = Document()
    doc.add_heading(test_name, level=1)
    if request.node.function.__doc__:
        p = doc.add_paragraph()
        run = p.add_run(request.node.function.__doc__.strip())
        run.italic = True

    # Pre-state
    pre = ARTIFACTS / f"{test_name}_pre.png"
    app.screenshot().save(pre)
    doc.add_paragraph("Initial state:")
    doc.add_picture(str(pre), width=Inches(6))

    captured = []

    def step(label: str):
        ts2 = datetime.now().strftime("%H%M%S_%f")[:-3]
        # Sanitise label so the filename is valid on Windows (no :/\\*?"<>|).
        safe_label = "".join(
            c if c.isalnum() or c in ("-", "_", ".") else "_"
            for c in label
        )[:60]
        path = ARTIFACTS / f"{test_name}_{ts2}_{safe_label}.png"
        app.screenshot().save(path)
        captured.append((label, path))

    yield step

    # Post-state
    post = ARTIFACTS / f"{test_name}_post.png"
    app.screenshot().save(post)
    doc.add_paragraph("Final state:")
    doc.add_picture(str(post), width=Inches(6))

    for label, path in captured:
        doc.add_paragraph(label)
        doc.add_picture(str(path), width=Inches(6))

    doc.save(docx_path)
    print(f"\n[recorder] {docx_path.name}")


# ─────────────────────────────────────────────────────────────────────────────
# Pixel-level helpers — shared across the whole suite
# ─────────────────────────────────────────────────────────────────────────────
def is_accent(rgb) -> bool:
    """Coral red on dark background — but anti-aliasing produces a wide
    family of reddish hues. Match anything where red dominates and
    saturation is high enough to be the accent (not just dark grey)."""
    r, g, b = rgb[:3]
    return r > 90 and r > g + 30 and r > b + 30 and g < 130 and b < 130


def find_accent_pixels(img: Image.Image, box) -> list[tuple[int, int]]:
    """All (x, y) within `box` whose colour matches the accent."""
    x0, y0, x1, y1 = box
    crop = img.crop(box).convert("RGB")
    out = []
    for y in range(crop.height):
        for x in range(crop.width):
            if is_accent(crop.getpixel((x, y))):
                out.append((x0 + x, y0 + y))
    return out


def cluster_2d(pixels, gap: int = 6):
    """Greedy 2-D clustering: pixels within `gap` (in either axis) of an
    existing cluster member join that cluster. Returns list of clusters,
    each cluster a list of (x, y).

    O(N * C) where N=pixels, C=clusters — good enough for a few hundred
    accent pixels per screenshot. We use this instead of the previous
    sort-by-x-then-walk because the priority circle directly under the
    QuickAdd "+" shares the same x range and would collapse into one
    fake cluster."""
    clusters: list[list[tuple[int, int]]] = []
    for x, y in pixels:
        joined = False
        for c in clusters:
            for cx, cy in c:
                if abs(cx - x) <= gap and abs(cy - y) <= gap:
                    c.append((x, y))
                    joined = True
                    break
            if joined:
                break
        if not joined:
            clusters.append([(x, y)])
    return clusters


def find_plus_glyph(img: Image.Image) -> tuple[int, int, int, int] | None:
    """Locate the QuickAdd '+' glyph by finding the topmost compact
    cluster of accent pixels in a horizontal band right of the sidebar.

    Constraints (the '+' is small and roughly square):
      - 4 ≤ width  ≤ 25 px
      - 4 ≤ height ≤ 22 px
      - ≥ 8 pixels in cluster (not subpixel anti-aliasing noise)
    Picks the topmost cluster — the QuickAdd row is at the top of the
    main content area; priority circles live below.
    """
    sidebar_max = 320
    title_bar_max = 90
    band_height = 80
    box = (sidebar_max, title_bar_max,
           sidebar_max + 100, title_bar_max + band_height)
    accent = find_accent_pixels(img, box)
    if len(accent) < 8:
        return None

    clusters = cluster_2d(accent, gap=4)
    candidates = []
    for c in clusters:
        if len(c) < 8:
            continue
        xs, ys = [p[0] for p in c], [p[1] for p in c]
        w, h = max(xs) - min(xs), max(ys) - min(ys)
        if 4 <= w <= 25 and 4 <= h <= 22:
            candidates.append((min(ys), (min(xs), min(ys), max(xs), max(ys))))
    if not candidates:
        return None
    # Topmost compact cluster wins.
    candidates.sort()
    return candidates[0][1]


def task_count_from_state() -> int:
    """How many non-deleted tasks live in state.json? Useful as a
    correctness oracle parallel to the visible UI count."""
    state = read_state()
    tasks = state.get("tasks", [])
    return sum(1 for t in tasks if not t.get("is_deleted"))
