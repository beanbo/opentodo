// =============================================================================
// CRDT multi-device interleaving — 3-4 device conflict simulation
// =============================================================================
//
// Complements tests/unit/test_crdt.cpp, test_merger.cpp, test_merger_edge_cases.cpp,
// test_hlc_edge_cases.cpp, test_merger_field_level_per_entity.cpp.
//
// All existing CRDT suites cover 2-device pairs (local vs remote). Real OpenTodo
// installs are commonly 3+ devices (desktop + laptop + phone, often with a
// shared Drive folder mounted on every device). This file drives the merger
// through interleavings with THREE and FOUR concurrent peers, plus a handful
// of clock-skew / forward-compat scenarios that touch the same code paths.
//
// Strategy: directly call Merger::mergeStates with hand-built JSON. By
// associativity (pinned in test_crdt.cpp::crdt_associativity), a 3-way merge
// is equivalent to two sequential 2-way merges in any order; we exploit that
// to simulate "device C pulls after A+B have synced" etc.
//
// Slots in this file pin CURRENT behaviour — they assert the merge result that
// the current implementation produces, and emit qInfo where the choice is
// subtle (e.g. OR-Set re-add HLC ordering, OR-Set deterministic sort by
// addedAt, HLC clamping rejection vs acceptance). When a future fix flips a
// behaviour, the assertion will fail loudly and document the change.
//
// Suite is QTEST_GUILESS_MAIN — no QGuiApplication needed.

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QDateTime>

#include "sync/merger.h"
#include "sync/crdt_meta.h"
#include "sync/hlc.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static const QString kDevA = QStringLiteral("dev-aaa");
static const QString kDevB = QStringLiteral("dev-bbb");
static const QString kDevC = QStringLiteral("dev-ccc");
static const QString kDevD = QStringLiteral("dev-ddd");

// Build an HLC string from raw components (mirrors makeHlc() in the
// surrounding CRDT suites; kept local so this file is independent).
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

static QJsonObject stateForOne(const QJsonObject &entity)
{
    QJsonArray a;
    a.append(entity);
    return makeState(a);
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

// Build an entity with a "labels" OR-Set populated from per-element tuples
// (add/remove HLCs). fieldTs/fieldDev are the per-field meta the merger uses
// when deciding which side's field-level meta "wins" for the OR-Set reconstruction.
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

    QJsonArray plain;
    QList<CrdtMeta::OrsElement> elems;
    for (const auto &l : labels) {
        CrdtMeta::OrsElement e;
        e.id        = l.id;
        e.addedAt   = l.addedAt;
        e.removedAt = l.removedAt;
        elems.append(e);
        const bool live = l.removedAt.isEmpty() ||
            HybridLogicalClock::compare(l.addedAt, l.removedAt) > 0;
        if (live) plain.append(l.id);
    }
    obj[QStringLiteral("labels")] = plain;
    CrdtMeta::setOrSetElements(obj, QStringLiteral("labels"), elems, fieldTs, fieldDev);
    return obj;
}

// Helper: perform a 3-way merge by chaining two pairwise merges. Order is
// (a + b) + c. By the associativity property pinned in test_crdt.cpp the
// result is independent of grouping.
static QJsonObject mergeThree(Merger &m,
                              const QJsonObject &a,
                              const QJsonObject &b,
                              const QJsonObject &c)
{
    const QJsonObject ab = m.mergeStates(a, b);
    return m.mergeStates(ab, c);
}

static QJsonObject mergeFour(Merger &m,
                             const QJsonObject &a,
                             const QJsonObject &b,
                             const QJsonObject &c,
                             const QJsonObject &d)
{
    const QJsonObject ab  = m.mergeStates(a, b);
    const QJsonObject abc = m.mergeStates(ab, c);
    return m.mergeStates(abc, d);
}

// ── Test class ───────────────────────────────────────────────────────────────

