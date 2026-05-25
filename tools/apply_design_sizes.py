#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Применяет к QML «верхнеуровневые» размеры из дизайн-проекта.

Что меняет (явная таблица — никакого парсера, только regex-замены по
уникальным якорям):

  • sidebarWidth         280 → 220   (DesktopLayout.qml)
  • topBar height         56 →  48   (DesktopLayout.qml — внутри id: topBar)
  • taskItemHeight        40 →  36   (DesktopLayout.qml)
  • viewTitleLabel        FontSize.title (18) → FontSize.subtitleSmall (15)
    fontWeight Bold     →   Font.Medium                     (DesktopLayout.qml)

Эта подборка — то, что бросается в глаза при первом сравнении со
скриншотом дизайна. Дополнительные правки в Sidebar/TaskItem/SectionHeader
делаются точечно после визуальной проверки.

Запуск:
    python tools/apply_design_sizes.py            # check (по умолчанию)
    python tools/apply_design_sizes.py --apply    # реально записать
    python tools/apply_design_sizes.py --diff     # + unified diff
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PATCHES: list[dict] = [
    {
        "file":  "qml/layouts/DesktopLayout.qml",
        "note":  "sidebarWidth 280 → 220 (Sidebar.width в дизайне)",
        "old":   "readonly property int sidebarWidth:      280",
        "new":   "readonly property int sidebarWidth:      220",
    },
    {
        "file":  "qml/layouts/DesktopLayout.qml",
        "note":  "taskItemHeight 40 → 36 (TaskRow.min-height = 36)",
        "old":   "readonly property int taskItemHeight:    40",
        "new":   "readonly property int taskItemHeight:    36",
    },
    {
        "file":  "qml/layouts/DesktopLayout.qml",
        "note":  "topBar height 56 → 48 (ViewHeader.height = 48)",
        "old":   (
            "                    id: topBar\n"
            "                    objectName: \"topBar\"\n"
            "                    Layout.fillWidth: true\n"
            "                    Layout.preferredHeight: 56"
        ),
        "new":   (
            "                    id: topBar\n"
            "                    objectName: \"topBar\"\n"
            "                    Layout.fillWidth: true\n"
            "                    Layout.preferredHeight: 48"
        ),
    },
    {
        "file":  "qml/layouts/DesktopLayout.qml",
        "note":  "viewTitleLabel: fontSize 18 → 15, weight Bold → Medium (дизайн ViewHeader title)",
        "old":   (
            "                            font.pixelSize: FontSize.title\n"
            "                            font.weight: Font.DemiBold\n"
            "                            color: textPrimary"
        ),
        "new":   (
            "                            font.pixelSize: FontSize.subtitleSmall\n"
            "                            font.weight: Font.Medium\n"
            "                            color: textPrimary"
        ),
    },
    # ── Sidebar (SidebarContent.qml) ────────────────────────────────────────
    {
        "file":  "qml/components/SidebarContent.qml",
        "note":  "SidebarContent default width 280 → 220 (Sidebar.width в дизайне)",
        "old":   "    width: 280\n    color: bgColor",
        "new":   "    width: 220\n    color: bgColor",
    },
    {
        "file":  "qml/components/SidebarContent.qml",
        "note":  "Section label: fontSize caption(11)→tiny(10), letterSpacing 0.5→0.8 (дизайн)",
        "old":   (
            "            font.pixelSize: FontSize.caption\n"
            "            font.weight: Font.DemiBold\n"
            "            font.letterSpacing: 0.5"
        ),
        "new":   (
            "            font.pixelSize: FontSize.tiny\n"
            "            font.weight: Font.DemiBold\n"
            "            font.letterSpacing: 0.8"
        ),
    },
    {
        "file":  "qml/components/SidebarContent.qml",
        "note":  "SidebarItem label: fontSize body(14)→bodySmall(13) (дизайн nav-item fontSize 13)",
        "old":   (
            "            text: sidebarItem.label\n"
            "            font.pixelSize: FontSize.body\n"
            "            elide: Text.ElideRight"
        ),
        "new":   (
            "            text: sidebarItem.label\n"
            "            font.pixelSize: FontSize.bodySmall\n"
            "            elide: Text.ElideRight"
        ),
    },
    # ── SectionHeader (SectionHeader.qml) ───────────────────────────────────
    {
        "file":  "qml/components/SectionHeader.qml",
        "note":  "SectionHeader margins 8 → 16 (дизайн padding 16 по бокам)",
        "old":   (
            "        anchors.leftMargin: 8\n"
            "        anchors.rightMargin: 8\n"
            "        spacing: 6"
        ),
        "new":   (
            "        anchors.leftMargin: 16\n"
            "        anchors.rightMargin: 16\n"
            "        spacing: 6"
        ),
    },
    {
        "file":  "qml/components/SectionHeader.qml",
        "note":  "SectionHeader chevron size 14 → 12 (дизайн)",
        "old":   (
            "            name: \"chevron-right\"\n"
            "            size: 14\n"
            "            color: root.isDarkTheme ? \"#cccccc\" : \"#333333\""
        ),
        "new":   (
            "            name: \"chevron-right\"\n"
            "            size: 12\n"
            "            color: root.isDarkTheme ? \"#cccccc\" : \"#333333\""
        ),
    },
    {
        "file":  "qml/components/SectionHeader.qml",
        "note":  "SectionHeader title: fontSize 14→12, Bold→Medium (дизайн)",
        "old":   (
            "            text: root.title\n"
            "            font.pixelSize: 14\n"
            "            font.bold: true"
        ),
        "new":   (
            "            text: root.title\n"
            "            font.pixelSize: FontSize.small\n"
            "            font.weight: Font.Medium"
        ),
    },
    {
        "file":  "qml/components/SectionHeader.qml",
        "note":  "SectionHeader count text: fontSize 13 → tiny(10) (дизайн)",
        "old":   (
            "            text: root.taskCount > 0 ? \"(\" + root.taskCount + \")\" : \"\"\n"
            "            font.pixelSize: 13"
        ),
        "new":   (
            "            text: root.taskCount > 0 ? \"(\" + root.taskCount + \")\" : \"\"\n"
            "            font.pixelSize: FontSize.tiny"
        ),
    },
    {
        "file":  "qml/components/SectionHeader.qml",
        "note":  "SectionHeader plus button size 16 → 14 (дизайн)",
        "old":   (
            "            id: addButton\n"
            "            name: \"plus\"\n"
            "            size: 16"
        ),
        "new":   (
            "            id: addButton\n"
            "            name: \"plus\"\n"
            "            size: 14"
        ),
    },
    # ── TaskItem (TaskItem.qml) ──────────────────────────────────────────────
    {
        "file":  "qml/components/TaskItem.qml",
        "note":  "TaskItem _rowMinHeight comfortable 44 → 36 (TaskRow.minHeight в дизайне)",
        "old":   "readonly property int _rowMinHeight: _compact ? 32 : 44",
        "new":   "readonly property int _rowMinHeight: _compact ? 32 : 36",
    },
    {
        "file":  "qml/components/TaskItem.qml",
        "note":  "TaskItem _rowVPadding comfortable 4(xs) → 8(sm) (дизайн TaskRow padding 8 vert)",
        "old":   "readonly property int _rowVPadding:  _compact ? 2  : Spacing.xs",
        "new":   "readonly property int _rowVPadding:  _compact ? 2  : Spacing.sm",
    },
    {
        "file":  "qml/components/TaskItem.qml",
        "note":  "TaskItem content label: body(14) → bodySmall(13) (дизайн TaskRow text fontSize 13)",
        "old":   (
            "                        linkColor:        ThemeManager.accentColor\n"
            "                        font.pixelSize:   root.isSubtask ? FontSize.caption : FontSize.body"
        ),
        "new":   (
            "                        linkColor:        ThemeManager.accentColor\n"
            "                        font.pixelSize:   root.isSubtask ? FontSize.caption : FontSize.bodySmall"
        ),
    },
    {
        "file":  "qml/components/TaskItem.qml",
        "note":  "TaskItem inline edit field: body(14) → bodySmall(13) (синхронизация с label)",
        "old":   (
            "                        // Match the read-only Label so swapping is visually quiet.\n"
            "                        font.pixelSize: root.isSubtask ? FontSize.caption : FontSize.body"
        ),
        "new":   (
            "                        // Match the read-only Label so swapping is visually quiet.\n"
            "                        font.pixelSize: root.isSubtask ? FontSize.caption : FontSize.bodySmall"
        ),
    },
    # ── ProjectView header (ProjectView.qml) ────────────────────────────────
    {
        "file":  "qml/views/ProjectView.qml",
        "note":  "ProjectView header: leftMargin md(16)→lg(24), spacing sm(8)→md(16) ближе к дизайну (padding 16 24, gap 12)",
        "old":   (
            "                anchors.leftMargin:  Spacing.md\n"
            "                anchors.rightMargin: Spacing.sm\n"
            "                spacing: Spacing.sm"
        ),
        "new":   (
            "                anchors.leftMargin:  Spacing.lg\n"
            "                anchors.rightMargin: Spacing.sm\n"
            "                spacing: Spacing.md"
        ),
    },
    {
        "file":  "qml/views/ProjectView.qml",
        "note":  "ProjectView project name: font.bold(700) → Font.DemiBold(600) (дизайн title weight 600)",
        "old":   (
            "                    text:            project ? project.name : \"\"\n"
            "                    font.pixelSize:  FontSize.h2\n"
            "                    font.bold:       true"
        ),
        "new":   (
            "                    text:            project ? project.name : \"\"\n"
            "                    font.pixelSize:  FontSize.h2\n"
            "                    font.weight:     Font.DemiBold"
        ),
    },
    {
        "file":  "qml/views/ProjectView.qml",
        "note":  "ProjectView project icon container 24×24 → 22×22 (дизайн icon.size 22)",
        "old":   (
            "                Item {\n"
            "                    width: 24\n"
            "                    height: 24\n"
            "                    Layout.alignment: Qt.AlignVCenter"
        ),
        "new":   (
            "                Item {\n"
            "                    width: 22\n"
            "                    height: 22\n"
            "                    Layout.alignment: Qt.AlignVCenter"
        ),
    },
    # ── SettingsView page-header (SettingsView.qml) ─────────────────────────
    {
        "file":  "qml/views/SettingsView.qml",
        "note":  "SettingsView title: 28/Bold → 24/DemiBold (дизайн fontSize 24 weight 600)",
        "old":   (
            "                text: qsTr(\"Настройки\")\n"
            "                font.pixelSize: 28\n"
            "                font.bold: true"
        ),
        "new":   (
            "                text: qsTr(\"Настройки\")\n"
            "                font.pixelSize: 24\n"
            "                font.weight: Font.DemiBold"
        ),
    },
    # ── NotificationsView page-header (NotificationsView.qml) ───────────────
    {
        "file":  "qml/views/NotificationsView.qml",
        "note":  "NotificationsView title: h1(28) → h2(22) (дизайн fontSize 20)",
        "old":   (
            "                    text: qsTr(\"Уведомления\")\n"
            "                    font.pixelSize: FontSize.h1\n"
            "                    font.weight: Font.DemiBold"
        ),
        "new":   (
            "                    text: qsTr(\"Уведомления\")\n"
            "                    font.pixelSize: FontSize.h2\n"
            "                    font.weight: Font.DemiBold"
        ),
    },
    # ── AIView page-header (AIView.qml) ─────────────────────────────────────
    {
        "file":  "qml/views/AIView.qml",
        "note":  "AIView title: h1(28) → h2(22) (дизайн fontSize 20)",
        "old":   (
            "                text: qsTr(\"Помощник\")\n"
            "                font.pixelSize: FontSize.h1\n"
            "                font.weight: Font.DemiBold"
        ),
        "new":   (
            "                text: qsTr(\"Помощник\")\n"
            "                font.pixelSize: FontSize.h2\n"
            "                font.weight: Font.DemiBold"
        ),
    },
    {
        "file":  "qml/views/AIView.qml",
        "note":  "AIView sparkles icon: 20 → 22 (дизайн icon.size 22 в view-header)",
        "old":   (
            "            IconSvg {\n"
            "                name: \"sparkles\"\n"
            "                size: 20\n"
            "                color: ThemeManager.accentColor\n"
            "                Layout.alignment: Qt.AlignVCenter"
        ),
        "new":   (
            "            IconSvg {\n"
            "                name: \"sparkles\"\n"
            "                size: 22\n"
            "                color: ThemeManager.accentColor\n"
            "                Layout.alignment: Qt.AlignVCenter"
        ),
    },
]


