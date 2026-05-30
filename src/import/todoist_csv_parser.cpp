#include "todoist_csv_parser.h"
#include "nlp/dateparser.h"

#include <QStringList>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringConverter>

// ---------------------------------------------------------------------------
// RFC-4180-compliant CSV tokenizer
// ---------------------------------------------------------------------------

QList<QStringList> TodoistCsvParser::tokenize(const QString &csv)
{
    QList<QStringList> rows;
    QStringList currentRow;
    QString currentField;
    bool inQuotes = false;

    const int len = csv.length();
    for (int i = 0; i < len; ++i) {
        const QChar ch = csv[i];

        if (inQuotes) {
            if (ch == u'"') {
                // Peek: if next char is also '"' → escaped quote inside field.
                if (i + 1 < len && csv[i + 1] == u'"') {
                    currentField += u'"';
                    ++i; // consume second quote
                } else {
                    // Closing quote
                    inQuotes = false;
                }
            } else {
                currentField += ch;
            }
        } else {
            if (ch == u'"') {
                inQuotes = true;
            } else if (ch == u',') {
                currentRow.append(currentField);
                currentField.clear();
            } else if (ch == u'\n') {
                currentRow.append(currentField);
                currentField.clear();
                rows.append(currentRow);
                currentRow.clear();
            } else if (ch == u'\r') {
                // Skip bare CR; \r\n is handled by the \n branch above
            } else {
                currentField += ch;
            }
        }
    }

    // Flush last field / row (file may not end with newline)
    currentRow.append(currentField);
    if (!currentRow.isEmpty() && !(currentRow.size() == 1 && currentRow[0].isEmpty()))
        rows.append(currentRow);

    return rows;
}

// ---------------------------------------------------------------------------
// Column indices in the Todoist CSV header
// ---------------------------------------------------------------------------
static const int COL_TYPE          = 0;
static const int COL_CONTENT       = 1;
static const int COL_DESCRIPTION   = 2;
static const int COL_PRIORITY      = 3;
static const int COL_INDENT        = 4;
// COL_AUTHOR = 5 (ignored)
// COL_RESPONSIBLE = 6 (ignored)
static const int COL_DATE          = 7;
static const int COL_DATE_LANG     = 8;
static const int COL_TIMEZONE      = 9;
static const int COL_DURATION      = 10;
static const int COL_DURATION_UNIT = 11;

static QString safeField(const QStringList &row, int idx)
{
    if (idx < 0 || idx >= row.size()) return {};
    return row[idx].trimmed();
}

// ---------------------------------------------------------------------------
// Priority mapping [B-5.43 / priority-inversion fix].
//
// Todoist CSV export encoding:      1 = Urgent (p1/highest shown in UI)
//                                   2 = High   (p2)
//                                   3 = Medium (p3)
//                                   4 = Normal (p4/lowest shown in UI)
//
// OpenTodo internal encoding (canonical -- used by the UI, quick-add and
// export; TodoistTask.priority now also carries this convention):
//                                   1 = Urgent (highest/p1, red flag)
//                                   4 = Normal (lowest/p4, no flag)
//
// The Todoist CSV scale and the OpenTodo internal scale are IDENTICAL
// (1 = urgent in both), so the CSV -> internal mapping is the IDENTITY and
// no conversion is needed. (The Todoist Sync API path uses the OPPOSITE
// scale, 4 = urgent, so todoist_api_client.cpp inverts with 5-v there.)
//
// F-Q2.24-C #3: Todoist's export emits PRIORITY=0 as a null sentinel meaning
// "no explicit priority set". We map it to internal 4 (Normal/p4), exactly
// the same bucket where an EXPLICIT Normal also lands. This is documented
// LOSSY behaviour -- distinguishing the two would require a Task::hasPriority
// flag (schema change, out of scope). The parser counts these rows so the
// importer can surface a single user-facing notice rather than silently
// collapsing them. Any other invalid / out-of-range value also -> internal 4.
// ---------------------------------------------------------------------------
int TodoistCsvParser::todoistPriority(const QString &raw)
{
    bool ok = false;
    int v = raw.toInt(&ok);
    if (!ok) return 4; // default: Normal (internal p4)
    if (v < 1 || v > 4) return 4; // null sentinel / out-of-range -> Normal
    // Todoist CSV (1=urgent) and OpenTodo internal (1=urgent) share the same
    // scale -> identity mapping, no inversion. [B-5.43 / priority-inversion fix]
    return v;
}

