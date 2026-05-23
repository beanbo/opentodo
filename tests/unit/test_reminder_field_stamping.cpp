// =============================================================================
// Iter-night-14 pin: Reminder per-field CRDT _meta must be written by
// (a) addReminder's initial stamp list (now includes due/location/
//     recurrence_rule alongside the existing task_id/type/minute_offset),
// (b) the new DataStore::markReminderFieldsModified Q_INVOKABLE called
//     from QML after setDueDatetime / setLocationCoords / setRepeatRule /
//     setMinuteOffset mutate the model object directly.
//
// Pre-fix:
//   - addReminder stamped only task_id/type/minute_offset/created_at/
//     updated_at/is_deleted. Due, location, and recurrence_rule had no
//     baseline meta at all.
//   - QML wrote the actual due/location/RRULE/offset values via model
//     setters and called only `dataStore.markDataChanged()` (which emits
//     signals but does not stamp). No markReminderFieldsModified helper
//     existed.
//   - On cross-device sync, Merger's lFm.isEmpty() / "remote with meta
//     wins over local without" branches silently overwrote the freshly-
//     set due time / geofence / RRULE with a peer's stale (or empty)
//     value.
//
// This pin file locks the post-fix shape: every Reminder _meta entry that
// the Merger needs to compare advances when the QML touches that field.
// =============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/reminder.h"

namespace {

// All keys Reminder::toJson() emits at the top level (sourced by reading
// src/model/reminder.cpp:96+). Keep in sync if a field is added there.
static QSet<QString> validReminderJsonKeys()
{
    return {
        QStringLiteral("id"),
        QStringLiteral("task_id"),
        QStringLiteral("type"),
        QStringLiteral("due"),
        QStringLiteral("minute_offset"),
        QStringLiteral("location"),
        QStringLiteral("recurrence_rule"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
}

static QJsonObject metaForReminder(const QJsonObject &state,
                                   const QString &reminderId)
{
    const QJsonArray reminders = state.value(QStringLiteral("reminders")).toArray();
    for (const auto &v : reminders) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("id")).toString() == reminderId)
            return r.value(QStringLiteral("_meta")).toObject();
    }
    return {};
}

static QString hlcFor(const QJsonObject &meta, const QString &field)
{
    const QJsonObject entry = meta.value(field).toObject();
    return entry.value(QStringLiteral("ts")).toString();
}

static void assertMetaKeysValid(const QJsonObject &meta, const char *site)
{
    const QSet<QString> valid = validReminderJsonKeys();
    for (auto it = meta.begin(); it != meta.end(); ++it) {
        QVERIFY2(valid.contains(it.key()),
                 qPrintable(QStringLiteral("[%1] _meta key '%2' is not a "
                                            "valid Reminder JSON key — "
                                            "Merger will never consult it")
                                .arg(QLatin1String(site), it.key())));
    }
}

} // anonymous namespace


