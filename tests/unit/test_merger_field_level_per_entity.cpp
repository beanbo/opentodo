// =============================================================================
// Field-level CRDT merge — per non-Task entity type
// =============================================================================
//
// Q2.10-A fixed a critical bug where the per-entity meta map was double-wrapped:
//
//     entity["_meta"] = { "_meta": { fieldName: { ts, dev } } }
//
// instead of the intended single-wrap shape:
//
//     entity["_meta"] = { fieldName: { ts, dev } }
//
// In the broken layout, Merger::resolveWithCrdt looked at
// entity["_meta"][fieldName] and got an opaque object that did NOT have "ts"
// and "dev" keys, so CrdtMeta::compareFieldMeta saw two empty objects and
// silently fell through to whole-entity LWW for ALL non-Task entity types.
//
// (Task wasn't affected because TaskViewModel::updateTask uses a slightly
// different code path.)
//
// These tests exercise the now-correct CRDT path for every other entity type:
// Project, Section, Label, Filter, Reminder, Comment. Each gets three slots:
//
//   a. Concurrent edits to DIFFERENT fields → both win
//   b. Concurrent edits to the SAME field, newer HLC wins
//   c. Tombstone wins over a (newer) field edit
//
// The state JSON is constructed by hand; no DataStore involvement. This
// keeps the test purely focused on Merger behaviour given correctly-shaped
// per-field meta blobs.

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

// Build an HLC string from raw components. Mirrors makeHlc() in
// test_merger_edge_cases.cpp — kept local so this file is independent.
static QString makeHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

// Empty-state skeleton. Caller fills in one collection.
static QJsonObject makeState()
{
    QJsonObject s;
    s[QStringLiteral("tasks")]     = QJsonArray();
    s[QStringLiteral("projects")]  = QJsonArray();
    s[QStringLiteral("sections")]  = QJsonArray();
    s[QStringLiteral("labels")]    = QJsonArray();
    s[QStringLiteral("filters")]   = QJsonArray();
    s[QStringLiteral("reminders")] = QJsonArray();
    s[QStringLiteral("comments")]  = QJsonArray();
    s[QStringLiteral("settings")]  = QJsonObject();
    return s;
}

