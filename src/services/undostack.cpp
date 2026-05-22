#include "undostack.h"

#include "storage/datastore.h"
#include "model/task.h"

#include <QJsonObject>

UndoStack::UndoStack(QObject *parent)
    : QObject(parent)
{
}

void UndoStack::push(const QString &description, UndoFn undoFn)
{
    // Enforce capacity: drop the oldest entry (bottom of stack) when full.
    // QStack is LIFO, so we trim from index 0 (oldest pushed).
    while (m_stack.size() >= MAX_ENTRIES)
        m_stack.removeFirst();

    m_stack.push({description, std::move(undoFn)});
    emit canUndoChanged();
}

// B-5.28/29: Typed delete-undo that captures task data by ID + JSON snapshot.
// This avoids raw Task* pointer capture which can become dangling if the store
// removes the object between push and undo.
//
// On undo:
//   - If the task still exists in `store`, its fields are repopulated from `snapshot`.
//   - If the task is gone (tombstone resolved), logs a warning and no-ops silently.
void UndoStack::pushDelete(const QString &description,
                            const QString &taskId,
                            const QJsonObject &snapshot,
                            DataStore *store)
{
    // Capture by value: taskId, snapshot, and a QPointer-style raw pointer to store.
    // We deliberately capture `store` as a plain pointer — the store outlives the
    // undo stack in all code paths (both are owned by main). If that invariant ever
    // changes, upgrade to QPointer<DataStore>.
    push(description, [taskId, snapshot, store]() {
        if (!store) {
            qWarning("UndoStack: DataStore is gone — cannot undo delete of task %s",
                     qPrintable(taskId));
            return;
        }
        Task *task = store->taskById(taskId);
        if (!task) {
            // Task has already been permanently removed (e.g. tombstone resolved by sync).
            // No-op silently — this is expected and not an error.
            qDebug("UndoStack: task %s no longer exists, undo-delete silently skipped.",
                   qPrintable(taskId));
            return;
        }
        // Restore the task from the JSON snapshot.
        // We use the individual setters via fromJson-style field re-application.
        // fromJson constructs a new object; here we repopulate the existing one.
        task->setIsDeleted(false);
        task->setContent(snapshot.value(QStringLiteral("content")).toString());
        task->setDescription(snapshot.value(QStringLiteral("description")).toString());
        task->setIsCompleted(snapshot.value(QStringLiteral("is_completed")).toBool());
        // Stamp every restored field — description in particular isn't in
        // addTask's initialFields, so without an explicit per-field stamp a
        // concurrent description edit on another device would auto-win via
        // empty-meta fallback and silently wipe the restored description.
        store->markTaskFieldsModified(taskId,
            {QStringLiteral("is_deleted"), QStringLiteral("content"),
             QStringLiteral("description"), QStringLiteral("is_completed")});
    });
}

bool UndoStack::undo()
{
    if (m_stack.isEmpty())
        return false;

    Entry entry = m_stack.pop();
    if (entry.fn)
        entry.fn();

    emit canUndoChanged();
    emit undoExecuted(entry.description);
    return true;
}

void UndoStack::clear()
{
    if (!m_stack.isEmpty()) {
        m_stack.clear();
        emit canUndoChanged();
    }
}

bool UndoStack::canUndo() const
{
    return !m_stack.isEmpty();
}

QString UndoStack::lastDescription() const
{
    if (m_stack.isEmpty())
        return {};
    return m_stack.top().description;
}

int UndoStack::count() const
{
    return m_stack.size();
}
