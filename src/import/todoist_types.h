#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QRegularExpression>

// ---------------------------------------------------------------------------
// D9: calendar date (YYYY-MM-DD) of a Todoist due datetime, in LOCAL time.
//
// Todoist sends fixed-timezone dues as UTC instants ("2024-01-16T01:00:00Z")
// and floating ones without an offset ("2024-01-16T09:00:00"). Taking the
// first 10 characters is only right for the floating form: in UTC-5 the UTC
// instant above is 2024-01-15 20:00 local, so left(10) filed the task under
// the wrong day. Strings with "Z" or a ±hh[:mm] offset are converted to local
// time; floating strings (and anything unparseable) keep their literal date.
// ---------------------------------------------------------------------------
inline QString todoistLocalDueDate(const QString &iso)
{
    static const QRegularExpression offsetRe(
        QStringLiteral(R"(T.*(Z|[+-]\d{2}(:?\d{2})?)$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (iso.size() > 10 && offsetRe.match(iso).hasMatch()) {
        const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
        if (dt.isValid())
            return dt.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd"));
    }
    return iso.left(10);
}

// ---------------------------------------------------------------------------
// Intermediate data structures produced by both CSV and API import paths.
// These are simple value types — no QObject overhead needed.
// ---------------------------------------------------------------------------

struct TodoistDue {
    QString date;         // YYYY-MM-DD
    QString datetime;     // ISO datetime if time present
    QString string;       // natural-language string e.g. "every day"
    QString lang;         // "en", "ru", …
    QString timezone;     // IANA timezone name, may be empty
    bool isRecurring = false;
};

struct TodoistTask {
    QString id;           // original Todoist id (string or int-as-string)
    QString content;
    QString description;
    QString projectId;    // maps to TodoistProject.id
    QString sectionId;    // maps to TodoistSection.id (may be empty)
    QString parentId;     // subtask parent (may be empty)
    int     priority = 4; // OpenTodo internal: 1=p1 urgent … 4=p4 normal (default: normal)
    int     order    = 0;
    QStringList labelNames; // label names (CSV) or ids (API, resolved later)
    TodoistDue due;
    bool    isCompleted = false;
    QDateTime completedAt;
    QDateTime createdAt;
    QString timezone;
    int     durationAmount = 0;
    QString durationUnit;
    // CSV-only: indent level used to reconstruct parent_id
    int     indent = 1;
};

struct TodoistNote {  // = comment
    QString id;
    QString taskId;    // may be empty (project note)
    QString projectId;
    QString content;
    QDateTime postedAt;
};

struct TodoistSection {
    QString id;
    QString projectId;
    QString name;
    int     order = 0;
};

struct TodoistProject {
    QString id;
    QString name;
    QString color;
    QString parentId;
    int     order = 0;
    bool    isFavorite  = false;
    bool    isArchived  = false;
    bool    isInbox     = false;
};

struct TodoistLabel {
    QString id;
    QString name;
    QString color;
    int     order = 0;
};

struct TodoistImportData {
    QList<TodoistProject> projects;
    QList<TodoistSection> sections;
    QList<TodoistTask>    tasks;
    QList<TodoistNote>    notes;
    QList<TodoistLabel>   labels;
};
