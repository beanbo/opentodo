#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>

#include "model/reminder.h"
#include "model/task.h"
#include "model/project.h"
#include "storage/datastore.h"
#include "utils/uuid.h"

/**
 * Tests for the Reminder entity AND the DataStore reminder operations.
 *
 * Notes on the tested model:
 *   - Reminder uses `type` (not `kind`). Recognised values are
 *     "absolute" (datetime-based), "relative" (minute-offset against the
 *     parent task's due date), and "location" (geofence). The default in
 *     the C++ constructor is "absolute".
 *   - There is no built-in `valid()` method on Reminder — validity is the
 *     responsibility of higher layers (e.g. ReminderScheduler simply
 *     skips reminders with no actionable data). These tests therefore
 *     check the structural pre-conditions that downstream code relies on
 *     (ReminderDue::isNull, LocationObject::isNull) instead of asserting a
 *     `valid()` API that does not exist.
 *   - DataStore.addReminder accepts an arbitrary taskId. Orphan
 *     reminders (no matching task) are NOT rejected — this is documented
 *     in the addReminder_for_nonexistent_task slot below.
 */
class TestReminderModel : public QObject
{
    Q_OBJECT

private:
    // Helper: a DataStore pre-loaded with one Inbox project and one Task,
    // so reminder slots have a real taskId to work with.
    DataStore *makeStoreWithTask(QString *outTaskId = nullptr)
    {
        auto *ds = new DataStore(this);
        auto *inbox = new Project(ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds->addExistingProject(inbox);
        Task *t = ds->addTask(QStringLiteral("Parent task"), QString());
        if (outTaskId) *outTaskId = t->id();
        return ds;
    }

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    // ── Reminder entity ───────────────────────────────────────────────────────

    void default_construction_assigns_uuid_and_timestamps()
    {
        Reminder r;
        QVERIFY(!r.id().isEmpty());
        QVERIFY(UuidHelper::isValid(r.id()));
        QVERIFY(r.createdAt().isValid());
        QVERIFY(r.updatedAt().isValid());
        // updatedAt should equal createdAt at construction time
        QCOMPARE(r.createdAt(), r.updatedAt());
        QVERIFY(!r.isDeleted());
        // Default type is "absolute"
        QCOMPARE(r.type(), QStringLiteral("absolute"));
        QCOMPARE(r.minuteOffset(), 0);
        QVERIFY(r.taskId().isEmpty());
        QVERIFY(r.recurrenceRule().isEmpty());
    }

    void json_roundtrip_absolute_full_fields()
    {
        Reminder orig;
        orig.setTaskId(QStringLiteral("task-aaaa"));
        orig.setType(QStringLiteral("absolute"));
        ReminderDue due;
        due.date = QStringLiteral("2026-08-15");
        due.datetime = QStringLiteral("2026-08-15T14:30:00");
        due.timezone = QStringLiteral("Europe/Moscow");
        due.lang = QStringLiteral("ru");
        orig.setDue(due);
        orig.setRecurrenceRule(QStringLiteral("FREQ=WEEKLY"));

        const QJsonObject json = orig.toJson();
        QCOMPARE(json[QStringLiteral("task_id")].toString(), QStringLiteral("task-aaaa"));
        QCOMPARE(json[QStringLiteral("type")].toString(), QStringLiteral("absolute"));
        QVERIFY(json.contains(QStringLiteral("due")));
        QCOMPARE(json[QStringLiteral("recurrence_rule")].toString(), QStringLiteral("FREQ=WEEKLY"));

        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->taskId(), QStringLiteral("task-aaaa"));
        QCOMPARE(r->type(), QStringLiteral("absolute"));
        QCOMPARE(r->due().date, QStringLiteral("2026-08-15"));
        QCOMPARE(r->due().datetime, QStringLiteral("2026-08-15T14:30:00"));
        QCOMPARE(r->due().timezone, QStringLiteral("Europe/Moscow"));
        QCOMPARE(r->due().lang, QStringLiteral("ru"));
        QCOMPARE(r->recurrenceRule(), QStringLiteral("FREQ=WEEKLY"));
    }

