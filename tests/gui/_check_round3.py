"""Round-3 visual smoke: project context menu, narrow-window mode,
section drop, all UI fixes."""
from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

from PIL import ImageGrab
from pywinauto import Application
from pywinauto.mouse import click, right_click
from pywinauto.keyboard import send_keys

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
OUT = Path(__file__).parent / "_artifacts"
APPDATA = Path(os.environ["APPDATA"]) / "OpenTodo" / "OpenTodo" / "OpenTodo"
state = APPDATA / "state.json"
locallog = APPDATA / "local_log"

if state.exists(): state.unlink()
if locallog.exists(): shutil.rmtree(locallog, ignore_errors=True)

subprocess.run(["taskkill", "/F", "/IM", "OpenTodo.exe"], capture_output=True, check=False)
time.sleep(0.5)

proc = subprocess.Popen([str(EXE)], cwd=str(EXE.parent))
time.sleep(3.5)

pwa = Application(backend="win32").connect(process=proc.pid)
dlg = pwa.window(title_re="OpenTodo")
dlg.wait("visible", timeout=15)
dlg.set_focus()
time.sleep(0.5)

r = dlg.rectangle()


def shoot(name):
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
                   all_screens=True).save(OUT / f"_r3_{name}.png")


# ── Add a project so we have something to right-click ────────────────────
# Click "+" next to ПРОЕКТЫ — the section header has add button at right edge
click(coords=(r.left + 270, r.top + 170))   # +next to "ПРОЕКТЫ"
time.sleep(0.6)
shoot("01_add_proj_dialog")
send_keys("Test Project", with_spaces=True)
time.sleep(0.3)
send_keys("{TAB}{TAB}{ENTER}")   # confirm with OK button
time.sleep(0.8)
shoot("02_after_proj_added")

# Right-click on the new project to test context menu — projects appear
# below "ПРОЕКТЫ" header at y around 200
right_click(coords=(r.left + 100, r.top + 205))
time.sleep(0.6)
shoot("03_project_ctx_menu")

# Esc to close
send_keys("{ESC}")
time.sleep(0.4)

# Click on the project to open it
click(coords=(r.left + 100, r.top + 205))
time.sleep(0.6)
shoot("04_project_view")

# Click "+ Раздел" button in toolbar to add a section
click(coords=(r.left + 580, r.top + 90))   # toolbar add-section button
time.sleep(0.6)
shoot("05_add_section_dialog")
send_keys("Section A", with_spaces=True)
time.sleep(0.3)
send_keys("{TAB}{TAB}{ENTER}")
time.sleep(0.6)
shoot("06_after_section_added")

# ── Narrow-window mode: shrink the window ────────────────────────────────
# Resize window to <760 px wide
import ctypes
user32 = ctypes.windll.user32
hwnd = dlg.handle
# Use SetWindowPos: x, y, cx, cy
user32.SetWindowPos(hwnd, 0, r.left, r.top, 600, r.bottom - r.top, 0)
time.sleep(0.8)
shoot("07_narrow_mode_no_sidebar")

# Click hamburger to open drawer
click(coords=(r.left + 30, r.top + 56))
time.sleep(0.6)
shoot("08_narrow_drawer_open")

# Restore wide
user32.SetWindowPos(hwnd, 0, r.left, r.top, 1296, r.bottom - r.top, 0)
time.sleep(0.8)
shoot("09_after_restore_wide")

proc.terminate()
print("done")
