"""Capture stdout from OpenTodo while clicking on Switch."""
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

# Launch with stdout/stderr captured
log_path = OUT / "_p5_app.log"
with open(log_path, 'w', encoding='utf-8') as logf:
    proc = subprocess.Popen([str(EXE)], cwd=str(EXE.parent), stdout=logf, stderr=subprocess.STDOUT)
    time.sleep(3.5)

    pwa = Application(backend="win32").connect(process=proc.pid)
    dlg = pwa.window(title_re="OpenTodo")
    dlg.wait("visible", timeout=15)
    dlg.set_focus()
    time.sleep(0.5)
    r = dlg.rectangle()

    # Settings → scroll → expand sidebar
    click(coords=(r.left + 100, r.top + 378))
    time.sleep(0.7)
    for _ in range(8):
        scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
        time.sleep(0.2)
    click(coords=(r.left + 700, r.top + 624))
    time.sleep(0.5)
    scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
    time.sleep(0.4)
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
                   all_screens=True).save(OUT / "_p5_before_click.png")

    # Click on the FIRST Switch — looking at the previous screenshot, Switch
    # was at (1060, 518). Try multiple positions.
    for y in [485, 500, 518, 538, 550, 568, 588]:
        click(coords=(r.left + 1060, r.top + y))
        time.sleep(0.4)
    time.sleep(2.0)
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
                   all_screens=True).save(OUT / "_p5_after_clicks.png")

    proc.terminate()
    time.sleep(1)

print("=== App log ===")
print(Path(log_path).read_text(encoding='utf-8', errors='replace')[-3000:])
print("=== State settings ===")
if STATE.exists():
    print(json.loads(STATE.read_text(encoding='utf-8')).get('settings', {}))
