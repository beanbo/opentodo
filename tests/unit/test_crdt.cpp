#include <QtTest>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

#include "sync/hlc.h"
#include "sync/crdt_meta.h"
#include "sync/merger.h"

// ── HLC test helpers ──────────────────────────────────────────────────────────

static const QString kDevA = QStringLiteral("device-aaa");
static const QString kDevB = QStringLiteral("device-bbb");
static const QString kDevC = QStringLiteral("device-ccc");

// Build an HLC string from raw components (bypasses QSettings, for tests).
static QString makeHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

// ── Entity builder helpers ────────────────────────────────────────────────────

static QJsonObject makeEntity(const QString &id,
                               const QString &updatedAt,
                               bool isDeleted = false)
{
    QJsonObject obj;
    obj[QStringLiteral("id")]         = id;
    obj[QStringLiteral("updated_at")] = updatedAt;
    obj[QStringLiteral("is_deleted")] = isDeleted;
    return obj;
}

static QJsonObject addField(QJsonObject obj, const QString &field, const QJsonValue &value)
{
    obj[field] = value;
    return obj;
}

static QJsonObject addFieldMeta(QJsonObject obj,
                                 const QString &field,
                                 const QString &ts,
                                 const QString &dev)
{
    CrdtMeta::setFieldMeta(obj, field, ts, dev);
    return obj;
}

// Build a state JSON with a single "tasks" array.
static QJsonObject makeState(const QJsonArray &tasks)
{
    QJsonObject state;
    state[QStringLiteral("tasks")]     = tasks;
    state[QStringLiteral("projects")]  = QJsonArray();
    state[QStringLiteral("sections")]  = QJsonArray();
    state[QStringLiteral("labels")]    = QJsonArray();
    state[QStringLiteral("filters")]   = QJsonArray();
    state[QStringLiteral("reminders")] = QJsonArray();
    state[QStringLiteral("comments")]  = QJsonArray();
    state[QStringLiteral("settings")]  = QJsonObject();
    return state;
}

