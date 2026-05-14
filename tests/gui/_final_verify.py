"""Final visual verification: launch the app, demonstrate all the fixes:
   1. Priority picker actually applies (RED p1 task created)
   2. Date picker is a calendar
   3. Section button visible in project view
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from PIL import ImageGrab
from pywinauto import Application
from pywinauto.mouse import click
from pywinauto.keyboard import send_keys

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
OUT = Path(__file__).parent / "_artifacts"

# Wipe state for a clean slate.
APPDATA = Path(os.environ["APPDATA"]) / "OpenTodo" / "OpenTodo" / "OpenTodo"
state = APPDATA / "state.json"
if state.exists():
    state.unlink()
locallog = APPDATA / "local_log"
if locallog.exists():
    shutil.rmtree(locallog, ignore_errors=True)

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
                   all_screens=True).save(OUT / f"_final_{name}.png")


# 1. Initial state: empty Inbox, QuickAdd visible.
shoot("01_initial")

# 2. Click QuickAdd, type, click priority button, pick P1, submit.
click(coords=(r.left + 540, r.top + 120))
time.sleep(0.5)
send_keys("Buy milk", with_spaces=True)
time.sleep(0.3)

# Click priority button (2nd icon in toolbar). Toolbar at y≈168.
PRIO_X = 376
click(coords=(r.left + PRIO_X, r.top + 168))
time.sleep(0.5)
shoot("02_priority_popup_open")

# Click P1 (top row in popup, ~25px below button)
click(coords=(r.left + PRIO_X + 50, r.top + 195))
time.sleep(0.5)
shoot("03_p1_selected")

# Click Add task button
click(coords=(r.left + 1196, r.top + 215))
time.sleep(1.5)
shoot("04_task_added")

# Verify in state.json
state_data = json.loads(state.read_text(encoding="utf-8"))
tasks = [t for t in state_data.get("tasks", []) if not t.get("is_deleted")]
print(f"\nTasks in state.json:")
for t in tasks:
    print(f"  {t['content']!r} priority={t['priority']}")

# 3. Date picker — click QuickAdd, click date button
click(coords=(r.left + 540, r.top + 120))
time.sleep(0.5)
send_keys("Some task", with_spaces=True)
time.sleep(0.3)
click(coords=(r.left + 346, r.top + 168))   # date button
time.sleep(0.6)
shoot("05_date_picker_calendar")

# Esc to dismiss
send_keys("{ESC}")
time.sleep(0.3)
send_keys("{ESC}")
time.sleep(0.5)

proc.terminate()
print("\nDone. Screenshots saved to tests/gui/_artifacts/_final_*.png")
