// =============================================================================
// Pin test — every key passed to DataStore::markTaskFieldsModified MUST appear
// as a top-level key in Task::toJson(). Otherwise the per-field _meta blob
// holds dead stamps that Merger never consults (it iterates the union of real
// JSON keys), and a "fix" silently falls back to whole-entity LWW.
//
// Iteration 2 of the bug-hunt loop landed five such mismatches — `label_ids`,
// `due_date`/`due_datetime`/`due_string`/etc., `duration_amount`/`_unit`,
// missing `completed_at`, and the recurring-advance path skipping the stamp
// entirely. These tests pin the iter-3 corrections so future refactors of
// Task::toJson can't drift the names apart again without a failing build.
// =============================================================================

#include <QtTest>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/taskviewmodel.h"

namespace {

// All keys that Task::toJson() can ever emit at the top level. Sourced by
// reading src/model/task.cpp:toJson — bump this list when adding a field
// there. If a key falls out of toJson, removing it here makes the assertion
// noisy at the call site that still stamps it.
static QSet<QString> validTaskJsonKeys()
{
    return {
        QStringLiteral("id"),
        QStringLiteral("content"),
        QStringLiteral("description"),
        QStringLiteral("project_id"),
        QStringLiteral("section_id"),
        QStringLiteral("parent_id"),
        QStringLiteral("order"),
        QStringLiteral("priority"),
        QStringLiteral("labels"),
        QStringLiteral("is_completed"),
        QStringLiteral("completed_at"),
        QStringLiteral("due"),
        QStringLiteral("deadline"),
        QStringLiteral("duration"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
        QStringLiteral("day_order"),
        QStringLiteral("color"),
        QStringLiteral("is_pinned"),
        QStringLiteral("estimate_minutes"),
    };
}

// Extract _meta blob for a given task id from a DataStore::toJson() snapshot.
static QJsonObject metaFor(const QJsonObject &state, const QString &taskId)
{
    const QJsonArray tasks = state.value(QStringLiteral("tasks")).toArray();
    for (const auto &v : tasks) {
        const QJsonObject t = v.toObject();
        if (t.value(QStringLiteral("id")).toString() == taskId)
            return t.value(QStringLiteral("_meta")).toObject();
    }
    return {};
}

// Assert every key in `meta` is a recognised top-level Task JSON field.
static void assertMetaKeysValid(const QJsonObject &meta, const char *site)
{
    const QSet<QString> valid = validTaskJsonKeys();
    for (auto it = meta.begin(); it != meta.end(); ++it) {
        QVERIFY2(valid.contains(it.key()),
                 qPrintable(QStringLiteral("[%1] _meta key '%2' is not a valid Task JSON key — Merger will never consult it")
                                .arg(QLatin1String(site), it.key())));
    }
}

} // namespace

class TestTaskVmFieldKeysPin : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    TaskViewModel *vm = nullptr;
    QString inboxId;
    QString taskId;

private slots:
    void init()
    {
        store = new DataStore;
        Project *inbox = store->addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();

        vm = new TaskViewModel(store);
        taskId = vm->addTask(QStringLiteral("pin-target"), inboxId,
                             /*priority=*/2, /*dueString=*/{}, /*labels=*/{});
        QVERIFY(!taskId.isEmpty());
    }

    void cleanup()
    {
        delete vm; vm = nullptr;
        delete store; store = nullptr;
    }

