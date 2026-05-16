#pragma once

#include "todoist_types.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * @brief Parses a single Todoist project CSV file into a flat list of items.
 *
 * The CSV format exported by Todoist backups has the following columns:
 *   TYPE, CONTENT, DESCRIPTION, PRIORITY, INDENT, AUTHOR, RESPONSIBLE,
 *   DATE, DATE_LANG, TIMEZONE, DURATION, DURATION_UNIT
 *
 * TYPE is one of: "task", "section", "note".
 * Rows of type "task" with INDENT > 1 are subtasks (indented under the
 * nearest preceding task with INDENT == INDENT-1).
 *
 * CSV parsing handles:
 *   - Quoted fields (fields may be surrounded by double-quotes)
 *   - Escaped quotes within quoted fields ("" → ")
 *   - Multi-line cell content (newlines inside quoted fields)
 *   - Leading / trailing whitespace in field values is preserved
 */
class TodoistCsvParser
{
public:
    struct ParseResult {
        QList<TodoistTask>    tasks;
        QList<TodoistNote>    notes;
        QList<TodoistSection> sections;
        QString               error; // empty on success

        // F-Q2.24-C #3: count of rows whose PRIORITY column was Todoist's
        // null sentinel (0). These tasks land at internal priority 1 (Normal,
        // p4 in UI), which is also where explicit Normal lands — the two are
        // indistinguishable after import. The importer surfaces this count
        // as a single qInfo / user warning at the end of an import.
        int lossyPriorityZeroCount = 0;
    };

    /**
     * @brief Parse CSV text for one project.
     *        Strips a leading UTF-8 BOM (U+FEFF) if present [B-5.39/61].
     * @param csv     Full content of one Todoist project CSV file (QString).
     * @param projectId  Todoist project id to tag all tasks with.
     * @return ParseResult containing tasks, notes, and sections.
     */
    static ParseResult parse(const QString &csv, const QString &projectId);

    /**
     * @brief Decode raw bytes then parse CSV for one project [B-5.40].
     *
     * Encoding detection order:
     *   1. BOM EF BB BF → UTF-8 (strip BOM).
     *   2. QStringConverter::Utf8 without errors → UTF-8.
     *   3. Fallback → Windows-1251 (common for Russian-locale older exports).
     *
     * @param rawBytes  Raw file/entry bytes before decoding.
     * @param projectId  Todoist project id to tag all tasks with.
     * @return ParseResult containing tasks, notes, and sections.
     */
    static ParseResult parseBytes(const QByteArray &rawBytes, const QString &projectId);

    /**
     * @brief Tokenise one CSV document into rows×fields.
     *        Handles quoting / escaping / multi-line cells correctly.
     */
    static QList<QStringList> tokenize(const QString &csv);

private:
    static TodoistDue parseDue(const QString &dateStr, const QString &lang,
                               const QString &timezone);
    static int todoistPriority(const QString &raw);
};
