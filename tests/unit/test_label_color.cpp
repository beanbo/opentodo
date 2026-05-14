#include <QtTest>
#include "storage/datastore.h"
#include "model/label.h"

class TestLabelColor : public QObject
{
    Q_OBJECT

private slots:
    void label_default_color()
    {
        DataStore store;
        auto *l = store.addLabel("test");
        QVERIFY(!l->color().isEmpty());  // some default
    }

    void label_color_set_and_persists_through_json()
    {
        DataStore store;
        auto *l = store.addLabel("urgent");
        l->setColor("#db4c3f");
        QCOMPARE(l->color(), QStringLiteral("#db4c3f"));

        const QJsonObject json = store.toJson();
        DataStore store2;
        store2.loadFromJson(json);
        QCOMPARE(store2.labelByName("urgent")->color(), QStringLiteral("#db4c3f"));
    }

    void label_color_per_label_independent()
    {
        DataStore store;
        auto *l1 = store.addLabel("red");
        l1->setColor("#db4c3f");
        auto *l2 = store.addLabel("blue");
        l2->setColor("#14aaf5");
        QCOMPARE(l1->color(), QStringLiteral("#db4c3f"));
        QCOMPARE(l2->color(), QStringLiteral("#14aaf5"));
    }

    void label_old_json_without_color_gets_default()
    {
        // Backwards-compat: JSON entry has no "color" key at all
        QJsonObject obj;
        obj["id"]   = QStringLiteral("aaaabbbb-0000-0000-0000-000000000001");
        obj["name"] = QStringLiteral("legacy");
        // no "color" key
        obj["order"]       = 0;
        obj["is_favorite"] = false;
        obj["is_deleted"]  = false;

        QJsonArray labelsArr;
        labelsArr.append(obj);
        QJsonObject root;
        root["labels"] = labelsArr;

        DataStore store;
        store.loadFromJson(root);

        auto *l = store.labelByName("legacy");
        QVERIFY(l != nullptr);
        // fromJson uses toString(QStringLiteral("charcoal")) as the fallback
        QCOMPARE(l->color(), QStringLiteral("charcoal"));
    }

    void markLabelModified_emits_labelsChanged()
    {
        DataStore store;
        auto *l = store.addLabel("work");
        QSignalSpy spy(&store, &DataStore::labelsChanged);
        // First emission was from addLabel above; clear it
        spy.clear();

        l->setColor("#7ecc49");
        store.markLabelModified(l->id());
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestLabelColor)
#include "test_label_color.moc"
