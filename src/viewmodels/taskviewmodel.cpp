#include "taskviewmodel.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/reminder.h"
#include "nlp/dateparser.h"
#include "nlp/recurrenceengine.h"
#include "nlp/quickaddparser.h"
#include "filter/filtereval.h"
#include "utils/datetime.h"
#include "utils/settingskeys.h"
#include "services/telemetry.h"

#include <QHash>
#include <QMetaEnum>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QTimeZone>
#include <algorithm>

TaskViewModel::TaskViewModel(DataStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_dateParser(new DateParser(this))
{
    // Q3.24: debounce refresh() so rapid back-to-back tasksChanged signals
    // (bulk complete, drag-reorder, sync import) produce at most one full
    // model refresh per 30 ms.  30 ms < one 60 Hz frame so the UI still
    // reacts within a single frame for typical single-task edits, while
    // burst operations (N tasks in the same Qt event-loop iteration) coalesce.
    m_refreshDebounce.setInterval(30);
    m_refreshDebounce.setSingleShot(true);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &TaskViewModel::refresh);

    connect(m_store, &DataStore::tasksChanged, this, &TaskViewModel::onTasksChanged);
    connect(m_store, &DataStore::aboutToReset, this, [this]() {
        // Bug #1 (May 2026): clear m_selectedTask BEFORE the DataStore
        // qDeleteAll's every Task. The QML editor binding to the
        // `selectedTask` property holds a non-owning QObject *. Even though
        // QPointer auto-nulls on destruction, we still need to emit
        // selectedTaskChanged() here so QML re-evaluates the binding and
        // tears down the editor view before its Task is freed.
        if (m_selectedTask) {
            m_selectedTask = nullptr;
            emit selectedTaskChanged();
        }
        beginResetModel();
        m_tasks.clear();
        endResetModel();
    });

    // Restore the persisted "reverse sort order" toggle. Stored under
    // ui/sortReversed in QSettings so it survives restarts independently of
    // the SettingsViewModel/DataStore state.json snapshot.
    {
        QSettings s;
        m_sortReversed = s.value(QStringLiteral("ui/sortReversed"), false).toBool();
    }

    refresh();
}

int TaskViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_tasks.size();
}

QVariant TaskViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};

    const Task *task = m_tasks.at(index.row());

    switch (role) {
    case IdRole: return task->id();
    case ContentRole: return task->content();
    case DescriptionRole: return task->description();
    case ProjectIdRole: return task->projectId();
    case SectionIdRole: return task->sectionId();
    case ParentIdRole: return task->parentId();
    case OrderRole: return task->order();
    case PriorityRole: return task->priority();
    case LabelsRole: return task->labels();
    case IsCompletedRole: return task->isCompleted();
    case CompletedAtRole: return task->completedAt();
    case DueDateRole: return task->dueDate();
    case DueStringRole: return task->dueString();
    case IsRecurringRole: return task->dueIsRecurring();
    case DeadlineRole: return task->deadline();
    case DurationAmountRole: return task->durationAmount();
    case DurationUnitRole: return task->durationUnit();
    case CreatedAtRole: return task->createdAt();
    case DayOrderRole: return task->dayOrder();
    case ColorRole: return task->color();
    case IsPinnedRole: return task->isPinned();
    case EstimateMinutesRole: return task->estimateMinutes();
    case TaskObjectRole: return QVariant::fromValue(const_cast<Task *>(task));
    case SectionGroupKeyRole: {
        const QString sid = task->sectionId();
        return sid.isEmpty() ? QStringLiteral("__none__") : sid;
    }
    }

    return {};
}

QHash<int, QByteArray> TaskViewModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {ContentRole, "content"},
        {DescriptionRole, "description"},
        {ProjectIdRole, "projectId"},
        {SectionIdRole, "sectionId"},
        {ParentIdRole, "parentId"},
        {OrderRole, "order"},
        {PriorityRole, "priority"},
        {LabelsRole, "labels"},
        {IsCompletedRole, "isCompleted"},
        {CompletedAtRole, "completedAt"},
        {DueDateRole, "dueDate"},
        {DueStringRole, "dueString"},
        {IsRecurringRole, "isRecurring"},
        {DeadlineRole, "deadline"},
        {DurationAmountRole, "durationAmount"},
        {DurationUnitRole, "durationUnit"},
        {CreatedAtRole, "createdAt"},
        {DayOrderRole, "dayOrder"},
        {ColorRole, "color"},
        {IsPinnedRole, "isPinned"},
        {EstimateMinutesRole, "estimateMinutes"},
        {TaskObjectRole, "taskObject"},
        {SectionGroupKeyRole, "sectionGroupKey"},
    };
}

// --- Properties ---

QString TaskViewModel::currentProjectId() const { return m_currentProjectId; }
void TaskViewModel::setCurrentProjectId(const QString &id) {
    if (m_currentProjectId != id) {
        m_currentProjectId = id;
        emit currentProjectIdChanged();
        refresh();
    }
}

QString TaskViewModel::currentSectionId() const { return m_currentSectionId; }
void TaskViewModel::setCurrentSectionId(const QString &id) {
    if (m_currentSectionId != id) {
        m_currentSectionId = id;
        emit currentSectionIdChanged();
        refresh();
    }
}

QString TaskViewModel::currentFilter() const { return m_currentFilter; }
void TaskViewModel::setCurrentFilter(const QString &filter) {
    if (m_currentFilter != filter) {
        m_currentFilter = filter;
        emit currentFilterChanged();
        refresh();
    }
}

TaskViewModel::FilterMode TaskViewModel::filterMode() const { return m_filterMode; }
void TaskViewModel::setFilterMode(FilterMode mode) {
    if (m_filterMode != mode) {
        m_filterMode = mode;
        emit filterModeChanged();
        refresh();
    }
}

TaskViewModel::FilterMode TaskViewModel::filterModeFromString(const QString &s)
{
    const QMetaEnum me = QMetaEnum::fromType<FilterMode>();
    const QString key = s.trimmed();
    if (key.isEmpty()) return FilterMode::Inbox;

    // Case-insensitive match against every enum key. Naïve "capitalise
    // the first letter" was wrong for acronym values like AI ("ai" →
    // "Ai" never matched the actual enum key "AI") and silently routed
    // the Помощник tab to Inbox.
    for (int i = 0; i < me.keyCount(); ++i) {
        if (key.compare(QString::fromUtf8(me.key(i)), Qt::CaseInsensitive) == 0)
            return static_cast<FilterMode>(me.value(i));
    }

    // Explicit aliases for view-specific names that differ from enum keys.
    if (key.compare(QStringLiteral("board"), Qt::CaseInsensitive) == 0)
        return FilterMode::Board;
    if (key.compare(QStringLiteral("calendar"), Qt::CaseInsensitive) == 0)
        return FilterMode::Calendar;

    return FilterMode::Inbox; // safe fallback
}

QString TaskViewModel::sortBy() const { return m_sortBy; }
void TaskViewModel::setSortBy(const QString &sort) {
    if (m_sortBy != sort) {
        m_sortBy = sort;
        emit sortByChanged();
        refresh();
    }
}

bool TaskViewModel::sortReversed() const { return m_sortReversed; }
void TaskViewModel::setSortReversed(bool reversed) {
    if (m_sortReversed == reversed)
        return;
    m_sortReversed = reversed;

    // Persist directly via QSettings so the toggle survives restarts without
    // needing to plumb anything through SettingsViewModel.
    QSettings s;
    s.setValue(QStringLiteral("ui/sortReversed"), m_sortReversed);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("sort_reversed_toggled"),
                                {{QStringLiteral("reversed"), QVariant(m_sortReversed)}});

    emit sortReversedChanged();
    refresh();
}

QString TaskViewModel::groupBy() const { return m_groupBy; }
void TaskViewModel::setGroupBy(const QString &group) {
    if (m_groupBy != group) {
        m_groupBy = group;
        emit groupByChanged();
        refresh();
    }
}

int TaskViewModel::count() const { return m_tasks.size(); }

bool TaskViewModel::showCompleted() const { return m_showCompleted; }
void TaskViewModel::setShowCompleted(bool show) {
    if (m_showCompleted != show) {
        m_showCompleted = show;
        emit showCompletedChanged();
        refresh();
    }
}

// --- UndoStack wiring ---

void TaskViewModel::setUndoStack(UndoStack *stack)
{
    m_undoStack = stack;
}

void TaskViewModel::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
}

// --- Task CRUD ---

QString TaskViewModel::addTask(const QString &content, const QString &projectId,
                                int priority, const QString &dueString,
                                const QStringList &labels)
{
    QString pid = projectId;
    if (pid.isEmpty()) {
        if (!m_currentProjectId.isEmpty())
            pid = m_currentProjectId;
        else if (auto *inbox = m_store->inboxProject())
            pid = inbox->id();
    }

    // ITER3 #16: if pid is still empty (0-project cold start or corrupted state.json),
    // auto-create an Inbox project so the task is never silently invisible.
    if (pid.isEmpty()) {
        qWarning("TaskViewModel::addTask — no project available; auto-creating Inbox");
        Project *autoInbox = m_store->addProject(QStringLiteral("Inbox"));
        if (autoInbox) {
            autoInbox->setIsInbox(true);
            pid = autoInbox->id();
        } else {
            qWarning("TaskViewModel::addTask — failed to auto-create Inbox; task dropped");
            return {};
        }
    }

    Task *task = m_store->addTask(content, pid);
    if (!task) return {};

    // Track which fields VM mutated past addTask's initialFields so the
    // per-field _meta stamp covers them too. Without this a concurrent peer
    // edit on labels/section_id/due wins by empty-local-meta fallback in
    // Merger (the freshly-added side has no stamp for the field).
    QStringList mutatedFields;
    mutatedFields << QStringLiteral("priority"); // setPriority always runs

    task->setPriority(priority);
    task->setLabels(labels);
    mutatedFields << QStringLiteral("labels");

    if (!m_currentSectionId.isEmpty()) {
        task->setSectionId(m_currentSectionId);
        mutatedFields << QStringLiteral("section_id");
    }

    if (!dueString.isEmpty()) {
        const QString lang = m_store->setting(QLatin1String(SettingsKeys::Language)).toString();
        DateParseResult result = m_dateParser->parse(dueString, lang.isEmpty() ? QStringLiteral("en") : lang);
        if (result.valid) {
            task->setDueDate(result.date);
            if (!result.datetime.isEmpty())
                task->setDueDatetime(result.datetime);
            task->setDueString(result.humanString);
            task->setDueIsRecurring(result.isRecurring);
            // B-4.60 follow-up: persist RRULE so RecurrenceEngine can use the
            // primary RRULE path instead of the legacy humanString fallback.
            task->setDueRecurrenceRule(result.recurrenceRule);
            mutatedFields << QStringLiteral("due");
        }
    }

    m_store->markTaskFieldsModified(task->id(), mutatedFields);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_add"),
                                {{QStringLiteral("has_due"), !dueString.isEmpty()},
                                 {QStringLiteral("priority"), priority}});

    emit taskAdded(task->id());
    return task->id();
}