static QJsonObject stateWith(const QString &collection, const QJsonObject &entity)
{
    QJsonObject s = makeState();
    QJsonArray arr;
    arr.append(entity);
    s[collection] = arr;
    return s;
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

// Stamp a single field with the flat (Q2.10-A-correct) meta shape:
//   entity["_meta"][fieldName] = { ts, dev }
// We use CrdtMeta::setFieldMeta which is the canonical writer for this shape;
// matches how DataStore::stampField writes a per-field stamp directly into a
// full entity JSON.
static QJsonObject withFieldMeta(QJsonObject obj,
                                 const QString &field,
                                 const QString &ts,
                                 const QString &dev)
{
    CrdtMeta::setFieldMeta(obj, field, ts, dev);
    return obj;
}

// Common base: id + updated_at + is_deleted.
static QJsonObject baseEntity(const QString &id,
                              const QString &updatedAt,
                              bool isDeleted = false)
{
    QJsonObject obj;
    obj[QStringLiteral("id")]         = id;
    obj[QStringLiteral("updated_at")] = updatedAt;
    obj[QStringLiteral("is_deleted")] = isDeleted;
    return obj;
}

// ── Test class ───────────────────────────────────────────────────────────────

class TestMergerFieldLevelPerEntity : public QObject
{
    Q_OBJECT

private:
    // Reusable timestamps for "concurrent" cases. Equal physMs, equal counter,
    // distinct devIds → compareFieldMeta tie-breaks but both fields are
    // independent so each wins on its own meta entry.
    QString tsA() const { return makeHlc(2000, 0, kDevA); }
    QString tsB() const { return makeHlc(2000, 0, kDevB); }

    // For "newer wins" cases.
    QString tsOld() const { return makeHlc(1000, 0, kDevA); }
    QString tsNew() const { return makeHlc(5000, 0, kDevB); }

    QString baseUpdatedAt() const { return QStringLiteral("2026-04-01T00:00:00.000Z"); }

private slots:
    // ════════════════════════════════════════════════════════════════════════
    // PROJECT — fields: name, color, description, parent_id, order, ...
    // ════════════════════════════════════════════════════════════════════════

    void project_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("proj-cf-1");
        const QString coll  = QStringLiteral("projects");

        // Local: stamps "name" with dev A. Remote unchanged value for "color".
        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")]  = QStringLiteral("LocalName");
        local[QStringLiteral("color")] = QStringLiteral("grey");
        local = withFieldMeta(local, QStringLiteral("name"), tsA(), kDevA);

        // Remote: stamps "color" with dev B. Remote name unchanged value.
        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("name")]  = QStringLiteral("LocalName");
        remote[QStringLiteral("color")] = QStringLiteral("teal");
        remote = withFieldMeta(remote, QStringLiteral("color"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject p = findById(merged[coll].toArray(), id);

        QVERIFY(!p.isEmpty());
        // Local's "name" stamp wins on its field; remote's "color" stamp wins
        // on its field — independent CRDT registers.
        QCOMPARE(p[QStringLiteral("name")].toString(),  QStringLiteral("LocalName"));
        QCOMPARE(p[QStringLiteral("color")].toString(), QStringLiteral("teal"));
    }

    void project_sameField_newerWins()
    {
        const QString id    = QStringLiteral("proj-sf-1");
        const QString coll  = QStringLiteral("projects");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")] = QStringLiteral("OldName");
        local = withFieldMeta(local, QStringLiteral("name"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("name")] = QStringLiteral("NewName");
        remote = withFieldMeta(remote, QStringLiteral("name"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject p = findById(merged[coll].toArray(), id);

        QVERIFY(!p.isEmpty());
        QCOMPARE(p[QStringLiteral("name")].toString(), QStringLiteral("NewName"));
    }

    void project_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("proj-tb-1");
        const QString coll  = QStringLiteral("projects");

        // Local: alive, has a NEWER stamp on "name" (HLC-wise).
        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")] = QStringLiteral("StillAlive");
        local = withFieldMeta(local, QStringLiteral("name"), tsNew(), kDevA);

        // Remote: tombstone with OLDER updated_at — but tombstone absorbs.
        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject p = findById(merged[coll].toArray(), id);

        QVERIFY(!p.isEmpty());
        QVERIFY2(p[QStringLiteral("is_deleted")].toBool(),
                 "Project tombstone must win over a newer field edit.");
    }

    // ════════════════════════════════════════════════════════════════════════
    // SECTION — fields: name, project_id, order, is_collapsed, is_archived
    // ════════════════════════════════════════════════════════════════════════

    void section_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("sec-cf-1");
        const QString coll  = QStringLiteral("sections");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")]         = QStringLiteral("LocalSectionName");
        local[QStringLiteral("is_collapsed")] = false;
        local = withFieldMeta(local, QStringLiteral("name"), tsA(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("name")]         = QStringLiteral("LocalSectionName");
        remote[QStringLiteral("is_collapsed")] = true;
        remote = withFieldMeta(remote, QStringLiteral("is_collapsed"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject s = findById(merged[coll].toArray(), id);

        QVERIFY(!s.isEmpty());
        QCOMPARE(s[QStringLiteral("name")].toString(), QStringLiteral("LocalSectionName"));
        QCOMPARE(s[QStringLiteral("is_collapsed")].toBool(), true);
    }

    void section_sameField_newerWins()
    {
        const QString id    = QStringLiteral("sec-sf-1");
        const QString coll  = QStringLiteral("sections");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("order")] = 1;
        local = withFieldMeta(local, QStringLiteral("order"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("order")] = 7;
        remote = withFieldMeta(remote, QStringLiteral("order"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject s = findById(merged[coll].toArray(), id);

        QVERIFY(!s.isEmpty());
        QCOMPARE(s[QStringLiteral("order")].toInt(), 7);
    }

    void section_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("sec-tb-1");
        const QString coll  = QStringLiteral("sections");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")] = QStringLiteral("StillHere");
        local = withFieldMeta(local, QStringLiteral("name"), tsNew(), kDevA);

        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject s = findById(merged[coll].toArray(), id);

        QVERIFY(!s.isEmpty());
        QVERIFY2(s[QStringLiteral("is_deleted")].toBool(),
                 "Section tombstone must win over a newer field edit.");
    }

    // ════════════════════════════════════════════════════════════════════════
    // LABEL — fields: name, color, order, is_favorite
    // ════════════════════════════════════════════════════════════════════════

    void label_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("lbl-cf-1");
        const QString coll  = QStringLiteral("labels");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")]  = QStringLiteral("urgent");
        local[QStringLiteral("color")] = QStringLiteral("charcoal");
        local = withFieldMeta(local, QStringLiteral("name"), tsA(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("name")]  = QStringLiteral("urgent");
        remote[QStringLiteral("color")] = QStringLiteral("red");
        remote = withFieldMeta(remote, QStringLiteral("color"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject l = findById(merged[coll].toArray(), id);

        QVERIFY(!l.isEmpty());
        QCOMPARE(l[QStringLiteral("name")].toString(),  QStringLiteral("urgent"));
        QCOMPARE(l[QStringLiteral("color")].toString(), QStringLiteral("red"));
    }

    void label_sameField_newerWins()
    {
        const QString id    = QStringLiteral("lbl-sf-1");
        const QString coll  = QStringLiteral("labels");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("color")] = QStringLiteral("blue");
        local = withFieldMeta(local, QStringLiteral("color"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("color")] = QStringLiteral("magenta");
        remote = withFieldMeta(remote, QStringLiteral("color"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject l = findById(merged[coll].toArray(), id);

        QVERIFY(!l.isEmpty());
        QCOMPARE(l[QStringLiteral("color")].toString(), QStringLiteral("magenta"));
    }

    void label_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("lbl-tb-1");
        const QString coll  = QStringLiteral("labels");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")] = QStringLiteral("renamed");
        local = withFieldMeta(local, QStringLiteral("name"), tsNew(), kDevA);

        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject l = findById(merged[coll].toArray(), id);

        QVERIFY(!l.isEmpty());
        QVERIFY2(l[QStringLiteral("is_deleted")].toBool(),
                 "Label tombstone must win over a newer field edit.");
    }

    // ════════════════════════════════════════════════════════════════════════
    // FILTER — fields: name, query, color, order, is_favorite
    // ════════════════════════════════════════════════════════════════════════

    void filter_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("flt-cf-1");
        const QString coll  = QStringLiteral("filters");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("name")]  = QStringLiteral("MyFilter");
        local[QStringLiteral("query")] = QStringLiteral("today");
        local = withFieldMeta(local, QStringLiteral("name"), tsA(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("name")]  = QStringLiteral("MyFilter");
        remote[QStringLiteral("query")] = QStringLiteral("overdue | p1");
        remote = withFieldMeta(remote, QStringLiteral("query"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject f = findById(merged[coll].toArray(), id);

        QVERIFY(!f.isEmpty());
        QCOMPARE(f[QStringLiteral("name")].toString(),  QStringLiteral("MyFilter"));
        QCOMPARE(f[QStringLiteral("query")].toString(), QStringLiteral("overdue | p1"));
    }

    void filter_sameField_newerWins()
    {
        const QString id    = QStringLiteral("flt-sf-1");
        const QString coll  = QStringLiteral("filters");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("query")] = QStringLiteral("today");
        local = withFieldMeta(local, QStringLiteral("query"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("query")] = QStringLiteral("p1 | p2");
        remote = withFieldMeta(remote, QStringLiteral("query"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject f = findById(merged[coll].toArray(), id);

        QVERIFY(!f.isEmpty());
        QCOMPARE(f[QStringLiteral("query")].toString(), QStringLiteral("p1 | p2"));
    }

    void filter_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("flt-tb-1");
        const QString coll  = QStringLiteral("filters");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("query")] = QStringLiteral("today");
        local = withFieldMeta(local, QStringLiteral("query"), tsNew(), kDevA);

        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject f = findById(merged[coll].toArray(), id);

        QVERIFY(!f.isEmpty());
        QVERIFY2(f[QStringLiteral("is_deleted")].toBool(),
                 "Filter tombstone must win over a newer field edit.");
    }

    // ════════════════════════════════════════════════════════════════════════
    // REMINDER — fields: type, recurrence_rule, minute_offset, due, location
    // ════════════════════════════════════════════════════════════════════════

    void reminder_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("rmd-cf-1");
        const QString coll  = QStringLiteral("reminders");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("type")]            = QStringLiteral("absolute");
        local[QStringLiteral("recurrence_rule")] = QStringLiteral("FREQ=DAILY");
        local = withFieldMeta(local, QStringLiteral("recurrence_rule"), tsA(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("type")]            = QStringLiteral("relative");
        remote[QStringLiteral("recurrence_rule")] = QStringLiteral("FREQ=DAILY");
        remote = withFieldMeta(remote, QStringLiteral("type"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject r = findById(merged[coll].toArray(), id);

        QVERIFY(!r.isEmpty());
        // type was stamped only by remote → remote value wins.
        QCOMPARE(r[QStringLiteral("type")].toString(), QStringLiteral("relative"));
        // recurrence_rule was stamped only by local → local value wins.
        QCOMPARE(r[QStringLiteral("recurrence_rule")].toString(),
                 QStringLiteral("FREQ=DAILY"));
    }

    void reminder_sameField_newerWins()
    {
        const QString id    = QStringLiteral("rmd-sf-1");
        const QString coll  = QStringLiteral("reminders");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("minute_offset")] = 5;
        local = withFieldMeta(local, QStringLiteral("minute_offset"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("minute_offset")] = 30;
        remote = withFieldMeta(remote, QStringLiteral("minute_offset"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject r = findById(merged[coll].toArray(), id);

        QVERIFY(!r.isEmpty());
        QCOMPARE(r[QStringLiteral("minute_offset")].toInt(), 30);
    }

    void reminder_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("rmd-tb-1");
        const QString coll  = QStringLiteral("reminders");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("recurrence_rule")] = QStringLiteral("FREQ=WEEKLY");
        local = withFieldMeta(local, QStringLiteral("recurrence_rule"), tsNew(), kDevA);

        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject r = findById(merged[coll].toArray(), id);

        QVERIFY(!r.isEmpty());
        QVERIFY2(r[QStringLiteral("is_deleted")].toBool(),
                 "Reminder tombstone must win over a newer field edit.");
    }

    // ════════════════════════════════════════════════════════════════════════
    // COMMENT — fields: content, attachment, task_id, project_id
    // ════════════════════════════════════════════════════════════════════════

    void comment_concurrentDifferentFields_bothWin()
    {
        const QString id    = QStringLiteral("cmt-cf-1");
        const QString coll  = QStringLiteral("comments");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("content")]    = QStringLiteral("LocalContent");
        local[QStringLiteral("project_id")] = QStringLiteral("p-original");
        local = withFieldMeta(local, QStringLiteral("content"), tsA(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("content")]    = QStringLiteral("LocalContent");
        remote[QStringLiteral("project_id")] = QStringLiteral("p-moved");
        remote = withFieldMeta(remote, QStringLiteral("project_id"), tsB(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject c = findById(merged[coll].toArray(), id);

        QVERIFY(!c.isEmpty());
        // content stamped only locally → local wins for content
        QCOMPARE(c[QStringLiteral("content")].toString(), QStringLiteral("LocalContent"));
        // project_id stamped only remotely → remote wins for project_id
        QCOMPARE(c[QStringLiteral("project_id")].toString(), QStringLiteral("p-moved"));
    }

    void comment_sameField_newerWins()
    {
        const QString id    = QStringLiteral("cmt-sf-1");
        const QString coll  = QStringLiteral("comments");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("content")] = QStringLiteral("Initial draft");
        local = withFieldMeta(local, QStringLiteral("content"), tsOld(), kDevA);

        QJsonObject remote = baseEntity(id, baseUpdatedAt());
        remote[QStringLiteral("content")] = QStringLiteral("Edited later");
        remote = withFieldMeta(remote, QStringLiteral("content"), tsNew(), kDevB);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject c = findById(merged[coll].toArray(), id);

        QVERIFY(!c.isEmpty());
        QCOMPARE(c[QStringLiteral("content")].toString(), QStringLiteral("Edited later"));
    }

    void comment_tombstoneWinsOverFieldEdit()
    {
        const QString id    = QStringLiteral("cmt-tb-1");
        const QString coll  = QStringLiteral("comments");

        QJsonObject local = baseEntity(id, baseUpdatedAt());
        local[QStringLiteral("content")] = QStringLiteral("Hot take");
        local = withFieldMeta(local, QStringLiteral("content"), tsNew(), kDevA);

        QJsonObject remote = baseEntity(id,
            QStringLiteral("2025-01-01T00:00:00.000Z"), /*deleted=*/true);

        Merger m;
        const QJsonObject merged = m.mergeStates(stateWith(coll, local),
                                                  stateWith(coll, remote));
        const QJsonObject c = findById(merged[coll].toArray(), id);

        QVERIFY(!c.isEmpty());
        QVERIFY2(c[QStringLiteral("is_deleted")].toBool(),
                 "Comment tombstone must win over a newer field edit.");
    }
};

QTEST_GUILESS_MAIN(TestMergerFieldLevelPerEntity)
#include "test_merger_field_level_per_entity.moc"
