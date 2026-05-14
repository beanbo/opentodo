#include "notificationsviewmodel.h"

#include "storage/datastore.h"
#include "sync/syncengine.h"
#include "model/task.h"
#include "utils/uuid.h"

// Convenience alias used throughout this file.
static inline QString newUuid() { return UuidHelper::generate(); }

#include <QDateTime>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NotificationsViewModel::NotificationsViewModel(DataStore *store,
                                               SyncEngine *syncEngine,
                                               QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_sync(syncEngine)
{
    // --- DataStore connections ---
    // taskChanged fires whenever TaskViewModel calls markTaskModified or when
    // the store mutates a task.  We inspect the new state and push a notification
    // when the task was just completed.
    connect(m_store, &DataStore::tasksChanged, this, [this]() {
        // tasksChanged is a bulk signal; iterate all tasks to find newly
        // completed ones we haven't seen yet.
        const auto tasks = m_store->allTasks();
        for (const Task *t : tasks) {
            if (!t || t->isDeleted()) continue;
            const QString &id = t->id();
            if (t->isCompleted() && !m_completedTaskIds.contains(id)) {
                m_completedTaskIds.insert(id);

                Notification n;
                n.id            = newUuid();
                n.title         = QStringLiteral("Задача выполнена");
                n.body          = t->content();
                n.timestamp     = t->completedAt().isValid()
                                      ? t->completedAt()
                                      : QDateTime::currentDateTime();
                n.kind          = Notification::TaskCompleted;
                n.relatedTaskId = id;
                push(std::move(n));

            } else if (!t->isCompleted()) {
                // If a task was un-completed, remove it from tracking so a
                // future re-completion fires another notification.
                m_completedTaskIds.remove(id);
            }
        }
    });

    // --- SyncEngine connections (optional) ---
    if (m_sync) {
        connect(m_sync, &SyncEngine::syncCompleted, this, &NotificationsViewModel::onSyncCompleted);
        connect(m_sync, &SyncEngine::syncFailed,    this, &NotificationsViewModel::onSyncFailed);
    }

    // --- Periodic overdue scan ---
    // Run once now (on next event-loop tick) and then every 60 s.
    m_overdueTimer.setInterval(60'000);
    m_overdueTimer.setSingleShot(false);
    connect(&m_overdueTimer, &QTimer::timeout, this, &NotificationsViewModel::scanOverdueTasks);
    m_overdueTimer.start();

    // Initial scan after construction — deferred so store is fully initialised.
    QTimer::singleShot(0, this, &NotificationsViewModel::scanOverdueTasks);

    // Pre-populate the completed-task tracker so existing completed tasks don't
    // generate spurious notifications at startup.
    for (const Task *t : m_store->allTasks()) {
        if (t && !t->isDeleted() && t->isCompleted())
            m_completedTaskIds.insert(t->id());
    }
}

// ---------------------------------------------------------------------------
// QAbstractListModel
// ---------------------------------------------------------------------------

int NotificationsViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_notifications.size();
}

QVariant NotificationsViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notifications.size())
        return {};

    const Notification &n = m_notifications.at(index.row());

    switch (role) {
    case IdRole:            return n.id;
    case TitleRole:         return n.title;
    case BodyRole:          return n.body;
    case TimestampRole:     return n.timestamp;
    case KindRole:          return static_cast<int>(n.kind);
    case RelatedTaskIdRole: return n.relatedTaskId;
    case IsReadRole:        return n.isRead;
    }
    return {};
}

QHash<int, QByteArray> NotificationsViewModel::roleNames() const
{
    return {
        {IdRole,            "notifId"},
        {TitleRole,         "title"},
        {BodyRole,          "body"},
        {TimestampRole,     "timestamp"},
        {KindRole,          "kind"},
        {RelatedTaskIdRole, "relatedTaskId"},
        {IsReadRole,        "isRead"}
    };
}

// ---------------------------------------------------------------------------
// Property
// ---------------------------------------------------------------------------