def _setup_utf8() -> None:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8")
            except Exception:
                pass


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Применяет верхнеуровневые дизайн-размеры к QML",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--apply", action="store_true",
                    help="реально записать изменения")
    ap.add_argument("--diff",  action="store_true",
                    help="показать unified diff")
    args = ap.parse_args()

    _setup_utf8()

    files: dict[Path, str] = {}
    summary = {"applied": 0, "already": 0, "missing": 0}
    diffs: list[tuple[Path, str, str]] = []

    for p in PATCHES:
        path = ROOT / p["file"]
        if path not in files:
            files[path] = path.read_text(encoding="utf-8")
        src = files[path]

        # ВАЖНО: проверяем new ПЕРВЫМ, без условия про old. Иначе если old
        # является substring of new (например `FontSize.body` ⊂ `FontSize.bodySmall`),
        # повторный запуск находит old внутри уже-применённого new и заменяет
        # снова, превращая `bodySmall` → `bodySmallSmall` → `bodySmallSmallSmall`.
        if p["new"] in src:
            print(f"  = {p['note']}  (уже применено)")
            summary["already"] += 1
            continue
        if p["old"] not in src:
            print(f"  ! {p['note']}  (якорь не найден в {p['file']})")
            summary["missing"] += 1
            continue

        new_src = src.replace(p["old"], p["new"], 1)
        files[path] = new_src
        print(f"  ~ {p['note']}")
        summary["applied"] += 1

    print()
    print(f"Итого: применить {summary['applied']}, "
          f"уже применено {summary['already']}, "
          f"якорей не найдено {summary['missing']}")

    if args.diff:
        print()
        for path, new_src in files.items():
            old_src = path.read_text(encoding="utf-8")
            if old_src == new_src:
                continue
            print(f"--- {path.relative_to(ROOT)} (current)")
            print(f"+++ {path.relative_to(ROOT)} (after apply)")
            sys.stdout.writelines(
                difflib.unified_diff(
                    old_src.splitlines(keepends=True),
                    new_src.splitlines(keepends=True),
                    fromfile="",
                    tofile="",
                    n=2,
                )
            )

    if not args.apply:
        if summary["applied"] > 0:
            print()
            print("Это check-режим. Чтобы записать изменения, запустите с --apply.")
        return

    for path, new_src in files.items():
        old_src = path.read_text(encoding="utf-8")
        if old_src != new_src:
            path.write_text(new_src, encoding="utf-8", newline="\n")
            print(f"Записано: {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