    void json_roundtrip_location_full_fields()
    {
        Reminder orig;
        orig.setTaskId(QStringLiteral("task-loc"));
        // Use the QML-friendly setter — also flips type to "location".
        orig.setLocationCoords(55.7558, 37.6173, 250,
                               QStringLiteral("Office"),
                               QStringLiteral("on_leave"));

        const QJsonObject json = orig.toJson();
        QCOMPARE(json[QStringLiteral("type")].toString(), QStringLiteral("location"));
        QVERIFY(json.contains(QStringLiteral("location")));
        const QJsonObject locObj = json[QStringLiteral("location")].toObject();
        QCOMPARE(locObj[QStringLiteral("name")].toString(), QStringLiteral("Office"));
        QCOMPARE(locObj[QStringLiteral("trigger")].toString(), QStringLiteral("on_leave"));

        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QCOMPARE(r->type(), QStringLiteral("location"));
        QCOMPARE(r->location().name, QStringLiteral("Office"));
        // floating-point compare: use a tolerance via QVERIFY
        QVERIFY(qAbs(r->location().latitude  - 55.7558) < 1e-9);
        QVERIFY(qAbs(r->location().longitude - 37.6173) < 1e-9);
        QCOMPARE(r->location().radius, 250);
        QCOMPARE(r->location().trigger, QStringLiteral("on_leave"));
    }

    void json_roundtrip_relative_minute_offset()
    {
        Reminder orig;
        orig.setTaskId(QStringLiteral("t-rel"));
        orig.setType(QStringLiteral("relative"));
        orig.setMinuteOffset(-30); // 30 minutes BEFORE due time

        const QJsonObject json = orig.toJson();
        QCOMPARE(json[QStringLiteral("type")].toString(), QStringLiteral("relative"));
        // For "relative" type, due block should NOT be serialized.
        QVERIFY(!json.contains(QStringLiteral("due")));
        QCOMPARE(json[QStringLiteral("minute_offset")].toInt(), -30);

        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QCOMPARE(r->type(), QStringLiteral("relative"));
        QCOMPARE(r->minuteOffset(), -30);
    }

    void empty_taskId_is_accepted_documented()
    {
        // DOCUMENT: Reminder accepts an empty taskId. There's no constraint at
        // the model level — orphan-by-construction is allowed. DataStore
        // similarly does not validate taskId existence (see
        // addReminder_for_nonexistent_task below).
        Reminder r;
        r.setTaskId(QString());
        QVERIFY(r.taskId().isEmpty());
        // Round-trip preserves the emptiness.
        QJsonObject json = r.toJson();
        QCOMPARE(json[QStringLiteral("task_id")].toString(), QString());
        QScopedPointer<Reminder> restored(Reminder::fromJson(json));
        QVERIFY(restored->taskId().isEmpty());
    }

    void absolute_with_due_is_structurally_actionable()
    {
        // Validity is informally: type=="absolute" AND due is non-null.
        Reminder r;
        r.setType(QStringLiteral("absolute"));
        ReminderDue due;
        due.datetime = QStringLiteral("2026-09-01T08:00:00");
        r.setDue(due);
        QVERIFY(!r.due().isNull());
        // The serialized form includes the "due" block.
        QJsonObject json = r.toJson();
        QVERIFY(json.contains(QStringLiteral("due")));
    }

    void absolute_without_due_is_not_actionable()
    {
        // Without a due, an "absolute" reminder cannot be scheduled — toJson
        // omits the "due" block entirely so the scheduler has nothing to fire.
        Reminder r;
        r.setType(QStringLiteral("absolute"));
        QVERIFY(r.due().isNull());
        QJsonObject json = r.toJson();
        QVERIFY(!json.contains(QStringLiteral("due")));
    }

    void location_with_coords_is_structurally_actionable()
    {
        Reminder r;
        r.setType(QStringLiteral("location"));
        LocationObject loc;
        loc.name = QStringLiteral("Home");
        loc.latitude = 51.5074;
        loc.longitude = -0.1278;
        loc.radius = 150;
        r.setLocation(loc);
        QVERIFY(!r.location().isNull());
        QJsonObject json = r.toJson();
        QVERIFY(json.contains(QStringLiteral("location")));
    }

    void location_without_coords_is_not_actionable()
    {
        // A "location" reminder with no coordinates is rendered without a
        // "location" block — geofence service has nothing to register.
        Reminder r;
        r.setType(QStringLiteral("location"));
        QVERIFY(r.location().isNull());
        QJsonObject json = r.toJson();
        QVERIFY(!json.contains(QStringLiteral("location")));
    }

