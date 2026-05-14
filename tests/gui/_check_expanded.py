"""Manually launch OpenTodo, click the QuickAdd, screenshot the expanded
state, then exit. Used to verify the QuickAdd expanded layout visually."""
import os
import subprocess
import sys
import time
from pathlib import Path

from PIL import ImageGrab
from pywinauto import Application
from pywinauto.mouse import click

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
OUT = Path(__file__).parent / "_artifacts"

# Kill any leftover.
subprocess.run(["taskkill", "/F", "/IM", "OpenTodo.exe"],
               capture_output=True, check=False)
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
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(OUT / "_check_collapsed.png")

# Click the QuickAdd.
click_x, click_y = r.left + 540, r.top + 120
click(coords=(click_x, click_y))
time.sleep(0.6)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(OUT / "_check_expanded_immediately.png")

# Type something to trigger preview row.
from pywinauto.keyboard import send_keys
send_keys("Buy milk tomorrow p1", with_spaces=True)
time.sleep(0.6)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(OUT / "_check_expanded_typed.png")

# Esc, then exit.
send_keys("{ESC}")
time.sleep(0.3)
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(OUT / "_check_after_esc.png")

proc.terminate()
print("done")