    // Regress pin: iter-2 stamped "label_ids", but Task::toJson emits "labels".
    void updateTask_labels_stampsRealJsonKey()
    {
        QVERIFY(vm->updateTask(taskId, QStringLiteral("labels"),
                               QStringList{QStringLiteral("work")}));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("labels")),
                 "updateTask(labels) must stamp the 'labels' JSON key (not 'label_ids')");
        assertMetaKeysValid(meta, "updateTask(labels)");
    }

    // Regress pin: iter-2 stamped due_date/due_datetime/etc, but Task::toJson
    // emits a single nested "due" object.
    void setTaskDue_stampsParentDueKey()
    {
        QVERIFY(vm->setTaskDue(taskId, QStringLiteral("tomorrow")));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("due")),
                 "setTaskDue must stamp the 'due' JSON key (not due_date/due_datetime/...)");
        QVERIFY2(!meta.contains(QStringLiteral("due_date")),
                 "'due_date' is not a real top-level JSON key");
        QVERIFY2(!meta.contains(QStringLiteral("due_datetime")),
                 "'due_datetime' is not a real top-level JSON key");
        assertMetaKeysValid(meta, "setTaskDue");
    }

    // Regress pin: iter-2 stamped duration_amount/duration_unit, but
    // Task::toJson emits a single nested "duration" object.
    void updateTask_duration_stampsParentDurationKey()
    {
        QVERIFY(vm->updateTask(taskId, QStringLiteral("durationAmount"), 30));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("duration")),
                 "updateTask(durationAmount) must stamp 'duration' (not 'duration_amount')");
        QVERIFY2(!meta.contains(QStringLiteral("duration_amount")),
                 "'duration_amount' is not a real top-level JSON key");
        assertMetaKeysValid(meta, "updateTask(durationAmount)");
    }

    // Regress pin: completeTask must stamp BOTH is_completed AND completed_at,
    // because Task::setIsCompleted(true) mutates both in lockstep.
    void completeTask_stampsBothIsCompletedAndCompletedAt()
    {
        QVERIFY(vm->completeTask(taskId));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("is_completed")),
                 "completeTask must stamp 'is_completed'");
        QVERIFY2(meta.contains(QStringLiteral("completed_at")),
                 "completeTask must stamp 'completed_at' (the setter mutates it in lockstep)");
        assertMetaKeysValid(meta, "completeTask");
    }

    // Regress pin: iter-3 missed three bulk mutators (bulkSetDue/Color/Deadline)
    // — they still went through markTaskModified, leaving _meta.due/color/
    // deadline stale. The single-field equivalents (setTaskDue, updateTask)
    // already stamp correctly. iter-4 fixed all three; this test pins them.
    void bulkSetDue_stampsDueKey()
    {
        vm->selectTask(taskId);
        vm->bulkSetDue(QStringLiteral("tomorrow"));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("due")),
                 "bulkSetDue must stamp 'due'");
        assertMetaKeysValid(meta, "bulkSetDue");
    }

    void bulkSetColor_stampsColorKey()
    {
        vm->selectTask(taskId);
        vm->bulkSetColor(QStringLiteral("red"));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("color")),
                 "bulkSetColor must stamp 'color'");
        assertMetaKeysValid(meta, "bulkSetColor");
    }

    void bulkSetDeadline_stampsDeadlineKey()
    {
        vm->selectTask(taskId);
        vm->bulkSetDeadline(QDate(2026, 12, 31));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("deadline")),
                 "bulkSetDeadline must stamp 'deadline'");
        assertMetaKeysValid(meta, "bulkSetDeadline");
    }

    void bulkSetPriority_stampsPriorityKey()
    {
        vm->selectTask(taskId);
        vm->bulkSetPriority(4);
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("priority")),
                 "bulkSetPriority must stamp 'priority'");
        assertMetaKeysValid(meta, "bulkSetPriority");
    }

    void bulkAddLabel_stampsLabelsKey()
    {
        vm->selectTask(taskId);
        vm->bulkAddLabel(QStringLiteral("work"));
        const QJsonObject meta = metaFor(store->toJson(), taskId);
        QVERIFY2(meta.contains(QStringLiteral("labels")),
                 "bulkAddLabel must stamp 'labels'");
        assertMetaKeysValid(meta, "bulkAddLabel");
    }

    // Smoke: every other simple-scalar mutator on the pin surface stamps a
    // valid key set. Catches future drift if Task::toJson renames a field.
    void variousMutators_allStampValidKeys()
    {
        QVERIFY(vm->updateTask(taskId, QStringLiteral("content"),
                               QStringLiteral("renamed")));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(content)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("priority"), 4));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(priority)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("description"),
                               QStringLiteral("notes")));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(description)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("color"),
                               QStringLiteral("red")));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(color)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("estimateMinutes"), 15));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(estimateMinutes)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("isPinned"), true));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(isPinned)");

        QVERIFY(vm->updateTask(taskId, QStringLiteral("deadline"),
                               QStringLiteral("2026-12-31")));
        assertMetaKeysValid(metaFor(store->toJson(), taskId), "updateTask(deadline)");
    }
};

QTEST_GUILESS_MAIN(TestTaskVmFieldKeysPin)
#include "test_taskvm_field_keys_pin.moc"
