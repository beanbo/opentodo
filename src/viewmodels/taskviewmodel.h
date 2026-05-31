#pragma once

#include <QAbstractListModel>
#include <QClipboard>
#include <QDate>
#include <QGuiApplication>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QQmlEngine>
#include <QTimer>

#include "model/task.h"
#include "storage/datastore.h"
#include "nlp/dateparser.h"
#include "services/undostack.h"

class Telemetry;

class TaskViewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TaskViewModel is constructed in C++ and exposed via context property; FilterMode enum is reachable via the type name.")

    Q_PROPERTY(QString currentProjectId READ currentProjectId WRITE setCurrentProjectId NOTIFY currentProjectIdChanged)
    Q_PROPERTY(QString currentSectionId READ currentSectionId WRITE setCurrentSectionId NOTIFY currentSectionIdChanged)
    Q_PROPERTY(QString currentFilter READ currentFilter WRITE setCurrentFilter NOTIFY currentFilterChanged)
    Q_PROPERTY(FilterMode filterMode READ filterMode WRITE setFilterMode NOTIFY filterModeChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(bool sortReversed READ sortReversed WRITE setSortReversed NOTIFY sortReversedChanged)
    Q_PROPERTY(QString groupBy READ groupBy WRITE setGroupBy NOTIFY groupByChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showCompleted READ showCompleted WRITE setShowCompleted NOTIFY showCompletedChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString hoveredTaskId READ hoveredTaskId WRITE setHoveredTaskId NOTIFY hoveredTaskIdChanged)
    Q_PROPERTY(QObject* selectedTask READ selectedTask WRITE setSelectedTask NOTIFY selectedTaskChanged)

public:
    enum class FilterMode {
        Inbox, Today, Upcoming, Project, Board, Calendar, Label, Filter,
        Completed, Settings, Productivity, Notifications, AI, Trash
    };
    Q_ENUM(FilterMode)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        ContentRole,
        DescriptionRole,
        ProjectIdRole,
        SectionIdRole,
        ParentIdRole,
        OrderRole,
        PriorityRole,
        LabelsRole,
        IsCompletedRole,
        CompletedAtRole,
        DueDateRole,
        DueStringRole,
        IsRecurringRole,
        DeadlineRole,
        DurationAmountRole,
        DurationUnitRole,
        CreatedAtRole,
        DayOrderRole,
        ColorRole,            // task->color() — empty string when no color set
        IsPinnedRole,         // task->isPinned() — закреплена ли задача наверху списка
        EstimateMinutesRole,  // task->estimateMinutes() — оценка трудозатрат в минутах (0 = не задана)
        TaskObjectRole,
        // Used by ListView.section.property — returns the sectionId, but
        // substitutes "__none__" when the task has no section. Qt's ListView
        // skips section.delegate creation when every visible item shares an
        // EMPTY section value, so we never expose "" via this role.
        SectionGroupKeyRole,
        // night iter-134: the scheduled date used to GROUP the Upcoming view —
        // dueDate, else the date part of dueDatetime. The Upcoming list is
        // already SORTED by this effective date (sortTasks "date" branch,
        // iter-115), but day-grouping keyed off raw dueDate, which is empty for
        // datetime-only tasks (B-1.28/B-1.30). A timed task (e.g. "1 июня 15:00")
        // thus sorted INTO the 1-июня block yet carried section "" — fragmenting
        // that day with a stray "Без даты" header. Grouping by this role matches
        // the sort, so timed tasks sit under their real day.
        EffectiveDueDateRole
    };
    Q_ENUM(Roles)

    explicit TaskViewModel(DataStore *store, QObject *parent = nullptr);

    // Inject the undo stack so actions can be reversed via the snackbar.
    Q_INVOKABLE void setUndoStack(UndoStack *stack);

    // Inject optional telemetry service. Safe to call unconditionally — trackEvent
    // checks m_enabled internally. Wired in main.cpp.
    void setTelemetry(Telemetry *telemetry);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Properties
    QString currentProjectId() const;
    void setCurrentProjectId(const QString &id);

    QString currentSectionId() const;
    void setCurrentSectionId(const QString &id);

    QString currentFilter() const;
    void setCurrentFilter(const QString &filter);

    FilterMode filterMode() const;
    void setFilterMode(FilterMode mode);
    static FilterMode filterModeFromString(const QString &s);

    QString sortBy() const;
    void setSortBy(const QString &sort);

    bool sortReversed() const;
    void setSortReversed(bool reversed);

    QString groupBy() const;
    void setGroupBy(const QString &group);

    int count() const;

    bool showCompleted() const;
    void setShowCompleted(bool show);

    // Hovered / selected task (used by keyboard shortcuts)
    QString hoveredTaskId() const;
    void setHoveredTaskId(const QString &id);

    QObject *selectedTask() const;
    void setSelectedTask(QObject *task);

    // Selection
    QStringList selectedIds() const;
    int selectionCount() const;
    Q_INVOKABLE void selectTask(const QString &id);
    void deselectTask(const QString &id);
    // Open the task editor for a given id without touching the multi-select set.
    // Used by NotificationsView and the OS notification-click handler.
    Q_INVOKABLE void openTaskById(const QString &id);
    Q_INVOKABLE void toggleSelection(const QString &id);
    Q_INVOKABLE void selectRange(const QString &fromId, const QString &toId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(const QString &id) const;

    // Bulk operations
    Q_INVOKABLE void bulkComplete();
    Q_INVOKABLE void bulkDelete();
    Q_INVOKABLE void bulkMoveToProject(const QString &projectId);
    Q_INVOKABLE void bulkSetPriority(int priority);
    Q_INVOKABLE void bulkAddLabel(const QString &label);
    Q_INVOKABLE void bulkSetPinned(bool pinned);
    Q_INVOKABLE void bulkSetColor(const QString &color);
    Q_INVOKABLE void bulkSetDue(const QString &dueString);
    // Apply a deadline (Task::deadline, ISO yyyy-MM-dd) to every selected task.
    // Pass an invalid QDate (e.g. QDate{}) to clear the deadline on all selected tasks.
    Q_INVOKABLE void bulkSetDeadline(const QDate &date);
    Q_INVOKABLE void bulkArchive();           // archive == soft-delete with distinct undo label
    Q_INVOKABLE QString bulkCopyAsMarkdown(); // returns markdown text + copies to clipboard

    // Task CRUD
    Q_INVOKABLE QString addTask(const QString &content, const QString &projectId = {},
                                 int priority = 1, const QString &dueString = {},
                                 const QStringList &labels = {});
    Q_INVOKABLE QString addTaskFromQuickAdd(const QString &input);
    Q_INVOKABLE QVariantMap previewQuickAdd(const QString &input);
    Q_INVOKABLE bool updateTask(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE bool completeTask(const QString &id);
    Q_INVOKABLE bool uncompleteTask(const QString &id);
    Q_INVOKABLE bool deleteTask(const QString &id);
    Q_INVOKABLE bool moveTask(const QString &id, const QString &projectId, const QString &sectionId = {});
    Q_INVOKABLE bool moveTaskOrder(const QString &id, int newIndex);
    Q_INVOKABLE bool setTaskPriority(const QString &id, int priority);
    Q_INVOKABLE bool setTaskLabels(const QString &id, const QStringList &labels);
    Q_INVOKABLE bool setTaskDue(const QString &id, const QString &dueString);
    Q_INVOKABLE QString duplicateTask(const QString &id);

    // Insert a new empty task above/below the given anchor task. Returns the
    // new task's id (empty on failure). Project/section/parent copied from anchor.
    Q_INVOKABLE QString addTaskAbove(const QString &anchorId, const QString &content = {});
    Q_INVOKABLE QString addTaskBelow(const QString &anchorId, const QString &content = {});

    // Reorder a task within its sibling scope (same project + section + parent_id)
    // so it becomes the first / last visible item. Implementation: assign the
    // task's order to (min/max sibling order) ∓ 1. Telemetry: task_move_to_top /
    // task_move_to_bottom. Returns true on success.
    Q_INVOKABLE bool moveTaskToTop(const QString &taskId);
    Q_INVOKABLE bool moveTaskToBottom(const QString &taskId);

    // Wave 3 follow-up (F-6.39): create a new "Шаблон: <name>" project
    // populated with copies of every non-deleted task from `projectId`.
    // Sections, descriptions, priorities, and labels are preserved; due
    // dates are NOT copied (templates shouldn't carry stale schedules).
    // Returns the new project's id, or empty string on failure.
    Q_INVOKABLE QString duplicateProjectAsTemplate(const QString &projectId);
    // --- Trash (Корзина) ---
    // Returns up to 100 most-recently-deleted tasks (is_deleted == true), sorted
    // by updatedAt descending. Each entry is a QVariantMap with id, content,
    // projectId, projectName, priority, updatedAt (ISO string).
    Q_INVOKABLE QVariantList deletedTasks() const;
    // Restores a soft-deleted task: clears is_deleted and bumps updatedAt.
    Q_INVOKABLE void restoreTask(const QString &taskId);
    // Permanently removes a task from DataStore. Local-only — no sync op.
    Q_INVOKABLE void permanentlyDelete(const QString &taskId);
    // Permanently removes every soft-deleted task.
    Q_INVOKABLE void emptyTrash();
    // Cheap count of soft-deleted tasks (no QVariantMap construction).
    Q_INVOKABLE int trashCount() const;

    Q_INVOKABLE QVariantList getSubtasks(const QString &parentId) const;
    Q_INVOKABLE bool hasSubtasks(const QString &taskId) const;
    Q_INVOKABLE QVariantList subtasksOf(const QString &parentId) const;
    Q_INVOKABLE QString addSubtask(const QString &parentId, const QString &content);
    Q_INVOKABLE bool convertToSubtask(const QString &taskId, const QString &newParentId);
    Q_INVOKABLE bool promoteToTopLevel(const QString &taskId);
    // Promote a subtask to standalone — clears parent_id, bumps updatedAt,
    // emits taskChanged. Returns true on success. Telemetry: task_promoted.
    Q_INVOKABLE bool promoteTask(const QString &taskId);
    // Demote a top-level task to a subtask of parentId. Validates that
    // parentId exists, lives in the same project, and is not a descendant
    // of taskId (cycle prevention). Telemetry: task_demoted.
    Q_INVOKABLE bool demoteTask(const QString &taskId, const QString &parentId);
    // Returns a list of {id, content} maps for tasks eligible to act as
    // a parent for taskId. Filters: same project as taskId, not the task
    // itself, not a current descendant of taskId, and currently top-level
    // (parentId().isEmpty()). The top-level-only rule keeps the picker
    // simple — users can only demote one level deep at a time, which
    // covers the common case without exposing the full subtree depth.
    Q_INVOKABLE QVariantList candidateParents(const QString &taskId) const;
    Q_INVOKABLE Task *taskAt(int index) const;
    Q_INVOKABLE int indexOfTask(const QString &id) const;

signals:
    void currentProjectIdChanged();
    void currentSectionIdChanged();
    void currentFilterChanged();
    void filterModeChanged();
    void sortByChanged();
    void sortReversedChanged();
    void groupByChanged();
    void countChanged();
    void showCompletedChanged();
    void taskAdded(const QString &id);
    void taskRemoved(const QString &id);
    void taskChanged(const QString &id);
    // ITER3 #1: bulk variant — emitted once after bulkDelete so the snackbar
    // shows a single "Удалено N задач" entry instead of N stacked toasts.
    void tasksRemoved(const QStringList &ids);
    void selectionChanged();
    void hoveredTaskIdChanged();
    void selectedTaskChanged();

private slots:
    void onTasksChanged();

private:
    void refresh();
    void sortTasks();
    QList<Task *> fetchTasks() const;
    QList<Task *> _siblingsOfAnchor(const Task *anchor) const;

    // Russian plural form for "задача/задачи/задач" depending on count.
    // Used by bulk undo entry labels.
    static QString pluralTasks(int n)
    {
        const int n100 = n % 100;
        if (n100 >= 11 && n100 <= 14) return QStringLiteral("задач");
        switch (n % 10) {
            case 1:                   return QStringLiteral("задача");
            case 2: case 3: case 4:   return QStringLiteral("задачи");
            default:                  return QStringLiteral("задач");
        }
    }

    DataStore *m_store;
    DateParser *m_dateParser;
    UndoStack *m_undoStack = nullptr;
    Telemetry *m_telemetry = nullptr;
    QList<Task *> m_tasks;
    QString m_currentProjectId;
    QString m_currentSectionId;
    QString m_currentFilter;
    FilterMode m_filterMode = FilterMode::Inbox;
    FilterMode m_lastFilterMode = FilterMode::Inbox; // for diff-based refresh
    QString m_sortBy = QStringLiteral("order");
    bool m_sortReversed = false;
    QString m_groupBy;
    bool m_showCompleted = false;
    // Insertion-ordered list so selectedIds()[0] is deterministic (B-3.61/62).
    QList<QString> m_selectedIds;
    QString m_selectionAnchorId; // for shift-click range selection
    QString m_hoveredTaskId;
    // Bug #1 (May 2026): UAF protection — DataStore::clear() qDeleteAll's
    // every Task, but the QML right-pane editor holds a non-owning pointer
    // to one of them via this property. QPointer auto-nulls on destruction
    // so the QML null guard `if (taskViewModel.selectedTask) ...` continues
    // to work after a sync pull, restoreFromBackup, or reset-all-data.
    QPointer<QObject> m_selectedTask;

    // Q3.24: debounce rapid tasksChanged bursts so refresh() runs at most
    // once per 30 ms.  Single task edits flush immediately (the timer fires
    // before the next event-loop iteration that processes the UI frame).
    // Bulk operations (sync import, bulk-complete) coalesce into one refresh.
    QTimer m_refreshDebounce;
};
