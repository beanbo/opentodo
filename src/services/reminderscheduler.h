#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>

class DataStore;
class NotificationService;

// ReminderScheduler bridges DataStore::Reminder entities with the platform
// NotificationService. It listens to tasksChanged() and remindersChanged()
// (not the aggregate dataChanged — avoids reschedule storms on unrelated mutations).
//
// Storm prevention (B-5.10): rapid successive signals (e.g. bulk import) are
// coalesced via a 500ms single-shot timer. When the timer fires, rescheduleAll()
// runs exactly once regardless of how many signals arrived in the window.
// The initial pass in the constructor runs synchronously (no timer) so that
// reminders loaded from disk are scheduled immediately on startup.
//
// Recurring task support (B-5.13): when a recurring task's due date advances
// after completion, any reminders attached to it are automatically rescheduled
// for the new instance. A per-reminder "last scheduled for date" map detects this.
class ReminderScheduler : public QObject
{
    Q_OBJECT

public:
    explicit ReminderScheduler(DataStore *dataStore,
                               NotificationService *notifService,
                               QObject *parent = nullptr);

    // B-5.10: exposed so the debounce timer can connect to it, and also so tests
    // can invoke it directly without needing an event loop to fire the timer.
    void rescheduleAll();

private slots:
    // Restarts the debounce timer on any relevant data signal.
    void onDataSignal();

private:
    // Parse the trigger time for a reminder, respecting timezone (B-5.10/11).
    QDateTime resolveTriggerTime(const class Reminder *reminder,
                                 const class Task *task) const;

    DataStore          *m_dataStore = nullptr;
    NotificationService *m_notif    = nullptr;

    // B-5.10: 500ms debounce timer — coalesces rapid bursts (e.g. bulk import)
    // into one rescheduleAll() pass.
    QTimer m_debounceTimer;

    // B-5.13: track the task due-date that each reminder was last scheduled for.
    // Key: reminderId, Value: due date string ("YYYY-MM-DD") at scheduling time.
    QMap<QString, QString> m_lastScheduledFor;
};