bool TaskViewModel::updateTask(const QString &id, const QString &field, const QVariant &value)
{
    Task *task = m_store->taskById(id);
    if (!task) return false;

    // The JSON (snake_case) name of the field actually mutated, for the
    // per-field CRDT stamp below. Keep in sync with each branch.
    QString jsonField;

    if (field == QStringLiteral("content"))                 { task->setContent(value.toString());            jsonField = QStringLiteral("content"); }
    else if (field == QStringLiteral("description"))        { task->setDescription(value.toString());        jsonField = QStringLiteral("description"); }
    else if (field == QStringLiteral("priority"))           { task->setPriority(value.toInt());              jsonField = QStringLiteral("priority"); }
    // labels is an OR-Set field; per-field LWW stamping is a degraded fallback
    // until DataStore::setTaskLabels uses CrdtMeta::setOrSetElements directly.
    // For now stamping the top-level "labels" key still beats not stamping at
    // all — the Merger's OR-Set branch only fires when local/remote already
    // carry OrsElement payloads, otherwise it falls back to per-field LWW.
    else if (field == QStringLiteral("labels"))             { task->setLabels(value.toStringList());         jsonField = QStringLiteral("labels"); }
    else if (field == QStringLiteral("order"))              { task->setOrder(value.toInt());                 jsonField = QStringLiteral("order"); }
    else if (field == QStringLiteral("dayOrder"))           { task->setDayOrder(value.toInt());              jsonField = QStringLiteral("day_order"); }
    else if (field == QStringLiteral("deadline"))           { task->setDeadline(value.toString());           jsonField = QStringLiteral("deadline"); }
    // duration_amount / duration_unit are NOT separate JSON keys — Task::toJson
    // emits `"duration": {...}` as one nested object. Stamp the parent key.
    else if (field == QStringLiteral("durationAmount"))     { task->setDurationAmount(value.toInt());        jsonField = QStringLiteral("duration"); }
    else if (field == QStringLiteral("durationUnit"))       { task->setDurationUnit(value.toString());       jsonField = QStringLiteral("duration"); }
    else if (field == QStringLiteral("color"))              { task->setColor(value.toString());              jsonField = QStringLiteral("color"); }
    else if (field == QStringLiteral("isPinned"))           { task->setIsPinned(value.toBool());             jsonField = QStringLiteral("is_pinned"); }
    else if (field == QStringLiteral("estimateMinutes"))    { task->setEstimateMinutes(value.toInt());       jsonField = QStringLiteral("estimate_minutes"); }
    // ITER2 #1: projectId/sectionId moves were silently dropped by the editor.
    // Delegate to moveTask which also stamps CRDT meta and refreshes the model.
    else if (field == QStringLiteral("projectId")) {
        return moveTask(id, value.toString(), task->sectionId());
    }
    else if (field == QStringLiteral("sectionId")) {
        return moveTask(id, task->projectId(), value.toString());
    }
    else return false;

    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(id, {jsonField});
    emit taskChanged(id);

    int idx = m_tasks.indexOf(task);
    if (idx >= 0)
        emit dataChanged(index(idx), index(idx));

    return true;
}

bool TaskViewModel::completeTask(const QString &id)
{
    Task *task = m_store->taskById(id);
    if (!task) return false;

    // Recurring tasks: advance the due date instead of marking completed.
    if (task->dueIsRecurring() && !task->dueString().isEmpty()) {
        // Iter-night-18: derive the recurrence base from BOTH dueDate AND
        // dueDatetime. Pre-fix this only consulted dueDate — so a recurring
        // task arriving via sync or import as datetime-only
        // (`dueDate=""`, `dueDatetime="2026-05-25T14:00:00"`) defaulted to
        // base=today and `advance(rule, today)` returned the next
        // occurrence relative to "now" rather than the original due date.
        //
        // Concrete drift example: every-3-days task originally due May 25,
        // completed on May 27 → pre-fix advances to May 30 (today + 3),
        // post-fix advances to May 28 (orig + 3). On every late completion
        // the recurrence schedule slid further from the user's intent;
        // over a few cycles the task could end up days out of phase.
        QDate currentDate;
        const QString currentDateStr = task->dueDate();
        if (!currentDateStr.isEmpty()) {
            currentDate = QDate::fromString(currentDateStr,
                                            QStringLiteral("yyyy-MM-dd"));
        } else if (!task->dueDatetime().isEmpty()) {
            const QDateTime dt = QDateTime::fromString(task->dueDatetime(),
                                                       Qt::ISODate);
            if (dt.isValid()) currentDate = dt.date();
        }
        const QDate base = currentDate.isValid() ? currentDate : QDate::currentDate();

        // B-4.60 follow-up: prefer RRULE-based advance; fall back to humanString
        // for legacy tasks where recurrence_rule wasn't captured at creation time.
        const QString recurInput = task->dueRecurrenceRule().isEmpty()
            ? task->dueString()
            : task->dueRecurrenceRule();
        const QDate nextDate = RecurrenceEngine::advance(recurInput, base);
        if (nextDate.isValid()) {
            task->setDueDate(nextDate.toString(QStringLiteral("yyyy-MM-dd")));

            // Preserve time-of-day if the original due had one.
            const QString existingDt = task->dueDatetime();
            if (!existingDt.isEmpty()) {
                const QDateTime old = QDateTime::fromString(existingDt, Qt::ISODate);
                if (old.isValid()) {
                    QDateTime newDt(nextDate, old.time(), old.timeRepresentation());
                    task->setDueDatetime(newDt.toString(Qt::ISODate));
                }
            }

            task->setUpdatedAt(QDateTime::currentDateTimeUtc());

            // Stamp the per-field CRDT meta for "due" so a concurrent peer
            // edit doesn't silently revert the advanced date in Merger LWW
            // fallback. markTaskFieldsModified emits dataChanged + tasksChanged
            // for autosave + UI; remindersChanged is still needed separately so
            // the scheduler picks up the new due_date.
            const auto reminders = m_store->remindersForTask(id);
            if (!reminders.isEmpty())
                emit m_store->remindersChanged();
            m_store->markTaskFieldsModified(id, {QStringLiteral("due")});

            emit taskChanged(id);

            int idx = m_tasks.indexOf(task);
            if (idx >= 0)
                emit dataChanged(index(idx), index(idx));
            return true;
        }
        // If we couldn't advance (unparseable rule, UNTIL exhausted, garbage
        // dueString from a legacy import), fall through to standard
        // completion — the task IS being completed and the user needs to
        // be able to undo it.
    }

    // Iter-night-13: register undo right before the standard-completion
    // mutation, NOT gated on `!task->dueIsRecurring()` at the top of the
    // function. Pre-fix, the top gate locked out the undo entry for any
    // recurring task — including the recurring-advance fall-through above,
    // where the task ends up fully completed with no Ctrl+Z to recover. The
    // most user-visible path is RRULE with UNTIL: when the last legitimate
    // occurrence is completed, `advance` returns invalid → fall through →
    // task completes silently with no undo. Imported tasks with unparseable
    // dueString hit the same path.
    if (m_undoStack) {
        m_undoStack->push(QStringLiteral("Задача завершена"), [this, id]() {
            uncompleteTask(id);
        });
    }

    task->setIsCompleted(true);
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // Mutating Task fields directly bypasses DataStore's CRUD signals — the
    // store offers an explicit "I touched this entity" notifier so we don't
    // have to break encapsulation by emitting its private signals.
    m_store->markTaskFieldsModified(id, {QStringLiteral("is_completed"),
                                          QStringLiteral("completed_at")});
    emit taskChanged(id);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_complete"), {});

    return true;
}

bool TaskViewModel::uncompleteTask(const QString &id)
{
    Task *task = m_store->taskById(id);
    if (!task) return false;

    if (m_undoStack) {
        m_undoStack->push(QStringLiteral("Задача восстановлена"), [this, id]() {
            completeTask(id);
        });
    }

    task->setIsCompleted(false);
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(id, {QStringLiteral("is_completed"),
                                          QStringLiteral("completed_at")});
    emit taskChanged(id);
    return true;
}

bool TaskViewModel::deleteTask(const QString &id)
{
    if (m_undoStack) {
        Task *task = m_store->taskById(id);
        if (task) {
            // iter-night-5: delegate to DataStore::restoreTask so the undo
            // recovers the WHOLE descendant subtree, including reminders and
            // comments. The old lambda only flipped the root task's
            // is_deleted flag directly, leaving every child / grandchild /
            // their reminders / their comments tombstoned forever (since
            // they were tombstoned with the deleteTask's cascade timestamp
            // but the root's updatedAt now diverges from it, so a manual
            // restoreTask later can't find them either). After iter-night-4
            // made removeTask cascade depth-N, the matching cascade-restore
            // in restoreTask is the only correct undo path.
            m_undoStack->push(QStringLiteral("Задача удалена"), [this, id]() {
                if (m_store->restoreTask(id))
                    emit taskChanged(id);
            });
        }
    }

    bool ok = m_store->removeTask(id);
    if (ok) emit taskRemoved(id);
    return ok;
}

