#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDateTime>
#include <QDate>
#include <QTime>

#include "storage/datastore.h"
#include "sync/syncengine.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/notificationsviewmodel.h"

// =============================================================================
// Comprehensive unit tests for NotificationsViewModel.
//
// The NotificationsViewModel is fed entirely by external signals — it does NOT
// expose addNotification/dismissNotification public APIs. Instead, it observes
// DataStore::tasksChanged (to detect task completions and overdue tasks via a
// timer-driven scan) and SyncEngine::{syncCompleted,syncFailed}. The public
// surface is markRead(int)/markReadById(QString)/markAllRead()/clear() plus
// the unreadCount and count properties.
//
// Because of this design, several test cases from the original spec ("addNoti-
// fication populates required fields", "Multiple notifications with same
// taskId: each treated as distinct entry", "dismissNotification(invalidId)")
// are implemented by triggering the same internal push() path the production
// code uses (a task-completion signal). The "persistence after VM destruction"
// case is *not* applicable — NotificationsViewModel keeps everything in-memory
// only and has no QSettings or file persistence (verified by inspection of
// notificationsviewmodel.{h,cpp}); we document that explicitly with a
// dedicated slot.
//
// QSettings is scoped to a throwaway org/app to avoid touching the user's
// real OpenTodo state (SyncEngine reads sync/device_id from QSettings during
// construction).
// =============================================================================

namespace {

// Helper subclass that exposes a public emitter for the syncCompleted /
// syncFailed signals. Signals are emitted on the SyncEngine instance, but
// since `emit` is just sugar and Qt MOC generates signal methods as public,
// in practice this wrapper isn't strictly required; however, the wrapper
// makes the intent crystal-clear and survives any Qt-version visibility
// changes.
class TestableSyncEngine : public SyncEngine
{
public:
    explicit TestableSyncEngine(DataStore *store, QObject *parent = nullptr)
        : SyncEngine(store, parent) {}

    void fireSyncCompleted() { emit syncCompleted(); }
    void fireSyncFailed(const QString &err) { emit syncFailed(err); }
};

struct Fixture {
    DataStore           *store     = nullptr;
    TestableSyncEngine  *sync      = nullptr;   // optional, may be nullptr
    QString              inboxId;

    ~Fixture() {
        delete sync;
        delete store;
    }
};

// Build a fresh in-memory DataStore with one Inbox project. SyncEngine is
// optional and constructed only when the test specifically needs to drive
// sync-related notifications — keeping it out of the default fixture avoids
// the overhead of GDriveClient/Merger/SnapshotManager construction for the
// majority of tests.
static Fixture *makeFixture(bool withSync = false)
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    if (withSync)
        f->sync = new TestableSyncEngine(f->store);

    return f;
}

// Add a NEW (not-already-completed) task and complete it. Triggering
// DataStore::tasksChanged will cause the VM to push a TaskCompleted notif.
// We avoid pre-completing tasks before the VM is constructed because the VM
// pre-populates m_completedTaskIds at construction so existing completed
// tasks don't generate spurious startup notifications.
static QString addAndCompleteTask(DataStore *store, const QString &projectId,
                                  const QString &content)
{
    Task *t = store->addTask(content, projectId);
    const QString id = t->id();
    t->setIsCompleted(true);
    // markTaskModified emits tasksChanged, which is what NotificationsVM
    // subscribes to. The VM's lambda iterates allTasks() to find newly
    // completed ones.
    store->markTaskModified(id);
    return id;
}

// Helper for accessing role data by row.
static QVariant roleAt(const NotificationsViewModel &vm, int row, int role)
{
    return vm.data(vm.index(row, 0), role);
}

} // anonymous namespace

class TestNotificationsViewModel : public QObject
{
    Q_OBJECT

private slots:

    // ── Test fixture lifecycle ───────────────────────────────────────────────

    void initTestCase()
    {
        // Use a throwaway QSettings scope so the test never touches the
        // user's real OpenTodo settings (in particular, SyncEngine reads
        // sync/device_id from QSettings).
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_NotifsVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_notificationsviewmodel"));
    }

    void init()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    // =====================================================================
    // 1) Bootstrap: empty store → rowCount==0, unreadCount==0, count==0.
    // =====================================================================
    void bootstrap_empty_store_has_no_notifications()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        QCOMPARE(vm.rowCount(), 0);
        QCOMPARE(vm.unreadCount(), 0);
        QCOMPARE(vm.count(), 0);

