#pragma once
#include <QObject>
#include <QString>
#include <QList>

class DataStore;
class Task;

// IcsExporter — emit RFC 5545 iCalendar (VCALENDAR / VTODO) for a task list,
// so users can subscribe to their OpenTodo tasks from Apple Calendar, Google
// Calendar, Outlook (Reminders / To-Do panes), etc.
//
// We emit VTODO (not VEVENT) because tasks are not time-blocked events.
// Calendar apps that don't render VTODO will simply ignore the file; the major
// three (Apple Calendar, Outlook, Google Calendar via the Tasks API) do.
//
// The class is intentionally stateless — both exported helpers are static so
// they can be called directly from tests without spinning up a DataStore.
// The QObject + DataStore-aware Q_INVOKABLE wrappers exist solely so QML /
// SettingsViewModel / a Save-As dialog can drive the export from the UI.
class IcsExporter : public QObject
{
    Q_OBJECT
public:
    explicit IcsExporter(DataStore *store, QObject *parent = nullptr);

    // ── Stateless serialisation API (used by tests and the QObject wrappers).

    // Render the full VCALENDAR text for `tasks`. Output is UTF-8 with CRLF
    // line endings (RFC 5545 §3.1). Deleted/tombstoned tasks are skipped.
    static QString exportTasks(const QList<Task *> &tasks);

    // Write the text from exportTasks() to `path` via QSaveFile (atomic).
    // Returns true on success.
    static bool exportToFile(const QList<Task *> &tasks, const QString &path);

    // ── QML-facing API.

    // Export every non-deleted task in the DataStore to `path`. Returns the
    // absolute path on success, empty string on failure (and emits
    // exportFailed). `path` may be a plain filesystem path or a file:// URL —
    // the file:// scheme is stripped before opening so QFileDialog output is
    // accepted verbatim.
    Q_INVOKABLE QString exportToIcs(const QString &path);

signals:
    void exportSucceeded(const QString &path);
    void exportFailed(const QString &message);

private:
    DataStore *m_store;
};
