"""Open the Settings page and screenshot the layout."""
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


def shoot(name):
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom),
                   all_screens=True).save(OUT / f"_settings_{name}.png")


shoot("01_inbox")

# Click Настройки in the sidebar — use a robust click on the bottom-most
# sidebar item. With no projects/filters, layout positions Настройки
# at approximately y=378 in 1296×839 window.
click(coords=(r.left + 100, r.top + 378))
time.sleep(1.0)
shoot("02_settings_top")

# Scroll down to see lower sections
from pywinauto.mouse import scroll
scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
time.sleep(0.5)
shoot("03_settings_scrolled")

scroll(coords=(r.left + 700, r.top + 400), wheel_dist=-3)
time.sleep(0.5)
shoot("04_settings_scrolled_more")

proc.terminate()
print("done")
