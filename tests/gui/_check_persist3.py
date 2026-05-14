"""Click a real Switch in expanded Sidebar settings, verify it persists."""
import json
import os
import shutil
import subprocess
import time
from pathlib import Path
from PIL import ImageGrab
from pywinauto import Application
from pywinauto.mouse import click, scroll
from pywinauto.keyboard import send_keys

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
APPDATA = Path(os.environ["APPDATA"]) / "OpenTodo" / "OpenTodo" / "OpenTodo"
STATE = APPDATA / "state.json"
OUT = Path(__file__).parent / "_artifacts"

if STATE.exists(): STATE.unlink()
ll = APPDATA / "local_log"
if ll.exists(): shutil.rmtree(ll, ignore_errors=True)

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
                   all_screens=True).save(OUT / f"_p3_{name}.png")


def state_now():
    if not STATE.exists(): return {}
    return json.loads(STATE.read_text(encoding='utf-8')).get('settings', {})


# Initial state — note baseline
print("Initial settings:", state_now())

# Settings page
click(coords=(r.left + 100, r.top + 378))
time.sleep(0.7)

# Scroll all the way down
for _ in range(8):
    scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
    time.sleep(0.2)
shoot("01_scrolled_max")

# From the previous screenshot we know "Боковая панель" header is around y=624.
# Click it to expand.
click(coords=(r.left + 700, r.top + 624))
time.sleep(0.6)
shoot("02_after_sidebar_expanded")

# After expand, the section grows. The first Switch (Показывать счётчик
# Входящих) is roughly 50 px below the header.
# Let me scroll a bit more to ensure visible.
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
time.sleep(0.3)
shoot("03_after_scroll_to_switch")

# Click ON the Switch (right-aligned). Switch indicator is roughly at x=1200
# in 1296 wide window.
# We don't know exact y — try a few likely positions and screenshot each.
print("\nBefore Switch click:", state_now())

click(coords=(r.left + 1200, r.top + 470))
time.sleep(2.0)   # > 1s debounce
shoot("04_after_switch_click")
print("After 1st Switch click:", state_now())

# Try second switch
click(coords=(r.left + 1200, r.top + 522))
time.sleep(2.0)
shoot("05_after_2nd_switch_click")
print("After 2nd Switch click:", state_now())

# Try third switch
click(coords=(r.left + 1200, r.top + 574))
time.sleep(2.0)
shoot("06_after_3rd_switch_click")
print("After 3rd Switch click:", state_now())

proc.terminate()
print("done")