bool TaskViewModel::moveTask(const QString &id, const QString &projectId, const QString &sectionId)
{
    Task *task = m_store->taskById(id);
    if (!task) return false;

    const QString oldProject = task->projectId();
    const QString oldSection = task->sectionId();
    const bool crossProject  = (projectId != oldProject);

    if (m_undoStack) {
        m_undoStack->push(QStringLiteral("Задача перемещена"), [this, id, oldProject, oldSection]() {
            moveTask(id, oldProject, oldSection);
        });
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    task->setProjectId(projectId);
    task->setSectionId(sectionId);
    task->setUpdatedAt(now);
    m_store->markTaskFieldsModified(id, {QStringLiteral("project_id"),
                                          QStringLiteral("section_id")});
    emit taskChanged(id);

    // Q3.08-C: when moving across projects, cascade the new projectId across
    // the entire descendant subtree so subtasks don't become orphans (their
    // parent link still pointed at the moved task, but they kept the OLD
    // projectId — invisible in both project views).
    //
    // Sections belong to a single project, so a cross-project move clears
    // sectionId on every cascaded subtask.
    //
    // BFS, bounded by MAX_DEPTH=100 with a visited-set to defend against
    // pre-existing cycles in the parent_id graph (the data layer never
    // enforces acyclicity at write time).
    if (crossProject) {
        constexpr int MAX_DEPTH = 100;
        QSet<QString> visited;
        visited.insert(id);
        QStringList frontier{id};
        int depth = 0;
        while (!frontier.isEmpty() && depth < MAX_DEPTH) {
            QStringList nextFrontier;
            for (const QString &parentId : frontier) {
                const auto kids = m_store->subtasks(parentId);
                for (Task *child : kids) {
                    if (!child) continue;
                    const QString cid = child->id();
                    if (visited.contains(cid)) continue;
                    visited.insert(cid);

                    child->setProjectId(projectId);
                    child->setSectionId(QString());
                    child->setUpdatedAt(now);
                    m_store->markTaskFieldsModified(cid, {QStringLiteral("project_id"),
                                                           QStringLiteral("section_id")});
                    emit taskChanged(cid);

                    nextFrontier.append(cid);
                }
            }
            frontier = nextFrontier;
            ++depth;
        }
    }

    return true;
}

bool TaskViewModel::setTaskPriority(const QString &id, int priority)
{
    return updateTask(id, QStringLiteral("priority"), priority);
}

bool TaskViewModel::setTaskLabels(const QString &id, const QStringList &labels)
{
    return updateTask(id, QStringLiteral("labels"), labels);
}

bool TaskViewModel::setTaskDue(const QString &id, const QString &dueString)
{
    Task *task = m_store->taskById(id);
    if (!task) return false;

    if (dueString.isEmpty()) {
        DueObject empty;
        task->setDue(empty);
    } else {
        const QString lang = m_store->setting(QLatin1String(SettingsKeys::Language)).toString();
        DateParseResult result = m_dateParser->parse(dueString, lang.isEmpty() ? QStringLiteral("en") : lang);
        if (result.valid) {
            task->setDueDate(result.date);
            if (!result.datetime.isEmpty())
                task->setDueDatetime(result.datetime);
            else
                task->setDueDatetime({});
            task->setDueString(result.humanString);
            task->setDueIsRecurring(result.isRecurring);
        } else {
            return false;
        }
    }

    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    // Task::toJson emits the whole due-object under a single "due" key —
    // stamping each sub-key (due_date / due_datetime / ...) writes _meta
    // entries that never line up with a real JSON key, so the Merger never
    // consults them and falls back to whole-entity LWW. Stamp the parent.
    m_store->markTaskFieldsModified(id, {QStringLiteral("due")});
    emit taskChanged(id);

    int idx = m_tasks.indexOf(task);
    if (idx >= 0)
        emit dataChanged(index(idx), index(idx));

    return true;
}

bool TaskViewModel::moveTaskOrder(const QString &id, int newIndex)
{
    // Find the dragged task in the current filtered list
    int srcIndex = -1;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i)->id() == id) {
            srcIndex = i;
            break;
        }
    }
    if (srcIndex < 0) return false;

    // Allow append-to-end: clamp to [0, size] (not size-1).
    newIndex = qBound(0, newIndex, m_tasks.size());

    // After clamping, normalise for the case where we remove the element first.
    // If we're moving forward, the effective target shifts down by one.
    const int insertAt = (newIndex > srcIndex) ? newIndex - 1 : newIndex;

    // Drop on self — no-op
    if (srcIndex == insertAt) return true;

    // Block the store's tasksChanged signal so that markTaskModified() calls
    // below do not re-enter refresh() while we are rearranging m_tasks.
    // We own both the reorder of m_tasks and the model reset ourselves.
    QSignalBlocker blocker(m_store);

    // Rearrange the in-memory list
    Task *dragged = m_tasks.takeAt(srcIndex);
    m_tasks.insert(insertAt, dragged);

    const bool isTodayView = (m_filterMode == FilterMode::Today);
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (isTodayView) {
        // In Today view, use spaced dayOrder values (10, 20, 30, ...) so
        // future inserts don't collide and tasks in other views are unaffected.
        for (int i = 0; i < m_tasks.size(); ++i) {
            Task *t = m_tasks.at(i);
            t->setDayOrder((i + 1) * 10);
            t->setUpdatedAt(now);
            // Per-field stamp so a peer doing the same day_order shuffle
            // doesn't silently lose either side's reorder in Merger fallback.
            m_store->markTaskFieldsModified(t->id(), {QStringLiteral("day_order")});
        }
    } else {
        // For non-Today views, use spaced order values (10, 20, 30, ...) for
        // visible tasks so future inserts between any two slots don't collide.
        // Tasks in this project that are not in the visible filtered list
        // (e.g. in a different section) keep their existing order values, which
        // may now overlap; shift them outside the visible range to avoid
        // collisions.
        const int visibleCount = m_tasks.size();
        const int base = (visibleCount + 1) * 10; // offset for invisible tasks

        for (int i = 0; i < m_tasks.size(); ++i) {
            Task *t = m_tasks.at(i);
            t->setOrder((i + 1) * 10);
            t->setUpdatedAt(now);
            m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order")});
        }

        // Fix up non-visible tasks with the same projectId so their order
        // values don't collide with the newly assigned visible ones.
        if (!m_currentProjectId.isEmpty()) {
            const auto projectTasks = m_store->tasksForProject(m_currentProjectId);
            int invisibleOffset = base;
            for (Task *t : projectTasks) {
                if (t->isDeleted() || t->isCompleted()) continue;
                // Skip tasks already in our visible list
                bool isVisible = false;
                for (const Task *vt : m_tasks) {
                    if (vt->id() == t->id()) { isVisible = true; break; }
                }
                if (isVisible) continue;
                t->setOrder(invisibleOffset);
                invisibleOffset += 10;
                t->setUpdatedAt(now);
                m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order")});
            }
        }
    }

    // Unblock: all store signals are restored.  Now do a single model reset
    // so QML ListView sees the new order, and emit dataChanged once so
    // FileManager's autosave debounce fires.
    beginResetModel();
    endResetModel();
    emit countChanged();

    // Persist: emit dataChanged (not tasksChanged — that would re-enter
    // refresh() and re-sort, undoing our work).  FileManager listens on
    // dataChanged and will write state.json on next debounce tick.
    emit m_store->dataChanged();

    return true;
}

QString TaskViewModel::duplicateTask(const QString &id)
{
    Task *original = m_store->taskById(id);
    if (!original || original->isDeleted()) return {};

    // Append " (копия)" suffix unless the source already carries it — repeated
    // duplications should not produce "Task (копия) (копия) (копия)".
    const QString suffix = QStringLiteral(" (копия)");
    QString newContent = original->content();
    if (!newContent.endsWith(suffix))
        newContent.append(suffix);

    Task *dup = m_store->addTask(newContent, original->projectId());
    if (!dup) return {};

    // Copy descriptive / classification fields verbatim.
    dup->setDescription(original->description());
    dup->setSectionId(original->sectionId());
    dup->setParentId(original->parentId());
    dup->setPriority(original->priority());
    dup->setLabels(original->labels());
    dup->setDeadline(original->deadline());
    dup->setDuration(original->duration());
    dup->setColor(original->color());
    dup->setEstimateMinutes(original->estimateMinutes());

    // Copy due fields but strip recurrence — a duplicate should not carry the
    // source's recurrence rule (per spec: is_recurring=false on copy).
    DueObject due = original->due();
    due.isRecurring = false;
    due.recurrenceRule.clear();
    dup->setDue(due);

    // Pinned state intentionally NOT copied — the duplicate starts unpinned.
    dup->setIsPinned(false);

    // Insert the duplicate immediately below the source: claim source.order+1
    // and shift every later sibling (same project / section / parent) down by 1.
    // Mirrors the slot-claim logic in addTaskBelow().
    const int anchorOrder = original->order();
    dup->setOrder(anchorOrder + 1);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto siblings = _siblingsOfAnchor(original);
    for (Task *sibling : siblings) {
        if (sibling == dup || sibling == original) continue;
        if (sibling->order() > anchorOrder) {
            sibling->setOrder(sibling->order() + 1);
            sibling->setUpdatedAt(now);
            m_store->markTaskFieldsModified(sibling->id(), {QStringLiteral("order")});
        }
    }

    // dup is a freshly added task; stamping the fields we mutated past
    // addTask's default ones so the merger can resolve concurrent edits.
    m_store->markTaskFieldsModified(dup->id(),
        {QStringLiteral("description"), QStringLiteral("section_id"),
         QStringLiteral("parent_id"), QStringLiteral("labels"),
         QStringLiteral("deadline"), QStringLiteral("duration"),
         QStringLiteral("color"), QStringLiteral("estimate_minutes"),
         QStringLiteral("due"), QStringLiteral("is_pinned"),
         QStringLiteral("order")});

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_duplicated"),
                                {{QStringLiteral("source_id"), id}});

    emit taskAdded(dup->id());
    return dup->id();
}

// ── addTaskAbove / addTaskBelow ───────────────────────────────────────────────
// Both helpers share the same structure:
//  1. Resolve the anchor task.
//  2. Create a new task in the same project (DataStore assigns a fresh UUID and
//     appends it at the end of the project's list — order value is irrelevant at
//     creation time).
//  3. Copy section / parent from the anchor so the new task lands in the same
//     visual group.
//  4. Claim the target order slot by assigning it to the new task, then bump
//     every existing sibling that occupies that slot or later by +1, keeping the
//     whole sibling list gap-free without touching unrelated tasks.
//  5. Stamp CRDT meta, emit taskAdded, optionally track telemetry.

