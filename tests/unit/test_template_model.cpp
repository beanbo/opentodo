// Template model coverage: toJson/fromJson round-trip (including the nested
// item tree) + partial applyJson semantics.

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>

#include "model/template.h"

class TestTemplateModel : public QObject
{
    Q_OBJECT
private slots:
    void toJson_fromJson_roundTrip()
    {
        Template t;
        t.setName(QStringLiteral("Weekly review"));

        QJsonObject child;
        child[QStringLiteral("content")] = QStringLiteral("Sub-step");
        child[QStringLiteral("priority")] = 4;

        QJsonObject item;
        item[QStringLiteral("content")] = QStringLiteral("Plan the week");
        item[QStringLiteral("priority")] = 1;
        QJsonArray labels; labels.append(QStringLiteral("work"));
        item[QStringLiteral("labels")] = labels;
        QJsonArray children; children.append(child);
        item[QStringLiteral("children")] = children;

        QJsonArray items; items.append(item);
        t.setItems(items);
        QCOMPARE(t.itemCount(), 1);

        const QJsonObject j = t.toJson();
        Template *r = Template::fromJson(j);
        QCOMPARE(r->id(), t.id());
        QCOMPARE(r->name(), QStringLiteral("Weekly review"));
        QCOMPARE(r->itemCount(), 1);
        const QJsonObject ri = r->items().first().toObject();
        QCOMPARE(ri[QStringLiteral("content")].toString(), QStringLiteral("Plan the week"));
        QCOMPARE(ri[QStringLiteral("priority")].toInt(), 1);
        QCOMPARE(ri[QStringLiteral("children")].toArray().size(), 1);
        QVERIFY(!r->isDeleted());
        delete r;
    }

    void applyJson_partial_touches_only_present_keys()
    {
        Template t;
        t.setName(QStringLiteral("Old"));
        QJsonArray items; items.append(QJsonObject{{QStringLiteral("content"), QStringLiteral("x")}});
        t.setItems(items);

        QJsonObject patch;
        patch[QStringLiteral("name")] = QStringLiteral("New");
        t.applyJson(patch);

        QCOMPARE(t.name(), QStringLiteral("New"));
        QCOMPARE(t.itemCount(), 1); // items NOT in the patch → untouched
    }

    void applyJson_tombstone()
    {
        Template t;
        QVERIFY(!t.isDeleted());
        t.applyJson(QJsonObject{{QStringLiteral("is_deleted"), true}});
        QVERIFY(t.isDeleted());
    }
};

QTEST_APPLESS_MAIN(TestTemplateModel)
#include "test_template_model.moc"
