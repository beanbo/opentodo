"""
Generates a synthetic Todoist backup ZIP fixture for unit tests.
Run once: python generate_fixture.py
Output: Todoist Backup 2024-01-01.zip
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
