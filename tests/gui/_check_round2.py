"""Visual round-2 verification: switch toggles, AI tab, project context menu,
TaskEditor save button + calendar."""
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
                   all_screens=True).save(OUT / f"_r2_{name}.png")


# ── Add a task so we have something to click ─────────────────────────────
click(coords=(r.left + 540, r.top + 120))
time.sleep(0.4)
send_keys("Test editor task", with_spaces=True)
time.sleep(0.2)
send_keys("{ENTER}")
time.sleep(1.0)
shoot("01_after_add")

# Click on the task to open editor — click well to the right of the circle
# so we hit the title area, not the priority circle.
click(coords=(r.left + 500, r.top + 175))
time.sleep(0.7)
shoot("02_editor_open")

# Close editor with Esc
send_keys("{ESC}")
time.sleep(0.4)

# ── Navigate to Помощник (AI) ────────────────────────────────────────────
# After fix the row should open the AI view, not Inbox. With no projects
# the rows are at: Входящие(57) Сегодня(93) Предстоящие(129) ПРОЕКТЫ(170)
# ФИЛЬТРЫ(210) Продуктивность(270) Уведомления(306) Помощник(342)
click(coords=(r.left + 100, r.top + 342))
time.sleep(0.8)
shoot("03_ai_view")

# Back to Inbox
click(coords=(r.left + 100, r.top + 57))
time.sleep(0.5)

# ── Open Settings → toggle a Switch ──────────────────────────────────────
click(coords=(r.left + 100, r.top + 378))   # Настройки
time.sleep(0.8)
shoot("04_settings_top")

# Scroll to bottom to find Sidebar section
from pywinauto.mouse import scroll
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-7)
time.sleep(0.5)
shoot("05_settings_lower")

# Close app
proc.terminate()
print("done")
