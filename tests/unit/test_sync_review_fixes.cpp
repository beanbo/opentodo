// ============================================================================
// test_sync_review_fixes.cpp
//
// Regression coverage for the sync-layer review findings (S1–S6, S11, S12):
//
//   S1  settings merge is per-key LWW on `settings_meta` stamps (was: remote
//       blob replaced local whole → every push reverted fresh local settings)
//   S2  a no-op merge does NOT reload the store (no DataStore::aboutToReset)
//   S3  `labels` without OR-Set elements honours its per-field stamp
//   S4  merging remote state advances the local HLC
//   S5  pull() with no remote HEAD still downloads + merges state.json
//   S6  push() aborts when the pre-upload state.json download FAILS
//   S11 a restore (newer is_deleted stamp) beats the older tombstone
//   S12 re-entrant push/pull is coalesced into one follow-up sync
//
// Engine-level slots run against an in-memory RemoteStore (FakeRemote) wired
// in through SyncEngine::setRemote — no Drive, no network.
// ============================================================================

#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <functional>

#include "storage/datastore.h"
#include "sync/hlc.h"
#include "sync/merger.h"
#include "sync/remotestore.h"
#include "sync/syncengine.h"
#include "model/task.h"

namespace {

// In-memory blob store implementing the RemoteStore contract.
class FakeRemote : public RemoteStore
{
public:
    QHash<QString, QByteArray> blobs;
    QSet<QString> failDownloads;
    bool locked = false;
    int lockAttempts = 0;
    int lockFailures = 0;
    QStringList uploadedPaths;
    std::function<void(const QString &)> onDownload;

    bool isAuthenticated() const override { return true; }
    bool acquireLock() override
    {
        ++lockAttempts;
        if (locked) { ++lockFailures; return false; }
        locked = true;
        return true;
    }
    bool releaseLock() override { locked = false; return true; }
    bool uploadFile(const QString &p, const QByteArray &d) override
    {
        uploadedPaths << p;
        blobs.insert(p, d);
        return true;
    }
    bool downloadFile(const QString &p, QByteArray &d, bool notFoundIsOk,
                      qint64 maxBytes) override
    {
        if (onDownload) onDownload(p);
        if (failDownloads.contains(p)) return false;
        if (!blobs.contains(p)) { d.clear(); return notFoundIsOk; }
        d = blobs.value(p);
        return maxBytes <= 0 || d.size() <= maxBytes;
    }
    bool deleteFile(const QString &p) override { return blobs.remove(p) > 0; }
    QStringList listFiles(const QString &folder) override
    {
        QStringList out;
        const QString prefix = folder + QLatin1Char('/');
        for (auto it = blobs.constBegin(); it != blobs.constEnd(); ++it)
            if (it.key().startsWith(prefix)) out << it.key().mid(prefix.size());
        return out;
    }
};

QString hlcAt(qint64 physMs, quint32 ctr, const QString &dev)
{
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<quint64>(physMs), 16, 16, QLatin1Char('0'))
        .arg(ctr, 8, 16, QLatin1Char('0'))
        .arg(dev);
}

QJsonObject stamp(const QString &ts, const QString &dev)
{
    QJsonObject fm;
    fm[QStringLiteral("ts")]  = ts;
    fm[QStringLiteral("dev")] = dev;
    return fm;
}

QJsonObject stateWith(const QJsonArray &tasks, const QJsonObject &settings = {},
                      const QJsonObject &settingsMeta = {})
{
    QJsonObject s;
    s[QStringLiteral("tasks")] = tasks;
    for (const char *k : {"projects", "sections", "labels", "filters", "reminders", "comments"})
        s[QLatin1String(k)] = QJsonArray();
    s[QStringLiteral("settings")] = settings;
    if (!settingsMeta.isEmpty())
        s[QStringLiteral("settings_meta")] = settingsMeta;
    return s;
}

QJsonObject findById(const QJsonArray &arr, const QString &id)
{
    for (const auto &v : arr)
        if (v.toObject().value(QStringLiteral("id")).toString() == id)
            return v.toObject();
    return {};
}

QByteArray toBytes(const QJsonObject &o) { return QJsonDocument(o).toJson(); }

const QString kDevA = QStringLiteral("dev-aaaa");
const QString kDevB = QStringLiteral("dev-bbbb");

} // namespace