class TestReminderFieldStamping : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    QString taskId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        Task *t = store->addTask(QStringLiteral("Buy milk"), inbox->id());
        taskId = t->id();
    }

    void cleanup()
    {
        delete store; store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) addReminder stamps the new initial-fields list — including `due`,
    //    `location`, `recurrence_rule` that pre-fix had no baseline meta.
    //    Without these initial stamps, the QML-side markReminderFieldsModified
    //    call would only land the post-edit stamp, but the legacy peer or
    //    any restored older snapshot may still arrive with no _meta at
    //    all → Merger falls back to whole-entity LWW.
    // ─────────────────────────────────────────────────────────────────────────
    void addReminder_seedsAllToplevelFields_includingDueAndLocation()
    {
        Reminder *r = store->addReminder(taskId, QStringLiteral("absolute"));
        QVERIFY(r);
        const QJsonObject meta = metaForReminder(store->toJson(), r->id());

        // Pre-fix stamps that must keep working.
        QVERIFY(meta.contains(QStringLiteral("task_id")));
        QVERIFY(meta.contains(QStringLiteral("type")));
        QVERIFY(meta.contains(QStringLiteral("minute_offset")));
        QVERIFY(meta.contains(QStringLiteral("created_at")));
        QVERIFY(meta.contains(QStringLiteral("updated_at")));
        QVERIFY(meta.contains(QStringLiteral("is_deleted")));

        // Iter-night-14 additions:
        QVERIFY2(meta.contains(QStringLiteral("due")),
                 "addReminder must seed initial _meta for `due` so a peer "
                 "without our QML stamp doesn't whole-entity LWW over us.");
        QVERIFY2(meta.contains(QStringLiteral("location")),
                 "addReminder must seed initial _meta for `location`.");
        QVERIFY2(meta.contains(QStringLiteral("recurrence_rule")),
                 "addReminder must seed initial _meta for `recurrence_rule`.");
        assertMetaKeysValid(meta, "addReminder");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) markReminderFieldsModified advances the per-field HLC. Mirrors
    //    the test_filtervm_field_stamping pattern; without the HLC bump on
    //    the OUTBOUND mutation path, Merger sees equal field meta on
    //    both peers and the "remoteWins = false" fallback silently keeps
    //    the local stale value forever.
    // ─────────────────────────────────────────────────────────────────────────
    void markReminderFieldsModified_advancesDueHlc()
    {
        Reminder *r = store->addReminder(taskId, QStringLiteral("absolute"));
        QVERIFY(r);
        const QString before = hlcFor(metaForReminder(store->toJson(), r->id()),
                                      QStringLiteral("due"));
        QVERIFY(!before.isEmpty());

        QThread::msleep(2);
        // Mimics the QML editor path: model setter, then stamp.
        r->setDueDatetime(QStringLiteral("2026-05-04T10:00:00"));
        store->markReminderFieldsModified(r->id(), {QStringLiteral("due")});

        const QJsonObject meta = metaForReminder(store->toJson(), r->id());
        const QString after = hlcFor(meta, QStringLiteral("due"));
        QVERIFY2(after > before,
                 qPrintable(QStringLiteral(
                     "Iter-night-14: markReminderFieldsModified must advance "
                     "the `due` HLC (was %1, after %2). Without this Merger "
                     "sees identical field meta on both peers and neither "
                     "side's due time wins.").arg(before, after)));
        assertMetaKeysValid(meta, "markReminderFieldsModified(due)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) recurrence_rule HLC advances independently when the user toggles
    //    a repeat. Pinned because the QML editor conditionally pushes
    //    "recurrence_rule" into the stamp list — verify the helper honors
    //    that and does not stamp other fields by accident.
    // ─────────────────────────────────────────────────────────────────────────
    void markReminderFieldsModified_advancesRecurrenceRuleHlcOnly()
    {
        Reminder *r = store->addReminder(taskId, QStringLiteral("absolute"));
        const QString dueBefore = hlcFor(metaForReminder(store->toJson(), r->id()),
                                         QStringLiteral("due"));
        const QString rrBefore  = hlcFor(metaForReminder(store->toJson(), r->id()),
                                         QStringLiteral("recurrence_rule"));

        QThread::msleep(2);
        r->setRepeatRule(QStringLiteral("FREQ=DAILY"));
        store->markReminderFieldsModified(r->id(),
                                          {QStringLiteral("recurrence_rule")});

        const QJsonObject meta = metaForReminder(store->toJson(), r->id());
        QCOMPARE(hlcFor(meta, QStringLiteral("due")), dueBefore);
        QVERIFY(hlcFor(meta, QStringLiteral("recurrence_rule")) > rrBefore);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Location-based reminder: `location` HLC advances on geofence edits.
    // ─────────────────────────────────────────────────────────────────────────
    void markReminderFieldsModified_advancesLocationHlc()
    {
        Reminder *r = store->addReminder(taskId, QStringLiteral("location"));
        QVERIFY(r);
        const QString before = hlcFor(metaForReminder(store->toJson(), r->id()),
                                      QStringLiteral("location"));
        QVERIFY(!before.isEmpty());

        QThread::msleep(2);
        r->setLocationCoords(55.75, 37.62, 200,
                             QStringLiteral("Home"),
                             QStringLiteral("on_enter"));
        store->markReminderFieldsModified(r->id(),
                                          {QStringLiteral("location")});

        const QJsonObject meta = metaForReminder(store->toJson(), r->id());
        QVERIFY(hlcFor(meta, QStringLiteral("location")) > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) markReminderFieldsModified on a NON-EXISTENT id is a safe no-op
    //    (mirrors markFilterFieldsModified / markTaskFieldsModified guard).
    // ─────────────────────────────────────────────────────────────────────────
    void markReminderFieldsModified_unknownId_isNoOp()
    {
        // Should not crash; should not touch any other reminder.
        store->markReminderFieldsModified(QStringLiteral("does-not-exist"),
                                          {QStringLiteral("due")});

        const QJsonObject state = store->toJson();
        const QJsonArray reminders =
            state.value(QStringLiteral("reminders")).toArray();
        QCOMPARE(reminders.size(), 0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6) updated_at advances on any field stamp (matches the markLabel/
    //    markFilter/markTask convention).
    // ─────────────────────────────────────────────────────────────────────────
    void markReminderFieldsModified_bumpsUpdatedAt()
    {
        Reminder *r = store->addReminder(taskId, QStringLiteral("absolute"));
        const QString before = hlcFor(metaForReminder(store->toJson(), r->id()),
                                      QStringLiteral("updated_at"));
        QThread::msleep(2);
        store->markReminderFieldsModified(r->id(),
                                          {QStringLiteral("due")});
        const QString after = hlcFor(metaForReminder(store->toJson(), r->id()),
                                     QStringLiteral("updated_at"));
        QVERIFY(after > before);
    }
};

QTEST_GUILESS_MAIN(TestReminderFieldStamping)
#include "test_reminder_field_stamping.moc"