// ---------------------------------------------------------------------------
// Due-date parsing
// ---------------------------------------------------------------------------
// F-Q2.24-C #4: parseDue now tries (in order):
//   1. ISO YYYY-MM-DD[T...] — the canonical Todoist API form.
//   2. US MM/DD/YYYY — common in user-edited CSVs from en-US locales.
//   3. EU DD.MM.YYYY — common in user-edited CSVs from de/ru/etc locales.
//   4. Natural-language via DateParser (en or ru) — handles "today",
//      "tomorrow", "next week", "every day", ordinals, weekday names, etc.
// On every successful conversion the original `string` field is preserved
// for round-trip fidelity; `date` is the ISO form. Recurrence flag is taken
// from DateParser when natural-language matches; otherwise a heuristic on
// keyword presence (unchanged from B-5.42).
TodoistDue TodoistCsvParser::parseDue(const QString &dateStr,
                                      const QString &lang,
                                      const QString &timezone)
{
    if (dateStr.isEmpty()) return {};

    TodoistDue due;
    due.string   = dateStr;
    due.lang     = lang.isEmpty() ? QStringLiteral("en") : lang;
    due.timezone = timezone;

    const QString trimmed = dateStr.trimmed();

    // 1. ISO date YYYY-MM-DD[Thh:mm:ss...]
    {
        static const QRegularExpression reIso(
            QStringLiteral(R"(^(\d{4}-\d{2}-\d{2})(T[\d:Z.+-]+)?$)"));
        const auto m = reIso.match(trimmed);
        if (m.hasMatch()) {
            due.date = m.captured(1);
            if (!m.captured(2).isEmpty()) {
                const QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODate);
                if (dt.isValid())
                    due.datetime = dt.toString(Qt::ISODate);
            }
            due.isRecurring = false;
            return due;
        }
    }

    // 2. US slash format MM/DD/YYYY (also accepts M/D/YY).
    {
        static const QRegularExpression reUs(
            QStringLiteral(R"(^(\d{1,2})/(\d{1,2})/(\d{2,4})$)"));
        const auto m = reUs.match(trimmed);
        if (m.hasMatch()) {
            int month = m.captured(1).toInt();
            int day   = m.captured(2).toInt();
            int year  = m.captured(3).toInt();
            if (year < 70)        year += 2000;
            else if (year < 100)  year += 1900;
            const QDate d(year, month, day);
            if (d.isValid()) {
                due.date = d.toString(QStringLiteral("yyyy-MM-dd"));
                due.isRecurring = false;
                return due;
            }
        }
    }

    // 3. EU dot format DD.MM.YYYY (also accepts D.M.YY).
    {
        static const QRegularExpression reEu(
            QStringLiteral(R"(^(\d{1,2})\.(\d{1,2})\.(\d{2,4})$)"));
        const auto m = reEu.match(trimmed);
        if (m.hasMatch()) {
            int day   = m.captured(1).toInt();
            int month = m.captured(2).toInt();
            int year  = m.captured(3).toInt();
            if (year < 70)        year += 2000;
            else if (year < 100)  year += 1900;
            const QDate d(year, month, day);
            if (d.isValid()) {
                due.date = d.toString(QStringLiteral("yyyy-MM-dd"));
                due.isRecurring = false;
                return due;
            }
        }
    }

    // 4. Natural-language via DateParser (en or ru). This handles "today",
    // "tomorrow", "yesterday", "next monday", "in 3 days", "every weekday",
    // ordinal forms ("3rd of march"), and more. We pass through the DATE_LANG
    // column verbatim so Russian rows ("сегодня") parse via rules_ru.
    {
        const DateParseResult pr = (due.lang == QStringLiteral("ru"))
            ? DateParser::parseRussian(trimmed)
            : DateParser::parseEnglish(trimmed);
        if (pr.valid) {
            due.date        = pr.date;
            due.datetime    = pr.datetime;
            due.isRecurring = pr.isRecurring;
            return due;
        }
    }

    // 5. Last-resort heuristic for unrecognized strings — still try to flag
    // recurrence so the rest of the pipeline knows. Date stays empty.
    const QString lower = trimmed.toLower();
    due.isRecurring =
        lower.contains(QStringLiteral("every "))   ||
        lower.contains(QStringLiteral("каждый "))  ||
        lower.contains(QStringLiteral("каждую "))  ||
        lower.contains(QStringLiteral("каждые "))  ||
        lower.contains(QStringLiteral("ежедневно")) ||
        lower.contains(QStringLiteral("еженедельно")) ||
        lower.startsWith(QStringLiteral("ev "));

    return due;
}

