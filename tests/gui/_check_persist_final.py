"""Verify the aboutToQuit fix: toggle Switch and close BEFORE the 1 s
debounce would have fired. State must still reach disk."""
import json, os, shutil, subprocess, time
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

# Open Settings → scroll → expand sidebar
click(coords=(r.left + 100, r.top + 378))
time.sleep(0.7)
for _ in range(8):
    scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
    time.sleep(0.2)
click(coords=(r.left + 700, r.top + 624))   # expand "Боковая панель"
time.sleep(0.5)
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
time.sleep(0.4)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
               all_screens=True).save(OUT / "_pf_before.png")

# Click ALL three switches to be sure at least one hits.
# Switch positions seen in screenshot: y=518, 570, 622 (Switch indicator x≈1060)
for y in [518, 570, 622]:
    click(coords=(r.left + 1060, r.top + y))
    time.sleep(0.2)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
               all_screens=True).save(OUT / "_pf_after_click.png")
# Wait < 1 s and close gracefully via WM_CLOSE so aboutToQuit fires.
time.sleep(0.3)
import ctypes
WM_CLOSE = 0x0010
ctypes.windll.user32.PostMessageW(dlg.handle, WM_CLOSE, 0, 0)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.terminate()
    proc.wait(timeout=2)
time.sleep(0.5)

# Now read the state — show_upcoming_count should be True
print("\n=== State after immediate-close ===")
if STATE.exists():
    s = json.loads(STATE.read_text(encoding='utf-8')).get('settings', {})
    print(f"show_inbox_count={s.get('show_inbox_count')}")
    print(f"show_today_count={s.get('show_today_count')}")
    print(f"show_upcoming_count={s.get('show_upcoming_count')}")
else:
    print("NO state.json!")
print("done")
