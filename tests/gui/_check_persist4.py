"""Click EXACTLY on Sidebar setting Switches."""
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
                   all_screens=True).save(OUT / f"_p4_{name}.png")


def state_now():
    if not STATE.exists(): return {}
    return json.loads(STATE.read_text(encoding='utf-8')).get('settings', {})


# Settings
click(coords=(r.left + 100, r.top + 378))
time.sleep(0.7)

# Scroll
for _ in range(8):
    scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
    time.sleep(0.2)

# Expand Sidebar section
click(coords=(r.left + 700, r.top + 624))
time.sleep(0.5)
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
time.sleep(0.3)
shoot("01_before_click")

print("Before click:", state_now())

# Click Switch indicators (toggle handles around x=1060 based on screenshot)
# 1st Switch around y=485
click(coords=(r.left + 1060, r.top + 485))
time.sleep(2.0)
shoot("02_after_click_1")
print("After 1st:", state_now())

proc.terminate()
print("done")
