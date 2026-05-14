#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QScopedPointer>

#include "model/label.h"
#include "storage/datastore.h"
#include "utils/uuid.h"

// Extended Label coverage: defaults, JSON roundtrip on every property,
// color pass-through (palette names + hex + edge cases),
// DataStore CRUD ops (add/remove/duplicate-name behaviour, lookup, soft delete).
//
// Note (verified against src/model/label.{h,cpp}, 2026-05-09):
// The C++ Label class stores `color` as an opaque QString and performs NO
// name->hex resolution at the model layer. The palette-name -> hex mapping
// lives in QML (qml/themes/ThemeManager.qml). The C++ default is the
// literal string "charcoal" (a palette name, not hex). So these tests
// verify that the model is a faithful pass-through container; mapping
// fidelity is a UI concern tested elsewhere.
class TestLabelExtended : public QObject
{
    Q_OBJECT

private slots:
    // ---------- Label entity defaults ----------

    void label_default_ctor_invariants()
    {
        Label l;
        QVERIFY(!l.id().isEmpty());
        QVERIFY(UuidHelper::isValid(l.id()));
        QVERIFY(l.name().isEmpty());
        // Verified default: literal "charcoal" (palette name, not hex).
        QCOMPARE(l.color(), QStringLiteral("charcoal"));
        QCOMPARE(l.order(), 0);
        QVERIFY(!l.isFavorite());
        QVERIFY(!l.isDeleted());
    }

    void label_two_default_ctors_have_distinct_ids()
    {
        Label a;
        Label b;
        QVERIFY(a.id() != b.id());
    }

    // ---------- Property setters round-trip through JSON ----------

    void label_full_json_roundtrip_preserves_every_field()
    {
        Label orig;
        orig.setName(QStringLiteral("urgent"));
        orig.setColor(QStringLiteral("#7ecc49"));
        orig.setOrder(7);
        orig.setIsFavorite(true);
        orig.setIsDeleted(false);

        const QString origId = orig.id();

        QJsonObject json = orig.toJson();

        // Verify JSON shape uses snake_case (matches Todoist API on disk).
        QVERIFY(json.contains(QStringLiteral("id")));
        QVERIFY(json.contains(QStringLiteral("name")));
        QVERIFY(json.contains(QStringLiteral("color")));
        QVERIFY(json.contains(QStringLiteral("order")));
        QVERIFY(json.contains(QStringLiteral("is_favorite")));
        QVERIFY(json.contains(QStringLiteral("is_deleted")));

        QScopedPointer<Label> r(Label::fromJson(json));
        QCOMPARE(r->id(), origId);
        QCOMPARE(r->name(), QStringLiteral("urgent"));
        QCOMPARE(r->color(), QStringLiteral("#7ecc49"));
        QCOMPARE(r->order(), 7);
        QVERIFY(r->isFavorite());
        QVERIFY(!r->isDeleted());
    }

