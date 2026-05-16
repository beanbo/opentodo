#pragma once

#include "todoist_types.h"

#include <QObject>
#include <QString>
#include <QQmlEngine>

class DataStore;

/**
 * @brief End-to-end Todoist import controller.
 *
 * Two entry points:
 *   - importFromBackupZip(filePath)  — parse a .zip backup file
 *   - importFromApiToken(token)      — fetch live data from Sync API v9
 *
 * Both paths converge on applyImport(TodoistImportData) which writes to
 * DataStore. Emits importProgress and importFinished signals for QML binding.
 *
 * Register in main.cpp:
 *   auto *todoistImporter = new TodoistImporter(dataStore, &app);
 *   ctx->setContextProperty("todoistImporter", todoistImporter);
 */
class TodoistImporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the todoistImporter context property.")

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit TodoistImporter(DataStore *store, QObject *parent = nullptr);

    bool busy() const;

    /**
     * @brief Import from a Todoist backup ZIP file.
     * @param filePath  Local path to the .zip file (may be a QUrl string like
     *                  "file:///C:/..."; the prefix is stripped automatically).
     * @param importCompleted  If true, completed tasks are also imported.
     */
    Q_INVOKABLE void importFromBackupZip(const QString &filePath,
                                         bool importCompleted = false);

    /**
     * @brief Import live data from Todoist Sync API v9.
     * @param apiToken        Personal API token.
     * @param importCompleted If true, completed tasks are also imported.
     */
    Q_INVOKABLE void importFromApiToken(const QString &apiToken,
                                        bool importCompleted = false);

signals:
    void importProgress(int done, int total, const QString &phase);
    void importFinished(int taskCount, int projectCount, const QString &error);
    /**
     * @brief Emitted after importFinished if any non-fatal parse errors
     *        were collected during the import (e.g. bad CSV rows) [B-5.62].
     *        QML can present these as a collapsible warning list.
     */
    void importWarnings(const QStringList &warnings);
    void busyChanged();

private:
    void applyImport(const TodoistImportData &data, bool importCompleted);
    void setBusy(bool busy);

    DataStore   *m_store;
    bool         m_busy = false;
    QStringList  m_warnings; // accumulated per-import non-fatal errors [B-5.62]

    // F-Q2.24-C #3: total rows with PRIORITY=0 (Todoist null sentinel) across
    // every CSV in the current import. Collapsed into a single end-of-import
    // user warning rather than one per row.
    int          m_lossyPriorityZeroCount = 0;

    // F-Q2.24-C #6: number of projects imported from the CSV path in the
    // current run. Used to emit a single user-facing notice about CSV color
    // unavailability (the per-project palette is only reachable via the API).
    int          m_csvProjectCount = 0;
};
