// =============================================================================
// test_applycommit_meta_preservation.cpp
//
// Regression coverage for the Q2.17-D / Q2.18-A CRDT bug:
//
//   SyncEngine::applyCommit's *_update branches built a `patched` JSON object
//   via applyPatch() that DID preserve the inbound commit's per-field _meta
//   block, but then only invoked the scalar setters (Task::setContent,
//   setPriority, …). The merged _meta was NEVER pushed back into
//   DataStore::m_taskMeta (or the equivalent per-entity meta maps), and the
//   subsequent markTaskModified() call re-stamped updated_at with the LOCAL
//   HLC — silently overwriting any inbound stamp.
//
//   Net effect: per-field LWW resolution silently degraded to whole-object
//   LWW for inbound updates. Two devices editing different fields
//   concurrently could lose changes after sync.
//
// Fix:
//   1. After applying patched scalars in each *_update branch, push the
//      patched _meta back into the DataStore via the new
//      DataStore::applyInbound<Entity>Meta(id, mergedMeta) entry points.
//      These do a PER-FIELD merge (inbound stamps overwrite local stamps
//      for fields they explicitly carry; local-only stamps survive).
//   2. DROP the markTask/Project/LabelModified() calls in applyCommit — they
//      were inappropriate for inbound changes; the merger has already
//      determined field stamps.
//
// Coverage in this file (slot count: 6):
//
//   1. inbound_task_update_preserves_remote_meta
//   2. inbound_task_update_does_not_clobber_unrelated_fields
//   3. mark_task_modified_no_longer_called_during_applyCommit
//   4. project_section_label_filter_reminder_comment_update_all_preserve_meta
//   5. inbound_local_only_field_meta_is_preserved_when_remote_silent
//   6. older_remote_meta_still_propagates_for_fields_only_remote_touched
//
// All assertions read DataStore::toJson() which calls injectMeta(), so the
// per-entity _meta we see in the serialised state is the EXACT meta map
// that Merger::resolveWithCrdt will consult on the next sync. That is the
// production-relevant surface.
// =============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/reminder.h"
#include "model/comment.h"
#include "storage/datastore.h"
#include "sync/commit.h"
#include "sync/syncengine.h"

// ── File-scope helpers ───────────────────────────────────────────────────────

namespace {

constexpr const char *kDevA = "device-aaa";
constexpr const char *kDevB = "device-bbb";

// Build an HLC timestamp string mirroring HybridLogicalClock::format().
// Used to fabricate deterministic per-field stamps.
QString makeHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

QJsonObject fieldStamp(const QString &ts, const QString &dev)
{
    QJsonObject stamp;
    stamp[QStringLiteral("ts")]  = ts;
    stamp[QStringLiteral("dev")] = dev;
    return stamp;
}

// Locate the serialised entity matching `id` in `arr`.
QJsonObject findById(const QJsonArray &arr, const QString &id)
{
    for (const auto &v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString() == id)
            return obj;
    }
    return QJsonObject();
}

// Pull a single entity's _meta blob out of a fresh DataStore::toJson()
// snapshot. Returns an empty object if the entity is missing.
QJsonObject entityMeta(const DataStore &store,
                       const QString &collection,
                       const QString &id)
{
    const QJsonObject snap = store.toJson();
    const QJsonArray arr   = snap.value(collection).toArray();
    const QJsonObject obj  = findById(arr, id);
    return obj.value(QStringLiteral("_meta")).toObject();
}

QJsonObject fieldMetaFromStore(const DataStore &store,
                               const QString &collection,
                               const QString &id,
                               const QString &field)
{
    return entityMeta(store, collection, id).value(field).toObject();
}

} // namespace

// ── Test class ───────────────────────────────────────────────────────────────
//
// NOTE: declared with the EXACT name `TestApplyCommitMetaPreservation` so the
// friend declaration in src/sync/syncengine.h can grant access to the
// private applyCommit() method without exposing it to production code.
class TestApplyCommitMetaPreservation : public QObject
{
    Q_OBJECT

private:
    // Apply a list of operations as a single inbound commit via the
    // engine's private applyCommit(). We can do this thanks to the friend
    // declaration in syncengine.h.
    static void applyInboundCommit(SyncEngine &engine,
                                   const QList<SyncOperation> &ops,
                                   const QString &remoteHlc,
                                   const QString &remoteDeviceId)
    {
        Commit commit;
        commit.setDeviceId(remoteDeviceId);
        commit.setHlc(remoteHlc);
        commit.setOperations(ops);
        engine.applyCommit(&commit);
    }