class TestCrdtMultiDevice : public QObject
{
    Q_OBJECT

private slots:
    // ─────────────────────────────────────────────────────────────────────────
    // 1. Three devices each edit a DIFFERENT field concurrently.
    //
    // Per per-field LWW semantics each device's edit wins on its own field
    // (the other two devices never touched that field, so their _meta entries
    // for it are absent → the editing device's meta wins by the "field only
    // on one side has meta" branch in resolveWithCrdt).
    //
    // Concurrency is faked by giving all three the SAME baseline updated_at
    // and disjoint per-field _meta — none of the three "observed" the others.
    // ─────────────────────────────────────────────────────────────────────────
    void three_devices_concurrent_field_edits_each_wins_its_own_field()
    {
        const QString id     = QStringLiteral("task-3dev-disjoint");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");

        // All three HLCs share physMs (concurrent ms); counter differs only
        // for entropy. They are deliberately NOT chained via HLC::update.
        const QString tsA = makeHlc(10000, 0, kDevA); // content
        const QString tsB = makeHlc(10000, 0, kDevB); // priority
        const QString tsC = makeHlc(10000, 0, kDevC); // due_date

        // Dev A: edits content; carries default priority/due so merge sees
        // them but they have no _meta on A.
        QJsonObject a = makeEntity(id, baseTs);
        a = withField(a, QStringLiteral("content"),  QStringLiteral("A-content"));
        a = withField(a, QStringLiteral("priority"), 4);
        a = withFieldMeta(a, QStringLiteral("content"), tsA, kDevA);

        QJsonObject b = makeEntity(id, baseTs);
        b = withField(b, QStringLiteral("content"),  QStringLiteral("baseline"));
        b = withField(b, QStringLiteral("priority"), 1);
        b = withFieldMeta(b, QStringLiteral("priority"), tsB, kDevB);

        QJsonObject c = makeEntity(id, baseTs);
        c = withField(c, QStringLiteral("content"),  QStringLiteral("baseline"));
        c = withField(c, QStringLiteral("priority"), 4);
        c = withField(c, QStringLiteral("due_date"), QStringLiteral("2026-06-01"));
        c = withFieldMeta(c, QStringLiteral("due_date"), tsC, kDevC);

        Merger m;
        const QJsonObject merged = mergeThree(m,
                                              stateForOne(a),
                                              stateForOne(b),
                                              stateForOne(c));
        const QJsonObject task = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());

        // Each editor wins its own field.
        QCOMPARE(task[QStringLiteral("content")].toString(),  QStringLiteral("A-content"));
        QCOMPARE(task[QStringLiteral("priority")].toInt(),    1);
        QCOMPARE(task[QStringLiteral("due_date")].toString(), QStringLiteral("2026-06-01"));