static QJsonObject findById(const QJsonArray &arr, const QString &id)
{
    for (const auto &v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj[QStringLiteral("id")].toString() == id)
            return obj;
    }
    return QJsonObject();
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestCrdt : public QObject
{
    Q_OBJECT

private slots:

    // =========================================================================
    // HLC Tests
    // =========================================================================

    // ── HLC ordering: physical ms dominates ──────────────────────────────────
    void hlc_ordering_physical()
    {
        const QString a = makeHlc(1000, 0, kDevA);
        const QString b = makeHlc(2000, 0, kDevA);
        QCOMPARE(HybridLogicalClock::compare(a, b), -1);
        QCOMPARE(HybridLogicalClock::compare(b, a), +1);
        QCOMPARE(HybridLogicalClock::compare(a, a),  0);
    }

    // ── HLC ordering: same physical, counter dominates ───────────────────────
    void hlc_ordering_counter()
    {
        const QString a = makeHlc(1000, 0, kDevA);
        const QString b = makeHlc(1000, 1, kDevA);
        QCOMPARE(HybridLogicalClock::compare(a, b), -1);
        QCOMPARE(HybridLogicalClock::compare(b, a), +1);
    }

    // ── HLC ordering: same physical+counter, deviceId tiebreak ───────────────
    void hlc_ordering_deviceId_tiebreak()
    {
        const QString a = makeHlc(1000, 0, QStringLiteral("aaa"));
        const QString b = makeHlc(1000, 0, QStringLiteral("bbb"));
        QVERIFY(HybridLogicalClock::compare(a, b) < 0);
        QVERIFY(HybridLogicalClock::compare(b, a) > 0);
    }

    // ── HLC: equal strings compare as equal ──────────────────────────────────
    void hlc_equal()
    {
        const QString a = makeHlc(999, 3, kDevA);
        QCOMPARE(HybridLogicalClock::compare(a, a), 0);
    }

    // ── HLC: invalid (empty) sorts before any valid ───────────────────────────
    void hlc_invalid_sorts_early()
    {
        const QString valid   = makeHlc(1000, 0, kDevA);
        const QString invalid = QString();
        QCOMPARE(HybridLogicalClock::compare(invalid, valid),  -1);
        QCOMPARE(HybridLogicalClock::compare(valid,   invalid), +1);
        QCOMPARE(HybridLogicalClock::compare(invalid, invalid),  0);
    }

    // ── HLC: now() returns incrementing values ────────────────────────────────
    void hlc_now_monotone()
    {
        const QString t1 = HybridLogicalClock::now(kDevA);
        const QString t2 = HybridLogicalClock::now(kDevA);
        const QString t3 = HybridLogicalClock::now(kDevA);
        // Each successive call must be ≥ the previous.
        QVERIFY(HybridLogicalClock::compare(t1, t2) <= 0);
        QVERIFY(HybridLogicalClock::compare(t2, t3) <= 0);
    }

    // ── HLC: monotonicity under simulated clock skew ─────────────────────────
    void hlc_monotone_under_skew()
    {
        // Simulate a remote timestamp far in the future (skewed clock).
        const quint64 futureMs = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) + 60000; // +60s
        const QString remote = makeHlc(futureMs, 0, kDevB);

        const QString localBefore = HybridLogicalClock::now(kDevA);
        const QString updated = HybridLogicalClock::update(localBefore, remote, kDevA);
        const QString localAfter  = HybridLogicalClock::now(kDevA);

        // After receiving future remote, local must advance >= remote.
        QVERIFY(HybridLogicalClock::compare(updated, remote)    >= 0);
        // Subsequent local ticks are >= updated.
        QVERIFY(HybridLogicalClock::compare(localAfter, updated) >= 0);
    }

    // ── HLC parse: round-trip ─────────────────────────────────────────────────
    void hlc_parse_roundtrip()
    {
        const QString ts = makeHlc(0x000186A0ul, 5, kDevA);
        const HybridLogicalClock::Components c = HybridLogicalClock::parse(ts);
        QVERIFY(c.valid);
        QCOMPARE(c.physMs,   quint64(0x000186A0ul));
        QCOMPARE(c.counter,  quint32(5));
        QCOMPARE(c.deviceId, kDevA);
    }

    // =========================================================================
    // CrdtMeta Tests
    // =========================================================================

    // ── hasMeta: false when absent ────────────────────────────────────────────
    void meta_hasMeta_false()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("x");
        QVERIFY(!CrdtMeta::hasMeta(obj));
    }

    // ── hasMeta: true after setFieldMeta ─────────────────────────────────────
    void meta_hasMeta_true()
    {
        QJsonObject obj;
        CrdtMeta::setFieldMeta(obj, QStringLiteral("content"),
                               makeHlc(1000, 0, kDevA), kDevA);
        QVERIFY(CrdtMeta::hasMeta(obj));
    }

    // ── getFieldMeta round-trip ───────────────────────────────────────────────
    void meta_getSetFieldMeta()
    {
        QJsonObject obj;
        const QString ts  = makeHlc(5000, 3, kDevA);
        CrdtMeta::setFieldMeta(obj, QStringLiteral("priority"), ts, kDevA);

        const QJsonObject fm = CrdtMeta::getFieldMeta(obj, QStringLiteral("priority"));
        QVERIFY(!fm.isEmpty());
        QCOMPARE(fm[QStringLiteral("ts")].toString(),  ts);
        QCOMPARE(fm[QStringLiteral("dev")].toString(), kDevA);
    }

    // ── compareFieldMeta ─────────────────────────────────────────────────────
    void meta_compareFieldMeta()
    {
        QJsonObject a, b;
        CrdtMeta::setFieldMeta(a, QStringLiteral("f"), makeHlc(1000, 0, kDevA), kDevA);
        CrdtMeta::setFieldMeta(b, QStringLiteral("f"), makeHlc(2000, 0, kDevA), kDevA);

        const QJsonObject fma = CrdtMeta::getFieldMeta(a, QStringLiteral("f"));
        const QJsonObject fmb = CrdtMeta::getFieldMeta(b, QStringLiteral("f"));

        QVERIFY(CrdtMeta::compareFieldMeta(fma, fmb) < 0);
        QVERIFY(CrdtMeta::compareFieldMeta(fmb, fma) > 0);
        QCOMPARE(CrdtMeta::compareFieldMeta(fma, fma), 0);
    }

    // =========================================================================
    // OR-Set Tests
    // =========================================================================

    // Helper: build an OrsElement.
    static CrdtMeta::OrsElement makeOrsElem(const QString &id,
                                             const QString &addedAt,
                                             const QString &removedAt = QString())
    {
        CrdtMeta::OrsElement e;
        e.id        = id;
        e.addedAt   = addedAt;
        e.removedAt = removedAt;
        return e;
    }

    // ── OR-Set: concurrent add on two devices → both present ─────────────────
    void orset_concurrent_add_both_present()
    {
        const QString tsA = makeHlc(1000, 0, kDevA);
        const QString tsB = makeHlc(1001, 0, kDevB);

        QList<CrdtMeta::OrsElement> local;
        local.append(makeOrsElem(QStringLiteral("label-A"), tsA));

        QList<CrdtMeta::OrsElement> remote;
        remote.append(makeOrsElem(QStringLiteral("label-B"), tsB));

        const auto merged = CrdtMeta::mergeOrSets(local, remote);
        const QStringList live = CrdtMeta::orSetToList(merged);

        QCOMPARE(live.size(), 2);
        QVERIFY(live.contains(QStringLiteral("label-A")));
        QVERIFY(live.contains(QStringLiteral("label-B")));
    }

    // ── OR-Set: same element on both sides → deduplicated ────────────────────
    void orset_same_element_deduped()
    {
        const QString ts = makeHlc(1000, 0, kDevA);

        QList<CrdtMeta::OrsElement> local;
        local.append(makeOrsElem(QStringLiteral("label-X"), ts));

        QList<CrdtMeta::OrsElement> remote;
        remote.append(makeOrsElem(QStringLiteral("label-X"), ts));

        const auto merged = CrdtMeta::mergeOrSets(local, remote);
        QCOMPARE(merged.size(), 1);

        const QStringList live = CrdtMeta::orSetToList(merged);
        QCOMPARE(live.size(), 1);
        QCOMPARE(live.first(), QStringLiteral("label-X"));
    }

    // ── OR-Set: add then remove on one device, just-add on another → add-wins
    void orset_addwins_over_remove()
    {
        const QString addTs    = makeHlc(1000, 0, kDevA);
        const QString removeTs = makeHlc(2000, 0, kDevA);
        // Device B adds the same element *after* device A removed it.
        const QString readdTs  = makeHlc(3000, 0, kDevB);

        QList<CrdtMeta::OrsElement> local;
        local.append(makeOrsElem(QStringLiteral("label-Y"), addTs, removeTs));

        QList<CrdtMeta::OrsElement> remote;
        remote.append(makeOrsElem(QStringLiteral("label-Y"), readdTs));

        const auto merged = CrdtMeta::mergeOrSets(local, remote);
        // Re-add (addedAt=3000) > remove (removedAt=2000) → element is live.
        const QStringList live = CrdtMeta::orSetToList(merged);
        QVERIFY(live.contains(QStringLiteral("label-Y")));
    }

    // ── OR-Set: remove wins when no concurrent re-add ─────────────────────────
    void orset_remove_wins_no_readd()
    {
        const QString addTs    = makeHlc(1000, 0, kDevA);
        const QString removeTs = makeHlc(2000, 0, kDevA);

        QList<CrdtMeta::OrsElement> local;
        local.append(makeOrsElem(QStringLiteral("label-Z"), addTs, removeTs));

        QList<CrdtMeta::OrsElement> remote;
        remote.append(makeOrsElem(QStringLiteral("label-Z"), addTs));
        // Remote doesn't know about the remove (concurrent); remote addedAt == local addedAt.
        // Merged: addedAt=1000, removedAt=2000 → removedAt > addedAt → element is dead.

        const auto merged = CrdtMeta::mergeOrSets(local, remote);
        const QStringList live = CrdtMeta::orSetToList(merged);
        QVERIFY(!live.contains(QStringLiteral("label-Z")));
    }

    // =========================================================================
    // Merger CRDT Tests
    // =========================================================================

    // ── Per-field merge: edit different fields concurrently → both kept ───────
    void crdt_differentFields_bothKept()
    {
        const QString id      = QStringLiteral("task-crdt-1");
        const QString baseTs  = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString titleTs = makeHlc(2000, 0, kDevA);
        const QString descTs  = makeHlc(1999, 0, kDevB);

        QJsonObject local = makeEntity(id, baseTs);
        local = addField(local, QStringLiteral("content"),     QStringLiteral("Local Title"));
        local = addField(local, QStringLiteral("description"), QStringLiteral("Original Desc"));
        local = addFieldMeta(local, QStringLiteral("content"), titleTs, kDevA);
        // No meta for description on local → legacy for that field

        QJsonObject remote = makeEntity(id, baseTs);
        remote = addField(remote, QStringLiteral("content"),     QStringLiteral("Remote Title"));
        remote = addField(remote, QStringLiteral("description"), QStringLiteral("Remote Desc"));
        remote = addFieldMeta(remote, QStringLiteral("description"), descTs, kDevB);
        // No meta for content on remote → legacy for that field

        QJsonArray localArr;
        localArr.append(local);
        QJsonArray remoteArr;
        remoteArr.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(localArr), makeState(remoteArr));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());

        // Local title has CRDT meta (titleTs=2000 > legacy), remote description
        // has CRDT meta — both should win for their respective fields.
        // content: local has meta, remote does not → local (meta) wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Local Title"));
        // description: remote has meta (descTs=1999), local does not → remote wins.
        QCOMPARE(task[QStringLiteral("description")].toString(), QStringLiteral("Remote Desc"));
    }

    // ── Per-field merge: edit same field → deterministic winner ──────────────
    void crdt_sameField_deterministicWinner()
    {
        const QString id    = QStringLiteral("task-crdt-2");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString tsA   = makeHlc(3000, 0, kDevA); // higher physical → wins
        const QString tsB   = makeHlc(2000, 0, kDevB);

        QJsonObject local = makeEntity(id, baseTs);
        local = addField(local, QStringLiteral("priority"), 2);
        local = addFieldMeta(local, QStringLiteral("priority"), tsA, kDevA);

        QJsonObject remote = makeEntity(id, baseTs);
        remote = addField(remote, QStringLiteral("priority"), 4);
        remote = addFieldMeta(remote, QStringLiteral("priority"), tsB, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // tsA (3000) > tsB (2000) → local wins → priority = 2
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 2);
    }

    // ── Per-field merge: same HLC, deviceId tiebreak ─────────────────────────
    void crdt_sameTimestamp_deviceIdTiebreak()
    {
        const QString id    = QStringLiteral("task-crdt-tie");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString ts    = makeHlc(1000, 0, QStringLiteral(""));
        // Fabricate same phys+ctr, different device IDs.
        const QString tsA = makeHlc(1000, 7, QStringLiteral("zzz")); // higher devId
        const QString tsB = makeHlc(1000, 7, QStringLiteral("aaa")); // lower devId

        QJsonObject local = makeEntity(id, baseTs);
        local = addField(local, QStringLiteral("content"), QStringLiteral("zzz-wins"));
        local = addFieldMeta(local, QStringLiteral("content"), tsA, QStringLiteral("zzz"));

        QJsonObject remote = makeEntity(id, baseTs);
        remote = addField(remote, QStringLiteral("content"), QStringLiteral("aaa-loses"));
        remote = addFieldMeta(remote, QStringLiteral("content"), tsB, QStringLiteral("aaa"));

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // "zzz" > "aaa" lexicographically → local (zzz) wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("zzz-wins"));
    }

    // ── Tombstone: always wins over CRDT field values ─────────────────────────
    void crdt_tombstone_wins()
    {
        const QString id     = QStringLiteral("task-tomb-crdt");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString highTs = makeHlc(99999, 0, kDevA); // very high HLC

        QJsonObject local = makeEntity(id, baseTs, /*isDeleted=*/true);
        local = addField(local, QStringLiteral("content"), QStringLiteral("Should be gone"));

        QJsonObject remote = makeEntity(id, baseTs, /*isDeleted=*/false);
        remote = addField(remote, QStringLiteral("content"), QStringLiteral("Remote Content"));
        remote = addFieldMeta(remote, QStringLiteral("content"), highTs, kDevA);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── Legacy entity without _meta: still merges correctly (LWW) ─────────────
    void crdt_legacyEntity_lwwFallback()
    {
        Merger m;
        const QString id = QStringLiteral("task-legacy");

        QJsonArray la;
        la.append(makeEntity(id, QStringLiteral("2026-04-02T12:00:00.000Z"))
            // No _meta → legacy
        );
        // Add content field manually.
        QJsonObject e = la[0].toObject();
        e[QStringLiteral("content")] = QStringLiteral("Local (newer)");
        la[0] = e;

        QJsonArray ra;
        ra.append(makeEntity(id, QStringLiteral("2026-04-01T12:00:00.000Z")));
        QJsonObject re = ra[0].toObject();
        re[QStringLiteral("content")] = QStringLiteral("Remote (older)");
        ra[0] = re;

        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Local (newer)"));
    }

    // ── Commutativity: merge(a,b) == merge(b,a) ──────────────────────────────
    void crdt_commutativity()
    {
        const QString id     = QStringLiteral("task-comm");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1500, 0, kDevA);
        const QString tsB    = makeHlc(2000, 0, kDevB);

        QJsonObject a = makeEntity(id, baseTs);
        a = addField(a, QStringLiteral("content"),     QStringLiteral("Title A"));
        a = addField(a, QStringLiteral("description"), QStringLiteral("Desc A"));
        a = addFieldMeta(a, QStringLiteral("content"),     tsA, kDevA);
        a = addFieldMeta(a, QStringLiteral("description"), tsA, kDevA);

        QJsonObject b = makeEntity(id, baseTs);
        b = addField(b, QStringLiteral("content"),     QStringLiteral("Title B"));
        b = addField(b, QStringLiteral("description"), QStringLiteral("Desc B"));
        b = addFieldMeta(b, QStringLiteral("content"),     tsB, kDevB);
        b = addFieldMeta(b, QStringLiteral("description"), tsB, kDevB);

        QJsonArray aa; aa.append(a);
        QJsonArray ba; ba.append(b);

        Merger m;
        const QJsonObject ab = findById(
            m.mergeStates(makeState(aa), makeState(ba))[QStringLiteral("tasks")].toArray(), id);
        const QJsonObject ba_ = findById(
            m.mergeStates(makeState(ba), makeState(aa))[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!ab.isEmpty());
        QVERIFY(!ba_.isEmpty());
        QCOMPARE(ab[QStringLiteral("content")].toString(),
                 ba_[QStringLiteral("content")].toString());
        QCOMPARE(ab[QStringLiteral("description")].toString(),
                 ba_[QStringLiteral("description")].toString());
    }

    // ── Idempotency: merge(a, a) == a ────────────────────────────────────────
    void crdt_idempotency()
    {
        const QString id     = QStringLiteral("task-idem");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString ts     = makeHlc(1000, 0, kDevA);

        QJsonObject a = makeEntity(id, baseTs);
        a = addField(a, QStringLiteral("content"), QStringLiteral("Hello"));
        a = addFieldMeta(a, QStringLiteral("content"), ts, kDevA);

        QJsonArray aa; aa.append(a);

        Merger m;
        const QJsonObject merged = findById(
            m.mergeStates(makeState(aa), makeState(aa))[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!merged.isEmpty());
        QCOMPARE(merged[QStringLiteral("content")].toString(), QStringLiteral("Hello"));
    }

    // ── Associativity: merge(merge(a,b), c) == merge(a, merge(b,c)) ──────────
    void crdt_associativity()
    {
        const QString id     = QStringLiteral("task-assoc");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1000, 0, kDevA);
        const QString tsB    = makeHlc(2000, 0, kDevB);
        const QString tsC    = makeHlc(1500, 0, kDevC);

        auto makeT = [&](const QString &content, const QString &ts, const QString &dev) {
            QJsonObject t = makeEntity(id, baseTs);
            t = addField(t, QStringLiteral("content"), content);
            t = addFieldMeta(t, QStringLiteral("content"), ts, dev);
            return t;
        };

        QJsonObject ta = makeT(QStringLiteral("A"), tsA, kDevA);
        QJsonObject tb = makeT(QStringLiteral("B"), tsB, kDevB);
        QJsonObject tc = makeT(QStringLiteral("C"), tsC, kDevC);

        QJsonArray aa; aa.append(ta);
        QJsonArray ba; ba.append(tb);
        QJsonArray ca; ca.append(tc);

        Merger m;

        // merge(merge(a,b), c)
        const QJsonObject ab = m.mergeStates(makeState(aa), makeState(ba));
        const QJsonArray  ab_tasks = ab[QStringLiteral("tasks")].toArray();
        QJsonArray aba; aba.append(findById(ab_tasks, id));
        const QJsonObject ab_c = findById(
            m.mergeStates(makeState(aba), makeState(ca))[QStringLiteral("tasks")].toArray(), id);

        // merge(a, merge(b,c))
        const QJsonObject bc = m.mergeStates(makeState(ba), makeState(ca));
        const QJsonArray  bc_tasks = bc[QStringLiteral("tasks")].toArray();
        QJsonArray bca; bca.append(findById(bc_tasks, id));
        const QJsonObject a_bc = findById(
            m.mergeStates(makeState(aa), makeState(bca))[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!ab_c.isEmpty());
        QVERIFY(!a_bc.isEmpty());
        // tsB (2000) is the global max → content == "B" in both cases.
        QCOMPARE(ab_c[QStringLiteral("content")].toString(),
                 a_bc[QStringLiteral("content")].toString());
        QCOMPARE(ab_c[QStringLiteral("content")].toString(), QStringLiteral("B"));
    }

    // ── OR-Set in merger: concurrent adds of label A and B → both present ─────
    void crdt_merger_orset_concurrent_adds()
    {
        const QString id     = QStringLiteral("task-orset-1");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1000, 0, kDevA);
        const QString tsB    = makeHlc(1001, 0, kDevB);

        // Local: has label-A
        QJsonObject local = makeEntity(id, baseTs);
        QJsonArray labelsA;
        labelsA.append(QStringLiteral("label-A"));
        local[QStringLiteral("labels")] = labelsA;
        {
            QList<CrdtMeta::OrsElement> elems;
            CrdtMeta::OrsElement e;
            e.id      = QStringLiteral("label-A");
            e.addedAt = tsA;
            elems.append(e);
            CrdtMeta::setOrSetElements(local, QStringLiteral("labels"), elems, tsA, kDevA);
        }

        // Remote: has label-B
        QJsonObject remote = makeEntity(id, baseTs);
        QJsonArray labelsB;
        labelsB.append(QStringLiteral("label-B"));
        remote[QStringLiteral("labels")] = labelsB;
        {
            QList<CrdtMeta::OrsElement> elems;
            CrdtMeta::OrsElement e;
            e.id      = QStringLiteral("label-B");
            e.addedAt = tsB;
            elems.append(e);
            CrdtMeta::setOrSetElements(remote, QStringLiteral("labels"), elems, tsB, kDevB);
        }

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = findById(
            m.mergeStates(makeState(la), makeState(ra))[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!merged.isEmpty());
        const QJsonArray labs = merged[QStringLiteral("labels")].toArray();
        QCOMPARE(labs.size(), 2);
        bool hasA = false, hasB = false;
        for (const auto &v : labs) {
            if (v.toString() == QStringLiteral("label-A")) hasA = true;
            if (v.toString() == QStringLiteral("label-B")) hasB = true;
        }
        QVERIFY(hasA);
        QVERIFY(hasB);
    }
};

QTEST_GUILESS_MAIN(TestCrdt)
#include "test_crdt.moc"