QList<Task *> TaskViewModel::_siblingsOfAnchor(const Task *anchor) const
{
    if (!anchor) return {};
    QList<Task *> result;
    const auto all = m_store->allTasks();
    for (Task *t : all) {
        if (!t || t->isDeleted()) continue;
        if (t->projectId() == anchor->projectId() &&
            t->sectionId() == anchor->sectionId() &&
            t->parentId()  == anchor->parentId())
            result.append(t);
    }
    return result;
}

QString TaskViewModel::addTaskAbove(const QString &anchorId, const QString &content)
{
    Task *anchor = m_store->taskById(anchorId);
    if (!anchor) return {};

    Task *t = m_store->addTask(content.isEmpty() ? QStringLiteral(" ") : content,
                               anchor->projectId());
    if (!t) return {};

    t->setSectionId(anchor->sectionId());
    t->setParentId(anchor->parentId());

    // Claim the anchor's current slot; push every sibling at that slot or later up by 1.
    const int anchorOrder = anchor->order();
    t->setOrder(anchorOrder);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto siblings = _siblingsOfAnchor(anchor);
    for (Task *sibling : siblings) {
        if (sibling == t) continue;
        if (sibling->order() >= anchorOrder) {
            sibling->setOrder(sibling->order() + 1);
            sibling->setUpdatedAt(now);
            // Per-field stamp so the shifted order survives merge — without
            // this the peer keeps its old order and the inserted task ends
            // up at a conflicting slot after sync.
            m_store->markTaskFieldsModified(sibling->id(), {QStringLiteral("order")});
        }
    }

    m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order"),
                                               QStringLiteral("section_id"),
                                               QStringLiteral("parent_id")});
    emit taskAdded(t->id());
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_add_above"), {});
    return t->id();
}

QString TaskViewModel::addTaskBelow(const QString &anchorId, const QString &content)
{
    Task *anchor = m_store->taskById(anchorId);
    if (!anchor) return {};

    Task *t = m_store->addTask(content.isEmpty() ? QStringLiteral(" ") : content,
                               anchor->projectId());
    if (!t) return {};

    t->setSectionId(anchor->sectionId());
    t->setParentId(anchor->parentId());

    // Claim the slot immediately after the anchor; push every sibling beyond
    // the anchor (order > anchorOrder) up by 1.
    const int anchorOrder = anchor->order();
    t->setOrder(anchorOrder + 1);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto siblings = _siblingsOfAnchor(anchor);
    for (Task *sibling : siblings) {
        if (sibling == t) continue;
        if (sibling->order() > anchorOrder) {
            sibling->setOrder(sibling->order() + 1);
            sibling->setUpdatedAt(now);
            m_store->markTaskFieldsModified(sibling->id(), {QStringLiteral("order")});
        }
    }

    m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order"),
                                               QStringLiteral("section_id"),
                                               QStringLiteral("parent_id")});
    emit taskAdded(t->id());
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_add_below"), {});
    return t->id();
}

// ── moveTaskToTop / moveTaskToBottom ─────────────────────────────────────────
// Reorder within the task's sibling scope (same project + section + parent_id).
// Top: order = (min sibling order) - 1; Bottom: order = (max sibling order) + 1.
// We do NOT renormalise the whole list — leaving sparse gaps is fine and matches
// how addTaskAbove/Below claim slots.  The store's signal-blocked emit pattern
// from moveTaskOrder() is unnecessary here because exactly one task is touched.

bool TaskViewModel::moveTaskToTop(const QString &taskId)
{
    Task *t = m_store->taskById(taskId);
    if (!t || t->isDeleted()) return false;

    const auto siblings = _siblingsOfAnchor(t);
    if (siblings.isEmpty()) return false;

    int minOrder = t->order();
    bool any = false;
    for (const Task *s : siblings) {
        if (s == t) continue;
        if (!any || s->order() < minOrder) {
            minOrder = s->order();
            any = true;
        }
    }
    // Already at the top (or only sibling) — no-op success.
    if (!any) return true;
    if (t->order() <= minOrder) return true;

    t->setOrder(minOrder - 1);
    m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order")});

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_move_to_top"),
                                {{QStringLiteral("task_id"), taskId}});

    emit taskChanged(t->id());
    emit m_store->dataChanged();
    return true;
}

bool TaskViewModel::moveTaskToBottom(const QString &taskId)
{
    Task *t = m_store->taskById(taskId);
    if (!t || t->isDeleted()) return false;

    const auto siblings = _siblingsOfAnchor(t);
    if (siblings.isEmpty()) return false;

    int maxOrder = t->order();
    bool any = false;
    for (const Task *s : siblings) {
        if (s == t) continue;
        if (!any || s->order() > maxOrder) {
            maxOrder = s->order();
            any = true;
        }
    }
    if (!any) return true;
    if (t->order() >= maxOrder) return true;

    t->setOrder(maxOrder + 1);
    m_store->markTaskFieldsModified(t->id(), {QStringLiteral("order")});

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_move_to_bottom"),
                                {{QStringLiteral("task_id"), taskId}});

    emit taskChanged(t->id());
    emit m_store->dataChanged();
    return true;
}

// Wave 3 follow-up (F-6.39): create a template project from an existing one.
//
// Iter-night-7: previously this function had three structural bugs:
//   (1) `tasksForProject(projectId)` returns ONLY top-level tasks (parent_id
//       empty — see datastore.cpp:550-557). The loop also had a defensive
//       `continue` on non-empty parent_id with a comment promising
//       "they'll be re-parented below" — but there was no "below" code. So
//       every subtask was silently dropped from the template.
//   (2) `copy->setSectionId(orig->sectionId())` copied the SOURCE project's
//       section id into the new template task. Sections were not
//       duplicated, so the template's tasks referenced sections living in
//       the source project — board grouping showed them under foreign
//       section headers, and the stale FK propagated through sync.
//   (3) Subtasks that DID make it (none, given #1) carried no parent_id
//       stamping anyway.
//
// The fix:
//   • Phase A: duplicate sections, build oldSectionId → newSectionId map.
//   • Phase B: BFS the task tree from top-level downwards. Use a
//     parent-before-child queue + visited set (cycle-safe). Each task gets
//     a fresh id; parent_id is remapped through oldToNewTaskId; section_id
//     is remapped through the Phase-A map (or cleared if the source
//     section has been tombstoned).
//
QString TaskViewModel::duplicateProjectAsTemplate(const QString &projectId)
{
    Project *src = m_store->projectById(projectId);
    if (!src || src->isDeleted())
        return {};

    Project *tpl = m_store->addProject(QStringLiteral("Шаблон: ") + src->name());
    if (!tpl)
        return {};
    tpl->setColor(src->color());
    tpl->setViewStyle(src->viewStyle());
    // color/view_style are already in addProject's initialFields, but stamping
    // again refreshes the HLC so a concurrent peer edit of these fields on
    // the (just-created) template doesn't race the addProject baseline.
    m_store->markProjectFieldsModified(tpl->id(), {QStringLiteral("color"),
                                                    QStringLiteral("view_style")});

    // ── Phase A: duplicate sections ────────────────────────────────────────
    // Build old → new section id map so Phase-B task copies can remap
    // section_id correctly. Tombstoned sections are skipped (and any task
    // pointing at one ends up sectionless in the template, which matches
    // how the source project displays such an orphaned task today).
    QHash<QString, QString> oldToNewSectionId;
    for (Section *origSec : m_store->sectionsForProject(projectId)) {
        if (!origSec || origSec->isDeleted()) continue;
        Section *newSec = m_store->addSection(origSec->name(), tpl->id());
        if (!newSec) continue;
        oldToNewSectionId.insert(origSec->id(), newSec->id());
    }

    // ── Phase B: duplicate tasks, depth-N, with parent-id remapping ────────
    // BFS so a parent is always copied before its children → the child's
    // setParentId() can look its new parent up in oldToNewTaskId.
    // The visited set defences against any pre-existing parent_id cycle on
    // disk (legacy state.json files may carry one despite applyCommit
    // guards).
    QHash<QString, QString> oldToNewTaskId;
    QStringList queue;
    QSet<QString> visited;

    // Seed with top-level tasks of the source project.
    for (Task *orig : m_store->tasksForProject(projectId)) {
        if (!orig || orig->isDeleted()) continue;
        queue.append(orig->id());
    }

    while (!queue.isEmpty()) {
        const QString origId = queue.takeFirst();
        if (visited.contains(origId)) continue;
        visited.insert(origId);

        Task *orig = m_store->taskById(origId);
        if (!orig || orig->isDeleted()) continue;

        // Resolve new parent_id (empty for top-level; mapped for descendants).
        QString newParentId;
        if (!orig->parentId().isEmpty()) {
            newParentId = oldToNewTaskId.value(orig->parentId());
            // Defensive: if the original's parent was tombstoned or was
            // outside this project's task set, the new parent id is empty
            // and we'd promote the subtask to top-level. That's not what
            // the user templated — skip the copy entirely so the template
            // doesn't grow stray top-level rows that weren't in the source.
            if (newParentId.isEmpty())
                continue;
        }

        Task *copy = m_store->addTask(orig->content(), tpl->id());
        if (!copy) continue;

        if (!newParentId.isEmpty()) {
            copy->setParentId(newParentId);
            // parent_id is NOT in addTask's initialFields → stamp it so a
            // concurrent peer edit doesn't auto-win via empty-meta fallback.
            m_store->markTaskFieldsModified(copy->id(),
                {QStringLiteral("parent_id")});
        }
        copy->setDescription(orig->description());
        // Remap section_id through Phase-A map. If the source had an empty
        // section_id (or pointed at a tombstoned/foreign section), this
        // resolves to empty — task lands in the unsectioned area.
        copy->setSectionId(oldToNewSectionId.value(orig->sectionId(), QString()));
        copy->setPriority(orig->priority());
        copy->setLabels(orig->labels());
        // Intentionally NOT copying due/deadline/completedAt — templates
        // shouldn't carry stale schedules.
        // description/section_id/labels aren't in addTask's initialFields —
        // stamp them explicitly so a concurrent peer edit on the freshly-
        // duplicated copy doesn't auto-win via empty-meta fallback.
        m_store->markTaskFieldsModified(copy->id(),
            {QStringLiteral("description"), QStringLiteral("section_id"),
             QStringLiteral("priority"),    QStringLiteral("labels")});

        oldToNewTaskId.insert(origId, copy->id());

        // Enqueue children for the next BFS layer.
        for (Task *child : m_store->subtasks(origId)) {
            if (child && !child->isDeleted())
                queue.append(child->id());
        }
    }

    emit dataChanged(QModelIndex(), QModelIndex());
    return tpl->id();
}

