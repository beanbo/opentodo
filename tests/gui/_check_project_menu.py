"""Visual check: create a project via dataStore directly, then right-click
on it to verify the context menu shows."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from PIL import ImageGrab
from pywinauto import Application
from pywinauto.mouse import click, right_click
from pywinauto.keyboard import send_keys

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "release" / "OpenTodo.exe"
OUT = Path(__file__).parent / "_artifacts"
APPDATA = Path(os.environ["APPDATA"]) / "OpenTodo" / "OpenTodo" / "OpenTodo"

# Pre-seed state.json with a project so we don't have to fight the dialog.
APPDATA.mkdir(parents=True, exist_ok=True)
state_path = APPDATA / "state.json"
state = {
    "settings": {},
    "tasks": [],
    "projects": [
        {
            "id": "inbox-id",
            "name": "Inbox",
            "color": "grey",
            "is_inbox": True,
            "is_archived": False,
            "is_favorite": False,
            "order": 0,
            "view_style": "list",
            "is_deleted": False,
            "created_at": "2026-05-03T00:00:00Z",
            "updated_at": "2026-05-03T00:00:00Z",
        },
        {
            "id": "test-proj-id",
            "name": "Тестовый проект",
            "color": "blue",
            "is_inbox": False,
            "is_archived": False,
            "is_favorite": False,
            "order": 1,
            "view_style": "list",
            "is_deleted": False,
            "created_at": "2026-05-03T00:00:00Z",
            "updated_at": "2026-05-03T00:00:00Z",
        },
    ],
    "sections": [],
    "labels": [],
    "filters": [],
    "comments": [],
}
locallog = APPDATA / "local_log"
if locallog.exists():
    shutil.rmtree(locallog, ignore_errors=True)
state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2),
                      encoding="utf-8")

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
                   all_screens=True).save(OUT / f"_pmenu_{name}.png")


# Initial — sidebar should show Inbox + Тестовый проект.
shoot("01_initial")

# Right-click on the project (~y=205 with one project).
right_click(coords=(r.left + 100, r.top + 205))
time.sleep(0.6)
shoot("02_ctx_menu")

# Esc to close
send_keys("{ESC}")
time.sleep(0.3)

proc.terminate()
print("done")
