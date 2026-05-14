#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QQmlEngine>

#include "model/project.h"
#include "storage/datastore.h"

class Telemetry;

class ProjectViewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ProjectViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showArchived READ showArchived WRITE setShowArchived NOTIFY showArchivedChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        ParentIdRole,
        OrderRole,
        IsFavoriteRole,
        IsInboxRole,
        IsArchivedRole,
        ViewStyleRole,
        TaskCountRole,
        ProjectObjectRole
    };
    Q_ENUM(Roles)

    explicit ProjectViewModel(DataStore *store, QObject *parent = nullptr);

    // Inject optional telemetry service. Wired in main.cpp.
    void setTelemetry(Telemetry *telemetry);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool showArchived() const;
    void setShowArchived(bool show);

    // CRUD
    Q_INVOKABLE QString addProject(const QString &name, const QString &color = QStringLiteral("grey"),
                                    const QString &parentId = {});
    Q_INVOKABLE bool updateProject(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE bool deleteProject(const QString &id);
    Q_INVOKABLE bool archiveProject(const QString &id);
    Q_INVOKABLE bool unarchiveProject(const QString &id);
    Q_INVOKABLE bool setFavorite(const QString &id, bool favorite);
    Q_INVOKABLE bool setViewStyle(const QString &id, const QString &style);
    Q_INVOKABLE int taskCountForProject(const QString &id) const;
    Q_INVOKABLE Project *projectAt(int index) const;
    Q_INVOKABLE Project *projectById(const QString &id) const;
    Q_INVOKABLE QVariantList childProjects(const QString &parentId) const;

    // Переместить проект с позиции sourceIndex на targetIndex в видимом списке.
    // Перезаписывает поле order у всех проектов и сохраняет изменения.
    Q_INVOKABLE bool reorderProject(int sourceIndex, int targetIndex);

    // Мягко удаляет все завершённые задачи в указанном проекте.
    // Возвращает количество затронутых задач.
    Q_INVOKABLE int removeCompletedTasksInProject(const QString &projectId);

    // Per-project default priority for newly created tasks. Stored in QSettings
    // (key "projects/<projectId>/defaultPriority") rather than on the Project
    // model, so it does not need to round-trip through the sync log. Returns
    // 4 (no priority) when nothing is set; setter clamps the value to [1, 4].
    Q_INVOKABLE int defaultPriorityForProject(const QString &projectId) const;
    Q_INVOKABLE void setDefaultPriorityForProject(const QString &projectId, int priority);

    // Per-project default labels for newly created tasks. Stored in QSettings
    // (key "projects/<projectId>/defaultLabels") rather than on the Project
    // model, so it does not need to round-trip through the sync log. Returns
    // an empty list when nothing is set. The QStringList holds label IDs.
    Q_INVOKABLE QStringList defaultLabelsForProject(const QString &projectId) const;
    Q_INVOKABLE void setDefaultLabelsForProject(const QString &projectId, const QStringList &labelIds);

signals:
    void countChanged();
    void showArchivedChanged();
    void projectAdded(const QString &id);
    void projectRemoved(const QString &id);
    void projectChanged(const QString &id);
    // Emits exactly once per (projectId, ISO week) when the project's
    // weekly completed-tasks count crosses from below to >= weeklyGoal.
    void weeklyGoalReached(const QString &projectId);

private slots:
    void onProjectsChanged();
    void onTasksChanged();

private:
    void refresh();

    // Compute number of completed tasks for `projectId` whose completedAt
    // falls in the current ISO week (Monday-based, matches QDate::weekNumber).
    int weeklyCompletedForProject(const QString &projectId) const;

    DataStore *m_store;
    QList<Project *> m_projects;
    bool m_showArchived = false;
    Telemetry *m_telemetry = nullptr;

    // Per-project memo of the ISO week number when we last fired
    // weeklyGoalReached. Reset implicitly when the week number rolls over.
    // Key: projectId, Value: encoded as year * 100 + isoWeekNumber.
    QHash<QString, int> m_celebratedWeek;
};