    void unknown_type_is_preserved_as_is()
    {
        // Reminder does not normalise the `type` field — any string survives
        // round-trip. (Garbage values simply make all downstream branches —
        // due / minute_offset / location — skip serialization, leaving
        // the reminder inert.)
        Reminder orig;
        orig.setType(QStringLiteral("invalid_value"));
        // Set ALL payloads — none should be serialized for an unknown type.
        ReminderDue d;
        d.datetime = QStringLiteral("2026-01-01T00:00:00");
        orig.setDue(d);
        orig.setMinuteOffset(99);
        LocationObject loc;
        loc.latitude = 1.0;
        loc.longitude = 2.0;
        orig.setLocation(loc);

        QJsonObject json = orig.toJson();
        QCOMPARE(json[QStringLiteral("type")].toString(), QStringLiteral("invalid_value"));
        QVERIFY(!json.contains(QStringLiteral("due")));
        QVERIFY(!json.contains(QStringLiteral("location")));
        QVERIFY(!json.contains(QStringLiteral("minute_offset")));

        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QCOMPARE(r->type(), QStringLiteral("invalid_value"));
    }

    void soft_delete_round_trip()
    {
        Reminder rem;
        rem.setTaskId(QStringLiteral("t-tomb"));
        rem.setIsDeleted(true);
        QJsonObject json = rem.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QVERIFY(r->isDeleted());
        QCOMPARE(r->taskId(), QStringLiteral("t-tomb"));
    }

    // ── DataStore reminder operations ─────────────────────────────────────────

