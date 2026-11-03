"""
Generates the synthetic Todoist backup ZIP fixtures for unit tests.
Run once: python generate_fixture.py
Output: Todoist Backup 2024-01-01.zip  (old 12-column export format)
        realformat_backup.zip          (current export format, see the bottom)
"""
import zipfile, io, os

# Todoist CSV export columns:
# TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT

def csv(*fields):
    """Format a CSV row, quoting fields that contain commas or double-quotes."""
    def escape(f):
        f = str(f)
        if ',' in f or '"' in f or '\n' in f:
            return '"' + f.replace('"', '""') + '"'
        return f
    return ','.join(escape(f) for f in fields) + '\n'

HEADER = "TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT\n"

# ── Inbox.csv ────────────────────────────────────────────────────────────────
inbox_csv = HEADER
inbox_csv += csv("task", "Купить молоко", "", 1, 1, "", "", "", "en", "", "", "")
inbox_csv += csv("task", "Задача с приоритетом p1", "Описание задачи", 4, 1, "", "", "2024-06-15", "en", "", "", "")
inbox_csv += csv("task", "Задача с дедлайном", "", 3, 1, "", "", "2024-07-01T09:00:00", "en", "Europe/Moscow", "30", "minute")
inbox_csv += csv("note", "Это комментарий к задаче", "", 1, 1, "", "", "", "", "", "", "")

# ── Work.csv — with sections and subtasks ─────────────────────────────────────
work_csv = HEADER
work_csv += csv("section", "Планирование", "", 1, 1, "", "", "", "", "", "", "")
work_csv += csv("task", "Написать отчёт", "", 2, 1, "", "", "2024-06-20", "ru", "", "", "")
work_csv += csv("task", "Собрать данные", "", 1, 2, "", "", "", "en", "", "", "")
work_csv += csv("task", "Проверить графики", "", 1, 2, "", "", "", "en", "", "", "")
work_csv += csv("task", "Отправить отчёт", "", 1, 3, "", "", "", "en", "", "", "")
work_csv += csv("section", "Встречи", "", 1, 1, "", "", "", "", "", "", "")
work_csv += csv("task", "Встреча с командой", "Обсудить Q3", 3, 1, "", "", "every monday", "en", "", "", "")
work_csv += csv("note", "Ссылка на zoom добавлена", "", 1, 1, "", "", "", "", "", "", "")

# ── Personal.csv — labels, recurring, deep nesting ───────────────────────────
personal_csv = HEADER
personal_csv += csv("task", "Бегать по утрам", "", 1, 1, "", "", "каждый день в 7:00", "ru", "Europe/Moscow", "30", "minute")
personal_csv += csv("task", "Читать книгу", "@reading, @важное", 2, 1, "", "", "every day", "en", "", "15", "minute")
personal_csv += csv("task", "Level 2 subtask", "", 1, 2, "", "", "", "", "", "", "")
personal_csv += csv("task", "Level 3 subtask", "", 1, 3, "", "", "", "", "", "", "")
personal_csv += csv("task", "Level 4 subtask", "", 1, 4, "", "", "", "", "", "", "")
# A task with commas in content (must be quoted)
personal_csv += csv("task", "Task with, comma in content", "Description: line1\nline2", 1, 1, "", "", "", "", "", "", "")
# A task with quotes in content
personal_csv += csv("task", 'Task with "quotes" in content', "", 1, 1, "", "", "", "", "", "", "")
# Completed task (should be skipped by default)
personal_csv += csv("task", "Завершённая задача", "", 1, 1, "", "", "", "", "", "", "")

# ── EmptyProject.csv — edge case: only header ─────────────────────────────────
empty_csv = HEADER

out_path = os.path.join(os.path.dirname(__file__), "Todoist Backup 2024-01-01.zip")

with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("Inbox.csv",         inbox_csv)
    zf.writestr("Work.csv",          work_csv)
    zf.writestr("Personal.csv",      personal_csv)
    zf.writestr("EmptyProject.csv",  empty_csv)

print(f"Written: {out_path}")
print(f"  Inbox: {inbox_csv.count(chr(10))-1} data rows")
print(f"  Work: {work_csv.count(chr(10))-1} data rows")
print(f"  Personal: {personal_csv.count(chr(10))-1} data rows")

# ── realformat_backup.zip — a replica of a current Todoist export ─────────────
# All content is made up, but the shape matches what Todoist produces today:
# one CSV per project named "<name> [<16-char id>].csv" (emoji, Cyrillic, a flag
# emoji, an emoji with a variation selector, a name with no space after the
# emoji), UTF-8 with BOM, CRLF line endings, 15 columns, a leading
# "meta,view_style=list" row, blank separator rows, sections, notes with an
# author and ISO timestamp (one of them multi-line), subtasks at INDENT 2, and
# entries STORED rather than deflated. 15 files: Inbox + 14 projects.

RF_HEADER = ("TYPE,CONTENT,DESCRIPTION,IS_COLLAPSED,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,"
             "DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT,DEADLINE,DEADLINE_LANG")
