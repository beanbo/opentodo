"""Manual verification: launch OpenTodo, click QuickAdd, then click the
priority and date picker buttons. Screenshot each state so we can see
whether the popups render correctly."""
from __future__ import annotations

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
print(f"window: {r}")


def shoot(name):
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
                   all_screens=True).save(OUT / f"_pkr_{name}.png")


# Click QuickAdd
click(coords=(r.left + 540, r.top + 120))
time.sleep(0.6)
send_keys("Test", with_spaces=True)
time.sleep(0.4)
shoot("01_typed")

# DATE button — try different positions
print("trying date button at various positions")
for x in [332, 335, 338, 340, 345, 350]:
    print(f"  click ({x}, 168)")
    click(coords=(r.left + x, r.top + 168))
    time.sleep(0.6)
    shoot(f"02_date_x{x}")
    # don't ESC — just keep clicking and see what shows

proc.terminate()
print("done")
