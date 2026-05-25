#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Сверка/применение иконок из дизайн-проекта в resources/icons/.

Источники:
  .design-temp/opentodo/project/icons.jsx          → resources/icons/ui/
  .design-temp/opentodo/project/project-icons.jsx  → resources/icons/projects/

Каноничный SVG-шаблон (как у обёртки Icon/_I в jsx):
    <svg xmlns="http://www.w3.org/2000/svg"
         width="24" height="24" viewBox="0 0 24 24"
         fill="none" stroke="currentColor" stroke-width="1.6"
         stroke-linecap="round" stroke-linejoin="round">
      {children}
    </svg>

Имя файла: CamelCase без суффикса "Icon" → kebab-case.
  InboxIcon          → inbox.svg
  ChevronRightIcon   → chevron-right.svg
  GraduationCapIcon  → graduation-cap.svg

Спецслучаи (пропуск):
  CalendarViewIcon — alias на CalendarIcon
  OpenTodoMark     — брендовая, своя структура; в проекте — resources/icons/icon.svg
  ProjectCubeIcon  — дубликат CubeIcon из project-icons.jsx

Декоративные из icons.jsx идут в projects/ (по комментарию в jsx):
  RocketIcon, RunnerIcon, PaletteIcon, TargetIcon, HeartIcon, BookIcon, PrinterIcon

Запуск:
    python tools/sync_design_icons.py             # check (по умолчанию)
    python tools/sync_design_icons.py --apply     # записать SVG-файлы
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN_DIR  = ROOT / ".design-temp" / "opentodo" / "project"
ICONS_JSX   = DESIGN_DIR / "icons.jsx"
PROJECT_JSX = DESIGN_DIR / "project-icons.jsx"

OUT_UI       = ROOT / "resources" / "icons" / "ui"
OUT_PROJECTS = ROOT / "resources" / "icons" / "projects"

# Иконки из icons.jsx, которые по смыслу относятся к проектному пакету —
# они в jsx даже выделены отдельной секцией «Декоративные иконки проектов».
DECORATIVE_PROJECT_ICONS = {
    "RocketIcon", "RunnerIcon", "PaletteIcon", "TargetIcon",
    "HeartIcon", "BookIcon", "PrinterIcon",
}

# Иконки, которые мы намеренно НЕ генерируем как SVG-файлы.
SKIP_ICONS = {
    "CalendarViewIcon",   # alias = CalendarIcon
    "OpenTodoMark",       # бренд, отдельная svg-структура (см. resources/icons/icon.svg)
    "ProjectCubeIcon",    # дубликат CubeIcon из project-icons.jsx
}

# Имена файлов, отличающиеся от автогенерации kebab-case. Источник истины —
# имена, на которые ссылается QML (например ProjectIconPicker.qml вкладывает
# "chart"/"pencil" в свои списки) или общая короткая форма.
NAME_OVERRIDES = {
    "ChartLineIcon":  "chart",
    "PencilEditIcon": "pencil",
    "OfflineDotIcon": "offline",
}

SVG_HEADER = (
    '<svg xmlns="http://www.w3.org/2000/svg" '
    'width="24" height="24" viewBox="0 0 24 24" '
    'fill="none" stroke="currentColor" stroke-width="1.6" '
    'stroke-linecap="round" stroke-linejoin="round">'
)
SVG_FOOTER = "</svg>"


# ─── имена ────────────────────────────────────────────────────────────────────

def camel_to_kebab(name: str) -> str:
    """InboxIcon → inbox, ChevronRightIcon → chevron-right."""
    if name.endswith("Icon"):
        name = name[: -len("Icon")]
    out: list[str] = []
    for i, c in enumerate(name):
        if i > 0 and c.isupper() and not name[i - 1].isupper():
            out.append("-")
        out.append(c.lower())
    return "".join(out)


# ─── парсер jsx ───────────────────────────────────────────────────────────────

# Шаблон «const NameIcon = (p) => ( <Icon|_I {...p}> ... </Icon|_I> );»
ICON_DEF_RE = re.compile(
    r"const\s+(\w+)\s*=\s*\(p\)\s*=>\s*\(\s*"
    r"<(?:Icon|_I)\s*\{\.\.\.p\}\s*>"
    r"(.*?)"
    r"</(?:Icon|_I)>\s*\)\s*;",
    re.DOTALL,
)

# Внутри тела извлекаем самозакрывающиеся теги <path .../>, <circle .../>, <rect .../>.
ELEMENT_RE = re.compile(
    r"<(path|circle|rect)\b([^/>]*)/\s*>",
    re.DOTALL,
)