RF_AUTHOR = "Test User (10000001)"
RF_TZ = "Europe/Berlin"


def rf_row(*fields):
    fields = list(fields) + [""] * (15 - len(fields))
    return csv(*fields).rstrip("\n")


def rf_task(content, indent=1, description=""):
    return rf_row("task", content, description, "", 4, indent, RF_AUTHOR, "", "", "", RF_TZ)


def rf_section(name):
    return rf_row("section", name, "", "False")


def rf_note(text, ts="2026-01-15T10:30:00.000000Z"):
    return rf_row("note", text, "", "", "", "", RF_AUTHOR, "", ts)


RF_BLANK = rf_row()

# (project name, Todoist id, rows)
RF_PROJECTS = [
    ("Inbox", "6aaaaaaaaaaaaa01", [rf_task("Разобрать входящие")]),
    ("🔨 Мастерская", "6aaaaaaaaaaaaa02", [
        rf_task("Наточить стамески"), rf_task("Купить наждачку P240"),
        rf_task("Собрать полку", description="Размеры в комментарии"),
        rf_task("Разметить доски", indent=2), rf_note("Ширина 80 см, глубина 25 см"),
        rf_section("Инструменты"), rf_task("Починить лобзик"), rf_task("Смазать фрезер"),
        rf_note("Многострочная заметка:\r\nпервая строка\r\n\"вторая\" строка", "2026-02-01T08:00:00.000000Z"),
    ]),
    ("🧹 Домашние дела", "6aaaaaaaaaaaaa03", [
        rf_section("Каждую неделю"), rf_task("Пропылесосить"), rf_task("Полить цветы"),
        rf_section("Раз в месяц"), rf_task("Разморозить холодильник"), rf_task("Почистить фильтры"),
    ]),
    ("📚 Учёба", "6aaaaaaaaaaaaa04", [rf_section("Курсы")]),
    ("💡Идеи", "6aaaaaaaaaaaaa05", [
        rf_task("Идея: приложение для рецептов"), rf_task("Идея: блог о путешествиях"),
        rf_task("Идея: настольная игра"),
    ]),
    ("👾 Работа", "6aaaaaaaaaaaaa06", [
        rf_section("Спринт"), rf_task("Подготовить демо"), rf_note("Демо в пятницу"),
        rf_section("Бэклог"), rf_task("Обновить зависимости"),
        rf_section("Готово"), rf_task("Написать ретро"),
    ]),
    ("🥷 Личное", "6aaaaaaaaaaaaa07", [
        rf_task("Записаться к стоматологу"), rf_task("Продлить страховку"),
        rf_task("Найти полис", indent=2), rf_task("Сравнить тарифы", indent=2),
        rf_section("Спорт"), rf_task("Купить кроссовки"), rf_note("Размер 43"),
    ]),
    ("🎬 Media", "6aaaaaaaaaaaaa08", [
        rf_task("Посмотреть фильм из списка"), rf_task("Выбрать сериал", indent=2),
        rf_task("Разобрать фото за год"),
    ]),
    ("🗄️ Очень длинное название проекта для проверки переноса строк в интерфейсе", "6aaaaaaaaaaaaa09", [
        rf_task("Первая задача"), rf_task("Вторая задача"), rf_note("Заметка к проекту"),
    ]),
    ("🤖 Нейросети и ИИ", "6aaaaaaaaaaaaa10", [rf_task("Попробовать локальную модель")]),
    ("🏘️ Соседи", "6aaaaaaaaaaaaa11", [
        rf_section("Собрание"), rf_task("Составить повестку"), rf_task("Разослать приглашения"),
        rf_note("Собрание в 19:00"),
    ]),
    ("🛒 Покупки", "6aaaaaaaaaaaaa12", [rf_section("Продукты"), rf_task("Хлеб")]),
    ("🇯🇵 Япония", "6aaaaaaaaaaaaa13", [
        rf_task("Оформить визу"), rf_task("Забронировать отель"), rf_task("Купить JR Pass"),
        rf_task("Скачать офлайн-карты"),
    ]),
    ("🏠 Дом", "6aaaaaaaaaaaaa14", []),
    ("💡 Развитие", "6aaaaaaaaaaaaa15", []),
]

rf_path = os.path.join(os.path.dirname(__file__), "realformat_backup.zip")
rf_tasks = 0
with zipfile.ZipFile(rf_path, "w", compression=zipfile.ZIP_STORED) as zf:
    for name, pid, rows in RF_PROJECTS:
        lines = [RF_HEADER, rf_row("meta", "view_style=list"), RF_BLANK]
        for row in rows:
            lines.append(row)
            if row.startswith("section,"):
                continue
            lines.append(RF_BLANK)
        info = zipfile.ZipInfo(f"{name} [{pid}].csv", date_time=(2026, 1, 1, 0, 0, 0))
        zf.writestr(info, ("﻿" + "\r\n".join(lines) + "\r\n").encode("utf-8"))
        rf_tasks += sum(1 for r in rows if r.startswith("task,"))

print(f"Written: {rf_path}")
print(f"  {len(RF_PROJECTS)} projects, {rf_tasks} tasks")