// --- Trash (Корзина) ---

QVariantList TaskViewModel::deletedTasks() const
{
    // Walk every task (including soft-deleted ones), collect those with
    // is_deleted == true, sort by updatedAt descending, cap at 100.
    QList<Task *> deleted;
    const auto all = m_store->allTasksIncludingDeleted();
    deleted.reserve(all.size());
    for (Task *t : all) {
        if (t && t->isDeleted())
            deleted.append(t);
    }

    std::sort(deleted.begin(), deleted.end(), [](const Task *a, const Task *b) {
        return a->updatedAt() > b->updatedAt();
    });

    QVariantList result;
    const int limit = qMin(100, static_cast<int>(deleted.size()));
    result.reserve(limit);
    for (int i = 0; i < limit; ++i) {
        Task *t = deleted.at(i);
        QVariantMap m;
        m[QStringLiteral("id")] = t->id();
        m[QStringLiteral("content")] = t->content();
        m[QStringLiteral("projectId")] = t->projectId();
        Project *p = m_store->projectById(t->projectId());
        m[QStringLiteral("projectName")] = p ? p->name() : QString();
        m[QStringLiteral("priority")] = t->priority();
        m[QStringLiteral("updatedAt")] = t->updatedAt().toString(Qt::ISODate);
        result.append(m);
    }
    return result;
}

void TaskViewModel::restoreTask(const QString &taskId)
{
    // Delegate to DataStore::restoreTask which performs the cascade-restore
    // of any reminder/comment that was tombstoned by the matching removeTask
    // call (identified by shared updatedAt timestamp). DataStore emits
    // tasksChanged/dataChanged itself; we still emit taskChanged here so VM
    // listeners (badges, sidebar) see the per-task signal.
    if (!m_store->restoreTask(taskId)) return;

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_restored"), {});

    emit taskChanged(taskId);
}

void TaskViewModel::permanentlyDelete(const QString &taskId)
{
    if (!m_store->taskById(taskId)) return;

    // Hard-remove via DataStore; this also frees the QObject and emits the
    // tasks/data signals. Sync op for permanent delete is intentionally not
    // produced — there is no `task_permanent_delete` slot in the dispatch
    // table, and the soft-delete tombstone has already propagated.
    m_store->purgeTask(taskId);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_permanent_delete"), {});

    emit taskRemoved(taskId);
}

void TaskViewModel::emptyTrash()
{
    // Snapshot ids first; purgeTask mutates m_tasks under the hood.
    QStringList ids;
    const auto all = m_store->allTasksIncludingDeleted();
    for (Task *t : all) {
        if (t && t->isDeleted())
            ids.append(t->id());
    }

    int count = 0;
    for (const QString &id : ids) {
        if (m_store->purgeTask(id))
            ++count;
    }

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("trash_emptied"),
                                {{QStringLiteral("count"), count}});
    if (!ids.isEmpty())
        emit tasksRemoved(ids);
}

int TaskViewModel::trashCount() const
{
    int n = 0;
    const auto all = m_store->allTasksIncludingDeleted();
    for (const Task *t : all) {
        if (t && t->isDeleted())
            ++n;
    }
    return n;
}

QVariantList TaskViewModel::getSubtasks(const QString &parentId) const
{
    // Return QVariantMap entries (NOT QVariant::fromValue(Task*)).
    //
    // Why: QML's Repeater delegate accesses modelData.content via JS dot
    // notation. When the list element is a QVariant wrapping a QObject* the
    // dereference can silently fail in Qt 6 — the delegate spawns (the
    // checkbox circle renders) but every property access returns undefined,
    // so the user sees an empty subtask row. The subtasksOf() variant of
    // this method already returns QVariantMaps and renders correctly in
    // TaskItem.qml; mirror that shape here so EditorSubtasksSection.qml
    // also gets readable text.
    auto subs = m_store->subtasks(parentId);
    // Sort by order (mirrors subtasksOf for consistency).
    std::sort(subs.begin(), subs.end(), [](const Task *a, const Task *b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->createdAt() < b->createdAt();
    });

    QVariantList result;
    result.reserve(subs.size());
    for (auto *t : subs) {
        QVariantMap map;
        map[QStringLiteral("id")]          = t->id();
        map[QStringLiteral("content")]     = t->content();
        map[QStringLiteral("description")] = t->description();
        map[QStringLiteral("isCompleted")] = t->isCompleted();
        map[QStringLiteral("priority")]    = t->priority();
        map[QStringLiteral("dueDate")]     = t->dueDate();
        map[QStringLiteral("dueString")]   = t->dueString();
        map[QStringLiteral("parentId")]    = t->parentId();
        map[QStringLiteral("projectId")]   = t->projectId();
        map[QStringLiteral("order")]       = t->order();
        result.append(map);
    }
    return result;
}

bool TaskViewModel::hasSubtasks(const QString &taskId) const
{
    return !m_store->subtasks(taskId).isEmpty();
}

QVariantList TaskViewModel::subtasksOf(const QString &parentId) const
{
    auto subs = m_store->subtasks(parentId);
    // Sort by order, then by createdAt as tiebreak
    std::sort(subs.begin(), subs.end(), [](const Task *a, const Task *b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->createdAt() < b->createdAt();
    });

    QVariantList result;
    result.reserve(subs.size());
    for (auto *t : subs) {
        QVariantMap map;
        map[QStringLiteral("id")]          = t->id();
        map[QStringLiteral("content")]     = t->content();
        map[QStringLiteral("isCompleted")] = t->isCompleted();
        map[QStringLiteral("priority")]    = t->priority();
        map[QStringLiteral("dueDate")]     = t->dueDate();
        map[QStringLiteral("dueString")]   = t->dueString();
        map[QStringLiteral("parentId")]    = t->parentId();
        result.append(map);
    }
    return result;
}

QString TaskViewModel::addSubtask(const QString &parentId, const QString &content)
{
    if (parentId.isEmpty() || content.trimmed().isEmpty())
        return {};

    Task *parent = m_store->taskById(parentId);
    if (!parent) return {};

    Task *task = m_store->addTask(content.trimmed(), parent->projectId());
    if (!task) return {};

    task->setParentId(parentId);
    task->setSectionId(parent->sectionId());
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(task->id(), {QStringLiteral("parent_id"),
                                                  QStringLiteral("section_id")});

    emit taskAdded(task->id());
    return task->id();
}

bool TaskViewModel::convertToSubtask(const QString &taskId, const QString &newParentId)
{
    Task *task = m_store->taskById(taskId);
    if (!task) return false;
    // Prevent circular: walk the parent chain from newParentId upward; if
    // we encounter taskId, the reparenting would create a cycle.
    QString cur = newParentId;
    while (!cur.isEmpty()) {
        if (cur == taskId) return false; // would form a cycle
        const Task *parent = m_store->taskById(cur);
        if (!parent) break;
        cur = parent->parentId();
    }

    task->setParentId(newParentId);
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(taskId, {QStringLiteral("parent_id")});
    emit taskChanged(taskId);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_convert_to_subtask"), {});

    return true;
}

bool TaskViewModel::promoteToTopLevel(const QString &taskId)
{
    Task *task = m_store->taskById(taskId);
    if (!task) return false;

    task->setParentId(QString());
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(taskId, {QStringLiteral("parent_id")});
    emit taskChanged(taskId);
    return true;
}

bool TaskViewModel::promoteTask(const QString &taskId)
{
    Task *task = m_store->taskById(taskId);
    if (!task) return false;

    task->setParentId(QString());
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(taskId, {QStringLiteral("parent_id")});
    emit taskChanged(taskId);

    if (m_telemetry) {
        QVariantMap props;
        props[QStringLiteral("task_id")] = taskId;
        m_telemetry->trackEvent(QStringLiteral("task_promoted"), props);
    }
    return true;
}

bool TaskViewModel::demoteTask(const QString &taskId, const QString &parentId)
{
    if (taskId.isEmpty() || parentId.isEmpty() || taskId == parentId)
        return false;

    Task *task = m_store->taskById(taskId);
    if (!task) return false;

    Task *newParent = m_store->taskById(parentId);
    if (!newParent) return false;

    // Same-project guard — never split a parent/child pair across projects.
    if (task->projectId() != newParent->projectId())
        return false;

    // Cycle guard: walk parent chain from newParent upward; if we hit
    // taskId, the reparenting would form a cycle.
    {
        QString cur = newParent->parentId();
        while (!cur.isEmpty()) {
            if (cur == taskId) return false;
            const Task *p = m_store->taskById(cur);
            if (!p) break;
            cur = p->parentId();
        }
    }

    // Defence-in-depth against deep descendant cycles: if newParent is
    // already a transitive descendant of taskId, reject.
    {
        // BFS from taskId's children downward.
        QList<QString> stack;
        const auto direct = m_store->subtasks(taskId);
        for (const Task *c : direct)
            if (c) stack.append(c->id());
        while (!stack.isEmpty()) {
            const QString cid = stack.takeLast();
            if (cid == parentId) return false;
            const auto deeper = m_store->subtasks(cid);
            for (const Task *c : deeper)
                if (c) stack.append(c->id());
        }
    }

    task->setParentId(parentId);
    task->setUpdatedAt(QDateTime::currentDateTimeUtc());
    m_store->markTaskFieldsModified(taskId, {QStringLiteral("parent_id")});
    emit taskChanged(taskId);

    if (m_telemetry) {
        QVariantMap props;
        props[QStringLiteral("task_id")]   = taskId;
        props[QStringLiteral("parent_id")] = parentId;
        m_telemetry->trackEvent(QStringLiteral("task_demoted"), props);
    }
    return true;
}

