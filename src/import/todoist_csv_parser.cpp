#include "todoist_csv_parser.h"

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
// Priority mapping [B-5.43] — INVERSION REQUIRED for CSV path.
//
// Todoist CSV export encoding:      1 = Urgent (p1/highest shown in UI)
//                                   2 = High   (p2)
//                                   3 = Medium (p3)
//                                   4 = Normal (p4/lowest shown in UI)
//
// Todoist Sync API / OpenTodo internal encoding (TodoistTask.priority):
//                                   1 = Normal (lowest)
//                                   4 = Urgent (highest/p1)
//
// The two conventions are INVERTED relative to each other.  The API path
// stores raw API values (4=urgent), so no conversion is needed there.
// The CSV path receives CSV values (1=urgent) and must invert them to match
// the internal convention used by applyImport() → setPriority().
//
// Inversion formula: internal = (5 - csv)
//   csv 1 (Urgent) → internal 4  ✓
//   csv 2 (High)   → internal 3  ✓
//   csv 3 (Medium) → internal 2  ✓
//   csv 4 (Normal) → internal 1  ✓
// ---------------------------------------------------------------------------
int TodoistCsvParser::todoistPriority(const QString &raw)
{
    bool ok = false;
    int v = raw.toInt(&ok);
    if (!ok) return 1; // default: Normal
    if (v < 1 || v > 4) return 1;
    // Invert CSV scale (1=urgent) to internal scale (4=urgent) [B-5.43]
    return 5 - v;
}

// ---------------------------------------------------------------------------
// Due-date parsing
// ---------------------------------------------------------------------------
TodoistDue TodoistCsvParser::parseDue(const QString &dateStr,
                                      const QString &lang,
                                      const QString &timezone)
{
    if (dateStr.isEmpty()) return {};

    TodoistDue due;
    due.string   = dateStr;
    due.lang     = lang.isEmpty() ? QStringLiteral("en") : lang;
    due.timezone = timezone;

    // Try ISO date YYYY-MM-DD
    static const QRegularExpression reDate(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2})(T[\d:Z.+-]+)?$)"));
    const auto m = reDate.match(dateStr.trimmed());
    if (m.hasMatch()) {
        due.date = m.captured(1);
        if (!m.captured(2).isEmpty()) {
            // Has a time part
            const QDateTime dt = QDateTime::fromString(dateStr.trimmed(), Qt::ISODate);
            if (dt.isValid())
                due.datetime = dt.toString(Qt::ISODate);
        }
        // A plain date without a recurrence string is not recurring
        due.isRecurring = false;
        return due;
    }

    // Natural-language string → mark as recurring if it contains recurrence keywords
    const QString lower = dateStr.toLower();
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
// ---------------------------------------------------------------------------
TodoistCsvParser::ParseResult TodoistCsvParser::parseBytes(const QByteArray &rawBytes,
                                                            const QString    &projectId)
{
    QString csv;

    // 1. UTF-8 BOM (EF BB BF) → UTF-8
    if (rawBytes.size() >= 3
        && quint8(rawBytes[0]) == 0xEF
        && quint8(rawBytes[1]) == 0xBB
        && quint8(rawBytes[2]) == 0xBF)
    {
        csv = QString::fromUtf8(rawBytes.constData() + 3, rawBytes.size() - 3);
        return parse(csv, projectId); // BOM already stripped
    }

    // 2. Try UTF-8 without errors (QStringDecoder::hasError() detects invalid sequences)
    {
        QStringDecoder decoder(QStringConverter::Utf8);
        QString attempt = decoder(rawBytes);
        if (!decoder.hasError()) {
            return parse(attempt, projectId);
        }
    }

    // 3. Fallback: use the system locale encoding (Windows-1251 on Russian-locale
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

        } else if (type == QStringLiteral("task")) {
            TodoistTask task;
            // CSV has no id column; generate a stable id from row number
            task.id          = QStringLiteral("csv-%1-%2").arg(projectId).arg(row);
            task.content     = safeField(fields, COL_CONTENT);
            task.description = safeField(fields, COL_DESCRIPTION);
            task.projectId   = projectId;
            task.priority    = todoistPriority(safeField(fields, COL_PRIORITY));
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
