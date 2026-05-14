"""Toggle the sidebar Switches in Settings and verify they reach disk."""
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
                   all_screens=True).save(OUT / f"_p2_{name}.png")


# Settings (with no projects, "Настройки" is at y≈378)
click(coords=(r.left + 100, r.top + 378))
time.sleep(0.7)
shoot("01_settings_top")

# Scroll all the way to the bottom to find Sidebar section
for _ in range(8):
    scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
    time.sleep(0.2)
shoot("02_settings_scrolled_max")

# Find the "Боковая панель" section header. Need to expand it (default collapsed).
# We'll need its Y position from the screenshot.

# Wait for save debounce
time.sleep(2.0)
print("Settings before any toggle:")
if STATE.exists():
    s = json.loads(STATE.read_text(encoding='utf-8'))
    print(f"  {s.get('settings', {})}")

proc.terminate()
print("done")
