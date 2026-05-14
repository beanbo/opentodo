#pragma once

#include <QObject>
#include <QString>
#include <QStack>
#include <QJsonObject>
#include <functional>

class DataStore;

// Lightweight undo for individual user actions. Each entry is a "command"
// that knows how to undo itself. Capacity-limited (last N actions).
//
// B-5.28/29: pushDelete (and similar typed helpers) capture task data by ID +
// JSON snapshot rather than by raw Task* pointer, so lambdas never hold
// dangling pointers across async store mutations.
class UndoStack : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(QString lastDescription READ lastDescription NOTIFY canUndoChanged)
    Q_PROPERTY(int count READ count NOTIFY canUndoChanged)

public:
    explicit UndoStack(QObject *parent = nullptr);

    using UndoFn = std::function<void()>;

    // Push a new undoable action. `description` shows in the snackbar.
    void push(const QString &description, UndoFn undoFn);

    // B-5.28/29: Typed API — captures task by ID + JSON snapshot.
    // On undo: if the task still exists in `store`, repopulates it from `snapshot`;
    // if it has been hard-deleted (tombstone resolved), no-ops silently.
    void pushDelete(const QString &description,
                    const QString &taskId,
                    const QJsonObject &snapshot,
                    DataStore *store);

    Q_INVOKABLE bool undo();   // execute the most-recent undo, pop it; returns true if anything undone
    Q_INVOKABLE void clear();
    bool canUndo() const;
    QString lastDescription() const;
    int count() const;

signals:
    void canUndoChanged();
    void undoExecuted(const QString &description);

private:
    struct Entry { QString description; UndoFn fn; };
    QStack<Entry> m_stack;
    static constexpr int MAX_ENTRIES = 20;
};
