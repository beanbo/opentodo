#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Сверка цветов из дизайн-проекта с цветами в ThemeManager.qml.

Источники:
  .design-temp/opentodo/project/OpenTodo Redesign.html
      Инлайн `const themes = { dark: {...}, kale: {...}, ... }` в <script>.
      В дизайне всего 5 палеток: dark, kale, blueberry, graphite, light.

  qml/themes/ThemeManager.qml
      Каждая палитра — это `readonly property QtObject <name>: QtObject { ... }`
      с набором `readonly property color X: "#rrggbb"` (либо Qt.rgba(...) —
      для selection/scrim, в дизайне эти свойства не моделируются).

Имена свойств в дизайне и в QML различаются — карта в PROP_MAP.
Имена тем тоже частично различаются (design "light" ↔ QML "classic_light") —
карта в THEME_MAP.

Запуск:
    python tools/check_design_colors.py
    python tools/check_design_colors.py --json    # машинно-читаемый вывод
    python tools/check_design_colors.py --strict  # код 1 при любом расхождении
    python tools/check_design_colors.py --no-color
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN_HTML = ROOT / ".design-temp" / "opentodo" / "project" / "OpenTodo Redesign.html"
QML_FILE    = ROOT / "qml" / "themes" / "ThemeManager.qml"

# Имя свойства в дизайн-палитре  →  имя свойства в QML-палитре.
PROP_MAP: dict[str, str] = {
    "bg":            "backgroundColor",
    "surface":       "surfaceColor",
    "sidebar":       "sidebarColor",
    "sidebarActive": "sidebarActiveColor",
    "titleBar":      "titleBarColor",
    "border":        "borderColor",
    "text":          "textColor",
    "sidebarText":   "sidebarTextColor",
    "subText":       "secondaryTextColor",
    "iconColor":     "iconColor",
    "accent":        "accentColor",
    "errorBadge":    "errorColor",
}

# Имя темы в дизайне  →  имя темы в QML.
THEME_MAP: dict[str, str] = {
    "dark":      "dark",
    "kale":      "kale",
    "blueberry": "blueberry",
    "graphite":  "graphite",
    "light":     "classic_light",
}


# ─── парсеры ──────────────────────────────────────────────────────────────────

def _find_matching_brace(src: str, open_idx: int) -> int:
    """src[open_idx] обязан быть '{'; вернуть индекс парного '}'."""
    assert src[open_idx] == "{"
    depth = 0
    for i in range(open_idx, len(src)):
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError(f"Несбалансированные скобки от позиции {open_idx}")


def parse_design(path: Path) -> dict[str, dict[str, str]]:
    """{ theme_name: { design_prop: '#rrggbb' } }"""
    src = path.read_text(encoding="utf-8")
    m = re.search(r"\bconst\s+themes\s*=\s*\{", src)
    if not m:
        raise ValueError("Не нашёл 'const themes = {' в дизайн-HTML")
    block_open = src.index("{", m.start())
    block_close = _find_matching_brace(src, block_open)
    body = src[block_open + 1 : block_close]

    themes: dict[str, dict[str, str]] = {}
    i = 0
    while i < len(body):
        nm = re.match(r"\s*(\w+)\s*:\s*\{", body[i:])
        if not nm:
            i += 1
            continue
        theme_name = nm.group(1)
        # Позиция '{' этой темы в `body`:
        rel_open = i + nm.end() - 1
        rel_close = _find_matching_brace(body, rel_open)
        inner = body[rel_open + 1 : rel_close]

        props: dict[str, str] = {}
        for pm in re.finditer(r"(\w+)\s*:\s*'(#[0-9a-fA-F]{3,8})'", inner):
            props[pm.group(1)] = pm.group(2).lower()
        themes[theme_name] = props
        i = rel_close + 1

    return themes


def _qt_rgba_to_hex(expr: str) -> str:
    """Qt.rgba(0.5, 0.5, 0.5, 0.2)  →  '#80808033'."""
    m = re.match(r"Qt\.rgba\(([^)]+)\)", expr)
    nums = [float(x.strip()) for x in m.group(1).split(",")]
    while len(nums) < 4:
        nums.append(1.0)
    r, g, b, a = nums
    return "#{:02x}{:02x}{:02x}{:02x}".format(
        round(r * 255), round(g * 255), round(b * 255), round(a * 255)
    )


def parse_qml(path: Path) -> dict[str, dict[str, str]]:
    """{ theme_name: { qml_prop: '#rrggbb' or '#rrggbbaa' } }

    Игнорирует bool-свойства (isDark) и нелитеральные expression-биндинги
    на корневом уровне ThemeManager (там цвета — выражения, а не строки).
    """
    src = path.read_text(encoding="utf-8")
    themes: dict[str, dict[str, str]] = {}

    palette_re = re.compile(
        r"readonly\s+property\s+QtObject\s+(\w+)\s*:\s*QtObject\s*\{"
    )
    color_re = re.compile(
        r"readonly\s+property\s+color\s+(\w+)\s*:\s*"
        r"(\"#[0-9a-fA-F]{3,8}\"|Qt\.rgba\([^)]+\))"
    )

    for m in palette_re.finditer(src):
        theme_name = m.group(1)
        brace_open = src.index("{", m.start())
        brace_close = _find_matching_brace(src, brace_open)
        body = src[brace_open + 1 : brace_close]

        props: dict[str, str] = {}
        for pm in color_re.finditer(body):
            raw = pm.group(2)
            if raw.startswith('"'):
                props[pm.group(1)] = raw.strip('"').lower()
            else:
                props[pm.group(1)] = _qt_rgba_to_hex(raw)
        themes[theme_name] = props

    return themes


# ─── сравнение ────────────────────────────────────────────────────────────────

