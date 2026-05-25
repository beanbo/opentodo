#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Применяет цвета из дизайн-проекта к ThemeManager.qml.

Источники:
  .design-temp/opentodo/project/OpenTodo Redesign.html
  qml/themes/ThemeManager.qml

Что делает:
  • Парсит 5 дизайн-палеток (dark, kale, blueberry, graphite, light) из
    инлайн-объекта `themes` в HTML-канвасе.
  • Для каждой соответствующей QML-палитры (light → classic_light)
    обновляет значения цветов, которые отличаются от дизайна.
  • Если в QML-палитре отсутствуют свойства, которые в дизайне есть
    (titleBarColor/iconColor у kale/blueberry/graphite/classic_light) —
    вставляет новые строки после secondaryTextColor с тем же отступом.
  • НЕ трогает QML-свойства, которых нет в дизайне: cardColor,
    accentForeground, hoverColor, selectionColor, scrimColor,
    warningColor, successColor, infoColor, priorityP*. Дизайн их не
    моделирует, поэтому источника истины для них нет.
  • НЕ трогает палитры без аналога в дизайне: lavender, raspberry,
    sunflower, highcontrast.

Зависимости:
  Импортирует parse_design, _find_matching_brace и константы PROP_MAP /
  THEME_MAP из соседнего check_design_colors.py — чтобы оба скрипта
  всегда использовали одни и те же правила сопоставления.

Запуск:
    python tools/apply_design_colors.py           # dry-run (по умолчанию)
    python tools/apply_design_colors.py --diff    # + unified diff
    python tools/apply_design_colors.py --apply   # реально записать
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

# Берём парсер дизайна и карты сопоставления из соседнего скрипта.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_design_colors import (  # noqa: E402
    DESIGN_HTML,
    PROP_MAP,
    QML_FILE,
    THEME_MAP,
    _find_matching_brace,
    parse_design,
)

# Якорь, после которого вставляем новые свойства. secondaryTextColor —
# самое стабильное (присутствует во всех 9 палитрах QML).
INSERT_AFTER = "secondaryTextColor"

# Ширина поля «имя_свойства:» до значения — копия стиля выравнивания
# из dark-палитры (см. ThemeManager.qml:194-228).
NAME_FIELD = 20


# ─── разбор QML ───────────────────────────────────────────────────────────────

def find_qml_palettes(src: str) -> dict[str, tuple[int, int]]:
    """{palette_name: (body_start, body_end)} — диапазон body без скобок."""
    palettes: dict[str, tuple[int, int]] = {}
    pat = re.compile(r"readonly\s+property\s+QtObject\s+(\w+)\s*:\s*QtObject\s*\{")
    for m in pat.finditer(src):
        name = m.group(1)
        bo = src.index("{", m.start())
        bc = _find_matching_brace(src, bo)
        palettes[name] = (bo + 1, bc)
    return palettes


# ─── применение патчей ───────────────────────────────────────────────────────