        qInfo("3-dev disjoint: each device wins its own field via field-only-on-one-side meta path");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Three devices ORDERED writes — each pulls predecessor first.
    //
    // A writes content="v1" at HLC=1000.
    // B observes A's HLC then writes content="v2" at HLC=2000 (post-observe).
    // C observes both then writes content="v3" at HLC=3000.
    //
    // Merge in any pairwise order: latest HLC = 3000 = dev C → v3 wins.
    // ─────────────────────────────────────────────────────────────────────────
    void three_devices_ordered_writes_observe_predecessor()
    {
        const QString id     = QStringLiteral("task-3dev-ordered");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString ts1    = makeHlc(1000, 0, kDevA);
        const QString ts2    = makeHlc(2000, 0, kDevB);
        const QString ts3    = makeHlc(3000, 0, kDevC);

        QJsonObject a = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("v1")),
            QStringLiteral("content"), ts1, kDevA);

        QJsonObject b = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("v2")),
            QStringLiteral("content"), ts2, kDevB);

        QJsonObject c = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("v3")),
            QStringLiteral("content"), ts3, kDevC);

        Merger m;
        const QJsonObject merged = mergeThree(m,
                                              stateForOne(a),
                                              stateForOne(b),
                                              stateForOne(c));
        const QJsonObject task = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("v3"));

        // Verify the meta is also from dev C.
        const QJsonObject fm = CrdtMeta::getFieldMeta(task, QStringLiteral("content"));
        QCOMPARE(fm[QStringLiteral("dev")].toString(), kDevC);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3. Four devices simultaneous label-set mutations (OR-Set union).
    //
    // A adds label-x at t=1000.
    // B adds label-y at t=1001.
    // C adds label-z at t=1002.
    // D removes label-w (which was never added on the merge inputs).
    //
    // Add-wins union semantics: all three concurrent ADDs survive. D's
    // removal of a phantom element materialises as an OR-Set entry with
    // a removedAt but no matching add — so it stays dead (not resurrected).
    // ─────────────────────────────────────────────────────────────────────────
    void four_devices_simultaneous_label_adds_union_via_orset()
    {
        const QString id     = QStringLiteral("task-4dev-labels");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString tsA    = makeHlc(1000, 0, kDevA);
        const QString tsB    = makeHlc(1001, 0, kDevB);
        const QString tsC    = makeHlc(1002, 0, kDevC);
        const QString tsD    = makeHlc(1003, 0, kDevD);

        QJsonObject a = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-x"), tsA, QString()} }, tsA, kDevA);
        QJsonObject b = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-y"), tsB, QString()} }, tsB, kDevB);
        QJsonObject c = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-z"), tsC, QString()} }, tsC, kDevC);
        // Dev D records a removal of a label that no one else has added.
        // OR-Set element: addedAt empty-ish (we encode it as an old HLC <
        // removedAt so it counts as removed in orSetToList).
        QJsonObject d = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-w"),
                         makeHlc(500, 0, kDevD),
                         tsD} }, tsD, kDevD);

        Merger m;
        const QJsonObject merged = mergeFour(m,
                                             stateForOne(a),
                                             stateForOne(b),
                                             stateForOne(c),
                                             stateForOne(d));
        const QJsonObject task = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();

        QSet<QString> seen;
        for (const auto &v : labels) seen.insert(v.toString());

        QVERIFY(seen.contains(QStringLiteral("label-x")));
        QVERIFY(seen.contains(QStringLiteral("label-y")));
        QVERIFY(seen.contains(QStringLiteral("label-z")));
        // label-w: D's "remove" without a concurrent add → stays absent.
        QVERIFY(!seen.contains(QStringLiteral("label-w")));
        QCOMPARE(labels.size(), 3);

        qInfo("4-dev OR-Set union: three concurrent adds preserved; phantom-remove ignored");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4. OR-Set add-then-remove-then-re-add (concurrent re-add vs remove).
    //
    // Scenario:
    //   t=1000: Dev A adds label-x.
    //   t=2000: Dev B (which observed A's add) removes label-x.
    //   t=1500: Dev A re-adds label-x at a STRICTLY LATER HLC than its own
    //           original add but EARLIER than B's remove.
    //   (i.e. A's re-add did NOT observe B's remove; concurrent.)
    //
    // Per CrdtMeta::mergeOrSets: takes max(addedAt), max(removedAt). Then
    // add-wins clears removedAt iff addedAt STRICTLY > removedAt.
    //
    // Here: addedAt=max(1000,1500)=1500, removedAt=2000. 1500 < 2000 →
    // removedAt stays → element is dead. So concurrent re-add at an HLC
    // BELOW the remove HLC does NOT resurrect — REMOVE wins on this branch.
    //
    // This pins the actual behaviour: it is NOT an unconditional "add wins
    // any conflict". It is "the LATEST add wins iff it exceeds the latest
    // removedAt by strict HLC".
    // ─────────────────────────────────────────────────────────────────────────
    void add_then_remove_then_re_add_orset_semantics()
    {
        const QString id     = QStringLiteral("task-readd-conflict");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString t1000  = makeHlc(1000, 0, kDevA); // A's original add
        const QString t1500  = makeHlc(1500, 0, kDevA); // A's re-add (concurrent w/ B)
        const QString t2000  = makeHlc(2000, 0, kDevB); // B's remove

        // Local (A): re-adds label-x at t1500 (latest addedAt on A's side).
        QJsonObject a = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-x"), t1500, QString()} },
            t1500, kDevA);

        // Remote (B): had label-x (added at t1000) and removed it at t2000.
        QJsonObject b = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-x"), t1000, t2000} },
            t2000, kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateForOne(a), stateForOne(b));
        const QJsonObject task   = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        const QJsonArray labels = task[QStringLiteral("labels")].toArray();

        bool found = false;
        for (const auto &v : labels) {
            if (v.toString() == QStringLiteral("label-x")) { found = true; break; }
        }

        // Pin: A's re-add at 1500 < B's remove at 2000 → REMOVE wins.
        QVERIFY2(!found,
            "Re-add at HLC<remove should NOT resurrect (remove-wins on this branch).");
        qInfo("OR-Set add-after-remove: resurrection requires re-add HLC STRICTLY > remove HLC");

        // Sanity: in the other direction (re-add HLC > remove HLC), the
        // contract pins add-wins. We re-test that path to make the asymmetry
        // explicit in this slot.
        QJsonObject a2 = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-x"), makeHlc(3000, 0, kDevA), QString()} },
            makeHlc(3000, 0, kDevA), kDevA);
        QJsonObject b2 = makeEntityWithLabels(id, baseTs,
            { LabelEntry{QStringLiteral("label-x"), t1000, t2000} },
            t2000, kDevB);
        const QJsonObject merged2 = m.mergeStates(stateForOne(a2), stateForOne(b2));
        const QJsonObject task2   = findById(
            merged2[QStringLiteral("tasks")].toArray(), id);
        const QJsonArray labels2  = task2[QStringLiteral("labels")].toArray();
        bool found2 = false;
        for (const auto &v : labels2) {
            if (v.toString() == QStringLiteral("label-x")) { found2 = true; break; }
        }
        QVERIFY2(found2, "Re-add at HLC strictly>remove must resurrect.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5. Tombstone propagates through three devices, surviving later edits.
    //
    // A marks the task deleted (with a high HLC on is_deleted flip).
    // B & C both pull A's commit (so they see is_deleted=true on the entity),
    // but they keep editing the entity LOCALLY (perhaps on an out-of-date
    // local cache) and push their edits back.
    //
    // Per the documented contract in Merger::mergeEntity:
    //   if (remote.is_deleted) return remote;
    //   if (local.is_deleted)  return local;
    // → tombstone is absorbing in ALL pairwise merges, regardless of HLC.
    //
    // So after (A + B) + C the tombstone wins; the post-tombstone edits by
    // B and C are silently discarded.
    // ─────────────────────────────────────────────────────────────────────────
    void tombstone_propagates_through_three_devices()
    {
        const QString id      = QStringLiteral("task-3dev-tomb");
        const QString baseTs  = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString delTs   = QStringLiteral("2026-05-02T00:00:00.000Z");
        const QString tsBHigh = makeHlc(99999999, 0, kDevB); // very newer edit
        const QString tsCHigh = makeHlc(99999999, 1, kDevC); // very newer edit

        // A: tombstoned.
        QJsonObject a = makeEntity(id, delTs, /*deleted=*/true);

        // B: alive, freshly edited content (unaware of tombstone).
        QJsonObject b = makeEntity(id, baseTs);
        b = withField(b, QStringLiteral("content"), QStringLiteral("B-late-edit"));
        b = withFieldMeta(b, QStringLiteral("content"), tsBHigh, kDevB);

        // C: alive, edited priority (also unaware).
        QJsonObject c = makeEntity(id, baseTs);
        c = withField(c, QStringLiteral("priority"), 1);
        c = withFieldMeta(c, QStringLiteral("priority"), tsCHigh, kDevC);

        Merger m;
        const QJsonObject merged = mergeThree(m,
                                              stateForOne(a),
                                              stateForOne(b),
                                              stateForOne(c));
        const QJsonObject task = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY2(task[QStringLiteral("is_deleted")].toBool(),
                 "Tombstone must absorb all subsequent edits across three devices.");

        // Edits by B and C are NOT preserved (the tombstoned A-entity returned
        // by mergeEntity has neither field).
        QVERIFY(!task.contains(QStringLiteral("content")) ||
                task[QStringLiteral("content")].toString().isEmpty());

        qInfo("3-dev tombstone: deletion is absorbing — post-tombstone edits SILENTLY DROPPED");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6. Physical clock skew 60min+ on Dev A, normal clocks on B and C.
    //
    // The HLC::update() path REJECTS inbound HLCs whose physMs exceeds the
    // local clock by more than 60 minutes (kMaxClockSkewMs). Q3.05-C added
    // defence-in-depth to the MERGER: far-future per-field HLCs are now
    // demoted (clamped to wall-now for comparison only) before LWW resolution.
    //
    // Pin (post-Q3.05-C): A's +65min HLC is clamped before comparison, so the
    // legitimate peer with a slightly newer real HLC (C, at +1s) wins the
    // per-field LWW. A's data is NOT lost — its ts is preserved on disk —
    // but the bad HLC no longer auto-wins.
    //
    // Defence-in-depth: HLC::update is the gate at SyncEngine ingest (rejects
    // outright); Merger clamps the bypassed paths (initial Drive bootstrap,
    // legacy pre-CRDT migration, direct file import).
    // ─────────────────────────────────────────────────────────────────────────
    void physical_clock_skew_60min_clamp_on_three_devices()
    {
        const QString id     = QStringLiteral("task-skew-3dev");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");

        // A's HLC is "year 2030" — far ahead of B/C's normal HLCs.
        const quint64 nowMs   = static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        const quint64 farMs   = nowMs + (60ULL * 60 * 1000) + (5ULL * 60 * 1000); // +65min
        const QString tsAhead = makeHlc(farMs, 0, kDevA);
        const QString tsB     = makeHlc(nowMs,         0, kDevB);
        // C must out-rank A's bad HLC after A is clamped to wall-now AT MERGE
        // TIME. A +1s margin (the original) flips under heavy parallel-test load
        // when the merge runs >1s after nowMs is captured here — the clamp
        // anchor then exceeds C's physMs and A wins (flaky failure). Use a +5min
        // margin: far above any realistic merge delay, yet well inside the 60-min
        // clamp window so C is NOT itself clamped. counter>0 also breaks any
        // same-ms tie against the clamped (counter=0) A.
        const QString tsC     = makeHlc(nowMs + 5ULL * 60 * 1000, 1, kDevC);

        QJsonObject a = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("A-skewed")),
            QStringLiteral("content"), tsAhead, kDevA);
        QJsonObject b = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("B-normal")),
            QStringLiteral("content"), tsB, kDevB);
        QJsonObject c = withFieldMeta(
            withField(makeEntity(id, baseTs), QStringLiteral("content"), QStringLiteral("C-normal")),
            QStringLiteral("content"), tsC, kDevC);

        // 6a. Direct merger — Q3.05-C clamps A's bad HLC. C wins (latest
        // legitimate edit by physMs+counter).
        Merger m;
        const QJsonObject mergedRaw = mergeThree(m,
                                                 stateForOne(a),
                                                 stateForOne(b),
                                                 stateForOne(c));
        const QJsonObject taskRaw   = findById(
            mergedRaw[QStringLiteral("tasks")].toArray(), id);
        QVERIFY(!taskRaw.isEmpty());
        QCOMPARE(taskRaw[QStringLiteral("content")].toString(), QStringLiteral("C-normal"));
        qInfo("Q3.05-C: Merger clamps far-future HLC — legitimate peer C wins, A's bad HLC demoted");

        // 6b. HLC::update() would have REJECTED A's HLC at SyncEngine ingest.
        // This is the FIRST line of defence — SyncEngine routes inbound
        // commits through HLC::update so the bad HLC never reaches Merger
        // for the commit path. The clamp in Merger is the SECOND line, for
        // bypass paths (Drive bootstrap, legacy file imports).
        const QString localBefore = makeHlc(nowMs - 1000, 0, kDevB);
        const QString afterReject = HybridLogicalClock::update(
            localBefore, tsAhead, kDevB);
        // The contract: rejected → local HLC returned UNCHANGED.
        QCOMPARE(afterReject, localBefore);
        qInfo("HLC::update gates the commit path (rejects); Merger clamps bypass paths (demotes)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 7. Concurrent section drag in kanban: two devices reorder same section.
    //
    // The Task model carries a "section_id" + "day_order" (int) per task,
    // not a list-on-section. Two devices dragging different tasks to the
    // top of the same section both set day_order=0 (or similar) at the same
    // logical moment.
    //
    // Pin: each task is an independent entity → per-task merge is
    // independent. Both tasks end up in section-1 with their respective
    // (concurrent) day_order writes preserved by per-field LWW.
    //
    // Tie-break rule when both day_order writes go to the SAME task: HLC
    // (later wins; deviceId lex tiebreak).
    // ─────────────────────────────────────────────────────────────────────────
    void concurrent_section_drag_in_kanban_preserves_both_orders()
    {
        const QString taskX  = QStringLiteral("task-x");
        const QString taskY  = QStringLiteral("task-y");
        const QString sec1   = QStringLiteral("section-1");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString tsA    = makeHlc(5000, 0, kDevA);
        const QString tsB    = makeHlc(5000, 0, kDevB); // same physMs+ctr, deviceId tiebreak

        // Device A: moves task X to top of section-1.
        QJsonObject xOnA = makeEntity(taskX, baseTs);
        xOnA = withField(xOnA, QStringLiteral("section_id"), sec1);
        xOnA = withField(xOnA, QStringLiteral("day_order"),  0);
        xOnA = withFieldMeta(xOnA, QStringLiteral("section_id"), tsA, kDevA);
        xOnA = withFieldMeta(xOnA, QStringLiteral("day_order"),  tsA, kDevA);

        // Device B: moves task Y to top of section-1.
        QJsonObject yOnB = makeEntity(taskY, baseTs);
        yOnB = withField(yOnB, QStringLiteral("section_id"), sec1);
        yOnB = withField(yOnB, QStringLiteral("day_order"),  0);
        yOnB = withFieldMeta(yOnB, QStringLiteral("section_id"), tsB, kDevB);
        yOnB = withFieldMeta(yOnB, QStringLiteral("day_order"),  tsB, kDevB);

        // Each side's state has BOTH tasks (B's state has Y dragged but
        // never touched X, so X-on-B is the baseline). For minimalism we
        // put only the dragged task on each side; the OTHER task carries
        // over verbatim from the side that has it.
        QJsonArray la; la.append(xOnA);
        QJsonArray ra; ra.append(yOnB);

        Merger m;
        const QJsonObject merged = m.mergeStates(makeState(la), makeState(ra));
        const QJsonArray tasks   = merged[QStringLiteral("tasks")].toArray();

        QCOMPARE(tasks.size(), 2);

        // Both tasks should be in section-1 at day_order=0.
        const QJsonObject finalX = findById(tasks, taskX);
        const QJsonObject finalY = findById(tasks, taskY);
        QVERIFY(!finalX.isEmpty());
        QVERIFY(!finalY.isEmpty());
        QCOMPARE(finalX[QStringLiteral("section_id")].toString(), sec1);
        QCOMPARE(finalY[QStringLiteral("section_id")].toString(), sec1);
        QCOMPARE(finalX[QStringLiteral("day_order")].toInt(), 0);
        QCOMPARE(finalY[QStringLiteral("day_order")].toInt(), 0);

        qInfo("Kanban drag: same day_order on different tasks → both preserved (per-task merge)");
        qInfo("Kanban drag: ordering tie-break on SAME task uses HLC + deviceId lex (see slot 1)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 8. Forward-compat: applyCommit-style scenario where one side carries
    //    an UNKNOWN field name (e.g. a future client added "ai_summary" to
    //    Task before the local client knows about it).
    //
    // For Merger (which operates on JSON only, no model schema): unknown
    // fields are just QJsonValue entries. They pass through to the merged
    // result transparently (carried over from whichever side has them).
    //
    // Pin: forward-compatibility IS preserved at the Merger layer. The
    // sanitisation/strip-down (if any) would happen at applyCommit or
    // toJson/fromJson in the model layer, NOT in Merger.
    // ─────────────────────────────────────────────────────────────────────────
    void commit_with_unknown_field_in_data_is_preserved_or_dropped()
    {
        const QString id     = QStringLiteral("task-unknown-field");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const QString ts     = makeHlc(7000, 0, kDevA);

        QJsonObject a = makeEntity(id, baseTs);
        a = withField(a, QStringLiteral("content"),    QStringLiteral("known"));
        a = withField(a, QStringLiteral("ai_summary"), QStringLiteral("future-field"));
        a = withFieldMeta(a, QStringLiteral("content"),    ts, kDevA);
        a = withFieldMeta(a, QStringLiteral("ai_summary"), ts, kDevA);

        QJsonObject b = makeEntity(id, baseTs);
        b = withField(b, QStringLiteral("content"), QStringLiteral("known"));
        b = withFieldMeta(b, QStringLiteral("content"), ts, kDevA);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateForOne(a), stateForOne(b));
        const QJsonObject task   = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QVERIFY2(task.contains(QStringLiteral("ai_summary")),
                 "Unknown future field should be preserved in merged JSON (forward-compat).");
        QCOMPARE(task[QStringLiteral("ai_summary")].toString(), QStringLiteral("future-field"));

        // Pin that the _meta entry for the unknown field is ALSO preserved
        // (so a roundtrip through Merger doesn't strip the per-field HLC).
        const QJsonObject fm = CrdtMeta::getFieldMeta(task, QStringLiteral("ai_summary"));
        QVERIFY2(!fm.isEmpty(),
                 "Unknown field _meta should be preserved too (no schema-aware sanitisation).");
        QCOMPARE(fm[QStringLiteral("ts")].toString(), ts);

        qInfo("Merger is schema-agnostic: unknown fields + their _meta pass through verbatim");
        qInfo("DOCUMENTED GAP: sanitisation (if needed) must happen at applyCommit / fromJson, not here");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9. HLC physMs claiming year 2099 — Merger CLAMPS for comparison.
    //
    // The Merger uses HybridLogicalClock::compare (string compare on numeric
    // hex). A "year 2099" HLC has physMs ≈ 4.1e12 vs current ≈ 1.8e12.
    //
    // Q3.05-C defence-in-depth: HybridLogicalClock::update() rejects
    // far-future HLCs at SyncEngine ingest, but Merger::mergeEntity() runs
    // on raw JSON that may BYPASS update() (initial Drive bootstrap, legacy
    // state.json migrated from pre-CRDT format, direct file import). To
    // close that gap, Merger now demotes any per-field HLC whose physMs is
    // more than 60 minutes ahead of local wall clock to a synthesised
    // clamped HLC anchored at wall-now (for comparison purposes only — the
    // on-disk ts in _meta is preserved for forward-compat).
    //
    // Result: a year-2099 HLC no longer auto-wins against a legitimate peer
    // with a normal clock. The legitimate peer's edit wins, but the
    // poisoned peer's data is NOT lost — it's just outranked.
    // ─────────────────────────────────────────────────────────────────────────
    void commit_with_HLC_in_far_future_is_clamped_not_rejected()
    {
        const QString id     = QStringLiteral("task-y2099");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        // Year 2099 in ms-since-epoch ≈ 4.07e12.
        const quint64 yr2099 = 4070000000000ULL;
        const QString tsFar  = makeHlc(yr2099, 0, kDevA);
        // +5min margin so B reliably out-ranks A's year-2099 HLC after it is
        // clamped to wall-now AT MERGE TIME. A counter-only margin (the
        // original) flips whenever the merge crosses a millisecond boundary
        // under parallel-test load — the clamp anchor then exceeds B's physMs
        // and the bad HLC wins (flaky failure). 5min is well inside the 60-min
        // clamp window, so tsNow is not itself clamped.
        const QString tsNow  = makeHlc(static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch())
            + 5ULL * 60 * 1000, 1, kDevB);

        QJsonObject a = withFieldMeta(
            withField(makeEntity(id, baseTs),
                      QStringLiteral("content"), QStringLiteral("Y2099-edit")),
            QStringLiteral("content"), tsFar, kDevA);

        QJsonObject b = withFieldMeta(
            withField(makeEntity(id, baseTs),
                      QStringLiteral("content"), QStringLiteral("present-day-edit")),
            QStringLiteral("content"), tsNow, kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateForOne(a), stateForOne(b));
        const QJsonObject task   = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // Q3.05-C: the legitimate peer's present-day edit must win — the
        // year-2099 HLC is clamped before comparison and therefore LOSES to
        // a normal-clock peer. This pins the fix.
        QCOMPARE(task[QStringLiteral("content")].toString(),
                 QStringLiteral("present-day-edit"));

        // The winning field meta should be the legitimate peer's (B's) ts,
        // not the year-2099 ts.
        const QJsonObject fm = CrdtMeta::getFieldMeta(task, QStringLiteral("content"));
        QCOMPARE(fm[QStringLiteral("ts")].toString(),         tsNow);
        QCOMPARE(fm[QStringLiteral("dev")].toString(),        kDevB);

        qInfo("Year-2099 HLC: CLAMPED by Merger — legitimate peer with normal clock wins");
        qInfo("Defence-in-depth: HLC::update gates SyncEngine ingest, Merger clamps bypassed-paths (Drive bootstrap, legacy import)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9b. Mirror of slot 9 with role-reversal: bad HLC on the REMOTE side
    // (rather than local) must also be demoted. This guards against a peer
    // pushing a poisoned state to Drive that another device pulls in.
    // ─────────────────────────────────────────────────────────────────────────
    void commit_with_HLC_in_far_future_is_clamped_on_remote_too()
    {
        const QString id     = QStringLiteral("task-y2099-remote");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const quint64 yr2099 = 4070000000000ULL;
        const QString tsFar  = makeHlc(yr2099, 0, kDevA);
        // +5min margin (see slot 9): robust against the wall-now-at-merge clamp
        // anchor drifting past a counter-only margin under parallel-test load.
        const QString tsNow  = makeHlc(static_cast<quint64>(
            QDateTime::currentDateTimeUtc().toMSecsSinceEpoch())
            + 5ULL * 60 * 1000, 1, kDevB);

        // This time A (with bad HLC) is REMOTE and B (legit) is LOCAL.
        QJsonObject local = withFieldMeta(
            withField(makeEntity(id, baseTs),
                      QStringLiteral("content"), QStringLiteral("present-day-edit")),
            QStringLiteral("content"), tsNow, kDevB);

        QJsonObject remote = withFieldMeta(
            withField(makeEntity(id, baseTs),
                      QStringLiteral("content"), QStringLiteral("Y2099-edit")),
            QStringLiteral("content"), tsFar, kDevA);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateForOne(local),
                                                  stateForOne(remote));
        const QJsonObject task   = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(),
                 QStringLiteral("present-day-edit"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9c. Bad HLC is clamped, NOT rejected — the on-disk ts is preserved.
    //
    // When a bad HLC field actually beats a competing field (e.g. its real
    // value is more recent than the legit peer's), it does win — but the
    // clamp guarantees that win is decided by the clamped comparison, not
    // by the year-2099 physMs. Once the win is decided, the original ts
    // (year-2099) is written back to merged["_meta"] so forward sync still
    // sees the true HLC of the edit. This is "demote, don't drop".
    //
    // Concretely: if only ONE side has the field at all (other side never
    // saw it), the bad-HLC side's value survives because there's nothing
    // to compete against — the clamp only matters when there's a competing
    // field on the other side.
    // ─────────────────────────────────────────────────────────────────────────
    void clamped_bad_HLC_preserves_original_ts_when_no_competition()
    {
        const QString id     = QStringLiteral("task-y2099-uncontested");
        const QString baseTs = QStringLiteral("2026-05-01T00:00:00.000Z");
        const quint64 yr2099 = 4070000000000ULL;
        const QString tsFar  = makeHlc(yr2099, 0, kDevA);

        // Only A has a content edit. B is bare (never touched content).
        QJsonObject a = withFieldMeta(
            withField(makeEntity(id, baseTs),
                      QStringLiteral("content"), QStringLiteral("Y2099-edit")),
            QStringLiteral("content"), tsFar, kDevA);

        QJsonObject b = makeEntity(id, baseTs);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateForOne(a), stateForOne(b));
        const QJsonObject task   = findById(
            merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        // Uncontested: A's value survives (clamp only matters in head-to-head).
        QCOMPARE(task[QStringLiteral("content")].toString(),
                 QStringLiteral("Y2099-edit"));

        // The original (unclamped) ts is preserved on disk so a future sync
        // sees the true HLC of the edit.
        const QJsonObject fm = CrdtMeta::getFieldMeta(task, QStringLiteral("content"));
        QCOMPARE(fm[QStringLiteral("ts")].toString(), tsFar);
    }
};

QTEST_GUILESS_MAIN(TestCrdtMultiDevice)
#include "test_crdt_multidevice.moc"
