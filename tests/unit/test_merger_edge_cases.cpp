// Edge-case test suite for the CRDT Merger.
//
// Complements tests/unit/test_merger.cpp (basic LWW, tombstones, set union)
// and tests/unit/test_crdt.cpp (HLC + CrdtMeta primitive coverage). Focuses
// on subtle scenarios at the boundary of per-field LWW + OR-Set + tombstones:
//
//   - Concurrent edits to disjoint fields (both must win their respective field)
//   - Same-field tie-break by deviceId (lexicographic per HLC::compare)
//   - Strictly newer field-meta wins
//   - Tombstones absorb both older AND newer concurrent edits
//   - OR-Set add-after-remove (re-add resurrects)
//   - OR-Set remove-after-add (concurrent remove kills element)
//   - Legacy entity (no _meta) → whole-object LWW path
//   - HLC tie + same dev (degenerate but must not crash)
//   - Empty-state on one side preserves the other
//   - Both empty → empty result
//   - Mixed CRDT-meta vs no-meta on individual fields
//   - Idempotence on tombstoned entities

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

#include "sync/merger.h"
#include "sync/crdt_meta.h"
#include "sync/hlc.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static const QString kDevA = QStringLiteral("dev-aaa");
static const QString kDevB = QStringLiteral("dev-bbb");
static const QString kDevC = QStringLiteral("dev-ccc");

// Build an HLC string from raw components (bypasses QSettings persistence).
static QString makeHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

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

static QJsonObject withField(QJsonObject obj, const QString &field, const QJsonValue &value)
{
    obj[field] = value;
    return obj;
}

static QJsonObject withFieldMeta(QJsonObject obj,
                                 const QString &field,
                                 const QString &ts,
                                 const QString &dev)
{
    CrdtMeta::setFieldMeta(obj, field, ts, dev);
    return obj;
}

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

// Build an entity with a "labels" OR-Set field populated from a list of
// (id, addedAt[, removedAt]) tuples. fieldTs/fieldDev determine the
// per-field meta (used by Merger to decide which side's element ordering
// "wins" when reconstructing the array).
struct LabelEntry {
    QString id;
    QString addedAt;
    QString removedAt;
};

static QJsonObject makeEntityWithLabels(const QString &id,
                                        const QString &updatedAt,
                                        const QList<LabelEntry> &labels,
                                        const QString &fieldTs,
                                        const QString &fieldDev)
{
    QJsonObject obj = makeEntity(id, updatedAt);

    // Mirror live ids into the plain "labels" array (matches what Merger
    // produces post-merge).
    QJsonArray plain;
    QList<CrdtMeta::OrsElement> elems;
    for (const auto &l : labels) {
        CrdtMeta::OrsElement e;
        e.id        = l.id;
        e.addedAt   = l.addedAt;
        e.removedAt = l.removedAt;
        elems.append(e);
        // Live = no removedAt OR addedAt strictly > removedAt
        const bool live = l.removedAt.isEmpty() ||
            HybridLogicalClock::compare(l.addedAt, l.removedAt) > 0;
        if (live) plain.append(l.id);
    }
    obj[QStringLiteral("labels")] = plain;
    CrdtMeta::setOrSetElements(obj, QStringLiteral("labels"), elems, fieldTs, fieldDev);
    return obj;
}

// ── Test class ───────────────────────────────────────────────────────────────