def compare(design: dict, qml: dict) -> dict:
    report = {
        "themes": {},
        "qml_only_themes": [],
        "design_only_themes": [],
    }
    qml_seen: set[str] = set()

    for d_name, d_props in design.items():
        q_name = THEME_MAP.get(d_name)
        if not q_name or q_name not in qml:
            report["design_only_themes"].append(d_name)
            continue
        qml_seen.add(q_name)
        q_props = qml[q_name]

        matches, mismatches, missing_in_qml = [], [], []
        for d_prop, d_val in d_props.items():
            q_prop = PROP_MAP.get(d_prop)
            if q_prop is None:
                continue  # 'font' и тому подобное — не цвет, пропускаем
            if q_prop not in q_props:
                missing_in_qml.append((d_prop, q_prop, d_val))
                continue
            q_val = q_props[q_prop]
            if d_val == q_val:
                matches.append((d_prop, q_prop, d_val))
            else:
                mismatches.append((d_prop, q_prop, d_val, q_val))

        modelled = {PROP_MAP[d] for d in d_props if d in PROP_MAP}
        extra_in_qml = [(p, q_props[p]) for p in q_props if p not in modelled]

        report["themes"][f"{d_name} → {q_name}"] = {
            "matches":         matches,
            "mismatches":      mismatches,
            "missing_in_qml":  missing_in_qml,
            "extra_in_qml":    extra_in_qml,
        }

    for q_name in qml:
        if q_name not in qml_seen:
            report["qml_only_themes"].append(q_name)

    return report


# ─── вывод ────────────────────────────────────────────────────────────────────

class _Ansi:
    GREEN = RED = YELLOW = BOLD = DIM = RESET = ""

    @classmethod
    def enable(cls) -> None:
        cls.GREEN, cls.RED, cls.YELLOW = "\033[32m", "\033[31m", "\033[33m"
        cls.BOLD, cls.DIM, cls.RESET = "\033[1m", "\033[2m", "\033[0m"


def _setup_colors(force_disable: bool) -> None:
    if force_disable or os.environ.get("NO_COLOR") or not sys.stdout.isatty():
        return
    if sys.platform == "win32":
        # Включаем VT processing в современной cmd/PowerShell.
        os.system("")
    _Ansi.enable()


def print_human(report: dict) -> int:
    A = _Ansi
    print(f"{A.BOLD}Сверка цветов дизайна и приложения{A.RESET}")
    print()

    if report["design_only_themes"]:
        print(
            f"{A.YELLOW}В дизайне есть тема без пары в QML:{A.RESET} "
            + ", ".join(report["design_only_themes"])
        )
        print()

    if report["qml_only_themes"]:
        print(
            f"{A.DIM}Темы только в QML (нет в дизайне):{A.RESET} "
            + ", ".join(report["qml_only_themes"])
        )
        print()

    total_match = total_miss = total_missing = 0
    for pair, t in report["themes"].items():
        print(f"{A.BOLD}━━━ {pair} ━━━{A.RESET}")
        for d_prop, q_prop, val in t["matches"]:
            print(f"  {A.GREEN}✓{A.RESET} {d_prop:<14} ↔ {q_prop:<22} {val}")
        for d_prop, q_prop, d_val, q_val in t["mismatches"]:
            print(
                f"  {A.RED}✗{A.RESET} {d_prop:<14} ↔ {q_prop:<22} "
                f"design={d_val}  qml={q_val}"
            )
        for d_prop, q_prop, d_val in t["missing_in_qml"]:
            print(
                f"  {A.YELLOW}?{A.RESET} {d_prop:<14} ↔ {q_prop:<22} "
                f"нет в QML-палитре (design={d_val})"
            )
        total_match   += len(t["matches"])
        total_miss    += len(t["mismatches"])
        total_missing += len(t["missing_in_qml"])

        if t["extra_in_qml"]:
            extras = ", ".join(name for name, _ in t["extra_in_qml"])
            print(
                f"  {A.DIM}…{A.RESET} в QML есть ещё {len(t['extra_in_qml'])} "
                f"цветов, которых нет в дизайне: {extras}"
            )
        print()

    print(
        f"{A.BOLD}Итого:{A.RESET} "
        f"{A.GREEN}{total_match} совпадений{A.RESET}, "
        f"{A.RED}{total_miss} расхождений{A.RESET}, "
        f"{A.YELLOW}{total_missing} свойств без пары в QML{A.RESET}"
    )
    return total_miss


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="Сверка цветов дизайн-проекта и ThemeManager.qml",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--json",     action="store_true", help="вывести JSON")
    ap.add_argument("--strict",   action="store_true", help="exit 1 при расхождениях")
    ap.add_argument("--no-color", action="store_true", help="отключить ANSI-цвета")
    ap.add_argument("--design",   type=Path, default=DESIGN_HTML)
    ap.add_argument("--qml",      type=Path, default=QML_FILE)
    args = ap.parse_args()

    if not args.design.exists():
        sys.exit(f"Файл дизайна не найден: {args.design}")
    if not args.qml.exists():
        sys.exit(f"QML-файл не найден: {args.qml}")

    # На Windows консоль по умолчанию в cp1251 — переключаем stdout/stderr
    # в UTF-8, чтобы кириллица и рамки печатались без UnicodeEncodeError.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8")
            except Exception:
                pass

    _setup_colors(force_disable=args.no_color or args.json)

    design = parse_design(args.design)
    qml    = parse_qml(args.qml)
    report = compare(design, qml)

    if args.json:
        json.dump(report, sys.stdout, indent=2, ensure_ascii=False)
        sys.stdout.write("\n")
        mismatches = sum(len(t["mismatches"]) for t in report["themes"].values())
    else:
        mismatches = print_human(report)

    if args.strict and mismatches > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