// ---------------------------------------------------------------------------
// Encoding-aware bytes entry point [B-5.40]
//
// F-Q2.24-C #5: added UTF-16 LE/BE BOM detection. The previous chain (UTF-8
// BOM → UTF-8 → local-8bit) silently mojibaked UTF-16 CSVs because their
// byte sequences look like ASCII followed by NULs — not invalid UTF-8 in the
// strict sense, but garbage when interpreted as 8-bit. Detecting the FF FE
// (LE) or FE FF (BE) prefix dispatches to the proper decoder.
// ---------------------------------------------------------------------------
TodoistCsvParser::ParseResult TodoistCsvParser::parseBytes(const QByteArray &rawBytes,
                                                            const QString    &projectId)
{
    QString csv;

    // 1a. UTF-16 LE BOM (FF FE)
    if (rawBytes.size() >= 2
        && quint8(rawBytes[0]) == 0xFF
        && quint8(rawBytes[1]) == 0xFE)
    {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        // Skip the 2-byte BOM before decoding so it doesn't surface as U+FEFF.
        csv = decoder(QByteArrayView(rawBytes.constData() + 2, rawBytes.size() - 2));
        return parse(csv, projectId);
    }

    // 1b. UTF-16 BE BOM (FE FF)
    if (rawBytes.size() >= 2
        && quint8(rawBytes[0]) == 0xFE
        && quint8(rawBytes[1]) == 0xFF)
    {
        QStringDecoder decoder(QStringConverter::Utf16BE);
        csv = decoder(QByteArrayView(rawBytes.constData() + 2, rawBytes.size() - 2));
        return parse(csv, projectId);
    }

    // 2. UTF-8 BOM (EF BB BF) → UTF-8
    if (rawBytes.size() >= 3
        && quint8(rawBytes[0]) == 0xEF
        && quint8(rawBytes[1]) == 0xBB
        && quint8(rawBytes[2]) == 0xBF)
    {
        csv = QString::fromUtf8(rawBytes.constData() + 3, rawBytes.size() - 3);
        return parse(csv, projectId); // BOM already stripped
    }

    // 3. Try UTF-8 without errors (QStringDecoder::hasError() detects invalid sequences)
    {
        QStringDecoder decoder(QStringConverter::Utf8);
        QString attempt = decoder(rawBytes);
        if (!decoder.hasError()) {
            return parse(attempt, projectId);
        }
    }

    // 4. Fallback: use the system locale encoding (Windows-1251 on Russian-locale
    //    Windows, which matches older Todoist CSV exports) [B-5.40].
    //    QString::fromLocal8Bit honours the process locale set by the OS.
    csv = QString::fromLocal8Bit(rawBytes);

    return parse(csv, projectId);
}