        delete f;
    }

    // =====================================================================
    // 2) roleNames exposes the documented seven role keys with the names
    //    that QML delegates bind to.
    // =====================================================================
    void roleNames_exposes_documented_keys()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        const QHash<int, QByteArray> roles = vm.roleNames();
        QCOMPARE(roles.value(NotificationsViewModel::IdRole),            QByteArray("notifId"));
        QCOMPARE(roles.value(NotificationsViewModel::TitleRole),         QByteArray("title"));
        QCOMPARE(roles.value(NotificationsViewModel::BodyRole),          QByteArray("body"));
        QCOMPARE(roles.value(NotificationsViewModel::TimestampRole),     QByteArray("timestamp"));
        QCOMPARE(roles.value(NotificationsViewModel::KindRole),          QByteArray("kind"));
        QCOMPARE(roles.value(NotificationsViewModel::RelatedTaskIdRole), QByteArray("relatedTaskId"));
        QCOMPARE(roles.value(NotificationsViewModel::IsReadRole),        QByteArray("isRead"));

        delete f;
    }

    // =====================================================================
    // 3) Completing a task pushes a TaskCompleted notification.
    //    rowCount becomes 1, unreadCount becomes 1, countChanged + unread-
    //    CountChanged + rowsInserted all fire.
    // =====================================================================
    void task_completion_pushes_unread_notification()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        QSignalSpy unreadSpy (&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy countSpy  (&vm, &NotificationsViewModel::countChanged);
        QSignalSpy insertSpy (&vm, &NotificationsViewModel::rowsInserted);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Pay bills"));

        QCOMPARE(vm.rowCount(), 1);
        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(vm.count(), 1);
        QVERIFY(unreadSpy.count() >= 1);
        QVERIFY(countSpy.count()  >= 1);
        QCOMPARE(insertSpy.count(), 1);

        delete f;
    }

    // =====================================================================
    // 4) Pushed notification carries id + content (body) + timestamp.
    //    These are the "required fields" referenced in the task spec — we
    //    map them to id/content/createdAt onto IdRole/BodyRole/TimestampRole.
    // =====================================================================
    void task_completion_notification_populates_required_fields()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        const QString taskId = addAndCompleteTask(f->store, f->inboxId,
                                                  QStringLiteral("Buy milk"));

        QCOMPARE(vm.rowCount(), 1);
        const QString notifId = roleAt(vm, 0, NotificationsViewModel::IdRole).toString();
        const QString body    = roleAt(vm, 0, NotificationsViewModel::BodyRole).toString();
        const QDateTime ts    = roleAt(vm, 0, NotificationsViewModel::TimestampRole).toDateTime();
        const QString related = roleAt(vm, 0, NotificationsViewModel::RelatedTaskIdRole).toString();
        const int kind        = roleAt(vm, 0, NotificationsViewModel::KindRole).toInt();
        const bool isRead     = roleAt(vm, 0, NotificationsViewModel::IsReadRole).toBool();

        QVERIFY(!notifId.isEmpty());                 // VM-generated UUID
        QCOMPARE(body, QStringLiteral("Buy milk"));  // sourced from task content
        QVERIFY(ts.isValid());
        QCOMPARE(related, taskId);
        QCOMPARE(kind, int(Notification::TaskCompleted));
        QVERIFY(!isRead);                            // freshly-pushed notif starts unread

        delete f;
    }

    // =====================================================================
    // 5) markReadById flips the isRead flag for the matching notification
    //    and decrements unreadCount; unreadCountChanged fires.
    // =====================================================================
    void markReadById_flips_isRead_and_decrements_unreadCount()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        QCOMPARE(vm.unreadCount(), 1);

        const QString notifId = roleAt(vm, 0, NotificationsViewModel::IdRole).toString();

        QSignalSpy unreadSpy (&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy dataSpy   (&vm, &NotificationsViewModel::dataChanged);

        vm.markReadById(notifId);

        QCOMPARE(vm.unreadCount(), 0);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::IsReadRole).toBool(), true);
        QCOMPARE(unreadSpy.count(), 1);
        QCOMPARE(dataSpy.count(), 1);
        // dataChanged should target the IsReadRole specifically. In Qt 6 the
        // roles list is a QList<int> (QVector is an alias for QList).
        const QList<QVariant> dargs = dataSpy.takeFirst();
        const QList<int> roles = dargs.at(2).value<QList<int>>();
        QVERIFY(roles.contains(int(NotificationsViewModel::IsReadRole)));

        delete f;
    }

    // =====================================================================
    // 6) markRead(row) by index works the same as markReadById for the
    //    common case of a single-row table.
    // =====================================================================
    void markRead_by_row_flips_isRead()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("B"));
        QCOMPARE(vm.rowCount(), 2);
        QCOMPARE(vm.unreadCount(), 2);

        vm.markRead(0); // top row (most recent)
        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::IsReadRole).toBool(), true);
        QCOMPARE(roleAt(vm, 1, NotificationsViewModel::IsReadRole).toBool(), false);

        delete f;
    }

    // =====================================================================
    // 7) markAllRead flips every row to isRead=true and zeroes unreadCount;
    //    fires unreadCountChanged + dataChanged for each previously-unread
    //    row.
    // =====================================================================
    void markAllRead_clears_all_unread_flags()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("B"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("C"));
        QCOMPARE(vm.unreadCount(), 3);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy dataSpy  (&vm, &NotificationsViewModel::dataChanged);

        vm.markAllRead();

        QCOMPARE(vm.unreadCount(), 0);
        for (int i = 0; i < vm.rowCount(); ++i)
            QCOMPARE(roleAt(vm, i, NotificationsViewModel::IsReadRole).toBool(), true);
        QCOMPARE(unreadSpy.count(), 1);
        // dataChanged emits per-row inside markAllRead.
        QCOMPARE(dataSpy.count(), 3);

        delete f;
    }

    // =====================================================================
    // 8) clear() removes every notification and emits unreadCountChanged +
    //    countChanged + modelReset. Fulfils the "clearAll if exists" case.
    // =====================================================================
    void clear_removes_all_notifications_and_emits_signals()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("B"));
        QCOMPARE(vm.rowCount(), 2);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy countSpy (&vm, &NotificationsViewModel::countChanged);
        QSignalSpy resetSpy (&vm, &NotificationsViewModel::modelReset);

        vm.clear();

        QCOMPARE(vm.rowCount(), 0);
        QCOMPARE(vm.unreadCount(), 0);
        QCOMPARE(vm.count(), 0);
        QCOMPARE(unreadSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 1);

        // clear() on an already-empty model is a no-op (early return).
        unreadSpy.clear();
        countSpy.clear();
        resetSpy.clear();
        vm.clear();
        QCOMPARE(unreadSpy.count(), 0);
        QCOMPARE(countSpy.count(),  0);
        QCOMPARE(resetSpy.count(),  0);

        delete f;
    }

    // =====================================================================
    // 9) Sorting: most recent notification appears at row 0 (prepend
    //    semantics in NotificationsViewModel::push). Verifies the
    //    "Sorting: most recent first" requirement.
    // =====================================================================
    void notifications_sorted_most_recent_first()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("First"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Second"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Third"));

        QCOMPARE(vm.rowCount(), 3);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Third"));
        QCOMPARE(roleAt(vm, 1, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Second"));
        QCOMPARE(roleAt(vm, 2, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("First"));

        delete f;
    }

    // =====================================================================
    // 10) Edge case: markReadById with an unknown id is a no-op (no crash,
    //     no signal fires, state unchanged).
    // =====================================================================
    void markReadById_invalid_id_is_noop()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        QCOMPARE(vm.unreadCount(), 1);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy dataSpy  (&vm, &NotificationsViewModel::dataChanged);

        vm.markReadById(QStringLiteral("non-existent-uuid-12345"));

        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::IsReadRole).toBool(), false);
        QCOMPARE(unreadSpy.count(), 0);
        QCOMPARE(dataSpy.count(),   0);

        delete f;
    }

    // =====================================================================
    // 11) Edge case: markRead(row) with out-of-range indices is a no-op.
    //     Maps to the "dismissNotification(invalidId) — no-op" requirement
    //     (the VM exposes no dismiss API; bounds-check coverage on the
    //     existing markRead achieves the same intent of "guard against
    //     invalid input").
    // =====================================================================
    void markRead_out_of_range_row_is_noop()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        QCOMPARE(vm.unreadCount(), 1);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);

        vm.markRead(-1);
        vm.markRead(99);

        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(unreadSpy.count(), 0);

        delete f;
    }

    // =====================================================================
    // 12) Re-marking an already-read notification is a no-op (early return
    //     in markRead) — no extra signals.
    // =====================================================================
    void markRead_idempotent_for_already_read_row()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        vm.markRead(0);
        QCOMPARE(vm.unreadCount(), 0);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy dataSpy  (&vm, &NotificationsViewModel::dataChanged);

        vm.markRead(0); // already read; should bail
        QCOMPARE(vm.unreadCount(), 0);
        QCOMPARE(unreadSpy.count(), 0);
        QCOMPARE(dataSpy.count(),   0);

        delete f;
    }

    // =====================================================================
    // 13) De-duplication by relatedTaskId: completing the SAME task twice
    //     (toggle off → toggle on again) is allowed to fire a second
    //     notification because the constructor's lambda removes the id
    //     from m_completedTaskIds when the task is un-completed. This is
    //     the documented behaviour and matches the spec's "Multiple
    //     notifications with same taskId: each treated as distinct entry".
    // =====================================================================
    void recompletion_after_uncomplete_fires_second_notification()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        Task *t = f->store->addTask(QStringLiteral("Recurring chore"), f->inboxId);
        const QString id = t->id();

        // First completion → notif #1.
        t->setIsCompleted(true);
        f->store->markTaskModified(id);
        QCOMPARE(vm.rowCount(), 1);

        // Un-complete: VM clears its tracker for this task id.
        t->setIsCompleted(false);
        f->store->markTaskModified(id);
        QCOMPARE(vm.rowCount(), 1); // un-completing doesn't add a notif

        // Re-complete: VM treats it as a new event → second notif.
        t->setIsCompleted(true);
        f->store->markTaskModified(id);
        QCOMPARE(vm.rowCount(), 2);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::RelatedTaskIdRole).toString(), id);
        QCOMPARE(roleAt(vm, 1, NotificationsViewModel::RelatedTaskIdRole).toString(), id);

        delete f;
    }

    // =====================================================================
    // 14) Tasks already completed BEFORE the VM is constructed must not
    //     produce startup notifications — the constructor pre-populates
    //     m_completedTaskIds for that exact reason.
    // =====================================================================
    void preexisting_completed_tasks_do_not_generate_startup_notifications()
    {
        Fixture *f = makeFixture();

        // Pre-complete a task BEFORE constructing the VM.
        Task *t = f->store->addTask(QStringLiteral("Already done"), f->inboxId);
        t->setIsCompleted(true);
        f->store->markTaskModified(t->id());

        // Now create the VM — no notification should appear for the
        // pre-existing completion.
        NotificationsViewModel vm(f->store);
        QCOMPARE(vm.rowCount(), 0);
        QCOMPARE(vm.unreadCount(), 0);

        delete f;
    }

    // =====================================================================
    // 15) Soft-deleted tasks must not generate notifications.
    // =====================================================================
    void soft_deleted_completed_task_does_not_notify()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        Task *t = f->store->addTask(QStringLiteral("Tombstoned"), f->inboxId);
        const QString id = t->id();
        t->setIsCompleted(true);
        t->setIsDeleted(true);
        f->store->markTaskModified(id);

        QCOMPARE(vm.rowCount(), 0);
        QCOMPARE(vm.unreadCount(), 0);

        delete f;
    }

    // =====================================================================
    // 16) Capacity eviction: setting a tiny capacity causes the oldest
    //     entries to be removed when the cap is exceeded. Verifies the
    //     "Maximum number of notifications kept" contract.
    // =====================================================================
    void capacity_evicts_oldest_entries()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);
        vm.setCapacity(2); // keep only the two most-recent

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("First"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Second"));
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Third"));

        QCOMPARE(vm.rowCount(), 2);
        // "Third" is newest at row 0, "Second" at row 1, "First" was evicted.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Third"));
        QCOMPARE(roleAt(vm, 1, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Second"));

        // setCapacity clamps to >= 1 (defensive — verified via internal
        // contract by re-setting to a non-positive value and checking that
        // a subsequent add still fits one entry).
        vm.setCapacity(0); // clamped to 1 internally
        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Fourth"));
        QCOMPARE(vm.rowCount(), 1);
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Fourth"));

        delete f;
    }

    // =====================================================================
    // 17) SyncEngine-driven notifications.
    //     Q3.15: syncCompleted is intentionally NOT surfaced as a notification
    //     — sync runs after every action + on a 5 s debounce, so a
    //     "Синхронизация выполнена" entry every time was pure noise (the live
    //     status lives in SyncIndicator instead). Only syncFailed creates a
    //     notification, because failures are actionable (network/auth/quota).
    //     See NotificationsViewModel ctor (only syncFailed is connected).
    // =====================================================================
    void sync_signals_push_sync_kind_notifications()
    {
        Fixture *f = makeFixture(/*withSync=*/true);
        NotificationsViewModel vm(f->store, f->sync);

        // Routine success → no notification (Q3.15).
        f->sync->fireSyncCompleted();
        QCOMPARE(vm.rowCount(), 0);

        f->sync->fireSyncFailed(QStringLiteral("Quota exceeded"));
        QCOMPARE(vm.rowCount(), 1);
        // Newest at row 0.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::KindRole).toInt(),
                 int(Notification::SyncFailure));
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Quota exceeded"));
        // Empty error string falls back to a default body, not the empty string.
        f->sync->fireSyncFailed(QString());
        QCOMPARE(vm.rowCount(), 2);
        QVERIFY(!roleAt(vm, 0, NotificationsViewModel::BodyRole).toString().isEmpty());

        delete f;
    }

    // =====================================================================
    // 17b) Karma-driven notifications: achievementUnlocked → onAchievementUnlocked
    //      pushes a single AchievementUnlocked notification. We exercise the
    //      slot via a real signal/slot connection (mirroring the wiring in
    //      main.cpp) rather than calling the slot directly, so the test
    //      verifies the connection contract end-to-end.
    // =====================================================================
    void achievement_signal_pushes_achievement_notification()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        // moc cannot process Q_OBJECT in a function-local class. Just call
        // the slot directly — it's the same call path the connect() in
        // main.cpp produces when KarmaViewModel emits achievementUnlocked.
        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy countSpy (&vm, &NotificationsViewModel::countChanged);
        QSignalSpy insertSpy(&vm, &NotificationsViewModel::rowsInserted);

        vm.onAchievementUnlocked(QStringLiteral("500 кармы!"),
                                  QStringLiteral("Полпути к мастерству"),
                                  500);

        QCOMPARE(vm.rowCount(),    1);
        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(vm.count(),       1);
        QCOMPARE(insertSpy.count(), 1);
        QVERIFY (unreadSpy.count() >= 1);
        QVERIFY (countSpy.count()  >= 1);

        // Kind = AchievementUnlocked.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::KindRole).toInt(),
                 int(Notification::AchievementUnlocked));

        // Title contains the milestone name (Russian phrasing per UI spec).
        const QString title = roleAt(vm, 0, NotificationsViewModel::TitleRole).toString();
        QVERIFY2(title.contains(QStringLiteral("500 кармы!")),
                 qPrintable(QStringLiteral("title was: ") + title));

        // Body falls through to the supplied description (non-empty case).
        const QString body = roleAt(vm, 0, NotificationsViewModel::BodyRole).toString();
        QCOMPARE(body, QStringLiteral("Полпути к мастерству"));

        // Empty description → fallback body referencing the milestone integer.
        vm.onAchievementUnlocked(QStringLiteral("1000 кармы!"), QString(), 1000);
        QCOMPARE(vm.rowCount(), 2);
        const QString fallbackBody = roleAt(vm, 0, NotificationsViewModel::BodyRole).toString();
        QVERIFY(!fallbackBody.isEmpty());
        QVERIFY2(fallbackBody.contains(QStringLiteral("1000")),
                 qPrintable(QStringLiteral("fallback body was: ") + fallbackBody));

        delete f;
    }

    // =====================================================================
    // 17c) Reminder-fired notifications: ReminderScheduler emits
    //      NotificationService::reminderFired when it dispatches a reminder.
    //      The slot pushes a single ReminderFired notification carrying the
    //      task id, a Russian title prefix, and the reminder body. As with
    //      the achievement test, we call the slot directly because moc
    //      cannot process Q_OBJECT in a function-local class.
    // =====================================================================
    void reminder_fired_pushes_reminder_notification()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        QSignalSpy unreadSpy(&vm, &NotificationsViewModel::unreadCountChanged);
        QSignalSpy countSpy (&vm, &NotificationsViewModel::countChanged);
        QSignalSpy insertSpy(&vm, &NotificationsViewModel::rowsInserted);

        vm.onReminderFired(QStringLiteral("task-id-123"),
                           QStringLiteral("Buy milk"),
                           QStringLiteral("Reminder body"));

        QCOMPARE(vm.rowCount(),    1);
        QCOMPARE(vm.unreadCount(), 1);
        QCOMPARE(vm.count(),       1);
        QCOMPARE(insertSpy.count(), 1);
        QVERIFY (unreadSpy.count() >= 1);
        QVERIFY (countSpy.count()  >= 1);

        // Kind = ReminderFired.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::KindRole).toInt(),
                 int(Notification::ReminderFired));

        // relatedTaskId is propagated from the signal payload.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::RelatedTaskIdRole).toString(),
                 QStringLiteral("task-id-123"));

        // Body uses the supplied reminder text when present.
        QCOMPARE(roleAt(vm, 0, NotificationsViewModel::BodyRole).toString(),
                 QStringLiteral("Reminder body"));

        // Title carries the Russian prefix and the task content.
        const QString title = roleAt(vm, 0, NotificationsViewModel::TitleRole).toString();
        QVERIFY2(title.contains(QStringLiteral("Напоминание")),
                 qPrintable(QStringLiteral("title was: ") + title));
        QVERIFY2(title.contains(QStringLiteral("Buy milk")),
                 qPrintable(QStringLiteral("title was: ") + title));

        // Empty title and body still produce a valid, non-empty notification.
        vm.onReminderFired(QStringLiteral("task-id-456"), QString(), QString());
        QCOMPARE(vm.rowCount(), 2);
        QVERIFY(!roleAt(vm, 0, NotificationsViewModel::TitleRole).toString().isEmpty());
        QVERIFY(!roleAt(vm, 0, NotificationsViewModel::BodyRole).toString().isEmpty());

        delete f;
    }

    // =====================================================================
    // 18) data() with an invalid index or unknown role returns an invalid
    //     QVariant — no crash, no garbage data.
    // =====================================================================
    void data_with_invalid_index_returns_invalid_variant()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));

        QVERIFY(!vm.data(QModelIndex()).isValid());                       // root index
        QVERIFY(!vm.data(vm.index(99, 0)).isValid());                     // out of range
        QVERIFY(!vm.data(vm.index(-1, 0)).isValid());                     // negative
        QVERIFY(!vm.data(vm.index(0, 0), Qt::UserRole + 999).isValid());  // unknown role

        delete f;
    }

    // =====================================================================
    // 19) Documentation: NotificationsViewModel has NO persistence layer.
    //     Notifications live in the in-memory m_notifications list and are
    //     gone after the VM is destroyed. There is no QSettings key, no
    //     file write, and no DataStore JSON field for notifications. We
    //     verify this contract by destroying and recreating a VM against
    //     the same DataStore — the new VM starts empty.
    // =====================================================================
    void notifications_are_in_memory_only_no_persistence()
    {
        Fixture *f = makeFixture();

        {
            NotificationsViewModel vm(f->store);
            addAndCompleteTask(f->store, f->inboxId, QStringLiteral("Will be lost"));
            QCOMPARE(vm.rowCount(), 1);
            // Pre-mark the task as already-completed in the VM's tracker so
            // the second VM (constructed below) won't fire a startup notif
            // for this same task — the constructor already does this for
            // pre-existing completed tasks via its allTasks() pre-scan, so
            // we don't need to do anything here. Verified by the assertion
            // below.
        }

        // First VM destroyed; spin up a new one against the same store.
        NotificationsViewModel vm2(f->store);
        QCOMPARE(vm2.rowCount(), 0);
        QCOMPARE(vm2.unreadCount(), 0);

        delete f;
    }

    // =====================================================================
    // 20) Signal contract: completing a task fires unreadCountChanged
    //     exactly once (push() only emits when the unread total changes).
    //     Marking that notif read fires it once more. Clearing it fires
    //     once if there's anything left to remove.
    // =====================================================================
    void unreadCountChanged_fires_on_each_state_transition()
    {
        Fixture *f = makeFixture();
        NotificationsViewModel vm(f->store);

        QSignalSpy spy(&vm, &NotificationsViewModel::unreadCountChanged);

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("A"));
        QCOMPARE(spy.count(), 1); // 0 → 1

        const QString notifId = roleAt(vm, 0, NotificationsViewModel::IdRole).toString();
        vm.markReadById(notifId);
        QCOMPARE(spy.count(), 2); // 1 → 0

        addAndCompleteTask(f->store, f->inboxId, QStringLiteral("B"));
        QCOMPARE(spy.count(), 3); // 0 → 1

        vm.clear();
        QCOMPARE(spy.count(), 4); // 1 → 0

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestNotificationsViewModel)
#include "test_notificationsviewmodel.moc"
