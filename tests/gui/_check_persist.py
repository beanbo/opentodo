"""Verify settings are actually persisted to state.json."""
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

# 1) Check state.json was created on first launch
print(f"After launch: state exists? {STATE.exists()}")
if STATE.exists():
    s = json.loads(STATE.read_text(encoding='utf-8'))
    print(f"  settings keys: {list(s.get('settings', {}).keys())[:10]}")

# 2) Add a task via QuickAdd (triggers dataChanged)
r = dlg.rectangle()
click(coords=(r.left + 540, r.top + 120))
time.sleep(0.4)
send_keys("PersistMe", with_spaces=True)
time.sleep(0.2)
send_keys("{ENTER}")
time.sleep(2.0)   # > 1s debounce

# 3) Re-read state.json
print(f"After add task + 2s: state exists? {STATE.exists()}")
if STATE.exists():
    s = json.loads(STATE.read_text(encoding='utf-8'))
    tasks = [t for t in s.get('tasks', []) if not t.get('is_deleted')]
    print(f"  task count: {len(tasks)}")
    for t in tasks:
        print(f"    {t.get('content')!r}")

# 4) Open Settings, scroll to Sidebar section, toggle Switch
click(coords=(r.left + 100, r.top + 378))   # Настройки
time.sleep(0.7)
# Scroll to find Sidebar section
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-10)
time.sleep(0.5)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
               all_screens=True).save(OUT / "_persist_settings.png")

# Look for Sidebar header — click it to expand if collapsed
# Click an arbitrary Switch position
# Then wait > 1s for save
time.sleep(2.0)

# 5) Final state check
print(f"After settings toggle + 2s: state exists? {STATE.exists()}")
if STATE.exists():
    s = json.loads(STATE.read_text(encoding='utf-8'))
    print(f"  settings: {s.get('settings', {})}")

proc.terminate()
print("done")
