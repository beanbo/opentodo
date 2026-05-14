#pragma once
#include <QObject>
#include <QString>

class DataStore;

class TaskExporter : public QObject
{
    Q_OBJECT
public:
    explicit TaskExporter(DataStore *store, QObject *parent = nullptr);

    // Both methods:
    //   - return the absolute path of the written file on success, OR
    //   - return empty string on failure (and emit exportFailed).
    // Output dir defaults to QStandardPaths::DocumentsLocation if `dir` is empty.
    Q_INVOKABLE QString exportToCsv(const QString &dir = QString());
    Q_INVOKABLE QString exportToMarkdown(const QString &dir = QString());

    // Exports a single project (its tasks, sections, comments, and metadata)
    // to a self-contained JSON file. Returns the absolute path on success,
    // empty string on failure.
    Q_INVOKABLE QString exportProjectToJson(const QString &projectId,
                                             const QString &dir = QString());

signals:
    void exportSucceeded(const QString &path);
    void exportFailed(const QString &message);

private:
    DataStore *m_store;
};
