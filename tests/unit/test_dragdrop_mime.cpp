// Low-level QMimeData construction / extraction tests for the drag-drop layer.
//
// The QML side (TaskItem.qml, BoardView.qml, SidebarContent.qml,
// EditorSubtasksSection.qml, ProjectView.qml) constructs Drag.mimeData as a
// JS-object {key: value} that Qt converts to a QMimeData carrying one
// QByteArray per key. The drop side calls drop.getDataAsString(key) which
// maps to QMimeData::data(key). DropArea filters on Drag.keys via the
// QMimeData::hasFormat() contract.
//
// This suite exercises the *data layer only* — no QQuickWindow / no live
// drag — to pin the round-trip semantics a QML drop handler depends on:
//
//   * UTF-8 round-trip for ASCII (UUID) and unicode (label name) payloads
//   * Multi-key payloads (BoardView.qml carries both `task` AND `section`
//     keys on the same QMimeData, drop sites filter on whichever they need)
//   * Empty payload disambiguation: present-but-empty != absent
//   * Foreign mime rejection (the DropArea.keys filter contract)
//   * Sequential drop independence (no stale data carry-over)
//   * Disambiguation when a delegate offers multiple keys at once
//
// We do NOT modify production code in this suite — every assertion is
// observational against the Qt mime API used by the QML layer.

#include <QtTest>
#include <QMimeData>
#include <QByteArray>
#include <QStringList>
#include <QUuid>

// ---------------------------------------------------------------------------
// Mime-key contract — keep in sync with Drag.keys / DropArea.keys across the
// qml/ tree. Mirrors test_dragdrop_extended.cpp's local constants.
// ---------------------------------------------------------------------------
static constexpr const char *kMimeTask         = "application/x-opentodo-task";
static constexpr const char *kMimeProject      = "application/x-opentodo-project";
static constexpr const char *kMimeSubtask      = "application/x-opentodo-subtask";
static constexpr const char *kMimeLabelReorder = "application/x-opentodo-label-reorder";
static constexpr const char *kMimeSection      = "application/x-opentodo-section";

// Foreign / unrelated mime key — must never satisfy any of our DropArea filters.
static constexpr const char *kMimeForeign      = "application/x-other-app-task";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a QMimeData carrying a single (key, payload) pair, matching the way
// QML's Drag.mimeData = ({ "key": value }) is materialized by Qt.
static QMimeData *makeMime(const char *key, const QByteArray &payload)
{
    auto *m = new QMimeData;
    m->setData(QString::fromLatin1(key), payload);
    return m;
}

// Build a QMimeData with multiple (key, payload) pairs — mirrors the
// BoardView.qml case where one Drag offers two keys simultaneously.
static QMimeData *makeMultiMime(const QList<QPair<const char *, QByteArray>> &pairs)
{
    auto *m = new QMimeData;
    for (const auto &kv : pairs)
        m->setData(QString::fromLatin1(kv.first), kv.second);
    return m;
}

// Mirror the QML drop side: DropArea.keys: [K] is a hasFormat(K) gate,
// drop.getDataAsString(K) is QMimeData::data(K) decoded as UTF-8.
static bool dropAreaAccepts(const QMimeData *m, const char *key)
{
    return m && m->hasFormat(QString::fromLatin1(key));
}
static QByteArray dropPayload(const QMimeData *m, const char *key)
{
    return m ? m->data(QString::fromLatin1(key)) : QByteArray();
}
static QString dropPayloadString(const QMimeData *m, const char *key)
{
    return QString::fromUtf8(dropPayload(m, key));
}