def apply_changes(qml_src: str, design: dict) -> tuple[str, list[dict]]:
    """Возвращает (новый_QML, журнал изменений).

    Журнал — список словарей: {theme, action, prop, old, new}.
    action ∈ {'update', 'insert'}.
    """
    palettes = find_qml_palettes(qml_src)
    patches: list[tuple[int, int, str]] = []
    changes: list[dict] = []

    for d_name, d_props in design.items():
        q_name = THEME_MAP.get(d_name)
        if not q_name or q_name not in palettes:
            continue

        body_start, body_end = palettes[q_name]
        body = qml_src[body_start:body_end]

        # Найти якорную строку — оттуда возьмём отступ и точку вставки.
        anchor_re = re.compile(
            r"^([ \t]+)readonly\s+property\s+color\s+"
            + re.escape(INSERT_AFTER)
            + r"\s*:\s*\"#[0-9a-fA-F]+\"[^\n]*\n",
            re.MULTILINE,
        )
        anchor = anchor_re.search(body)
        if anchor is None:
            print(
                f"warning: в палитре {q_name} нет якоря "
                f"`{INSERT_AFTER}`, пропускаю вставки",
                file=sys.stderr,
            )
            indent = "        "
            insert_pos_abs: int | None = None
        else:
            indent = anchor.group(1)
            insert_pos_abs = body_start + anchor.end()

        insertions: list[tuple[str, str]] = []

        for d_prop, d_val in d_props.items():
            q_prop = PROP_MAP.get(d_prop)
            if q_prop is None:
                continue  # 'font' и т.п. — не цвет

            existing_re = re.compile(
                r"readonly\s+property\s+color\s+"
                + re.escape(q_prop)
                + r"\s*:\s*\"(#[0-9a-fA-F]+)\""
            )
            em = existing_re.search(body)
            if em is not None:
                old = em.group(1).lower()
                if old != d_val:
                    # Заменяем только содержимое кавычек, кавычки тоже.
                    quote_start = body_start + em.start(1) - 1
                    quote_end = body_start + em.end(1) + 1
                    patches.append((quote_start, quote_end, f'"{d_val}"'))
                    changes.append({
                        "theme":  q_name,
                        "action": "update",
                        "prop":   q_prop,
                        "old":    old,
                        "new":    d_val,
                    })
                continue

            # Свойства нет — добавим вставкой ниже.
            if insert_pos_abs is None:
                continue
            insertions.append((q_prop, d_val))
            changes.append({
                "theme":  q_name,
                "action": "insert",
                "prop":   q_prop,
                "old":    None,
                "new":    d_val,
            })

        if insertions and insert_pos_abs is not None:
            block = "".join(_render_color_line(indent, n, v) for n, v in insertions)
            patches.append((insert_pos_abs, insert_pos_abs, block))

    # Применяем патчи в обратном порядке, чтобы смещения не сбивались.
    patches.sort(key=lambda p: p[0], reverse=True)
    new_src = qml_src
    for start, end, repl in patches:
        new_src = new_src[:start] + repl + new_src[end:]

    return new_src, changes


def _render_color_line(indent: str, name: str, value: str) -> str:
    """Один аккуратно выровненный `readonly property color X: "#..."`."""
    pad = " " * max(1, NAME_FIELD - len(name) - 1)
    return f'{indent}readonly property color {name}:{pad}"{value}"\n'


# ─── CLI ──────────────────────────────────────────────────────────────────────

def _setup_utf8() -> None:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8")
            except Exception:
                pass


def _print_changes(changes: list[dict]) -> None:
    by_theme: dict[str, list[dict]] = {}
    for ch in changes:
        by_theme.setdefault(ch["theme"], []).append(ch)

    print(f"Изменений: {len(changes)} в {len(by_theme)} палитрах")
    print()
    for theme, items in by_theme.items():
        print(f"━━━ {theme} ━━━")
        for ch in items:
            if ch["action"] == "update":
                print(f"  ~ {ch['prop']:<22} {ch['old']} → {ch['new']}")
            else:
                print(f"  + {ch['prop']:<22} {ch['new']}  (новое свойство)")
        print()


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Применяет цвета из дизайн-проекта к ThemeManager.qml",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--apply",  action="store_true",
                    help="реально записать изменения в QML")
    ap.add_argument("--diff",   action="store_true",
                    help="показать unified diff")
    ap.add_argument("--design", type=Path, default=DESIGN_HTML)
    ap.add_argument("--qml",    type=Path, default=QML_FILE)
    args = ap.parse_args()

    _setup_utf8()

    if not args.design.exists():
        sys.exit(f"Файл дизайна не найден: {args.design}")
    if not args.qml.exists():
        sys.exit(f"QML-файл не найден: {args.qml}")

    design  = parse_design(args.design)
    qml_src = args.qml.read_text(encoding="utf-8")
    new_src, changes = apply_changes(qml_src, design)

    if not changes:
        print("Дизайн и приложение уже совпадают — менять нечего.")
        return

    _print_changes(changes)

    if args.diff:
        diff = difflib.unified_diff(
            qml_src.splitlines(keepends=True),
            new_src.splitlines(keepends=True),
            fromfile=f"{args.qml.name} (current)",
            tofile=f"{args.qml.name} (after apply)",
        )
        sys.stdout.writelines(diff)
        print()

    if args.apply:
        args.qml.write_text(new_src, encoding="utf-8", newline="\n")
        print(f"Записано в {args.qml}")
    else:
        print("Это dry-run. Чтобы записать изменения, запустите с --apply.")


if __name__ == "__main__":
    main()