    void label_setName_roundtrips()
    {
        Label l;
        l.setName(QStringLiteral("X"));
        QCOMPARE(l.name(), QStringLiteral("X"));
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->name(), QStringLiteral("X"));
    }

    void label_setColor_palette_name_roundtrips()
    {
        Label l;
        l.setColor(QStringLiteral("kale"));
        QCOMPARE(l.color(), QStringLiteral("kale"));
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->color(), QStringLiteral("kale"));
    }

    void label_setIsFavorite_true_roundtrips()
    {
        Label l;
        l.setIsFavorite(true);
        QVERIFY(l.isFavorite());
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QVERIFY(r->isFavorite());
    }

    void label_setOrder_roundtrips()
    {
        Label l;
        l.setOrder(42);
        QCOMPARE(l.order(), 42);
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->order(), 42);
    }

    void label_setOrder_negative_roundtrips()
    {
        // No clamping in the model; negative is accepted as-is.
        Label l;
        l.setOrder(-1);
        QCOMPARE(l.order(), -1);
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->order(), -1);
    }

    // ---------- Color pass-through edge cases ----------

    void color_palette_names_data()
    {
        QTest::addColumn<QString>("colorName");
        QTest::newRow("kale")          << "kale";
        QTest::newRow("blueberry")     << "blueberry";
        QTest::newRow("lavender")      << "lavender";
        QTest::newRow("raspberry")     << "raspberry";
        QTest::newRow("graphite")      << "graphite";
        QTest::newRow("sunflower")     << "sunflower";
        QTest::newRow("todoist_red")   << "todoist_red";
        QTest::newRow("todoist_blue")  << "todoist_blue";
        QTest::newRow("charcoal")      << "charcoal";
    }

    void color_palette_names()
    {
        // The model stores the palette name verbatim (mapping to hex is a
        // QML/ThemeManager concern, not part of the C++ data layer).
        QFETCH(QString, colorName);
        Label l;
        l.setColor(colorName);
        QCOMPARE(l.color(), colorName);
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->color(), colorName);
    }

    void color_custom_hex_passes_through()
    {
        Label l;
        l.setColor(QStringLiteral("#db4c3f"));
        QCOMPARE(l.color(), QStringLiteral("#db4c3f"));
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->color(), QStringLiteral("#db4c3f"));
    }

    void color_uppercase_hex_passes_through()
    {
        // No normalisation: upper- and lower-case hex are both preserved
        // exactly as written.
        Label l;
        l.setColor(QStringLiteral("#FFAA00"));
        QCOMPARE(l.color(), QStringLiteral("#FFAA00"));
    }

    void color_invalid_name_is_stored_verbatim()
    {
        // The C++ model performs no validation. An "invalid" name is stored
        // verbatim; the QML layer falls back if the palette doesn't know it.
        Label l;
        l.setColor(QStringLiteral("not_a_real_color"));
        QCOMPARE(l.color(), QStringLiteral("not_a_real_color"));
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->color(), QStringLiteral("not_a_real_color"));
    }

    void color_empty_string_overrides_default()
    {
        // The setter only fires colorChanged on real changes; setting "" on
        // a fresh label DOES change from "charcoal" -> "" and is preserved.
        Label l;
        QCOMPARE(l.color(), QStringLiteral("charcoal")); // sanity
        l.setColor(QString());
        QVERIFY(l.color().isEmpty());

        // JSON roundtrip: fromJson uses toString("charcoal") as fallback
        // ONLY when the key is absent. Explicit empty string survives as "".
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QVERIFY(r->color().isEmpty());
    }

    void color_unicode_name_passes_through()
    {
        // Cyrillic palette name. The model is opaque; it survives JSON.
        // (UI fallback to default-grey happens in QML, not here.)
        const QString red = QString::fromUtf8("\xD0\xBA\xD1\x80\xD0\xB0\xD1\x81\xD0\xBD\xD1\x8B\xD0\xB9");
        Label l;
        l.setColor(red);
        QCOMPARE(l.color(), red);
        QScopedPointer<Label> r(Label::fromJson(l.toJson()));
        QCOMPARE(r->color(), red);
    }

    // ---------- Setter idempotence ----------

    void setColor_same_value_does_not_emit_signal()
    {
        Label l;
        l.setColor(QStringLiteral("kale"));
        QSignalSpy spy(&l, &Label::colorChanged);
        l.setColor(QStringLiteral("kale")); // same value
        QCOMPARE(spy.count(), 0);
    }

    void setColor_different_value_emits_signal_once()
    {
        Label l;
        QSignalSpy spy(&l, &Label::colorChanged);
        l.setColor(QStringLiteral("kale"));
        QCOMPARE(spy.count(), 1);
    }

    // ---------- DataStore label ops ----------

    void datastore_addLabel_appears_in_allLabels()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("urgent"));
        QVERIFY(l != nullptr);

        const auto labels = store.allLabels();
        bool found = false;
        for (auto *x : labels) {
            if (x->id() == l->id()) { found = true; break; }
        }
        QVERIFY(found);
        QCOMPARE(store.labelByName(QStringLiteral("urgent")), l);
    }

    void datastore_addLabel_assigns_default_color()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("plain"));
        // DataStore does NOT override; we get the Label ctor default.
        QCOMPARE(l->color(), QStringLiteral("charcoal"));
    }

    void datastore_addLabel_assigns_sequential_order()
    {
        DataStore store;
        auto *a = store.addLabel(QStringLiteral("a"));
        auto *b = store.addLabel(QStringLiteral("b"));
        auto *c = store.addLabel(QStringLiteral("c"));
        QCOMPARE(a->order(), 0);
        QCOMPARE(b->order(), 1);
        QCOMPARE(c->order(), 2);
    }

    void datastore_set_color_via_label_pointer_updates_color()
    {
        // Mutation API on labels is direct (Label* setters), unlike sections
        // which have DataStore::renameSection. This test documents that.
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("work"));
        l->setColor(QStringLiteral("kale"));
        QCOMPARE(l->color(), QStringLiteral("kale"));
        QCOMPARE(store.labelByName(QStringLiteral("work"))->color(),
                 QStringLiteral("kale"));
    }

    void datastore_set_name_via_label_pointer_updates_property()
    {
        // No DataStore::setLabelName guard exists; the underlying Label
        // setter accepts any QString (including empty) and reports the
        // change via signal. The DataStore name index is NOT updated when
        // name is mutated directly (only addLabel inserts into the index).
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("oldname"));
        l->setName(QStringLiteral("newname"));
        QCOMPARE(l->name(), QStringLiteral("newname"));
    }

    void datastore_set_name_to_empty_is_accepted()
    {
        // No guard against empty name on the model.
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("temp"));
        l->setName(QString());
        QVERIFY(l->name().isEmpty());
    }

    void datastore_removeLabel_soft_deletes()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("doomed"));
        const QString id = l->id();

        QVERIFY(store.removeLabel(id));
        // labelById still returns the object (tombstone), but...
        auto *still = store.labelById(id);
        QVERIFY(still != nullptr);
        QVERIFY(still->isDeleted());
        // ...allLabels() filters out tombstones.
        for (auto *x : store.allLabels()) {
            QVERIFY(x->id() != id);
        }
    }

    void datastore_removeLabel_unknown_id_returns_false()
    {
        DataStore store;
        QVERIFY(!store.removeLabel(QStringLiteral("does-not-exist")));
    }

    void datastore_removeLabel_emits_labelsChanged()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("x"));
        QSignalSpy spy(&store, &DataStore::labelsChanged);
        QVERIFY(store.removeLabel(l->id()));
        QCOMPARE(spy.count(), 1);
    }

    void datastore_addLabel_duplicate_name_is_accepted()
    {
        // Documented behaviour (verified in src/storage/datastore.cpp,
        // DataStore::addLabel): duplicate names are accepted. Both labels
        // coexist with distinct ids; the name index stores the most-recently
        // added (lookup by name returns the second).
        DataStore store;
        auto *first  = store.addLabel(QStringLiteral("dup"));
        auto *second = store.addLabel(QStringLiteral("dup"));
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        QVERIFY(first->id() != second->id());

        // Both should appear in allLabels (neither is auto-tombstoned).
        int dupCount = 0;
        for (auto *x : store.allLabels()) {
            if (x->name() == QStringLiteral("dup")) ++dupCount;
        }
        QCOMPARE(dupCount, 2);

        // labelByName resolves the most-recent (insertion overwrites the index).
        QCOMPARE(store.labelByName(QStringLiteral("dup")), second);
    }

    void datastore_labelByName_is_case_insensitive()
    {
        // labelByName lowercases the lookup key; verified in
        // src/storage/datastore.cpp.
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("Urgent"));
        QCOMPARE(store.labelByName(QStringLiteral("urgent")), l);
        QCOMPARE(store.labelByName(QStringLiteral("URGENT")), l);
        QCOMPARE(store.labelByName(QStringLiteral("Urgent")), l);
    }

    void datastore_labelByName_unknown_returns_null()
    {
        DataStore store;
        QVERIFY(store.labelByName(QStringLiteral("nope")) == nullptr);
    }

    // ---------- Round-trip through DataStore::toJson / loadFromJson ----------

    void datastore_label_full_roundtrip_preserves_color_and_favorite()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("home"));
        l->setColor(QStringLiteral("lavender"));
        l->setIsFavorite(true);
        l->setOrder(5);

        const QJsonObject snapshot = store.toJson();

        DataStore restored;
        restored.loadFromJson(snapshot);

        auto *r = restored.labelByName(QStringLiteral("home"));
        QVERIFY(r != nullptr);
        QCOMPARE(r->color(), QStringLiteral("lavender"));
        QVERIFY(r->isFavorite());
        QCOMPARE(r->order(), 5);
        QVERIFY(!r->isDeleted());
    }

    void datastore_label_soft_deleted_survives_roundtrip()
    {
        DataStore store;
        auto *l = store.addLabel(QStringLiteral("ghost"));
        QVERIFY(store.removeLabel(l->id()));

        const QJsonObject snapshot = store.toJson();

        DataStore restored;
        restored.loadFromJson(snapshot);

        // Tombstone must survive (soft-delete propagates through sync /
        // serialisation). It WON'T appear in allLabels(), but labelById will
        // find it.
        auto *r = restored.labelById(l->id());
        QVERIFY(r != nullptr);
        QVERIFY(r->isDeleted());

        for (auto *x : restored.allLabels()) {
            QVERIFY(x->id() != l->id());
        }
    }
};

QTEST_GUILESS_MAIN(TestLabelExtended)
#include "test_label_extended.moc"