class TestMergerEdgeCases : public QObject
{
    Q_OBJECT

private slots:
    // ── 1. Concurrent edits to different fields → both win ───────────────────
    void concurrent_differentFields_bothWin()
    {
        const QString id     = QStringLiteral("task-cf-1");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString tsA    = makeHlc(2000, 0, kDevA); // for priority
        const QString tsB    = makeHlc(2000, 0, kDevB); // for content (same physMs+ctr, diff dev)

        // Device A: changes priority=1, leaves content unchanged ("Original")
        QJsonObject local = makeEntity(id, baseTs);
        local = withField(local, QStringLiteral("priority"), 1);
        local = withField(local, QStringLiteral("content"),  QStringLiteral("Original"));
        local = withFieldMeta(local, QStringLiteral("priority"), tsA, kDevA);

        // Device B: changes content="X", leaves priority unchanged (default 4)
        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("priority"), 4);
        remote = withField(remote, QStringLiteral("content"),  QStringLiteral("X"));
        remote = withFieldMeta(remote, QStringLiteral("content"), tsB, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // priority: local has CRDT meta, remote has none (and remote entity
        // has _meta so the "remote has _meta but not for this field" branch
        // makes local win).
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 1);
        // content: remote has CRDT meta, local has none (and local entity
        // has _meta) → remote wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("X"));
    }

    // ── 2. Same field, equal HLC → tiebreak by deviceId (higher devId wins)
    //
    // HLC::compare uses lexicographic on the deviceId portion of the HLC
    // when phys+ctr are equal. compareFieldMeta then *also* compares the
    // dev field — they're set to the same value so the HLC tiebreak fires.
    void sameField_equalHlc_deviceIdTiebreak()
    {
        const QString id     = QStringLiteral("task-tie-1");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");

        // Same phys + same ctr, different deviceIds embedded in the HLC.
        const QString tsA = makeHlc(5000, 0, QStringLiteral("aaa"));
        const QString tsZ = makeHlc(5000, 0, QStringLiteral("zzz"));

        QJsonObject local = makeEntity(id, baseTs);
        local = withField(local, QStringLiteral("priority"), 1);
        local = withFieldMeta(local, QStringLiteral("priority"), tsA, QStringLiteral("aaa"));

        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("priority"), 3);
        remote = withFieldMeta(remote, QStringLiteral("priority"), tsZ, QStringLiteral("zzz"));

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // "zzz" > "aaa" lexicographically → remote (zzz) wins.
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 3);
    }

    // ── 3. Strictly newer field-meta wins ─────────────────────────────────────
    void newer_field_wins()
    {
        const QString id     = QStringLiteral("task-newer-1");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString t1     = makeHlc(1000, 0, kDevA);
        const QString t2     = makeHlc(2000, 0, kDevB); // strictly newer

        QJsonObject local = makeEntity(id, baseTs);
        local = withField(local, QStringLiteral("priority"), 1);
        local = withFieldMeta(local, QStringLiteral("priority"), t1, kDevA);

        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("priority"), 2);
        remote = withFieldMeta(remote, QStringLiteral("priority"), t2, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 2);
    }

    // ── 4. Tombstone wins over concurrent edit (delete is older) ──────────────
    void tombstone_wins_overOlderEdit()
    {
        const QString id     = QStringLiteral("task-tomb-1");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString t1     = makeHlc(1000, 0, kDevB); // edit ts (older)

        // Local: deleted at later wall-clock (updated_at later)
        QJsonObject local = makeEntity(id,
            QStringLiteral("2026-05-02T00:00:00.000Z"), /*deleted=*/true);

        // Remote: alive, edited content earlier (and at older HLC).
        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("content"), QStringLiteral("Edit"));
        remote = withFieldMeta(remote, QStringLiteral("content"), t1, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── 5. Tombstone wins even when the *edit is newer* ───────────────────────
    //
    // The merger treats tombstones as absorbing — `mergeEntity` checks
    // is_deleted on either side BEFORE any HLC comparison runs. This is
    // important: it means deletes can never be "undone" by a concurrent
    // edit on a peer that hadn't yet seen the delete.
    void tombstone_wins_overNewerEdit()
    {
        const QString id      = QStringLiteral("task-tomb-2");
        const QString delTs   = QStringLiteral("2025-01-01T00:00:00.000Z");
        const QString editAt  = QStringLiteral("2026-12-31T00:00:00.000Z");
        const QString editHlc = makeHlc(99999999, 0, kDevB); // very high HLC

        // Local: deleted (older wall-clock).
        QJsonObject local = makeEntity(id, delTs, /*deleted=*/true);

        // Remote: alive, edited later both in wall-clock and HLC.
        QJsonObject remote = makeEntity(id, editAt);
        remote = withField(remote, QStringLiteral("content"), QStringLiteral("Newer Edit"));
        remote = withFieldMeta(remote, QStringLiteral("content"), editHlc, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // Tombstone is absorbing: stays deleted regardless of edit's HLC.
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── 6. OR-Set: add-after-remove → element resurrected (add-wins) ──────────
    void orset_addAfterRemove_resurrects()
    {
        const QString id     = QStringLiteral("task-ors-1");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString t1     = makeHlc(1000, 0, kDevA); // A: original add
        const QString t2     = makeHlc(2000, 0, kDevA); // A: removes label X
        const QString t3     = makeHlc(3000, 0, kDevB); // B: re-adds (newer)

        // Local (device A): had X, then removed it.
        QJsonObject local = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("X"), t1, t2} },
            t2, kDevA);

        // Remote (device B): adds X at a later HLC.
        QJsonObject remote = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("X"), t3, QString()} },
            t3, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();
        bool found = false;
        for (const auto &v : labels) {
            if (v.toString() == QStringLiteral("X")) { found = true; break; }
        }
        QVERIFY2(found, "OR-Set add-after-remove: X should be live (add-wins).");
    }

    // ── 7. OR-Set: remove-after-add → element gone ───────────────────────────
    //
    // A adds X at t1. B (which already saw the add via earlier sync) removes
    // X at t2 (newer). Per OR-Set semantics: removedAt > addedAt → element
    // is dead. The remove is *not* "concurrent with" the add — it observed
    // the add — so add-wins doesn't trigger.
    void orset_removeAfterAdd_gone()
    {
        const QString id     = QStringLiteral("task-ors-2");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString t1     = makeHlc(1000, 0, kDevA); // add
        const QString t2     = makeHlc(2000, 0, kDevB); // remove (newer)

        // Local: knows about the add only.
        QJsonObject local = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("X"), t1, QString()} },
            t1, kDevA);

        // Remote: same element id, but with removedAt set (saw the add then removed).
        QJsonObject remote = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("X"), t1, t2} },
            t2, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();
        for (const auto &v : labels) {
            QVERIFY2(v.toString() != QStringLiteral("X"),
                "OR-Set remove-after-add: X should be dead.");
        }
    }

    // ── 8. Legacy entity (no _meta) → falls back to whole-object LWW ─────────
    void legacy_noMeta_lwwFallback()
    {
        Merger m;
        const QString id = QStringLiteral("task-legacy-1");

        // Both sides: NO _meta blob anywhere. updated_at differs.
        QJsonObject local;
        local[QStringLiteral("id")]         = id;
        local[QStringLiteral("updated_at")] = QStringLiteral("2026-04-15T00:00:00.000Z");
        local[QStringLiteral("is_deleted")] = false;
        local[QStringLiteral("content")]    = QStringLiteral("Local newer");
        local[QStringLiteral("priority")]   = 2;

        QJsonObject remote;
        remote[QStringLiteral("id")]         = id;
        remote[QStringLiteral("updated_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
        remote[QStringLiteral("is_deleted")] = false;
        remote[QStringLiteral("content")]    = QStringLiteral("Remote older");
        remote[QStringLiteral("priority")]   = 4;

        // Sanity: confirm the helper agrees neither side has meta.
        QVERIFY(!CrdtMeta::hasMeta(local));
        QVERIFY(!CrdtMeta::hasMeta(remote));

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // Local is newer → whole local wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Local newer"));
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 2);
    }

    // ── 9. HLC tie + same dev: degenerate but must not crash ─────────────────
    //
    // This shouldn't happen in practice (the same device producing two
    // identical HLCs would mean a counter collision), but if it does the
    // merger must still terminate with a deterministic answer.
    void degenerate_sameHlc_sameDev_doesNotCrash()
    {
        const QString id     = QStringLiteral("task-degen-1");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString ts     = makeHlc(7777, 3, kDevA);

        QJsonObject local = makeEntity(id, baseTs);
        local = withField(local, QStringLiteral("content"), QStringLiteral("LL"));
        local = withFieldMeta(local, QStringLiteral("content"), ts, kDevA);

        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("content"), QStringLiteral("RR"));
        remote = withFieldMeta(remote, QStringLiteral("content"), ts, kDevA);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // compareFieldMeta returns 0 → remoteWins = (rFm vs lFm) > 0 → false
        // → local wins. Whichever side wins, the result must be a valid string.
        const QString c = task[QStringLiteral("content")].toString();
        QVERIFY(c == QStringLiteral("LL") || c == QStringLiteral("RR"));
    }

    // ── 10. Empty local + non-empty remote → remote preserved ────────────────
    void emptyLocal_nonEmptyRemote_preserved()
    {
        Merger m;

        QJsonArray remoteArr;
        QJsonObject t = makeEntity(QStringLiteral("only-remote"),
            QStringLiteral("2026-05-01T00:00:00.000Z"));
        t = withField(t, QStringLiteral("content"), QStringLiteral("Hello"));
        remoteArr.append(t);

        const QJsonObject merged = m.mergeStates(makeState(QJsonArray()), makeState(remoteArr));
        const QJsonArray  tasks  = merged[QStringLiteral("tasks")].toArray();

        QCOMPARE(tasks.size(), 1);
        QCOMPARE(findById(tasks, QStringLiteral("only-remote"))[QStringLiteral("content")]
                 .toString(),
                 QStringLiteral("Hello"));
    }

    // ── 11. Both empty → empty result ────────────────────────────────────────
    void bothEmpty_emptyResult()
    {
        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(QJsonArray()),
                                                 makeState(QJsonArray()));

        QCOMPARE(merged[QStringLiteral("tasks")].toArray().size(),     0);
        QCOMPARE(merged[QStringLiteral("projects")].toArray().size(),  0);
        QCOMPARE(merged[QStringLiteral("sections")].toArray().size(),  0);
        QCOMPARE(merged[QStringLiteral("labels")].toArray().size(),    0);
        QCOMPARE(merged[QStringLiteral("filters")].toArray().size(),   0);
        QCOMPARE(merged[QStringLiteral("reminders")].toArray().size(), 0);
        QCOMPARE(merged[QStringLiteral("comments")].toArray().size(),  0);
    }

    // ── 12. Mixed: one side CRDT-meta, other side legacy whole-entity ────────
    //
    // Neither side has _meta on the *content* field, but local has _meta on
    // some other field, so it's "CRDT". Remote is fully legacy. Per merger
    // rules: when local has CRDT meta and remote is legacy, the local field's
    // HLC physMs is compared against remote's updated_at ms. If local HLC
    // physMs >= remote.updated_at, local wins.
    void mixed_localCrdt_remoteLegacy_hlcVsUpdatedAt()
    {
        const QString id = QStringLiteral("task-mix-1");
        // Local: has CRDT meta on "priority", but not on "content".
        // The fact that _meta exists means hasMeta(local)==true.
        const QString prioTs = makeHlc(100, 0, kDevA);

        QJsonObject local = makeEntity(id, QStringLiteral("2026-04-01T00:00:00.000Z"));
        local = withField(local, QStringLiteral("priority"), 1);
        local = withField(local, QStringLiteral("content"),  QStringLiteral("LocalContent"));
        local = withFieldMeta(local, QStringLiteral("priority"), prioTs, kDevA);

        // Remote: pure legacy, newer updated_at.
        QJsonObject remote = makeEntity(id, QStringLiteral("2026-04-15T00:00:00.000Z"));
        remote = withField(remote, QStringLiteral("priority"), 4);
        remote = withField(remote, QStringLiteral("content"),  QStringLiteral("RemoteContent"));
        QVERIFY(!CrdtMeta::hasMeta(remote));

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // priority: local has CRDT meta, remote has none, remote has no _meta.
        //   → branch: localTs(physMs=100) < remote.updated_at_ms(very large)
        //   → remoteWins = true → priority = 4
        QCOMPARE(task[QStringLiteral("priority")].toInt(), 4);
        // content: neither side has meta, so falls back to whole-entity LWW.
        //   localTime (Apr 1) < remoteTime (Apr 15) → !localIsNewer → remote wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("RemoteContent"));
    }

    // ── 13. Tombstone idempotence: merging deleted with itself stays deleted ─
    void tombstone_idempotent()
    {
        const QString id = QStringLiteral("task-tomb-idem");
        const QString ts = QStringLiteral("2026-04-01T00:00:00.000Z");

        QJsonObject e = makeEntity(id, ts, /*deleted=*/true);
        e = withField(e, QStringLiteral("content"), QStringLiteral("doomed"));

        QJsonArray a; a.append(e);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(a), makeState(a));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── 14. Both sides delete + concurrent edits → deletion is preserved ─────
    void bothDeleted_concurrentEdits_stayDeleted()
    {
        const QString id     = QStringLiteral("task-tomb-conc");
        const QString baseTs = QStringLiteral("2026-04-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1000, 0, kDevA);
        const QString tsB    = makeHlc(2000, 0, kDevB);

        QJsonObject local = makeEntity(id, baseTs, true);
        local = withField(local, QStringLiteral("content"), QStringLiteral("a-edit"));
        local = withFieldMeta(local, QStringLiteral("content"), tsA, kDevA);

        QJsonObject remote = makeEntity(id, baseTs, true);
        remote = withField(remote, QStringLiteral("content"), QStringLiteral("b-edit"));
        remote = withFieldMeta(remote, QStringLiteral("content"), tsB, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── 15. OR-Set: concurrent ADD of two distinct labels on disjoint devices
    //
    // Both labels must end up live (add-wins union semantics).
    void orset_concurrentAdd_distinctLabels_unioned()
    {
        const QString id     = QStringLiteral("task-ors-3");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1000, 0, kDevA);
        const QString tsB    = makeHlc(1001, 0, kDevB);

        QJsonObject local = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("L-A"), tsA, QString()} }, tsA, kDevA);
        QJsonObject remote = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("L-B"), tsB, QString()} }, tsB, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();
        bool foundA = false, foundB = false;
        for (const auto &v : labels) {
            if (v.toString() == QStringLiteral("L-A")) foundA = true;
            if (v.toString() == QStringLiteral("L-B")) foundB = true;
        }
        QVERIFY(foundA);
        QVERIFY(foundB);
        QCOMPARE(labels.size(), 2);
    }

    // ── 16. OR-Set: equal addedAt vs removedAt → "removed" wins (deterministic)
    //
    // Per crdt_meta.cpp comments (B-2.25), when addedAt == removedAt the
    // element is considered removed (strict > clears removedAt; equality
    // does not).
    void orset_equalAddRemove_removedWins()
    {
        const QString id     = QStringLiteral("task-ors-eq");
        const QString baseTs = QStringLiteral("2026-01-01T00:00:00.000Z");
        const QString ts     = makeHlc(5000, 0, kDevA);

        QJsonObject local = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("Y"), ts, ts} }, ts, kDevA);

        QJsonArray la; la.append(local);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(QJsonArray()));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();
        for (const auto &v : labels)
            QVERIFY2(v.toString() != QStringLiteral("Y"),
                     "addedAt == removedAt should be treated as removed.");
    }

    // ── 17. Field exists only on remote (not local) → carried over ────────────
    void fieldOnlyOnRemote_carried()
    {
        const QString id     = QStringLiteral("task-only-remote-field");
        const QString baseTs = QStringLiteral("2026-04-01T00:00:00.000Z");
        const QString tsB    = makeHlc(2000, 0, kDevB);

        // Local: only "content" field with meta.
        QJsonObject local = makeEntity(id, baseTs);
        local = withField(local, QStringLiteral("content"), QStringLiteral("c"));
        local = withFieldMeta(local, QStringLiteral("content"), tsB, kDevA);

        // Remote: has "content" + an extra "description" with its own meta.
        QJsonObject remote = makeEntity(id, baseTs);
        remote = withField(remote, QStringLiteral("content"),     QStringLiteral("c"));
        remote = withField(remote, QStringLiteral("description"), QStringLiteral("desc-from-remote"));
        remote = withFieldMeta(remote, QStringLiteral("content"),     tsB, kDevA);
        remote = withFieldMeta(remote, QStringLiteral("description"), tsB, kDevB);

        QJsonArray la; la.append(local);
        QJsonArray ra; ra.append(remote);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("description")].toString(),
                 QStringLiteral("desc-from-remote"));
    }
};

QTEST_GUILESS_MAIN(TestMergerEdgeCases)
#include "test_merger_edge_cases.moc"