QVariantList TaskViewModel::candidateParents(const QString &taskId) const
{
    QVariantList result;
    if (taskId.isEmpty()) return result;

    const Task *self = m_store->taskById(taskId);
    if (!self) return result;
    const QString projectId = self->projectId();

    // Collect all transitive descendants of self so the picker excludes
    // them — demoting to a descendant would create a cycle.
    QSet<QString> descendants;
    {
        QList<QString> stack;
        const auto direct = m_store->subtasks(taskId);
        for (const Task *c : direct)
            if (c) stack.append(c->id());
        while (!stack.isEmpty()) {
            const QString cid = stack.takeLast();
            if (descendants.contains(cid)) continue;
            descendants.insert(cid);
            const auto deeper = m_store->subtasks(cid);
            for (const Task *c : deeper)
                if (c) stack.append(c->id());
        }
    }

    const auto all = m_store->allTasks();
    for (const Task *t : all) {
        if (!t) continue;
        if (t->isDeleted()) continue;
        if (t->id() == taskId) continue;
        if (t->projectId() != projectId) continue;
        // Top-level-only rule: candidate must currently have no parent.
        if (!t->parentId().isEmpty()) continue;
        if (descendants.contains(t->id())) continue;

        QVariantMap m;
        m[QStringLiteral("id")]      = t->id();
        m[QStringLiteral("content")] = t->content();
        result.append(m);
    }
    return result;
}

Task *TaskViewModel::taskAt(int index) const
{
    if (index < 0 || index >= m_tasks.size()) return nullptr;
    return m_tasks.at(index);
}

int TaskViewModel::indexOfTask(const QString &id) const
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i)->id() == id) return i;
    }
    return -1;
}

// --- Private ---

void TaskViewModel::onTasksChanged()
{
    // Q3.24: start (or restart) the debounce timer instead of calling refresh()
    // directly.  Bursts of tasksChanged signals within one 30 ms window produce
    // a single refresh() call rather than N calls.
    m_refreshDebounce.start();
}

void TaskViewModel::refresh()
{
    // Full reset whenever the filter mode itself changed (the structure of the
    // list is fundamentally different) or on the very first call.
    const bool modeChanged = (m_filterMode != m_lastFilterMode);
    m_lastFilterMode = m_filterMode;

    // Non-task views (Settings, Productivity, Notifications, AI, Trash):
    // early-return so we do NOT clear m_tasks.  The QML view for these modes
    // doesn't bind to the task list, so leaving stale data is fine and avoids
    // a flash of an empty list when the user navigates back. The Trash view
    // pulls its data via deletedTasks() directly rather than through m_tasks.
    if (m_filterMode == FilterMode::Settings ||
        m_filterMode == FilterMode::Productivity ||
        m_filterMode == FilterMode::Notifications ||
        m_filterMode == FilterMode::AI ||
        m_filterMode == FilterMode::Trash) {
        // Non-task modes: leave m_tasks untouched to avoid clearing the list
        // that QML may navigate back to.  filterModeChanged is already emitted
        // by setFilterMode() before refresh() is called.
        return;
    }

    // Fetch and sort the new task list. sortTasks() operates on m_tasks, so we
    // temporarily swap, sort, then restore for the diff pass.
    QList<Task *> newTasks = fetchTasks();
    {
        QList<Task *> saved = m_tasks;
        m_tasks = newTasks;
        sortTasks();
        newTasks = m_tasks;
        m_tasks = saved;
    }

    // For mode changes or large structural differences, do a full reset.
    if (modeChanged) {
        beginResetModel();
        m_tasks = newTasks;
        endResetModel();
        emit countChanged();
        return;
    }

    // Diff-based update: compute removes, then inserts, then role changes.
    // Build an id→index map for the new list.
    QHash<QString, int> newIndex;
    newIndex.reserve(newTasks.size());
    for (int i = 0; i < newTasks.size(); ++i)
        newIndex.insert(newTasks.at(i)->id(), i);

    // Remove tasks that are no longer present (iterate in reverse to keep indices stable).
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (!newIndex.contains(m_tasks.at(i)->id())) {
            beginRemoveRows(QModelIndex(), i, i);
            m_tasks.removeAt(i);
            endRemoveRows();
        }
    }

    // Build current id→index after removals.
    QHash<QString, int> curIndex;
    curIndex.reserve(m_tasks.size());
    for (int i = 0; i < m_tasks.size(); ++i)
        curIndex.insert(m_tasks.at(i)->id(), i);

    // Insert tasks that are new.
    for (int i = 0; i < newTasks.size(); ++i) {
        const QString &nid = newTasks.at(i)->id();
        if (!curIndex.contains(nid)) {
            beginInsertRows(QModelIndex(), i, i);
            m_tasks.insert(i, newTasks.at(i));
            endInsertRows();
            // Rebuild curIndex after insertion.
            curIndex.clear();
            for (int j = 0; j < m_tasks.size(); ++j)
                curIndex.insert(m_tasks.at(j)->id(), j);
        }
    }

    // Emit dataChanged for rows whose pointer changed (task mutated externally).
    // Also handle ordering: if the final list differs in order, fall back to reset.
    if (m_tasks.size() != newTasks.size()) {
        // Should not happen after the insert/remove passes above, but guard anyway.
        // newTasks is already sorted; assign directly.
        beginResetModel();
        m_tasks = newTasks;
        endResetModel();
    } else {
        bool orderDiffers = false;
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks.at(i)->id() != newTasks.at(i)->id()) {
                orderDiffers = true;
                break;
            }
        }
        if (orderDiffers) {
            beginResetModel();
            m_tasks = newTasks;
            endResetModel();
        } else {
            // Q3.24: emit dataChanged only for rows where the Task pointer
            // actually differs (i.e. the object was replaced by a new allocation)
            // or where relevant scalar fields may have changed.  A full-range
            // dataChanged(0, N-1) forces QML to re-fetch every role for every
            // visible delegate on every task mutation — O(N) binding evaluations
            // per edit.  We scan the list and batch-emit contiguous dirty runs
            // instead, which is O(dirty_rows) ≈ O(1) for a typical single-task edit.
            // NOTE: because DataStore mutates Task objects in-place (no reallocation)
            // the pointer itself never changes, so we cannot use pointer equality
            // to detect mutations.  Instead we use the diffed id list to confirm
            // ordering is identical, then fall back to a single-row emit for the
            // task that just changed (tracked via the markTaskModified id, which
            // we don't have here).  Emitting the full range is still necessary for
            // bulk operations (sync, import).  To avoid the per-cell QML cost we
            // limit to just the roles that are commonly mutated (all roles for now,
            // but expressed as a targeted single call so QML knows only to check
            // the changed range, not rebuild delegates).
            if (!m_tasks.isEmpty())
                emit dataChanged(index(0), index(m_tasks.size() - 1));
        }
    }

    emit countChanged();
}

QList<Task *> TaskViewModel::fetchTasks() const
{
    QList<Task *> result;

    if (m_filterMode == FilterMode::Inbox) {
        auto *inbox = m_store->inboxProject();
        if (inbox) result = m_store->tasksForProject(inbox->id());
    } else if (m_filterMode == FilterMode::Today) {
        result = m_store->tasksForToday();
        // Also include overdue
        const auto overdue = m_store->overdueTasks();
        for (auto *t : overdue) {
            if (!result.contains(t)) result.append(t);
        }
    } else if (m_filterMode == FilterMode::Upcoming) {
        // All tasks with a due date in the future
        const auto all = m_store->allTasks();
        const QDate today = QDate::currentDate();
        for (auto *t : all) {
            if (!t->isCompleted() && !t->dueDate().isEmpty()) {
                QDate d = QDate::fromString(t->dueDate(), QStringLiteral("yyyy-MM-dd"));
                if (d >= today) result.append(t);
            }
        }
    } else if (m_filterMode == FilterMode::Project
               || m_filterMode == FilterMode::Board) {
        // Board and Project share the same task source; rendering differences
        // are handled by the QML views that bind to filterMode.
        if (!m_currentSectionId.isEmpty())
            result = m_store->tasksForSection(m_currentSectionId);
        else if (!m_currentProjectId.isEmpty())
            result = m_store->tasksForProject(m_currentProjectId);
    } else if (m_filterMode == FilterMode::Calendar) {
        // Q3.27: when a project is active, calendar shows that project's tasks
        // (legacy per-project calendar view from ProjectView). When NO project
        // is active, Calendar is the "calendar display of Upcoming" — same
        // dataset as FilterMode::Upcoming. This lets the Upcoming top bar
        // toggle between list (Upcoming) and calendar (Calendar) without
        // losing the task set.
        if (!m_currentProjectId.isEmpty()) {
            if (!m_currentSectionId.isEmpty())
                result = m_store->tasksForSection(m_currentSectionId);
            else
                result = m_store->tasksForProject(m_currentProjectId);
        } else {
            const auto all = m_store->allTasks();
            const QDate today = QDate::currentDate();
            for (auto *t : all) {
                if (!t->isCompleted() && !t->dueDate().isEmpty()) {
                    QDate d = QDate::fromString(t->dueDate(),
                                                 QStringLiteral("yyyy-MM-dd"));
                    if (d >= today) result.append(t);
                }
            }
        }
    } else if (m_filterMode == FilterMode::Label) {
        const auto all = m_store->allTasks();
        const QString labelFilter = m_currentFilter.toLower();
        for (auto *t : all) {
            if (!t->isCompleted()) {
                for (const auto &l : t->labels()) {
                    if (l.compare(labelFilter, Qt::CaseInsensitive) == 0) {
                        result.append(t);
                        break;
                    }
                }
            }
        }
    } else if (m_filterMode == FilterMode::Filter) {
        if (!m_currentFilter.isEmpty()) {
            FilterEvaluator eval(m_store);
            result = eval.evaluate(m_currentFilter);
        }
    } else if (m_filterMode == FilterMode::Completed) {
        result = m_store->completedTasks();
    }

    // Filter out completed unless requested
    if (!m_showCompleted && m_filterMode != FilterMode::Completed) {
        result.erase(std::remove_if(result.begin(), result.end(),
                     [](Task *t) { return t->isCompleted(); }), result.end());
    }

    // Filter to top-level tasks only (no subtasks in main list)
    if (m_filterMode != FilterMode::Filter) {
        result.erase(std::remove_if(result.begin(), result.end(),
                     [](Task *t) { return !t->parentId().isEmpty(); }), result.end());
    }

    return result;
}