// ---------------------------------------------------------------------------
// Main parse entry point
// ---------------------------------------------------------------------------
TodoistCsvParser::ParseResult TodoistCsvParser::parse(const QString &csv,
                                                       const QString &projectId)
{
    ParseResult result;

    // Strip UTF-8 BOM (U+FEFF) if present — common in Windows-exported CSVs
    // and required to avoid the header check failing [B-5.39/61].
    const QString &csvRef = (!csv.isEmpty() && csv[0] == QChar(0xFEFF))
                            ? csv.mid(1)
                            : csv;

    const QList<QStringList> rows = tokenize(csvRef);
    if (rows.isEmpty()) {
        result.error = QStringLiteral("Empty CSV file");
        return result;
    }

    // Validate / skip header row
    const QStringList &header = rows[0];
    if (header.isEmpty() || header[0].toUpper() != QStringLiteral("TYPE")) {
        result.error = QStringLiteral("Unexpected CSV header (first column should be TYPE)");
        return result;
    }

    // Track task stack for parent_id resolution (subtask indent hierarchy).
    // Index N stores the last task id seen at indent level N.
    QMap<int, QString> indentStack; // indent → task id

    int taskOrder   = 0;
    int sectionOrder = 0;

    // Q3.08-A: track the currently-open section so subsequent task rows can
    // adopt its id. Todoist CSV uses a TYPE=section row as a header that
    // applies to all following TYPE=task rows in the same project until the
    // next section row. The parser previously emitted the section entity but
    // never wired task.sectionId, leaving tasks section-less in DataStore.
    QString currentSectionId;

    for (int row = 1; row < rows.size(); ++row) {
        const QStringList &fields = rows[row];
        if (fields.isEmpty()) continue;

        const QString type = safeField(fields, COL_TYPE).toLower();

        if (type == QStringLiteral("section")) {
            TodoistSection sec;
            sec.id        = QString::number(row); // synthesized id for CSV (no real id in export)
            sec.projectId = projectId;
            sec.name      = safeField(fields, COL_CONTENT);
            sec.order     = sectionOrder++;
            result.sections.append(sec);

            // Q3.08-A: this section is now the "open" one for following tasks.
            currentSectionId = sec.id;

        } else if (type == QStringLiteral("task")) {
            TodoistTask task;
            // CSV has no id column; generate a stable id from row number
            task.id          = QStringLiteral("csv-%1-%2").arg(projectId).arg(row);
            task.content     = safeField(fields, COL_CONTENT);
            task.description = safeField(fields, COL_DESCRIPTION);
            task.projectId   = projectId;

            // Q3.08-A: adopt the most recently opened section in this project.
            // Within a single CSV every row carries the same projectId (set by
            // the caller), so the project-match guard is automatically met;
            // the empty-check is what disambiguates "tasks above the first
            // SECTION row" (sectionId stays "") from "tasks under a section".
            if (!currentSectionId.isEmpty())
                task.sectionId = currentSectionId;

            // F-Q2.24-C #3: detect Todoist's null sentinel (PRIORITY=0) and
            // count it. The collapse to internal Normal happens inside
            // todoistPriority(); we just want the count here.
            const QString rawPri = safeField(fields, COL_PRIORITY);
            {
                bool ok = false;
                if (rawPri.toInt(&ok) == 0 && ok)
                    ++result.lossyPriorityZeroCount;
            }
            task.priority    = todoistPriority(rawPri);
            task.indent      = qMax(1, safeField(fields, COL_INDENT).toInt());
            task.order       = taskOrder++;

            // Resolve parent from indent stack
            if (task.indent > 1) {
                const int parentIndent = task.indent - 1;
                if (indentStack.contains(parentIndent))
                    task.parentId = indentStack[parentIndent];
            }
            indentStack[task.indent] = task.id;
            // Clear all deeper indents to handle "going back up"
            for (int i = task.indent + 1; i <= 10; ++i)
                indentStack.remove(i);

            // Due date
            task.due = parseDue(
                safeField(fields, COL_DATE),
                safeField(fields, COL_DATE_LANG),
                safeField(fields, COL_TIMEZONE)
            );
            task.timezone = safeField(fields, COL_TIMEZONE);

            // Duration
            const QString durStr = safeField(fields, COL_DURATION);
            if (!durStr.isEmpty()) {
                bool ok = false;
                int dur = durStr.toInt(&ok);
                if (ok) task.durationAmount = dur;
            }
            task.durationUnit = safeField(fields, COL_DURATION_UNIT);

            result.tasks.append(task);

        } else if (type == QStringLiteral("note")) {
            TodoistNote note;
            note.id        = QStringLiteral("csv-note-%1-%2").arg(projectId).arg(row);
            note.projectId = projectId;
            note.content   = safeField(fields, COL_CONTENT);
            note.postedAt  = QDateTime::currentDateTimeUtc();

            // Attach note to the most recent task (last task in result.tasks)
            if (!result.tasks.isEmpty())
                note.taskId = result.tasks.last().id;

            result.notes.append(note);
        }
        // Unknown types are silently ignored
    }

    return result;
}