def _normalize_attrs(raw: str) -> str:
    """JSX-атрибуты → SVG-атрибуты: strokeWidth → stroke-width и т.п.

    Возвращает строку с ровно одним ведущим пробелом, чтобы безопасно
    конкатенироваться с тегом: f"<{tag}{_normalize_attrs(...)}/>".
    """
    raw = re.sub(r"\s+", " ", raw.replace("\n", " ")).strip()
    # camelCase атрибуты → kebab-case (известные).
    for jsx_attr, svg_attr in (
        ("strokeWidth",   "stroke-width"),
        ("strokeLinecap", "stroke-linecap"),
        ("strokeLinejoin", "stroke-linejoin"),
        ("fillRule",      "fill-rule"),
        ("clipRule",      "clip-rule"),
    ):
        raw = raw.replace(jsx_attr, svg_attr)
    # В jsx число без кавычек — `strokeWidth={2.2}`. Привести к строке: stroke-width="2.2".
    raw = re.sub(r"=\{([0-9.]+)\}", r'="\1"', raw)
    return " " + raw if raw else ""


def parse_icons(src: str) -> dict[str, str]:
    """jsx-исходник → {ComponentName: inner_svg_string_canonical}."""
    icons: dict[str, str] = {}
    for m in ICON_DEF_RE.finditer(src):
        name, body = m.group(1), m.group(2)
        children: list[str] = []
        for em in ELEMENT_RE.finditer(body):
            tag, attrs = em.group(1), em.group(2)
            children.append(f"<{tag}{_normalize_attrs(attrs)}/>")
        if not children:
            continue
        icons[name] = "".join(children)
    return icons


def render_svg(inner: str) -> str:
    return SVG_HEADER + inner + SVG_FOOTER + "\n"


# ─── план: что куда записывать ────────────────────────────────────────────────

def plan(icons_jsx_data: dict[str, str],
         project_jsx_data: dict[str, str]) -> list[tuple[str, Path, str]]:
    """Возвращает список (component_name, out_path, svg_text)."""
    plan_list: list[tuple[str, Path, str]] = []

    for name, inner in icons_jsx_data.items():
        if name in SKIP_ICONS:
            continue
        out_dir = OUT_PROJECTS if name in DECORATIVE_PROJECT_ICONS else OUT_UI
        fname = NAME_OVERRIDES.get(name, camel_to_kebab(name))
        path = out_dir / f"{fname}.svg"
        plan_list.append((name, path, render_svg(inner)))

    for name, inner in project_jsx_data.items():
        if name in SKIP_ICONS:
            continue
        fname = NAME_OVERRIDES.get(name, camel_to_kebab(name))
        path = OUT_PROJECTS / f"{fname}.svg"
        plan_list.append((name, path, render_svg(inner)))

    return plan_list


# ─── сверка и запись ──────────────────────────────────────────────────────────

def _read_or_none(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None


def diff_and_apply(plan_list: list[tuple[str, Path, str]],
                   apply: bool) -> dict:
    same: list[Path] = []
    changed: list[tuple[Path, str, str]] = []   # (path, old, new)
    created: list[Path] = []

    for _name, path, new_content in plan_list:
        existing = _read_or_none(path)
        if existing is None:
            created.append(path)
            if apply:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(new_content, encoding="utf-8", newline="\n")
        elif existing.strip() != new_content.strip():
            changed.append((path, existing, new_content))
            if apply:
                path.write_text(new_content, encoding="utf-8", newline="\n")
        else:
            same.append(path)

    return {"same": same, "changed": changed, "created": created}


# ─── CLI ──────────────────────────────────────────────────────────────────────

def _setup_utf8() -> None:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8")
            except Exception:
                pass


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Сверяет/применяет SVG-иконки из дизайн-проекта",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--apply", action="store_true",
                    help="реально записать SVG-файлы (по умолчанию — check)")
    ap.add_argument("--verbose", "-v", action="store_true",
                    help="печатать совпавшие и пропущенные иконки")
    args = ap.parse_args()

    _setup_utf8()

    if not ICONS_JSX.exists():
        sys.exit(f"icons.jsx не найден: {ICONS_JSX}")
    if not PROJECT_JSX.exists():
        sys.exit(f"project-icons.jsx не найден: {PROJECT_JSX}")

    icons_data   = parse_icons(ICONS_JSX.read_text(encoding="utf-8"))
    project_data = parse_icons(PROJECT_JSX.read_text(encoding="utf-8"))

    print(f"В дизайне найдено: icons.jsx — {len(icons_data)} иконок, "
          f"project-icons.jsx — {len(project_data)} иконок")
    print()

    plan_list = plan(icons_data, project_data)
    result = diff_and_apply(plan_list, apply=args.apply)

    if result["created"]:
        print(f"Создать ({len(result['created'])}):")
        for p in result["created"]:
            print(f"  + {p.relative_to(ROOT)}")
        print()

    if result["changed"]:
        print(f"Обновить ({len(result['changed'])}):")
        for p, _old, _new in result["changed"]:
            print(f"  ~ {p.relative_to(ROOT)}")
        print()

    if args.verbose and result["same"]:
        print(f"Без изменений ({len(result['same'])}):")
        for p in result["same"]:
            print(f"  = {p.relative_to(ROOT)}")
        print()

    print(f"Итого: совпадают {len(result['same'])}, "
          f"будут обновлены {len(result['changed'])}, "
          f"будут созданы {len(result['created'])}")

    if not args.apply and (result["changed"] or result["created"]):
        print()
        print("Это check-режим. Чтобы записать SVG-файлы, запустите с --apply.")


if __name__ == "__main__":
    main()