// --- Hover / selection (keyboard navigation) ---

QString TaskViewModel::hoveredTaskId() const { return m_hoveredTaskId; }

void TaskViewModel::setHoveredTaskId(const QString &id)
{
    if (m_hoveredTaskId != id) {
        m_hoveredTaskId = id;
        emit hoveredTaskIdChanged();
    }
}

QObject *TaskViewModel::selectedTask() const { return m_selectedTask; }

void TaskViewModel::setSelectedTask(QObject *task)
{
    if (m_selectedTask != task) {
        m_selectedTask = task;
        emit selectedTaskChanged();
    }
}

// --- Selection ---

QStringList TaskViewModel::selectedIds() const
{
    return QStringList(m_selectedIds.begin(), m_selectedIds.end());
}

int TaskViewModel::selectionCount() const
{
    return m_selectedIds.size();
}

void TaskViewModel::selectTask(const QString &id)
{
    if (!m_selectedIds.contains(id)) {
        m_selectedIds.append(id);
        // Record the first selected id as the anchor for shift-click ranges.
        if (m_selectedIds.size() == 1)
            m_selectionAnchorId = id;
        emit selectionChanged();
    }
}

void TaskViewModel::deselectTask(const QString &id)
{
    if (m_selectedIds.removeOne(id))
        emit selectionChanged();
}

void TaskViewModel::openTaskById(const QString &id)
{
    // selectedTask is the QML-facing pointer that drives the right-pane editor;
    // setting it (rather than touching the multi-select set) is the canonical
    // way to open the editor for a single task.
    auto *t = m_store ? m_store->taskById(id) : nullptr;
    if (t)
        setSelectedTask(t);
}

void TaskViewModel::toggleSelection(const QString &id)
{
    if (m_selectedIds.contains(id))
        m_selectedIds.removeOne(id);
    else {
        m_selectedIds.append(id);
        if (m_selectedIds.size() == 1)
            m_selectionAnchorId = id;
    }
    emit selectionChanged();
}

void TaskViewModel::selectRange(const QString &fromId, const QString &toId)
{
    // Use the stored anchor if available, otherwise fall back to fromId.
    const QString anchorId = m_selectionAnchorId.isEmpty() ? fromId : m_selectionAnchorId;
    int fromIdx = indexOfTask(anchorId);
    int toIdx   = indexOfTask(toId);
    if (fromIdx < 0 || toIdx < 0) {
        selectTask(toId);
        return;
    }
    if (fromIdx > toIdx) std::swap(fromIdx, toIdx);
    for (int i = fromIdx; i <= toIdx; ++i) {
        const QString &rid = m_tasks.at(i)->id();
        if (!m_selectedIds.contains(rid))
            m_selectedIds.append(rid);
    }
    emit selectionChanged();
}

void TaskViewModel::clearSelection()
{
    if (!m_selectedIds.isEmpty()) {
        m_selectedIds.clear();
        m_selectionAnchorId.clear();
        emit selectionChanged();
    }
}

bool TaskViewModel::isSelected(const QString &id) const
{
    return m_selectedIds.contains(id);
}


// --- Bulk operations ---

void TaskViewModel::bulkComplete()
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_complete"),
                                {{QStringLiteral("count"), ids.size()}});

    // Push a single bulk undo entry BEFORE completing individual tasks (which
    // would themselves push per-task entries).  We bypass completeTask's own
    // undo push by calling the store directly for the bulk case.
    if (m_undoStack) {
        // Collect only the ids that will actually transition to completed:
        // skip tasks that are already completed and skip recurring tasks
        // (completing a recurring task advances its due date — there is no
        // simple "mark completed = false" undo for that path).
        QStringList transitioned;
        transitioned.reserve(ids.size());
        for (const QString &id : ids) {
            Task *t = m_store->taskById(id);
            if (!t || t->isCompleted() || t->dueIsRecurring()) continue;
            transitioned.append(id);
        }

        if (!transitioned.isEmpty()) {
            const int n = transitioned.size();
            const QString label = QStringLiteral("Завершено %1 %2")
                                      .arg(n)
                                      .arg(pluralTasks(n));
            m_undoStack->push(label, [this, transitioned]() {
                for (const QString &id : transitioned) {
                    Task *t = m_store->taskById(id);
                    if (!t) continue;
                    t->setIsCompleted(false);
                    t->setUpdatedAt(QDateTime::currentDateTimeUtc());
                    m_store->markTaskFieldsModified(id, {QStringLiteral("is_completed"),
                                          QStringLiteral("completed_at")});
                    emit taskChanged(id);
                }
            });
        }

        // Complete without pushing individual undo entries.
        // Recurring tasks are handled by completeTask's own advance logic;
        // call it directly for those to avoid duplicating the recurrence logic here.
        for (const QString &id : ids) {
            Task *t = m_store->taskById(id);
            if (!t) continue;
            if (t->dueIsRecurring()) {
                // Use completeTask for recurring tasks — it advances the due
                // date correctly. completeTask skips pushing an undo entry for
                // recurring tasks (see its guard), so no double-push occurs.
                completeTask(id);
            } else {
                t->setIsCompleted(true);
                t->setUpdatedAt(QDateTime::currentDateTimeUtc());
                m_store->markTaskFieldsModified(id, {QStringLiteral("is_completed"),
                                          QStringLiteral("completed_at")});
                emit taskChanged(id);
            }
        }
        clearSelection();
        return;
    }

    for (const QString &id : ids)
        completeTask(id);
    clearSelection();
}

void TaskViewModel::bulkDelete()
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_delete"),
                                {{QStringLiteral("count"), ids.size()}});

    if (m_undoStack) {
        // iter-night-5: capture only ids, undo through restoreTask for the
        // depth-N cascade restore (subtasks/reminders/comments). The old
        // path flipped is_deleted on the root only — children and their
        // reminders were forever lost.
        QStringList undoIds;
        undoIds.reserve(ids.size());
        for (const QString &id : ids) {
            if (m_store->taskById(id))
                undoIds.append(id);
        }
        const int n = undoIds.size();
        const QString label = QStringLiteral("Удалено %1 %2")
                                  .arg(n)
                                  .arg(pluralTasks(n));
        m_undoStack->push(label, [this, undoIds]() {
            for (const QString &id : undoIds) {
                if (m_store->restoreTask(id))
                    emit taskChanged(id);
            }
        });
        // Delete without pushing individual undo entries.
        // ITER3 #1: don't emit taskRemoved per id — that produced N snackbars
        // stacked. Emit once after the loop with the bulk-aware tasksRemoved
        // signal, which Main.qml dispatches to a single snackbar.
        for (const QString &id : ids)
            m_store->removeTask(id);
        emit tasksRemoved(ids);
        clearSelection();
        return;
    }

    for (const QString &id : ids)
        deleteTask(id);
    clearSelection();
}

void TaskViewModel::bulkMoveToProject(const QString &projectId)
{
    const QStringList ids = selectedIds();
    for (const QString &id : ids)
        moveTask(id, projectId, {});
    clearSelection();
}

void TaskViewModel::bulkSetPriority(int priority)
{
    const QStringList ids = selectedIds();
    for (const QString &id : ids) {
        Task *task = m_store->taskById(id);
        if (!task) continue;
        task->setPriority(priority);
        task->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("priority")});
        emit taskChanged(id);
    }
    clearSelection();
}

void TaskViewModel::bulkAddLabel(const QString &label)
{
    const QStringList ids = selectedIds();
    for (const QString &id : ids) {
        Task *task = m_store->taskById(id);
        if (!task) continue;
        QStringList labels = task->labels();
        if (!labels.contains(label, Qt::CaseInsensitive))
            labels.append(label);
        task->setLabels(labels);
        task->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("labels")});
        emit taskChanged(id);
    }
    clearSelection();
}

void TaskViewModel::bulkSetPinned(bool pinned)
{
    if (m_selectedIds.isEmpty()) return;
    const QStringList ids = QStringList(m_selectedIds.begin(), m_selectedIds.end());
    int count = 0;
    for (const QString &id : ids) {
        Task *t = m_store->taskById(id);
        if (!t || t->isDeleted()) continue;
        if (t->isPinned() == pinned) continue;
        t->setIsPinned(pinned);
        t->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("is_pinned")});
        emit taskChanged(id);
        count++;
    }
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_pin"),
                                {{QStringLiteral("count"), count},
                                 {QStringLiteral("pinned"), pinned}});
    clearSelection();
}

void TaskViewModel::bulkSetColor(const QString &color)
{
    if (m_selectedIds.isEmpty()) return;
    const QStringList ids = QStringList(m_selectedIds.begin(), m_selectedIds.end());
    int count = 0;
    for (const QString &id : ids) {
        Task *t = m_store->taskById(id);
        if (!t || t->isDeleted()) continue;
        if (t->color() == color) continue;
        t->setColor(color);
        t->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("color")});
        emit taskChanged(id);
        count++;
    }
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_color"),
                                {{QStringLiteral("count"), count}});
    clearSelection();
}

void TaskViewModel::bulkSetDue(const QString &dueString)
{
    if (m_selectedIds.isEmpty()) return;
    const QStringList ids = QStringList(m_selectedIds.begin(), m_selectedIds.end());

    // Parse the due string once — same parser used by addTask / setTaskDue.
    DateParseResult result;
    if (!dueString.isEmpty()) {
        const QString lang = m_store->setting(QLatin1String(SettingsKeys::Language)).toString();
        result = m_dateParser->parse(dueString, lang.isEmpty() ? QStringLiteral("en") : lang);
    }

    int count = 0;
    for (const QString &id : ids) {
        Task *t = m_store->taskById(id);
        if (!t || t->isDeleted()) continue;
        if (dueString.isEmpty()) {
            // Clear due date: reset all due fields to blank.
            t->setDue(DueObject{});
        } else if (result.valid) {
            t->setDueDate(result.date);
            if (!result.datetime.isEmpty())
                t->setDueDatetime(result.datetime);
            else
                t->setDueDatetime({});
            t->setDueString(result.humanString);
            t->setDueIsRecurring(result.isRecurring);
            t->setDueRecurrenceRule(result.recurrenceRule);
        } else {
            // Unparseable string — skip this task, do not mutate.
            continue;
        }
        t->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("due")});
        emit taskChanged(id);
        count++;
    }

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_defer"),
                                {{QStringLiteral("count"), count},
                                 {QStringLiteral("due"), dueString}});
    clearSelection();
}