    static SyncOperation makeUpdateOp(const QString &type,
                                      const QString &entityId,
                                      const QJsonObject &patchFields,
                                      const QJsonObject &metaPatch)
    {
        SyncOperation op;
        op.type     = type;
        op.entityId = entityId;
        QJsonObject data = patchFields;
        if (!metaPatch.isEmpty())
            data[QStringLiteral("_meta")] = metaPatch;
        op.data = data;
        return op;
    }

private slots:
    // ── Test fixture isolation ───────────────────────────────────────────────
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ApplyCommitMeta"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_applycommit_meta_preservation"));
    }

    // =========================================================================
    // SLOT 1 — Core invariant: inbound _meta replaces local stamp for the
    //           fields it touches; local stamps for OTHER fields survive.
    // =========================================================================
    void inbound_task_update_preserves_remote_meta()
    {
        DataStore store;
        SyncEngine engine(&store);

        // Local task with two stamped fields — content & priority both bear
        // device-A stamps with a low HLC.
        Project *proj = store.addProject(QStringLiteral("Pinned Project"));
        QVERIFY(proj != nullptr);
        Task *task = store.addTask(QStringLiteral("Original content"),
                                   proj->id());
        QVERIFY(task != nullptr);

        // Replace the addTask-generated stamps with deterministic device-A
        // stamps so the assertion below can compare ts/dev exactly.
        const QString tsLocal  = makeHlc(10, 0, QString::fromLatin1(kDevA));
        const QString tsRemote = makeHlc(20, 0, QString::fromLatin1(kDevB));

        QJsonObject localMeta;
        localMeta[QStringLiteral("content")]  = fieldStamp(tsLocal, kDevA);
        localMeta[QStringLiteral("priority")] = fieldStamp(tsLocal, kDevA);
        store.applyInboundTaskMeta(task->id(), localMeta);

        // Inbound commit: bumps priority to 2 and stamps ONLY priority with
        // a newer device-B HLC. Content's local stamp must survive.
        QJsonObject patch;
        patch[QStringLiteral("priority")] = 2;

        QJsonObject metaPatch;
        metaPatch[QStringLiteral("priority")] = fieldStamp(tsRemote, kDevB);

        applyInboundCommit(
            engine,
            {makeUpdateOp(QStringLiteral("task_update"),
                          task->id(), patch, metaPatch)},
            tsRemote, QString::fromLatin1(kDevB));

        // Verify state changes:
        QCOMPARE(task->content(), QStringLiteral("Original content"));
        QCOMPARE(task->priority(), 2);

        // Verify meta:
        const QJsonObject finalMeta =
            entityMeta(store, QStringLiteral("tasks"), task->id());

        // Priority stamp is the inbound device-B stamp.
        const QJsonObject pStamp =
            finalMeta.value(QStringLiteral("priority")).toObject();
        QCOMPARE(pStamp.value(QStringLiteral("ts")).toString(),  tsRemote);
        QCOMPARE(pStamp.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevB));

        // Content stamp is the local stamp — untouched by the inbound
        // commit because it did not appear in metaPatch.
        const QJsonObject cStamp =
            finalMeta.value(QStringLiteral("content")).toObject();
        QCOMPARE(cStamp.value(QStringLiteral("ts")).toString(),  tsLocal);
        QCOMPARE(cStamp.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevA));
    }

    // =========================================================================
    // SLOT 2 — Subset-field patch: stamps for fields the patch does NOT
    //           mention must be preserved verbatim.
    // =========================================================================
    void inbound_task_update_does_not_clobber_unrelated_fields()
    {
        DataStore store;
        SyncEngine engine(&store);

        Project *proj = store.addProject(QStringLiteral("Subset Project"));
        QVERIFY(proj != nullptr);
        Task *task = store.addTask(QStringLiteral("Hello"), proj->id());
        QVERIFY(task != nullptr);

        const QString tsLocalA = makeHlc(100, 0, QString::fromLatin1(kDevA));
        const QString tsLocalB = makeHlc(101, 0, QString::fromLatin1(kDevA));
        const QString tsLocalC = makeHlc(102, 0, QString::fromLatin1(kDevA));
        const QString tsRemote = makeHlc(200, 0, QString::fromLatin1(kDevB));

        QJsonObject localMeta;
        localMeta[QStringLiteral("content")]     = fieldStamp(tsLocalA, kDevA);
        localMeta[QStringLiteral("description")] = fieldStamp(tsLocalB, kDevA);
        localMeta[QStringLiteral("priority")]    = fieldStamp(tsLocalC, kDevA);
        store.applyInboundTaskMeta(task->id(), localMeta);

        // Inbound mentions only content. Description and priority stamps
        // must remain at their local-A values.
        QJsonObject patch;
        patch[QStringLiteral("content")] = QStringLiteral("Привет");

        QJsonObject metaPatch;
        metaPatch[QStringLiteral("content")] = fieldStamp(tsRemote, kDevB);

        applyInboundCommit(
            engine,
            {makeUpdateOp(QStringLiteral("task_update"),
                          task->id(), patch, metaPatch)},
            tsRemote, QString::fromLatin1(kDevB));

        QCOMPARE(task->content(), QStringLiteral("Привет"));

        const QJsonObject finalMeta =
            entityMeta(store, QStringLiteral("tasks"), task->id());

        QCOMPARE(finalMeta.value(QStringLiteral("content")).toObject()
                          .value(QStringLiteral("ts")).toString(),
                 tsRemote);
        QCOMPARE(finalMeta.value(QStringLiteral("description")).toObject()
                          .value(QStringLiteral("ts")).toString(),
                 tsLocalB);
        QCOMPARE(finalMeta.value(QStringLiteral("priority")).toObject()
                          .value(QStringLiteral("ts")).toString(),
                 tsLocalC);
        QCOMPARE(finalMeta.value(QStringLiteral("priority")).toObject()
                          .value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevA));
    }

    // =========================================================================
    // SLOT 3 — markTaskModified() is no longer fired by applyCommit. The
    //           DataStore::tasksChanged signal must fire EXACTLY ONCE per
    //           inbound task_update (from applyInboundTaskMeta), not twice
    //           (once for the setter side-effects + once for markTaskModified).
    //           Pre-fix the count would have been at least 1 from
    //           markTaskModified plus the bug-driven extra stamp; post-fix
    //           it is exactly one.
    // =========================================================================
    void mark_task_modified_no_longer_called_during_applyCommit()
    {
        DataStore store;
        SyncEngine engine(&store);

        Project *proj = store.addProject(QStringLiteral("Signal Project"));
        QVERIFY(proj != nullptr);
        Task *task = store.addTask(QStringLiteral("init"), proj->id());
        QVERIFY(task != nullptr);

        const QString tsRemote = makeHlc(500, 0, QString::fromLatin1(kDevB));

        // Spy AFTER addTask/addProject so we only count emissions caused
        // by applyCommit().
        QSignalSpy tasksSpy(&store, &DataStore::tasksChanged);

        QJsonObject patch;
        patch[QStringLiteral("priority")] = 3;

        QJsonObject metaPatch;
        metaPatch[QStringLiteral("priority")] = fieldStamp(tsRemote, kDevB);

        applyInboundCommit(
            engine,
            {makeUpdateOp(QStringLiteral("task_update"),
                          task->id(), patch, metaPatch)},
            tsRemote, QString::fromLatin1(kDevB));

        // The only emission must come from applyInboundTaskMeta.
        QCOMPARE(tasksSpy.count(), 1);

        // And the inbound stamp must have landed (the meta we expected).
        const QJsonObject fm =
            fieldMetaFromStore(store, QStringLiteral("tasks"),
                               task->id(), QStringLiteral("priority"));
        QCOMPARE(fm.value(QStringLiteral("ts")).toString(),  tsRemote);
        QCOMPARE(fm.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevB));
    }

    // =========================================================================
    // SLOT 4 — Sanity sweep for every other *_update branch: project,
    //           section, label, filter, reminder, comment. Each must push
    //           inbound _meta back into its respective meta map. Combined
    //           into a single slot to keep the test surface narrow — six
    //           assertions, six entity types, one fixture.
    // =========================================================================
    void project_section_label_filter_reminder_comment_update_all_preserve_meta()
    {
        DataStore store;
        SyncEngine engine(&store);

        // ── Project ────────────────────────────────────────────────────────
        Project *proj = store.addProject(QStringLiteral("Mark"));
        QVERIFY(proj != nullptr);
        const QString tsProj = makeHlc(11, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("name")] = QStringLiteral("Renamed");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("name")] = fieldStamp(tsProj, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("project_update"),
                              proj->id(), patch, metaPatch)},
                tsProj, QString::fromLatin1(kDevB));
        }
        QCOMPARE(proj->name(), QStringLiteral("Renamed"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("projects"),
                                    proj->id(), QStringLiteral("name"))
                     .value(QStringLiteral("ts")).toString(),
                 tsProj);

        // ── Section ────────────────────────────────────────────────────────
        Section *sec = store.addSection(QStringLiteral("Old name"), proj->id());
        QVERIFY(sec != nullptr);
        const QString tsSec = makeHlc(12, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("name")] = QStringLiteral("New section name");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("name")] = fieldStamp(tsSec, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("section_update"),
                              sec->id(), patch, metaPatch)},
                tsSec, QString::fromLatin1(kDevB));
        }
        QCOMPARE(sec->name(), QStringLiteral("New section name"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("sections"),
                                    sec->id(), QStringLiteral("name"))
                     .value(QStringLiteral("ts")).toString(),
                 tsSec);

        // ── Label ──────────────────────────────────────────────────────────
        Label *lbl = store.addLabel(QStringLiteral("home"));
        QVERIFY(lbl != nullptr);
        const QString tsLbl = makeHlc(13, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("color")] = QStringLiteral("crimson");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("color")] = fieldStamp(tsLbl, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("label_update"),
                              lbl->id(), patch, metaPatch)},
                tsLbl, QString::fromLatin1(kDevB));
        }
        QCOMPARE(lbl->color(), QStringLiteral("crimson"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("labels"),
                                    lbl->id(), QStringLiteral("color"))
                     .value(QStringLiteral("ts")).toString(),
                 tsLbl);

        // ── Filter ─────────────────────────────────────────────────────────
        Filter *flt = store.addFilter(QStringLiteral("All"),
                                      QStringLiteral("view all"));
        QVERIFY(flt != nullptr);
        const QString tsFlt = makeHlc(14, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("query")] = QStringLiteral("today");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("query")] = fieldStamp(tsFlt, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("filter_update"),
                              flt->id(), patch, metaPatch)},
                tsFlt, QString::fromLatin1(kDevB));
        }
        QCOMPARE(flt->query(), QStringLiteral("today"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("filters"),
                                    flt->id(), QStringLiteral("query"))
                     .value(QStringLiteral("ts")).toString(),
                 tsFlt);

        // ── Reminder ───────────────────────────────────────────────────────
        Task *task = store.addTask(QStringLiteral("Reminder task"),
                                   proj->id());
        QVERIFY(task != nullptr);
        Reminder *rem = store.addReminder(task->id(),
                                          QStringLiteral("absolute"));
        QVERIFY(rem != nullptr);
        const QString tsRem = makeHlc(15, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("type")] = QStringLiteral("relative");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("type")] = fieldStamp(tsRem, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("reminder_update"),
                              rem->id(), patch, metaPatch)},
                tsRem, QString::fromLatin1(kDevB));
        }
        QCOMPARE(rem->type(), QStringLiteral("relative"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("reminders"),
                                    rem->id(), QStringLiteral("type"))
                     .value(QStringLiteral("ts")).toString(),
                 tsRem);

        // ── Comment ────────────────────────────────────────────────────────
        Comment *cmt = store.addComment(QStringLiteral("Hi"),
                                        task->id(), proj->id());
        QVERIFY(cmt != nullptr);
        const QString tsCmt = makeHlc(16, 0, QString::fromLatin1(kDevB));
        {
            QJsonObject patch;
            patch[QStringLiteral("content")] = QStringLiteral("Bye");
            QJsonObject metaPatch;
            metaPatch[QStringLiteral("content")] = fieldStamp(tsCmt, kDevB);
            applyInboundCommit(
                engine,
                {makeUpdateOp(QStringLiteral("comment_update"),
                              cmt->id(), patch, metaPatch)},
                tsCmt, QString::fromLatin1(kDevB));
        }
        QCOMPARE(cmt->content(), QStringLiteral("Bye"));
        QCOMPARE(fieldMetaFromStore(store, QStringLiteral("comments"),
                                    cmt->id(), QStringLiteral("content"))
                     .value(QStringLiteral("ts")).toString(),
                 tsCmt);
    }

    // =========================================================================
    // SLOT 5 — Locally-stamped field absent from the inbound _meta map must
    //           survive the apply intact. Variant of slot 2 that also asserts
    //           the local "dev" identity is retained — pre-fix this was the
    //           field most clearly clobbered by markTaskModified's local-HLC
    //           re-stamp of updated_at.
    // =========================================================================
    void inbound_local_only_field_meta_is_preserved_when_remote_silent()
    {
        DataStore store;
        SyncEngine engine(&store);

        Project *proj = store.addProject(QStringLiteral("LocalOnly"));
        QVERIFY(proj != nullptr);
        Task *task = store.addTask(QStringLiteral("body"), proj->id());
        QVERIFY(task != nullptr);

        const QString tsLocalUpdated = makeHlc(42, 7,
                                               QString::fromLatin1(kDevA));
        const QString tsRemote       = makeHlc(99, 0,
                                               QString::fromLatin1(kDevB));

        // Local has a custom updated_at stamp with deviceId = A. Inbound
        // commit touches priority only — updated_at stamp must stay at A.
        QJsonObject localMeta;
        localMeta[QStringLiteral("updated_at")] =
            fieldStamp(tsLocalUpdated, kDevA);
        store.applyInboundTaskMeta(task->id(), localMeta);

        QJsonObject patch;
        patch[QStringLiteral("priority")] = 4;
        QJsonObject metaPatch;
        metaPatch[QStringLiteral("priority")] = fieldStamp(tsRemote, kDevB);

        applyInboundCommit(
            engine,
            {makeUpdateOp(QStringLiteral("task_update"),
                          task->id(), patch, metaPatch)},
            tsRemote, QString::fromLatin1(kDevB));

        const QJsonObject finalMeta =
            entityMeta(store, QStringLiteral("tasks"), task->id());

        // updated_at stamp must be UNCHANGED (still device-A with the local
        // HLC) — proving applyCommit no longer re-stamps it locally.
        const QJsonObject uStamp =
            finalMeta.value(QStringLiteral("updated_at")).toObject();
        QCOMPARE(uStamp.value(QStringLiteral("ts")).toString(),
                 tsLocalUpdated);
        QCOMPARE(uStamp.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevA));

        // Priority stamp landed.
        const QJsonObject pStamp =
            finalMeta.value(QStringLiteral("priority")).toObject();
        QCOMPARE(pStamp.value(QStringLiteral("ts")).toString(),  tsRemote);
        QCOMPARE(pStamp.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevB));
    }

    // =========================================================================
    // SLOT 6 — If the inbound commit references a field with an OLDER stamp
    //           than the local one, the merger should — eventually, during
    //           a real sync — discard it. But applyCommit's contract is
    //           "trust the wire": the merger has already decided. So
    //           applyCommit DOES overwrite even with an older HLC. This slot
    //           pins that exact contract so the next maintainer doesn't
    //           "fix" it by sneaking comparison logic into applyCommit.
    //
    //           Pre-fix this contract was inverted in a different way:
    //           markTaskModified would unconditionally bump updated_at, so
    //           there was no way to distinguish "newer wire stamp" vs
    //           "older wire stamp" at all.
    // =========================================================================
    void older_remote_meta_still_propagates_for_fields_only_remote_touched()
    {
        DataStore store;
        SyncEngine engine(&store);

        Project *proj = store.addProject(QStringLiteral("OlderWire"));
        QVERIFY(proj != nullptr);
        Task *task = store.addTask(QStringLiteral("text"), proj->id());
        QVERIFY(task != nullptr);

        const QString tsLocalNewer = makeHlc(10000, 0,
                                             QString::fromLatin1(kDevA));
        const QString tsRemoteOlder = makeHlc(1000, 0,
                                              QString::fromLatin1(kDevB));

        // Local content has a newer stamp.
        QJsonObject localMeta;
        localMeta[QStringLiteral("content")] =
            fieldStamp(tsLocalNewer, kDevA);
        store.applyInboundTaskMeta(task->id(), localMeta);

        // Inbound carries an older stamp for content. applyCommit trusts
        // the wire — the merger upstream of applyCommit picks the winner.
        // Here we verify that the wire stamp actually gets written. This
        // documents the "wire-trust" contract for future maintainers.
        QJsonObject patch;
        patch[QStringLiteral("content")] = QStringLiteral("remote-content");
        QJsonObject metaPatch;
        metaPatch[QStringLiteral("content")] =
            fieldStamp(tsRemoteOlder, kDevB);

        applyInboundCommit(
            engine,
            {makeUpdateOp(QStringLiteral("task_update"),
                          task->id(), patch, metaPatch)},
            tsRemoteOlder, QString::fromLatin1(kDevB));

        QCOMPARE(task->content(), QStringLiteral("remote-content"));

        const QJsonObject cStamp =
            fieldMetaFromStore(store, QStringLiteral("tasks"),
                               task->id(), QStringLiteral("content"));
        // The wire stamp wins per the "trust the wire" contract.
        QCOMPARE(cStamp.value(QStringLiteral("ts")).toString(),
                 tsRemoteOlder);
        QCOMPARE(cStamp.value(QStringLiteral("dev")).toString(),
                 QString::fromLatin1(kDevB));
    }
};

QTEST_GUILESS_MAIN(TestApplyCommitMetaPreservation)
#include "test_applycommit_meta_preservation.moc"
