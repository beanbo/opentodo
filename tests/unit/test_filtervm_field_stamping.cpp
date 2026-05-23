// =============================================================================
// Iter-night-12 pin: FilterViewModel::updateFilter must stamp the per-field
// CRDT _meta blob for every editable field. Without that stamping, the
// Filter's _meta stays pinned to its addFilter HLC, and two devices editing
// the same filter's name/query/color/order/is_favorite end up with identical
// field meta on both sides. Merger::compareFieldMeta returns 0, the
// "remoteWins" decision flips false, and each device silently keeps its
// own local value — saved filters never converge after sync.
//
// addFilter has always stamped initial fields and applyCommit filter_update
// has always applied inbound _meta — only the OUTBOUND local-edit path was
// missing stamps. This file pins the iter-night-12 fix.
//
// Companion to test_taskvm_field_keys_pin.cpp (the equivalent guard for
// Task field stamping).
// =============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>

#include "storage/datastore.h"
#include "model/filter.h"
#include "viewmodels/filterviewmodel.h"

namespace {

// All keys Filter::toJson() emits at the top level (sourced by reading
// src/model/filter.cpp:toJson). Keep in sync with that file — if a key is
// added/removed there, mirror it here.
static QSet<QString> validFilterJsonKeys()
{
    return {
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("query"),
        QStringLiteral("color"),
        QStringLiteral("order"),
        QStringLiteral("is_favorite"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
        QStringLiteral("is_deleted"),
    };
}

static QJsonObject metaForFilter(const QJsonObject &state, const QString &filterId)
{
    const QJsonArray filters = state.value(QStringLiteral("filters")).toArray();
    for (const auto &v : filters) {
        const QJsonObject f = v.toObject();
        if (f.value(QStringLiteral("id")).toString() == filterId)
            return f.value(QStringLiteral("_meta")).toObject();
    }
    return {};
}

static void assertMetaKeysValid(const QJsonObject &meta, const char *site)
{
    const QSet<QString> valid = validFilterJsonKeys();
    for (auto it = meta.begin(); it != meta.end(); ++it) {
        QVERIFY2(valid.contains(it.key()),
                 qPrintable(QStringLiteral("[%1] _meta key '%2' is not a "
                                            "valid Filter JSON key — Merger "
                                            "will never consult it")
                                .arg(QLatin1String(site), it.key())));
    }
}

// Read the HLC timestamp string for a given field from a Filter's _meta blob.
// Returns an empty string if the field is missing — that signals the
// stamping never happened and the iter-night-12 bug is reachable.
static QString hlcFor(const QJsonObject &meta, const QString &field)
{
    const QJsonObject entry = meta.value(field).toObject();
    return entry.value(QStringLiteral("ts")).toString();
}

} // anonymous namespace


class TestFilterVmFieldStamping : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    FilterViewModel *vm = nullptr;
    QString filterId;

private slots:
    void init()
    {
        store = new DataStore;
        vm = new FilterViewModel(store);
        filterId = vm->addFilter(QStringLiteral("Initial"),
                                 QStringLiteral("today"),
                                 QStringLiteral("charcoal"));
        QVERIFY(!filterId.isEmpty());
    }

