#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Извлекает дизайн-токены (размеры, отступы, шрифты) из jsx-файлов
дизайн-проекта и печатает по компонентам.

Источники:
  .design-temp/opentodo/project/app-shell.jsx       — WindowChrome, Sidebar, ViewHeader
  .design-temp/opentodo/project/view-project.jsx    — ProjectView, TaskRow, SectionHeader, …
  .design-temp/opentodo/project/view-others.jsx     — Card, ProductivityView, SettingsView, …

Назначение: служит living-spec'ом для правок в QML. Не модифицирует никаких
файлов, только печатает таблицу токенов. По числам из этого отчёта
правится QML вручную (или последующим скриптом apply_design_sizes.py).

Запуск:
    python tools/extract_design_sizes.py            # человеко-читаемый отчёт
    python tools/extract_design_sizes.py --json     # машинно-читаемый JSON
    python tools/extract_design_sizes.py --component TaskRow   # один компонент
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN_DIR = ROOT / ".design-temp" / "opentodo" / "project"

# (имя_компонента, файл, regex_начала). Конец компонента ищем балансировкой
# фигурных скобок от позиции первого '{' после старта или закрытия arrow-fn.
COMPONENTS: list[tuple[str, str]] = [
    ("WindowChrome",        "app-shell.jsx"),
    ("Sidebar",             "app-shell.jsx"),
    ("ViewHeader",          "app-shell.jsx"),
    ("PillIconBtn",         "view-project.jsx"),
    ("PriorityCheckCircle", "view-project.jsx"),
    ("TaskChip",            "view-project.jsx"),
    ("TaskRow",             "view-project.jsx"),
    ("SectionHeader",       "view-project.jsx"),
    ("ProjectView",         "view-project.jsx"),
    ("Card",                "view-others.jsx"),
    ("InboxView",           "view-others.jsx"),
    ("ProductivityView",    "view-others.jsx"),
    ("NotificationsView",   "view-others.jsx"),
    ("AssistantView",       "view-others.jsx"),
    ("SettingsView",        "view-others.jsx"),
    ("BoardView",           "view-others.jsx"),
]

# Свойства, которые имеют смысл для геометрии/типографики.
NUMERIC_PROPS = {
    "width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight",
    "padding", "paddingTop", "paddingBottom", "paddingLeft", "paddingRight",
    "margin", "marginTop", "marginBottom", "marginLeft", "marginRight",
    "gap", "rowGap", "columnGap",
    "borderRadius", "borderWidth",
    "fontSize", "fontWeight", "letterSpacing", "lineHeight",
    "top", "left", "right", "bottom",
}


# ─── парсер ───────────────────────────────────────────────────────────────────

def _slice_component(src: str, comp_name: str) -> str | None:
    """Возвращает тело определения `const <comp_name> = ... ;`, включая
    выражение arrow-функции. Поиск конца — балансировка ((), {}, [])."""
    pat = re.compile(r"\bconst\s+" + re.escape(comp_name) + r"\s*=\s*")
    m = pat.search(src)
    if not m:
        return None
    i = m.end()
    depth = 0
    started = False
    while i < len(src):
        c = src[i]
        if c in "({[":
            depth += 1
            started = True
        elif c in ")}]":
            depth -= 1
        elif c == ";" and started and depth == 0:
            return src[m.start():i + 1]
        i += 1
    return src[m.start():]


def _extract_props(body: str) -> dict[str, list[str]]:
    """Достаёт пары prop:value для NUMERIC_PROPS внутри inline-стилей.

    Захватывает обе формы:
      строки  — `padding: '12px 16px'`
      числа   — `borderRadius: 6`
      выражения с шаблонами строк не разбираем — пропускаем.
    """
    props: dict[str, list[str]] = {p: [] for p in NUMERIC_PROPS}

    str_re = re.compile(
        r"\b(" + "|".join(NUMERIC_PROPS) + r")\s*:\s*(['\"])([^'\"]+)\2"
    )
    num_re = re.compile(
        r"\b(" + "|".join(NUMERIC_PROPS) + r")\s*:\s*(-?\d+(?:\.\d+)?)\b"
    )

    for m in str_re.finditer(body):
        props[m.group(1)].append(f"'{m.group(3)}'")
    for m in num_re.finditer(body):
        props[m.group(1)].append(m.group(2))

    return {k: sorted(set(v), key=_sort_key) for k, v in props.items() if v}


def _sort_key(s: str):
    if s.startswith("'"):
        return (1, s)
    try:
        return (0, float(s))
    except ValueError:
        return (2, s)


def _extract_icon_sizes(body: str) -> list[str]:
    """Размеры иконок: <SomeIcon size={N} ... /> или size={var ?? N}."""
    sizes: set[int] = set()
    for m in re.finditer(r"<[A-Z]\w*Icon\b[^/>]*\bsize=\{(\d+)\}", body):
        sizes.add(int(m.group(1)))
    for m in re.finditer(r"<OpenTodoMark\b[^/>]*\bsize=\{(\d+)\}", body):
        sizes.add(int(m.group(1)))
    return [str(s) for s in sorted(sizes)]


def extract() -> dict:
    """Все компоненты → {prop: [values]}."""
    out: dict[str, dict] = {}
    cache: dict[str, str] = {}
    for comp, fname in COMPONENTS:
        if fname not in cache:
            cache[fname] = (DESIGN_DIR / fname).read_text(encoding="utf-8")
        body = _slice_component(cache[fname], comp)
        if body is None:
            out[comp] = {"_source": fname, "_missing": True}
            continue
        props = _extract_props(body)
        icons = _extract_icon_sizes(body)
        if icons:
            props["icon.size"] = icons
        props["_source"] = fname
        out[comp] = props
    return out


# ─── вывод ────────────────────────────────────────────────────────────────────

def print_human(data: dict, only: str | None = None) -> None:
    for comp, props in data.items():
        if only and comp != only:
            continue
        src = props.get("_source", "?")
        if props.get("_missing"):
            print(f"━━━ {comp}  [НЕ НАЙДЕН в {src}]")
            print()
            continue
        print(f"━━━ {comp}  [из {src}]")
        ordered_keys = sorted(k for k in props.keys() if not k.startswith("_"))
        if not ordered_keys:
            print("  (значений не найдено)")
        for k in ordered_keys:
            vals = ", ".join(props[k])
            print(f"  {k:<16}{vals}")
        print()


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Извлекает дизайн-токены из jsx и печатает по компонентам",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--json", action="store_true", help="JSON-вывод")
    ap.add_argument("--component", "-c",
                    help="напечатать только один компонент (имя как в COMPONENTS)")
    args = ap.parse_args()

    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8")
            except Exception:
                pass

    data = extract()

    if args.json:
        json.dump(data, sys.stdout, indent=2, ensure_ascii=False)
        sys.stdout.write("\n")
    else:
        print_human(data, only=args.component)


if __name__ == "__main__":
    main()