    void addReminder_appears_in_remindersForTask()
    {
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));

        Reminder *rem = ds->addReminder(taskId, QStringLiteral("absolute"));
        QVERIFY(rem != nullptr);
        QCOMPARE(rem->taskId(), taskId);
        QCOMPARE(rem->type(), QStringLiteral("absolute"));

        QList<Reminder *> list = ds->remindersForTask(taskId);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first(), rem);
        QCOMPARE(ds->reminderById(rem->id()), rem);
    }

    void removeReminder_soft_deletes_and_excludes_from_query()
    {
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));
        Reminder *rem = ds->addReminder(taskId, QStringLiteral("absolute"));
        const QString remId = rem->id();

        QSignalSpy spyReminders(ds.data(), &DataStore::remindersChanged);
        QSignalSpy spyData(ds.data(), &DataStore::dataChanged);

        QVERIFY(ds->removeReminder(remId));
        // Reminder is soft-deleted, not erased: still findable by id.
        Reminder *still = ds->reminderById(remId);
        QVERIFY(still != nullptr);
        QVERIFY(still->isDeleted());
        // remindersForTask excludes tombstones.
        QCOMPARE(ds->remindersForTask(taskId).size(), 0);
        // Both signals fire on remove.
        QVERIFY(spyReminders.count() >= 1);
        QVERIFY(spyData.count() >= 1);
    }

    void removeReminder_unknown_id_returns_false()
    {
        QScopedPointer<DataStore> ds(makeStoreWithTask(nullptr));
        QVERIFY(!ds->removeReminder(QStringLiteral("does-not-exist")));
    }

    void multiple_reminders_per_task_returned_in_insertion_order()
    {
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));
        Reminder *r1 = ds->addReminder(taskId, QStringLiteral("absolute"));
        Reminder *r2 = ds->addReminder(taskId, QStringLiteral("relative"));
        Reminder *r3 = ds->addReminder(taskId, QStringLiteral("location"));

        QList<Reminder *> list = ds->remindersForTask(taskId);
        QCOMPARE(list.size(), 3);
        // remindersForTask iterates m_reminders (the QList) in order, so we
        // expect insertion order to be preserved.
        QCOMPARE(list.at(0), r1);
        QCOMPARE(list.at(1), r2);
        QCOMPARE(list.at(2), r3);
    }

    void addReminder_for_nonexistent_task_is_accepted_orphan()
    {
        // DOCUMENT: DataStore does NOT validate that taskId refers to an
        // existing Task. Reminder for a nonexistent task is accepted as an
        // orphan. Sync may pull in the matching task later, or the reminder
        // simply remains inert (ReminderScheduler skips orphans).
        QScopedPointer<DataStore> ds(makeStoreWithTask(nullptr));
        Reminder *rem = ds->addReminder(QStringLiteral("ghost-task-id"),
                                        QStringLiteral("absolute"));
        QVERIFY(rem != nullptr);
        QCOMPARE(rem->taskId(), QStringLiteral("ghost-task-id"));
        QCOMPARE(ds->remindersForTask(QStringLiteral("ghost-task-id")).size(), 1);
    }

    void task_soft_delete_cascades_tombstone_to_reminders()
    {
        // DOCUMENT: When a task is soft-deleted via DataStore::removeTask,
        // its non-deleted reminders are auto-tombstoned in the same op.
        // This is wired in DataStore::removeTask so dependent metadata stays
        // consistent in the sync log.
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));
        Reminder *r1 = ds->addReminder(taskId, QStringLiteral("absolute"));
        Reminder *r2 = ds->addReminder(taskId, QStringLiteral("relative"));

        QVERIFY(ds->removeTask(taskId));
        // Both reminders should now be soft-deleted.
        QVERIFY(r1->isDeleted());
        QVERIFY(r2->isDeleted());
        QCOMPARE(ds->remindersForTask(taskId).size(), 0);
    }

    // ── Sync metadata (CRDT _meta blob) ───────────────────────────────────────

    void meta_blob_present_after_addReminder()
    {
        // addReminder stamps initial CRDT field metadata (task_id, type,
        // minute_offset, created_at, updated_at, is_deleted). The _meta blob
        // is injected into the JSON only at DataStore::toJson() time, so
        // we round-trip via the store to observe it.
        //
        // SHAPE: single-level. found["_meta"][fieldName] == { ts, dev }.
        // (Earlier builds had a buggy double-wrap found["_meta"]["_meta"]
        //  [fieldName]; the fix landed alongside a v1->v2 schema migration
        //  in DataStore::migrateJson that flattens any legacy state.json.)
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));
        Reminder *rem = ds->addReminder(taskId, QStringLiteral("absolute"));
        const QString remId = rem->id();

        QJsonObject root = ds->toJson();
        QJsonArray remArr = root[QStringLiteral("reminders")].toArray();
        QVERIFY(!remArr.isEmpty());

        // Find the entry matching our reminder id.
        QJsonObject found;
        for (const auto &v : remArr) {
            QJsonObject obj = v.toObject();
            if (obj[QStringLiteral("id")].toString() == remId) {
                found = obj;
                break;
            }
        }
        QVERIFY(!found.isEmpty());
        QVERIFY(found.contains(QStringLiteral("_meta")));
        const QJsonObject meta = found[QStringLiteral("_meta")].toObject();
        QVERIFY2(!meta.contains(QStringLiteral("_meta")),
                 "_meta must not contain a nested '_meta' key (legacy "
                 "double-wrap bug must not return)");
        QVERIFY(!meta.isEmpty());
        // Expect at least the fields that addReminder explicitly stamps.
        QVERIFY(meta.contains(QStringLiteral("task_id")));
        QVERIFY(meta.contains(QStringLiteral("type")));
        QVERIFY(meta.contains(QStringLiteral("created_at")));
        QVERIFY(meta.contains(QStringLiteral("updated_at")));
        QVERIFY(meta.contains(QStringLiteral("is_deleted")));
    }

    void tombstone_preserved_across_full_datastore_roundtrip()
    {
        // A soft-deleted reminder should survive DataStore.toJson →
        // DataStore.loadFromJson with its is_deleted flag intact (and not be
        // returned by remindersForTask).
        QString taskId;
        QScopedPointer<DataStore> ds(makeStoreWithTask(&taskId));
        Reminder *rem = ds->addReminder(taskId, QStringLiteral("absolute"));
        const QString remId = rem->id();
        QVERIFY(ds->removeReminder(remId));
        QVERIFY(rem->isDeleted());

        QJsonObject root = ds->toJson();

        // Reload into a fresh DataStore and verify the tombstone is still
        // there (recent tombstones aren't pruned — the prune threshold is
        // 30 days old).
        QScopedPointer<DataStore> ds2(new DataStore);
        ds2->loadFromJson(root);
        Reminder *restored = ds2->reminderById(remId);
        QVERIFY(restored != nullptr);
        QVERIFY(restored->isDeleted());
        QCOMPARE(ds2->remindersForTask(taskId).size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestReminderModel)
#include "test_reminder_model.moc"