class TestSyncReviewFixes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SyncReviewFixes"));
        QCoreApplication::setApplicationName(QStringLiteral("test_sync_review_fixes"));
        QSettings s;
        s.clear();
        s.setValue(QStringLiteral("sync/device_id"), QStringLiteral("dev-local-test"));
        s.sync();
    }

    // ── S1: Merger per-key settings LWW ───────────────────────────────────────
    void s1_settings_newerLocalStampBeatsRemote()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonObject lSet{{QStringLiteral("theme"), QStringLiteral("dark")},
                         {QStringLiteral("language"), QStringLiteral("ru")}};
        QJsonObject lMeta{{QStringLiteral("theme"), stamp(hlcAt(now - 1000, 0, kDevA), kDevA)}};
        QJsonObject rSet{{QStringLiteral("theme"), QStringLiteral("light")},
                         {QStringLiteral("language"), QStringLiteral("en")}};
        QJsonObject rMeta{{QStringLiteral("theme"), stamp(hlcAt(now - 5000, 0, kDevB), kDevB)}};

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith({}, lSet, lMeta),
                                                 stateWith({}, rSet, rMeta));
        const QJsonObject s = merged.value(QStringLiteral("settings")).toObject();
        // Stamped key: newer local wins (was reverted to remote before S1).
        QCOMPARE(s.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
        // Unstamped on both sides: legacy rule — remote wins.
        QCOMPARE(s.value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        // Winning stamp travels with the merged state.
        QCOMPARE(merged.value(QStringLiteral("settings_meta")).toObject()
                     .value(QStringLiteral("theme")).toObject()
                     .value(QStringLiteral("dev")).toString(), kDevA);
    }

    void s1_settings_newerRemoteStampWins_andOneSidedKeysKept()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonObject lSet{{QStringLiteral("theme"), QStringLiteral("dark")},
                         {QStringLiteral("karma_score"), 42}};
        QJsonObject lMeta{{QStringLiteral("theme"), stamp(hlcAt(now - 5000, 0, kDevA), kDevA)},
                          {QStringLiteral("karma_score"), stamp(hlcAt(now - 4000, 0, kDevA), kDevA)}};
        QJsonObject rSet{{QStringLiteral("theme"), QStringLiteral("light")},
                         {QStringLiteral("hotkey"), QStringLiteral("Ctrl+Alt+A")}};
        QJsonObject rMeta{{QStringLiteral("theme"), stamp(hlcAt(now - 1000, 0, kDevB), kDevB)}};

        Merger m;
        const QJsonObject s = m.mergeStates(stateWith({}, lSet, lMeta),
                                            stateWith({}, rSet, rMeta))
                                  .value(QStringLiteral("settings")).toObject();
        QCOMPARE(s.value(QStringLiteral("theme")).toString(), QStringLiteral("light"));
        QCOMPARE(s.value(QStringLiteral("karma_score")).toInt(), 42);          // local-only
        QCOMPARE(s.value(QStringLiteral("hotkey")).toString(), QStringLiteral("Ctrl+Alt+A")); // remote-only
    }

    void s1_settings_stampedRemovalWins()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        // Local removed "collaborators" (stamp present, key absent).
        QJsonObject lMeta{{QStringLiteral("collaborators"), stamp(hlcAt(now - 1000, 0, kDevA), kDevA)}};
        QJsonObject rSet{{QStringLiteral("collaborators"),
                          QJsonObject{{QStringLiteral("me"), QJsonObject{{QStringLiteral("name"), QStringLiteral("X")}}}}}};
        QJsonObject rMeta{{QStringLiteral("collaborators"), stamp(hlcAt(now - 5000, 0, kDevB), kDevB)}};

        Merger m;
        const QJsonObject s = m.mergeStates(stateWith({}, {}, lMeta),
                                            stateWith({}, rSet, rMeta))
                                  .value(QStringLiteral("settings")).toObject();
        QVERIFY(!s.contains(QStringLiteral("collaborators")));
    }

    // ── S1: DataStore stamps settings and round-trips settings_meta ──────────
    void s1_datastore_stampsChangedKeysOnly()
    {
        DataStore store;
        store.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));
        const QJsonObject meta1 = store.toJson().value(QStringLiteral("settings_meta")).toObject();
        const QString ts1 = meta1.value(QStringLiteral("theme")).toObject()
                                .value(QStringLiteral("ts")).toString();
        QVERIFY(HybridLogicalClock::parse(ts1).valid);
        QVERIFY(!store.settings().contains(QStringLiteral("settings_meta"))); // never inside settings

        // Re-writing the same value must not re-stamp (would out-rank peers).
        store.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));
        QCOMPARE(store.toJson().value(QStringLiteral("settings_meta")).toObject()
                     .value(QStringLiteral("theme")).toObject()
                     .value(QStringLiteral("ts")).toString(), ts1);

        // setSettings (read-modify-write) stamps only the key that changed.
        QJsonObject all = store.settings();
        all[QStringLiteral("karma_trend")] = QJsonArray{1, 2};
        store.setSettings(all);
        const QJsonObject meta2 = store.toJson().value(QStringLiteral("settings_meta")).toObject();
        QVERIFY(meta2.contains(QStringLiteral("karma_trend")));
        QCOMPARE(meta2.value(QStringLiteral("theme")).toObject()
                     .value(QStringLiteral("ts")).toString(), ts1);

        // Round trip through loadFromJson.
        DataStore other;
        other.loadFromJson(store.toJson());
        QCOMPARE(other.toJson().value(QStringLiteral("settings_meta")).toObject(), meta2);
    }

    // ── S3: labels without OR-Set elements honour the field stamp ─────────────
    void s3_labelsStampBeatsNewerUpdatedAtFromOtherFieldEdit()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString id = QStringLiteral("task-s3");
        // A: added label "x" at t=-2000 (stamped like addTask/updateTask do).
        QJsonObject a{{QStringLiteral("id"), id},
                      {QStringLiteral("content"), QStringLiteral("orig")},
                      {QStringLiteral("labels"), QJsonArray{QStringLiteral("x")}},
                      {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:02.000Z")}};
        QJsonObject aMeta{{QStringLiteral("labels"), stamp(hlcAt(now - 2000, 0, kDevA), kDevA)},
                          {QStringLiteral("content"), stamp(hlcAt(now - 9000, 0, kDevA), kDevA)}};
        a[QStringLiteral("_meta")] = aMeta;
        // B: concurrently edited content (newer updated_at), labels untouched.
        QJsonObject b{{QStringLiteral("id"), id},
                      {QStringLiteral("content"), QStringLiteral("edited on B")},
                      {QStringLiteral("labels"), QJsonArray()},
                      {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:09.000Z")}};
        QJsonObject bMeta{{QStringLiteral("labels"), stamp(hlcAt(now - 9000, 0, kDevA), kDevA)},
                          {QStringLiteral("content"), stamp(hlcAt(now - 1000, 0, kDevB), kDevB)}};
        b[QStringLiteral("_meta")] = bMeta;

        Merger m;
        for (bool aIsLocal : {true, false}) {
            const QJsonObject merged = m.mergeStates(stateWith({aIsLocal ? a : b}),
                                                     stateWith({aIsLocal ? b : a}));
            const QJsonObject t = findById(merged.value(QStringLiteral("tasks")).toArray(), id);
            QCOMPARE(t.value(QStringLiteral("labels")).toArray(), QJsonArray{QStringLiteral("x")});
            QCOMPARE(t.value(QStringLiteral("content")).toString(), QStringLiteral("edited on B"));
        }
    }

    // ── S11: restore beats the older tombstone ────────────────────────────────
    void s11_restoreWithNewerStampBeatsTombstone()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString id = QStringLiteral("task-s11");
        QJsonObject dead{{QStringLiteral("id"), id}, {QStringLiteral("content"), QStringLiteral("c")},
                         {QStringLiteral("is_deleted"), true}};
        dead[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 5000, 0, kDevB), kDevB)}};
        QJsonObject live{{QStringLiteral("id"), id}, {QStringLiteral("content"), QStringLiteral("c")},
                         {QStringLiteral("is_deleted"), false}};
        live[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 1000, 0, kDevA), kDevA)}};

        Merger m;
        for (bool liveIsLocal : {true, false}) {
            const QJsonObject merged = m.mergeStates(stateWith({liveIsLocal ? live : dead}),
                                                     stateWith({liveIsLocal ? dead : live}));
            const QJsonObject t = findById(merged.value(QStringLiteral("tasks")).toArray(), id);
            QVERIFY2(!t.value(QStringLiteral("is_deleted")).toBool(),
                     "a newer restore must not be re-deleted by the older tombstone");
        }

        // Older live stamp (e.g. the add) ⇒ tombstone still wins.
        live[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 9000, 0, kDevA), kDevA)}};
        const QJsonObject merged = m.mergeStates(stateWith({live}), stateWith({dead}));
        QVERIFY(findById(merged.value(QStringLiteral("tasks")).toArray(), id)
                    .value(QStringLiteral("is_deleted")).toBool());
    }

    // A permanent delete (is_purged on a tombstone) must survive a merge with
    // a peer that only has the plain tombstone — on either side — and a
    // restore that wins must not inherit the purge marker.
    void purgedFlagSurvivesMergeOfTwoTombstones()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString id = QStringLiteral("task-purged");
        QJsonObject plain{{QStringLiteral("id"), id}, {QStringLiteral("content"), QStringLiteral("c")},
                          {QStringLiteral("is_deleted"), true}};
        plain[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 5000, 0, kDevB), kDevB)}};
        QJsonObject purged = plain;
        purged[QStringLiteral("is_purged")] = true;
        purged[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 1000, 0, kDevA), kDevA)}};

        Merger m;
        for (bool purgedIsLocal : {true, false}) {
            const QJsonObject merged = m.mergeStates(stateWith({purgedIsLocal ? purged : plain}),
                                                     stateWith({purgedIsLocal ? plain : purged}));
            const QJsonObject t = findById(merged.value(QStringLiteral("tasks")).toArray(), id);
            QVERIFY(t.value(QStringLiteral("is_deleted")).toBool());
            QVERIFY2(t.value(QStringLiteral("is_purged")).toBool(),
                     "a permanent delete must not be undone into a plain Trash entry");
        }

        // A newer restore beats the purge tombstone and must come back clean.
        QJsonObject live{{QStringLiteral("id"), id}, {QStringLiteral("content"), QStringLiteral("c")},
                         {QStringLiteral("is_deleted"), false}};
        live[QStringLiteral("_meta")] = QJsonObject{
            {QStringLiteral("is_deleted"), stamp(hlcAt(now - 100, 0, kDevB), kDevB)}};
        const QJsonObject merged = m.mergeStates(stateWith({live}), stateWith({purged}));
        const QJsonObject t = findById(merged.value(QStringLiteral("tasks")).toArray(), id);
        QVERIFY(!t.value(QStringLiteral("is_deleted")).toBool());
        QVERIFY(!t.contains(QStringLiteral("is_purged")));
    }

    void s11_datastoreRestoreSurvivesMergeWithStaleTombstone()
    {
        DataStore store;
        Task *t = store.addTask(QStringLiteral("restore me"), QString());
        QVERIFY(t);
        const QString id = t->id();
        QVERIFY(store.removeTask(id));
        const QJsonObject remoteWithTombstone = store.toJson();   // pushed earlier
        QVERIFY(store.restoreTask(id));

        Merger m;
        const QJsonObject merged = m.mergeStates(store.toJson(), remoteWithTombstone);
        QVERIFY(!findById(merged.value(QStringLiteral("tasks")).toArray(), id)
                     .value(QStringLiteral("is_deleted")).toBool());
    }

    // ── S4: maxHlc + HLC advance on merge ─────────────────────────────────────
    void s4_maxHlcFindsHighestAcceptableStamp()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString lo  = hlcAt(now - 5000, 0, kDevA);
        const QString hi  = hlcAt(now + 20 * 60 * 1000, 3, kDevB);       // +20 min: accepted
        const QString bad = hlcAt(now + 48LL * 3600 * 1000, 0, kDevB);   // +2 days: skipped
        QJsonObject t1{{QStringLiteral("id"), QStringLiteral("t1")}};
        t1[QStringLiteral("_meta")] = QJsonObject{{QStringLiteral("content"), stamp(lo, kDevA)},
                                                  {QStringLiteral("priority"), stamp(bad, kDevB)}};
        const QJsonObject st = stateWith({t1}, {},
            QJsonObject{{QStringLiteral("theme"), stamp(hi, kDevB)}});
        QCOMPARE(Merger::maxHlc(st), hi);
        QVERIFY(Merger::maxHlc(stateWith({})).isEmpty());
    }

    void s4_pullAdvancesLocalHlcPastRemoteStamps()
    {
        DataStore remoteStore;
        Task *rt = remoteStore.addTask(QStringLiteral("from peer"), QString());
        QJsonObject remoteState = remoteStore.toJson();
        // Re-stamp the peer task's content 30 minutes ahead (peer clock leads).
        const QString ahead = hlcAt(QDateTime::currentMSecsSinceEpoch() + 30 * 60 * 1000, 7, kDevB);
        QJsonArray tasks = remoteState.value(QStringLiteral("tasks")).toArray();
        QJsonObject task = tasks.at(0).toObject();
        QJsonObject meta = task.value(QStringLiteral("_meta")).toObject();
        meta[QStringLiteral("content")] = stamp(ahead, kDevB);
        task[QStringLiteral("_meta")] = meta;
        tasks[0] = task;
        remoteState[QStringLiteral("tasks")] = tasks;

        FakeRemote remote;
        remote.blobs.insert(QStringLiteral("state.json"), toBytes(remoteState));
        DataStore store;
        SyncEngine engine(&store);
        engine.setRemote(&remote);
        engine.pull();
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QVERIFY(store.taskById(rt->id()));

        // A local edit made right after the merge must out-rank the peer stamp.
        QVERIFY(HybridLogicalClock::compare(HybridLogicalClock::loadLast(), ahead) > 0);
        QVERIFY(HybridLogicalClock::compare(HybridLogicalClock::now(QStringLiteral("dev-local-test")),
                                            ahead) > 0);
    }

    // ── S5: pull merges state.json even without a remote HEAD ─────────────────
    void s5_pullWithoutHeadMergesSnapshot()
    {
        DataStore peer;
        Task *pt = peer.addTask(QStringLiteral("peer task"), QString());
        FakeRemote remote;
        remote.blobs.insert(QStringLiteral("state.json"), toBytes(peer.toJson()));
        QVERIFY(!remote.blobs.contains(QStringLiteral("HEAD")));

        DataStore store;
        // Keep the id, not the pointer: a merging pull reloads the store
        // (DataStore::clear() deletes every Task), so `mine` would dangle.
        const QString mineId = store.addTask(QStringLiteral("local task"), QString())->id();
        SyncEngine engine(&store);
        engine.setRemote(&remote);
        QSignalSpy completed(&engine, &SyncEngine::syncCompleted);
        engine.pull();

        QCOMPARE(engine.status(), SyncEngine::Synced);
        QVERIFY2(store.taskById(pt->id()), "pull() must merge remote state.json when HEAD is absent");
        QVERIFY(store.taskById(mineId));     // merge, not overwrite
        QVERIFY(engine.lastSyncTime().isValid());
        QCOMPARE(completed.count(), 1);
        QVERIFY(!remote.locked);

        // No remote state at all (first sync) is still a clean success.
        FakeRemote empty;
        DataStore s2;
        SyncEngine e2(&s2);
        e2.setRemote(&empty);
        e2.pull();
        QCOMPARE(e2.status(), SyncEngine::Synced);

        // A corrupt snapshot is refused and local data is untouched.
        FakeRemote corrupt;
        corrupt.blobs.insert(QStringLiteral("state.json"), QByteArrayLiteral("{\"oops\":1}"));
        SyncEngine e3(&store);
        e3.setRemote(&corrupt);
        e3.pull();
        QCOMPARE(e3.status(), SyncEngine::Error);
        QVERIFY(store.taskById(mineId));
        QVERIFY(!corrupt.locked);
    }

    // ── S6: push aborts when the pre-upload download fails ────────────────────
    void s6_pushAbortsWhenRemoteStateDownloadFails()
    {
        FakeRemote remote;
        remote.blobs.insert(QStringLiteral("state.json"), QByteArrayLiteral("peer data"));
        remote.failDownloads.insert(QStringLiteral("state.json"));

        DataStore store;
        store.addTask(QStringLiteral("local"), QString());
        SyncEngine engine(&store);
        engine.setRemote(&remote);
        engine.push();

        QCOMPARE(engine.status(), SyncEngine::Error);
        QVERIFY2(!remote.uploadedPaths.contains(QStringLiteral("state.json")),
                 "push() must not overwrite remote state.json it could not read");
        QCOMPARE(remote.blobs.value(QStringLiteral("state.json")), QByteArrayLiteral("peer data"));
        QVERIFY(!remote.locked);

        // Genuinely missing remote state (first push) still uploads.
        remote.failDownloads.clear();
        remote.blobs.clear();
        engine.push();
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QVERIFY(remote.uploadedPaths.contains(QStringLiteral("state.json")));
    }

    // ── S2: unchanged merge does not reset the store ──────────────────────────
    void s2_noopMergeDoesNotReloadStore()
    {
        DataStore store;
        store.addTask(QStringLiteral("stable"), QString());
        store.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));
        FakeRemote remote;
        remote.blobs.insert(QStringLiteral("state.json"), toBytes(store.toJson()));

        SyncEngine engine(&store);
        engine.setRemote(&remote);
        QSignalSpy resets(&store, &DataStore::aboutToReset);
        engine.push();
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QCOMPARE(resets.count(), 0);
        engine.pull();
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QCOMPARE(resets.count(), 0);

        // A real remote change still loads (exactly once).
        DataStore peer;
        peer.loadFromJson(store.toJson());
        Task *added = peer.addTask(QStringLiteral("new on peer"), QString());
        remote.blobs.insert(QStringLiteral("state.json"), toBytes(peer.toJson()));
        engine.push();
        QCOMPARE(resets.count(), 1);
        QVERIFY(store.taskById(added->id()));
    }

    // ── S1 end-to-end: a fresh local setting survives push ────────────────────
    void s1_pushKeepsFreshLocalSettingOverLegacyRemote()
    {
        DataStore store;
        store.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));
        FakeRemote remote;
        // Legacy remote (no settings_meta) with a stale theme.
        remote.blobs.insert(QStringLiteral("state.json"),
                            toBytes(stateWith({}, QJsonObject{{QStringLiteral("theme"),
                                                               QStringLiteral("light")}})));
        SyncEngine engine(&store);
        engine.setRemote(&remote);
        engine.push();
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QCOMPARE(store.setting(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
        const QJsonObject uploaded =
            QJsonDocument::fromJson(remote.blobs.value(QStringLiteral("state.json"))).object();
        QCOMPARE(uploaded.value(QStringLiteral("settings")).toObject()
                     .value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
    }

    // ── S12: re-entrant push is coalesced, not run against our own lock ───────
    void s12_reentrantPushIsDeferredNotRunNested()
    {
        DataStore store;
        FakeRemote remote;
        SyncEngine engine(&store);
        engine.setRemote(&remote);

        bool reentered = false;
        remote.onDownload = [&](const QString &path) {
            if (path == QStringLiteral("state.json") && !reentered) {
                reentered = true;
                QVERIFY(engine.isBusy());
                engine.push();     // e.g. a timer firing inside the nested loop
                engine.pull();
            }
        };
        engine.push();

        QVERIFY(reentered);
        QCOMPARE(remote.lockFailures, 0);        // nested call never hit our own lock
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QCOMPARE(remote.lockAttempts, 1);
        // Exactly one follow-up sync() (pull + push) runs afterwards.
        QTRY_COMPARE(remote.lockAttempts, 3);
        QTRY_VERIFY(!engine.isBusy());
        QCOMPARE(remote.lockFailures, 0);
        QCOMPARE(engine.status(), SyncEngine::Synced);
    }

    // The engine's own load of a merged state is flagged so the after-action
    // scheduler can tell it apart from a user edit.
    void s12_applyingRemoteFlagCoversEngineLoad()
    {
        DataStore peer;
        peer.addTask(QStringLiteral("peer"), QString());
        FakeRemote remote;
        remote.blobs.insert(QStringLiteral("state.json"), toBytes(peer.toJson()));

        DataStore store;
        SyncEngine engine(&store);
        engine.setRemote(&remote);
        int changesDuringApply = 0, changesOutside = 0;
        connect(&store, &DataStore::dataChanged, this, [&]() {
            (engine.isApplyingRemoteState() ? changesDuringApply : changesOutside)++;
        });
        engine.pull();
        QVERIFY(changesDuringApply > 0);
        QCOMPARE(changesOutside, 0);
        QVERIFY(!engine.isApplyingRemoteState());
    }
};

QTEST_MAIN(TestSyncReviewFixes)
#include "test_sync_review_fixes.moc"