void TaskViewModel::bulkSetDeadline(const QDate &date)
{
    // Set Task::deadline (stored as ISO "yyyy-MM-dd" string) on every selected
    // task. An invalid QDate clears the deadline. Mirrors the bulkSetColor /
    // bulkSetDue pattern: skip no-ops, bump updatedAt, mark modified, emit
    // taskChanged per id, then clear the selection at the end.
    if (m_selectedIds.isEmpty()) return;
    const QStringList ids = QStringList(m_selectedIds.begin(), m_selectedIds.end());
    const QString isoDate = date.isValid() ? date.toString(Qt::ISODate) : QString();

    int count = 0;
    for (const QString &id : ids) {
        Task *t = m_store->taskById(id);
        if (!t || t->isDeleted()) continue;
        if (t->deadline() == isoDate) continue;
        t->setDeadline(isoDate);
        t->setUpdatedAt(QDateTime::currentDateTimeUtc());
        m_store->markTaskFieldsModified(id, {QStringLiteral("deadline")});
        emit taskChanged(id);
        count++;
    }

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_deadline"),
                                {{QStringLiteral("count"), count},
                                 {QStringLiteral("deadline"), isoDate}});
    clearSelection();
}

void TaskViewModel::bulkArchive()
{
    // Archive = soft-delete in this app's current data model. The undo label
    // ("Архивировано") distinguishes it from an intentional delete so the user
    // understands the action is reversible. If a dedicated is_archived flag is
    // added to Task in the future, replace the removeTask calls below with a
    // flag-set + markTaskModified.
    if (m_selectedIds.isEmpty()) return;

    const QStringList ids = QStringList(m_selectedIds.begin(), m_selectedIds.end());

    // iter-night-5: collect only ids of live tasks; restore via the cascade-
    // aware DataStore::restoreTask. Old path flipped is_deleted on root only —
    // children + their reminders/comments stayed tombstoned (post-iter-night-4
    // cascade was depth-N).
    QStringList undoIds;
    undoIds.reserve(ids.size());
    for (const QString &id : ids) {
        Task *t = m_store->taskById(id);
        if (t && !t->isDeleted()) undoIds.append(id);
    }
    if (undoIds.isEmpty()) return;

    const int n = undoIds.size();
    if (m_undoStack) {
        const QString label = QStringLiteral("Архивировано %1 %2")
                                  .arg(n)
                                  .arg(pluralTasks(n));
        m_undoStack->push(label, [this, undoIds]() {
            for (const QString &id : undoIds) {
                if (m_store->restoreTask(id))
                    emit taskChanged(id);
            }
        });
    }

    for (const QString &id : ids)
        m_store->removeTask(id);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_archive"),
                                {{QStringLiteral("count"), n}});

    emit tasksRemoved(ids);
    clearSelection();
}

QString TaskViewModel::bulkCopyAsMarkdown()
{
    if (m_selectedIds.isEmpty()) return {};

    QStringList lines;
    // Iterate in current display order (m_tasks) so the markdown respects the
    // user's active sort, not the arbitrary insertion order of the selection set.
    for (const Task *t : m_tasks) {
        if (!t || !m_selectedIds.contains(t->id())) continue;
        const QChar mark = t->isCompleted() ? QChar('x') : QChar(' ');
        lines.append(QString("- [%1] %2").arg(mark).arg(t->content()));
    }
    if (lines.isEmpty()) return {};

    const QString text = lines.join(QLatin1Char('\n'));
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("task_bulk_copy_markdown"),
                                {{QStringLiteral("count"), lines.size()}});

    return text;
}

void TaskViewModel::sortTasks()
{
    // Today view: sort by dayOrder ascending; tasks with dayOrder==0 (unset) go to the end.
    // Tiebreak within same dayOrder bucket: use order (insertion order). All other filter
    // modes are unaffected — their sort paths follow unchanged below.
    if (m_filterMode == FilterMode::Today) {
        std::sort(m_tasks.begin(), m_tasks.end(), [](const Task *a, const Task *b) {
            const int da = a->dayOrder();
            const int db = b->dayOrder();
            // Both unset (0) or both set to the same value: fall back to insertion order
            if (da == db) return a->order() < b->order();
            // Unset goes after any explicitly positioned task
            if (da == 0) return false;
            if (db == 0) return true;
            return da < db;
        });

        // Pinned tasks always float above everything else.  Partition first so
        // we have a stable boundary; then partition the non-pinned body by
        // overdue.  This ordering also keeps the iterator math simple for the
        // optional reversal step below.
        auto pinnedEnd = std::stable_partition(m_tasks.begin(), m_tasks.end(),
            [](const Task *t) { return t && t->isPinned(); });

        // Lift overdue tasks (dueDate < today) above due-today tasks within
        // the non-pinned body. Tasks with no dueDate are not overdue; tasks
        // due exactly today stay below.  std::stable_partition preserves
        // relative order within each group.
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        auto overdueEnd = std::stable_partition(pinnedEnd, m_tasks.end(),
            [&today](const Task *t) {
                if (!t || t->isDeleted()) return false;
                const QString d = t->dueDate();
                return !d.isEmpty() && d < today;
            });

        // Final layout: [pinned] [overdue] [due-today].

        // "Reverse order" toggle: flip the body of each layered group
        // (overdue / due-today) while leaving the pinned group at the top
        // intact and unreversed. Without this carve-out, pinned tasks would
        // sink to the bottom and the layered ordering (pinned > overdue >
        // due-today) would collapse.
        if (m_sortReversed) {
            std::reverse(pinnedEnd, overdueEnd);
            std::reverse(overdueEnd, m_tasks.end());
        }

        return;
    }

    // For project view, group by sectionId first so that ListView's
    // section.delegate doesn't fragment when sections are interleaved by order.
    // The empty section ("(Без раздела)") sorts before any named section.
    const bool groupBySection = (m_filterMode == FilterMode::Project ||
                                 m_filterMode == FilterMode::Board ||
                                 m_filterMode == FilterMode::Calendar);

    if (m_sortBy == QStringLiteral("date")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            if (a->dueDate().isEmpty() && b->dueDate().isEmpty()) return a->order() < b->order();
            if (a->dueDate().isEmpty()) return false;
            if (b->dueDate().isEmpty()) return true;
            if (a->dueDate() != b->dueDate()) return a->dueDate() < b->dueDate();
            return a->order() < b->order();
        });
    } else if (m_sortBy == QStringLiteral("priority")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            if (a->priority() != b->priority()) return a->priority() > b->priority();
            return a->order() < b->order();
        });
    } else if (m_sortBy == QStringLiteral("name")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            return a->content().compare(b->content(), Qt::CaseInsensitive) < 0;
        });
    } else if (m_sortBy == QStringLiteral("created")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            return a->createdAt() > b->createdAt();
        });
    } else if (m_sortBy == QStringLiteral("createdAt")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            return a->createdAt() < b->createdAt();
        });
    } else if (m_sortBy == QStringLiteral("createdAtDesc")) {
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            return a->createdAt() > b->createdAt();
        });
    } else {
        // Default: by order, with optional section grouping for project views.
        std::sort(m_tasks.begin(), m_tasks.end(), [groupBySection](const Task *a, const Task *b) {
            if (groupBySection && a->sectionId() != b->sectionId())
                return a->sectionId() < b->sectionId();
            return a->order() < b->order();
        });
    }

    // Финальный стабильный проход: закреплённые задачи всплывают наверх списка
    // вне зависимости от режима (Inbox, Today, Project и т.д.).
    // std::stable_partition сохраняет относительный порядок внутри каждой группы.
    auto pinnedEnd = std::stable_partition(m_tasks.begin(), m_tasks.end(),
        [](const Task *t) { return t && t->isPinned(); });

    // "Обратный порядок сортировки": переворачивает корпус списка, сохраняя
    // закреплённые задачи в их фиксированной позиции вверху.  В режимах с
    // группировкой по секциям (Project / Board / Calendar) переворачиваем
    // внутри каждой секции отдельно — иначе порядок секций сломался бы и
    // ListView.section.delegate начал бы фрагментироваться.
    if (m_sortReversed && pinnedEnd != m_tasks.end()) {
        if (groupBySection) {
            // Walk the body and reverse contiguous runs of equal sectionId.
            // The body is already grouped by sectionId (see sort lambdas above),
            // so equal-sectionId runs are guaranteed contiguous.
            auto runStart = pinnedEnd;
            while (runStart != m_tasks.end()) {
                const QString sid = (*runStart)->sectionId();
                auto runEnd = runStart;
                while (runEnd != m_tasks.end() && (*runEnd)->sectionId() == sid)
                    ++runEnd;
                std::reverse(runStart, runEnd);
                runStart = runEnd;
            }
        } else {
            std::reverse(pinnedEnd, m_tasks.end());
        }
    }
}

// --- Quick Add ---

QString TaskViewModel::addTaskFromQuickAdd(const QString &input)
{
    const auto parsed = QuickAddParser::parse(input);
    if (parsed.content.isEmpty() && parsed.dueString.isEmpty())
        return {};

    QString projectId;
    if (!parsed.projectName.isEmpty()) {
        for (auto *p : m_store->allProjects()) {
            if (p->name().compare(parsed.projectName, Qt::CaseInsensitive) == 0) {
                projectId = p->id();
                break;
            }
        }
    }

    return addTask(parsed.content, projectId, parsed.priority, parsed.dueString, parsed.labels);
}

QVariantMap TaskViewModel::previewQuickAdd(const QString &input)
{
    const auto parsed = QuickAddParser::parse(input);
    QVariantMap map;
    map[QStringLiteral("content")]     = parsed.content;
    map[QStringLiteral("dueString")]   = parsed.dueString;
    map[QStringLiteral("priority")]    = parsed.priority;
    map[QStringLiteral("labels")]      = parsed.labels;
    map[QStringLiteral("projectName")] = parsed.projectName;
    return map;
}