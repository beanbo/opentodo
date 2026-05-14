#pragma once

// Core wiring (DataStore + SyncEngine) is complete in main.cpp.
// Two optional notification kinds (ReminderFired, AchievementUnlocked) are
// stubbed out below — see the inline comments inside Notification::Kind.

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimer>

class DataStore;
class SyncEngine;

// ---------------------------------------------------------------------------
// Plain-old-data struct stored in the model's internal list.
// ---------------------------------------------------------------------------
struct Notification {
    enum Kind {
        TaskCompleted,
        TaskOverdue,
        SyncSuccess,
        SyncFailure,
        AchievementUnlocked,
        ReminderFired
    };

    QString  id;            // UUID, generated on creation
    QString  title;
    QString  body;
    QDateTime timestamp;
    Kind     kind = TaskCompleted;
    QString  relatedTaskId; // empty if not task-related
    bool     isRead = false;
};

// ---------------------------------------------------------------------------
// NotificationsViewModel
// ---------------------------------------------------------------------------
class NotificationsViewModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
    // count is a proper Q_PROPERTY so QML can bind to notificationsViewModel.count
    // rather than calling rowCount() (which is not a property).
    // NOTE for QML agent: replace any `notificationsViewModel.rowCount()` calls in
    // NotificationsView.qml (lines ~85, 108, 118) with `notificationsViewModel.count`.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        BodyRole,
        TimestampRole,
        KindRole,
        RelatedTaskIdRole,
        IsReadRole
    };
    Q_ENUM(Roles)

    // kind exposed as int to QML; mirrors Notification::Kind values.
    enum NotificationKind {
        KindTaskCompleted       = Notification::TaskCompleted,
        KindTaskOverdue         = Notification::TaskOverdue,
        KindSyncSuccess         = Notification::SyncSuccess,
        KindSyncFailure         = Notification::SyncFailure,
        KindAchievementUnlocked = Notification::AchievementUnlocked,
        KindReminderFired       = Notification::ReminderFired
    };
    Q_ENUM(NotificationKind)

    // SyncEngine is optional: pass nullptr if it is not yet available.
    explicit NotificationsViewModel(DataStore *store,
                                    SyncEngine *syncEngine = nullptr,
                                    QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Properties
    int unreadCount() const;
    int count() const { return m_notifications.size(); }

    // Invokables
    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void markRead(int row);
    // ITER1 #26: row indices become stale after model changes; mark by id is safer.
    Q_INVOKABLE void markReadById(const QString &id);
    Q_INVOKABLE void clear();

    // Maximum number of notifications kept (oldest are evicted first).
    void setCapacity(int cap);

public slots:
    // Wired from KarmaViewModel::achievementUnlocked in main.cpp. Pushes a
    // notification of kind AchievementUnlocked.
    void onAchievementUnlocked(const QString &name, const QString &description, int milestone);

    // Wired from NotificationService::reminderFired in main.cpp. Pushes a
    // notification of kind ReminderFired so the user can review past reminders
    // in the in-app notification centre even after the OS toast disappears.
    void onReminderFired(const QString &taskId, const QString &title, const QString &body);

signals:
    void unreadCountChanged();
    void countChanged();

private slots:
    void onTaskChanged(const QString &taskId);
    void onSyncCompleted();
    void onSyncFailed(const QString &error);
    void scanOverdueTasks();

private:
    void push(Notification n);
    bool hasOverdueNotification(const QString &taskId) const;
    void emitUnreadIfChanged(int before);

    DataStore  *m_store;
    SyncEngine *m_sync;

    QList<Notification> m_notifications;
    QSet<QString>       m_overdueTaskIds; // tasks already notified as overdue
    QSet<QString>       m_completedTaskIds; // tasks already notified as completed

    int m_capacity = 100;

    // Tracks prior completion state of tasks seen via onTaskChanged.
    QSet<QString> m_prevCompleted;

    QTimer m_overdueTimer;
};