int NotificationsViewModel::unreadCount() const
{
    int count = 0;
    for (const Notification &n : m_notifications)
        if (!n.isRead) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Invokables
// ---------------------------------------------------------------------------

void NotificationsViewModel::markAllRead()
{
    const int before = unreadCount();
    bool any = false;
    for (int i = 0; i < m_notifications.size(); ++i) {
        if (!m_notifications[i].isRead) {
            m_notifications[i].isRead = true;
            const QModelIndex idx = createIndex(i, 0);
            emit dataChanged(idx, idx, {IsReadRole});
            any = true;
        }
    }
    if (any) {
        emitUnreadIfChanged(before);
        emit countChanged();
    }
}

void NotificationsViewModel::markRead(int row)
{
    if (row < 0 || row >= m_notifications.size()) return;
    if (m_notifications[row].isRead) return;

    const int before = unreadCount();
    m_notifications[row].isRead = true;
    const QModelIndex idx = createIndex(row, 0);
    emit dataChanged(idx, idx, {IsReadRole});
    emitUnreadIfChanged(before);
    emit countChanged();
}

void NotificationsViewModel::markReadById(const QString &id)
{
    // ITER1 #26: stable-id lookup (rows shift when items are added/removed
    // between delegate creation and click).
    for (int row = 0; row < m_notifications.size(); ++row) {
        if (m_notifications[row].id == id) {
            markRead(row);
            return;
        }
    }
}

void NotificationsViewModel::clear()
{
    if (m_notifications.isEmpty()) return;
    beginResetModel();
    m_notifications.clear();
    endResetModel();
    emit unreadCountChanged();
    emit countChanged();
}

void NotificationsViewModel::setCapacity(int cap)
{
    if (cap < 1) cap = 1;
    m_capacity = cap;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void NotificationsViewModel::onTaskChanged(const QString & /*taskId*/)
{
    // This slot would be called if DataStore exposed a per-task signal.
    // Currently DataStore emits tasksChanged (bulk); individual tracking
    // is handled there in the constructor lambda.
}

void NotificationsViewModel::onSyncCompleted()
{
    Notification n;
    n.id        = newUuid();
    n.title     = QStringLiteral("Синхронизация выполнена");
    n.body      = QStringLiteral("Данные успешно синхронизированы с облаком");
    n.timestamp = QDateTime::currentDateTime();
    n.kind      = Notification::SyncSuccess;
    push(std::move(n));
}

void NotificationsViewModel::onSyncFailed(const QString &error)
{
    Notification n;
    n.id        = newUuid();
    n.title     = QStringLiteral("Ошибка синхронизации");
    n.body      = error.isEmpty() ? QStringLiteral("Не удалось синхронизировать данные") : error;
    n.timestamp = QDateTime::currentDateTime();
    n.kind      = Notification::SyncFailure;
    push(std::move(n));
}

void NotificationsViewModel::onAchievementUnlocked(const QString &name,
                                                   const QString &description,
                                                   int milestone)
{
    Notification n;
    n.id        = newUuid();
    n.title     = QStringLiteral("Достижение разблокировано: ") + name;
    n.body      = description.isEmpty()
                      ? QStringLiteral("Вы достигли %1 кармы").arg(milestone)
                      : description;
    n.timestamp = QDateTime::currentDateTime();
    n.kind      = Notification::AchievementUnlocked;
    push(std::move(n));
}

void NotificationsViewModel::onReminderFired(const QString &taskId,
                                             const QString &title,
                                             const QString &body)
{
    Notification n;
    n.id            = newUuid();
    n.title         = title.isEmpty()
                          ? QStringLiteral("Напоминание")
                          : QStringLiteral("Напоминание: ") + title;
    n.body          = body.isEmpty()
                          ? QStringLiteral("Сработало напоминание о задаче")
                          : body;
    n.timestamp     = QDateTime::currentDateTime();
    n.kind          = Notification::ReminderFired;
    n.relatedTaskId = taskId;
    push(std::move(n));
}

void NotificationsViewModel::scanOverdueTasks()
{
    const QDateTime now = QDateTime::currentDateTime();
    const auto tasks = m_store->overdueTasks();

    for (const Task *t : tasks) {
        if (!t || t->isDeleted() || t->isCompleted()) continue;
        if (m_overdueTaskIds.contains(t->id())) continue; // already notified

        m_overdueTaskIds.insert(t->id());

        Notification n;
        n.id            = newUuid();
        n.title         = QStringLiteral("Просроченная задача");
        n.body          = t->content();
        n.timestamp     = now;
        n.kind          = Notification::TaskOverdue;
        n.relatedTaskId = t->id();
        push(std::move(n));
    }

    // If a previously-overdue task was completed or its due date was cleared /
    // updated to a future date, stop tracking it so it can fire again if it
    // becomes overdue in the future.
    for (auto it = m_overdueTaskIds.begin(); it != m_overdueTaskIds.end(); ) {
        Task *t = m_store->taskById(*it);
        // Remove if the task no longer exists, was deleted, was completed, or
        // its due date is now empty (cleared).  The original code had the
        // condition inverted: `!t->dueDate().isEmpty()` kept tasks that still
        // had a due date (correct) but also removed tasks that should stay
        // (those without a due date should be removed, not kept).
        if (!t || t->isDeleted() || t->isCompleted() || t->dueDate().isEmpty()) {
            // Re-check: if the task is no longer in the current overdue list,
            // remove from our tracking set.
            bool stillOverdue = false;
            for (const Task *ot : tasks) {
                if (ot && ot->id() == *it) { stillOverdue = true; break; }
            }
            if (!stillOverdue) {
                it = m_overdueTaskIds.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void NotificationsViewModel::push(Notification n)
{
    const int before = unreadCount();
    const int beforeCount = m_notifications.size();

    // Prepend (newest first) for a natural notification-centre order.
    beginInsertRows(QModelIndex(), 0, 0);
    m_notifications.prepend(std::move(n));
    endInsertRows();

    // Evict oldest entries that exceed the cap.
    while (m_notifications.size() > m_capacity) {
        const int last = m_notifications.size() - 1;
        beginRemoveRows(QModelIndex(), last, last);
        m_notifications.removeLast();
        endRemoveRows();
    }

    if (m_notifications.size() != beforeCount)
        emit countChanged();
    emitUnreadIfChanged(before);
}

bool NotificationsViewModel::hasOverdueNotification(const QString &taskId) const
{
    return m_overdueTaskIds.contains(taskId);
}

void NotificationsViewModel::emitUnreadIfChanged(int before)
{
    if (unreadCount() != before)
        emit unreadCountChanged();
}