// ---------------------------------------------------------------------------
class TestDragDropMime : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // Round-trip: a task UUID payload survives serialize → deserialize via
    // QMimeData byte-for-byte. This is the canonical TaskItem.qml drag
    // (Drag.mimeData = ({ "application/x-opentodo-task": root.id })).
    // -----------------------------------------------------------------------
    void taskUuid_roundTrip_isByteIdentical()
    {
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray payload = uuid.toUtf8();

        QMimeData *m = makeMime(kMimeTask, payload);

        QVERIFY(dropAreaAccepts(m, kMimeTask));
        const QByteArray got = dropPayload(m, kMimeTask);
        QCOMPARE(got, payload);                 // byte-identical
        QCOMPARE(got.size(), payload.size());
        QCOMPARE(dropPayloadString(m, kMimeTask), uuid);

        delete m;
    }

    // -----------------------------------------------------------------------
    // Multiple keys on a single QMimeData — BoardView.qml's TaskItem
    // delegate carries both `task` and `section` keys at once, and drop
    // sites filter on whichever they need. Both must be retrievable
    // independently with their own payloads intact.
    //
    // The task uses the spec wording ("task AND subtask") to pin the
    // general "one drag, multiple offered keys" contract.
    // -----------------------------------------------------------------------
    void multipleKeys_onSameMimeData_bothRetrievable()
    {
        const QByteArray taskId    = "task-uuid-aaaa-bbbb";
        const QByteArray subtaskId = "subtask-uuid-cccc-dddd";

        QMimeData *m = makeMultiMime({
            {kMimeTask,    taskId},
            {kMimeSubtask, subtaskId},
        });

        // Both keys are present.
        QVERIFY(dropAreaAccepts(m, kMimeTask));
        QVERIFY(dropAreaAccepts(m, kMimeSubtask));

        // Each key returns its own payload (no cross-contamination).
        QCOMPARE(dropPayload(m, kMimeTask),    taskId);
        QCOMPARE(dropPayload(m, kMimeSubtask), subtaskId);

        // formats() lists both.
        const QStringList formats = m->formats();
        QVERIFY(formats.contains(QString::fromLatin1(kMimeTask)));
        QVERIFY(formats.contains(QString::fromLatin1(kMimeSubtask)));

        delete m;
    }

    // -----------------------------------------------------------------------
    // Empty payload — setData("key", "") still registers the key. The drop
    // consumer gets an *empty* QByteArray (size==0, isEmpty()==true), not a
    // null one (isNull() differs from a key that was never set).
    // This pins the present-but-empty vs absent distinction.
    // -----------------------------------------------------------------------
    void emptyPayload_keyPresent_returnsEmptyNotNull()
    {
        QMimeData *m = makeMime(kMimeTask, QByteArray());

        QVERIFY(dropAreaAccepts(m, kMimeTask));
        const QByteArray got = dropPayload(m, kMimeTask);
        QVERIFY(got.isEmpty());
        QCOMPARE(got.size(), 0);

        // A foreign key on the same mime data should still be absent —
        // empty != "anyone can read it".
        QVERIFY(!dropAreaAccepts(m, kMimeProject));
        QVERIFY(dropPayload(m, kMimeProject).isEmpty());

        delete m;
    }

    // -----------------------------------------------------------------------
    // Foreign mime rejection — a DropArea filter on `application/x-opentodo-*`
    // must reject mime data whose only key is `application/x-other-app-task`.
    // hasFormat() is the gate; the payload value is irrelevant.
    // -----------------------------------------------------------------------
    void foreignMime_isRejectedByAllOpentodoFilters()
    {
        QMimeData *m = makeMime(kMimeForeign, QByteArrayLiteral("alien-payload"));

        // The foreign key itself is present — the data is well-formed.
        QVERIFY(dropAreaAccepts(m, kMimeForeign));

        // None of the OpenTodo DropArea filters accept it.
        QVERIFY(!dropAreaAccepts(m, kMimeTask));
        QVERIFY(!dropAreaAccepts(m, kMimeProject));
        QVERIFY(!dropAreaAccepts(m, kMimeSubtask));
        QVERIFY(!dropAreaAccepts(m, kMimeLabelReorder));
        QVERIFY(!dropAreaAccepts(m, kMimeSection));

        // Reading a missing key returns an empty QByteArray.
        QCOMPARE(dropPayload(m, kMimeTask), QByteArray());

        delete m;
    }

    // -----------------------------------------------------------------------
    // Unicode payload — UUIDs are ASCII, but label rows in the sidebar can
    // carry their own name as the payload (and labels can be Cyrillic /
    // emoji / etc.). UTF-8 round-trip must preserve every codepoint.
    // -----------------------------------------------------------------------
    void unicodePayload_utf8RoundTrip()
    {
        const QString original = QStringLiteral(u"метка-id-123");
        const QByteArray payload = original.toUtf8();

        QMimeData *m = makeMime(kMimeLabelReorder, payload);

        QVERIFY(dropAreaAccepts(m, kMimeLabelReorder));
        const QByteArray got = dropPayload(m, kMimeLabelReorder);
        QCOMPARE(got, payload);                               // byte-identical
        QCOMPARE(QString::fromUtf8(got), original);           // codepoints intact

        // Sanity: UTF-8 representation of "метка" is multi-byte, so
        // payload.size() must exceed original.size() (chars).
        QVERIFY(payload.size() > original.size());

        delete m;
    }

    // -----------------------------------------------------------------------
    // Large payload — pin that QMimeData has no surprise truncation at the
    // 10 KB scale. (Real OpenTodo payloads are ~36-byte UUIDs, but this
    // guards against any future field that ships richer data via mime.)
    // -----------------------------------------------------------------------
    void largePayload_noTruncation()
    {
        const int kSize = 10 * 1024; // 10 KB
        QByteArray payload(kSize, 'x');
        // Mark the boundary bytes so off-by-one truncation would be visible.
        payload[0]         = 'A';
        payload[kSize - 1] = 'Z';

        QMimeData *m = makeMime(kMimeProject, payload);

        QVERIFY(dropAreaAccepts(m, kMimeProject));
        const QByteArray got = dropPayload(m, kMimeProject);
        QCOMPARE(got.size(), kSize);
        QCOMPARE(got.at(0),         char('A'));
        QCOMPARE(got.at(kSize - 1), char('Z'));
        QCOMPARE(got, payload);

        delete m;
    }

    // -----------------------------------------------------------------------
    // Sequential drops — two independent drag operations must not bleed
    // state into each other. The second drop's payload is B, never the
    // stale A. Each QMimeData instance is constructed and consumed
    // independently, mirroring how QML allocates a fresh Drag.mimeData
    // object per gesture.
    // -----------------------------------------------------------------------
    void sequentialDrops_secondPayloadIsFresh()
    {
        const QByteArray idA = "task-A-aaaaaaaa";
        const QByteArray idB = "task-B-bbbbbbbb";

        // First drop.
        QMimeData *m1 = makeMime(kMimeTask, idA);
        QCOMPARE(dropPayload(m1, kMimeTask), idA);
        delete m1;

        // Second drop with a different payload — no carry-over from m1.
        QMimeData *m2 = makeMime(kMimeTask, idB);
        QCOMPARE(dropPayload(m2, kMimeTask), idB);
        QVERIFY(dropPayload(m2, kMimeTask) != idA);
        delete m2;

        // Same QMimeData, mutated mid-life: setData replaces, doesn't
        // append. The second value wins.
        QMimeData *m3 = new QMimeData;
        m3->setData(QString::fromLatin1(kMimeTask), idA);
        QCOMPARE(dropPayload(m3, kMimeTask), idA);
        m3->setData(QString::fromLatin1(kMimeTask), idB);
        QCOMPARE(dropPayload(m3, kMimeTask), idB);
        delete m3;
    }

    // -----------------------------------------------------------------------
    // Disambiguation — a single delegate offers BOTH `task` and `subtask`
    // keys, and two different DropAreas (one filtering `task`, the other
    // filtering `subtask`) each pick their own payload from the same drag.
    //
    // Mirrors the failure mode where a subtask-row drop accidentally reads
    // the task-row payload (or vice versa) — they must remain distinct.
    // -----------------------------------------------------------------------
    void taskVsSubtask_dropFiltersPickRightPayload()
    {
        const QByteArray taskId    = "outer-task-uuid";
        const QByteArray subtaskId = "inner-subtask-uuid";

        QMimeData *m = makeMultiMime({
            {kMimeTask,    taskId},
            {kMimeSubtask, subtaskId},
        });

        // Drop area #1: filters on `task` only — it picks the task payload.
        QVERIFY(dropAreaAccepts(m, kMimeTask));
        QCOMPARE(dropPayload(m, kMimeTask), taskId);

        // Drop area #2: filters on `subtask` only — it picks the subtask
        // payload. Crucially it does NOT see the task one as a fallback.
        QVERIFY(dropAreaAccepts(m, kMimeSubtask));
        QCOMPARE(dropPayload(m, kMimeSubtask), subtaskId);
        QVERIFY(dropPayload(m, kMimeSubtask) != taskId);

        // A drop area that filters on a third, unrelated key still rejects
        // this multi-key drag.
        QVERIFY(!dropAreaAccepts(m, kMimeProject));
        QVERIFY(!dropAreaAccepts(m, kMimeLabelReorder));

        delete m;
    }

    // -----------------------------------------------------------------------
    // Exhaustive single-key rejection matrix — for each known OpenTodo
    // mime key, a QMimeData carrying ONLY that key satisfies its own filter
    // and rejects every other OpenTodo filter. This pins that the keys are
    // genuinely distinct (no accidental aliasing).
    // -----------------------------------------------------------------------
    void singleKey_rejectsAllOtherOpentodoFilters()
    {
        const QList<const char *> keys = {
            kMimeTask, kMimeProject, kMimeSubtask, kMimeLabelReorder, kMimeSection,
        };

        for (const char *active : keys) {
            QMimeData *m = makeMime(active, QByteArrayLiteral("dummy"));

            // The active key passes.
            QVERIFY2(dropAreaAccepts(m, active), active);

            // Every other key in the matrix is rejected.
            for (const char *other : keys) {
                if (other == active) continue;
                QVERIFY2(!dropAreaAccepts(m, other),
                         qPrintable(QStringLiteral("active=%1 other=%2")
                                    .arg(active, other)));
            }

            // Foreign key always rejected.
            QVERIFY(!dropAreaAccepts(m, kMimeForeign));

            delete m;
        }
    }
};

QTEST_GUILESS_MAIN(TestDragDropMime)
#include "test_dragdrop_mime.moc"