    void cleanup()
    {
        delete vm; vm = nullptr;
        delete store; store = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) updateFilter("name", …) stamps the `name` field with a fresh HLC.
    //    Was the bug — `name` stayed pinned to the addFilter timestamp, so
    //    a concurrent rename on a peer with the same baseline timestamp
    //    never won the merge.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_name_stampsNameKey()
    {
        const QString hlcBefore = hlcFor(
            metaForFilter(store->toJson(), filterId),
            QStringLiteral("name"));
        QVERIFY(!hlcBefore.isEmpty()); // addFilter stamped the initial value

        // Sleep ≥1ms so the HLC monotonic counter or wall-clock ticks. The
        // HLC composite includes physical ms; we want a strictly greater
        // post-edit stamp regardless of tiebreak path.
        QThread::msleep(2);

        QVERIFY(vm->updateFilter(filterId, QStringLiteral("name"),
                                 QStringLiteral("Renamed")));

        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QVERIFY2(meta.contains(QStringLiteral("name")),
                 "updateFilter(name) must stamp the 'name' JSON key");
        assertMetaKeysValid(meta, "updateFilter(name)");

        const QString hlcAfter = hlcFor(meta, QStringLiteral("name"));
        QVERIFY2(hlcAfter > hlcBefore,
                 qPrintable(QStringLiteral(
                     "Iter-night-12: updateFilter(name) must ADVANCE the "
                     "name field HLC (was %1, after %2). Without this the "
                     "Merger sees identical field meta on both peers and "
                     "neither rename wins — both devices silently keep "
                     "their own local value.")
                     .arg(hlcBefore, hlcAfter)));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) updateFilter("query", …) stamps the `query` field.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_query_stampsQueryKey()
    {
        const QString before = hlcFor(metaForFilter(store->toJson(), filterId),
                                      QStringLiteral("query"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("query"),
                                 QStringLiteral("p1 & @work")));
        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QVERIFY(meta.contains(QStringLiteral("query")));
        assertMetaKeysValid(meta, "updateFilter(query)");
        const QString after = hlcFor(meta, QStringLiteral("query"));
        QVERIFY(after > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) updateFilter("color", …) stamps the `color` field.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_color_stampsColorKey()
    {
        const QString before = hlcFor(metaForFilter(store->toJson(), filterId),
                                      QStringLiteral("color"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("color"),
                                 QStringLiteral("red")));
        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QVERIFY(meta.contains(QStringLiteral("color")));
        assertMetaKeysValid(meta, "updateFilter(color)");
        QVERIFY(hlcFor(meta, QStringLiteral("color")) > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) updateFilter("order", …) stamps the `order` field — used by drag-
    //    reorder in the sidebar, which goes through reorderFilter → updateFilter.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_order_stampsOrderKey()
    {
        const QString before = hlcFor(metaForFilter(store->toJson(), filterId),
                                      QStringLiteral("order"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("order"), 42));
        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QVERIFY(meta.contains(QStringLiteral("order")));
        assertMetaKeysValid(meta, "updateFilter(order)");
        QVERIFY(hlcFor(meta, QStringLiteral("order")) > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) updateFilter("isFavorite", …) translates to the JSON snake_case key
    //    `is_favorite` (mirrors the project/label is_favorite handling).
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_isFavorite_stampsIsFavoriteKey()
    {
        const QString before = hlcFor(metaForFilter(store->toJson(), filterId),
                                      QStringLiteral("is_favorite"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("isFavorite"), true));
        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QVERIFY2(meta.contains(QStringLiteral("is_favorite")),
                 "updateFilter(isFavorite) must stamp the snake_case JSON "
                 "key 'is_favorite' — not 'isFavorite'");
        QVERIFY2(!meta.contains(QStringLiteral("isFavorite")),
                 "'isFavorite' is the QML property name, not a JSON top-"
                 "level key. Stamping under that name would be invisible "
                 "to Merger.");
        assertMetaKeysValid(meta, "updateFilter(isFavorite)");
        QVERIFY(hlcFor(meta, QStringLiteral("is_favorite")) > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6) Every updateFilter call also bumps `updated_at` (mirrors markLabel/
    //    markProject/markTask conventions). Saves the Merger from re-comparing
    //    a stale-looking updated_at and dropping back to whole-entity LWW.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_bumpsUpdatedAt()
    {
        const QString before = hlcFor(metaForFilter(store->toJson(), filterId),
                                      QStringLiteral("updated_at"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("color"),
                                 QStringLiteral("blue")));
        const QString after = hlcFor(metaForFilter(store->toJson(), filterId),
                                     QStringLiteral("updated_at"));
        QVERIFY(after > before);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 7) Unrelated fields' HLCs do NOT advance when a single field is edited.
    //    The point of per-field stamping is that two devices editing
    //    different fields BOTH win their respective edits — that only holds
    //    if a name-edit doesn't accidentally stamp `color`.
    // ─────────────────────────────────────────────────────────────────────────
    void updateFilter_doesNotStampUnrelatedFields()
    {
        const QString colorBefore = hlcFor(metaForFilter(store->toJson(), filterId),
                                           QStringLiteral("color"));
        const QString queryBefore = hlcFor(metaForFilter(store->toJson(), filterId),
                                           QStringLiteral("query"));
        QThread::msleep(2);
        QVERIFY(vm->updateFilter(filterId, QStringLiteral("name"),
                                 QStringLiteral("OnlyName")));
        const QJsonObject meta = metaForFilter(store->toJson(), filterId);
        QCOMPARE(hlcFor(meta, QStringLiteral("color")), colorBefore);
        QCOMPARE(hlcFor(meta, QStringLiteral("query")), queryBefore);
    }
};

QTEST_GUILESS_MAIN(TestFilterVmFieldStamping)
#include "test_filtervm_field_stamping.moc"
