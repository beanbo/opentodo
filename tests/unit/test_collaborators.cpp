// Tests for DataStore's first-class collaborator (comment-author) identities.
//
// Covers: built-in default resolution (me / claude), empty-author-id
// normalisation, override persistence in the synced settings blob, clearing an
// override back to the built-in default, JSON round-trip (sync durability), the
// collaborators() editor list, and the collaboratorsChanged() signal.
//
// IMPORTANT: avoid raw string literals — moc choked on R"(...)" in the past.

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>

#include "storage/datastore.h"

class TestCollaborators : public QObject
{
    Q_OBJECT

private slots:
    // ── Built-in defaults ──────────────────────────────────────────────────
    void defaults_names()
    {
        DataStore ds;
        // Empty author id normalises to the built-in "me".
        QCOMPARE(ds.collaboratorName(QString()),          QStringLiteral("Я"));
        QCOMPARE(ds.collaboratorName(QStringLiteral("me")),     QStringLiteral("Я"));
        QCOMPARE(ds.collaboratorName(QStringLiteral("claude")), QStringLiteral("Claude"));
        // Unknown id falls back to showing the id itself.
        QCOMPARE(ds.collaboratorName(QStringLiteral("bob")),    QStringLiteral("bob"));
    }

    void defaults_assistantFlagAndIcon()
    {
        DataStore ds;
        QVERIFY(!ds.collaboratorIsAssistant(QString()));
        QVERIFY(!ds.collaboratorIsAssistant(QStringLiteral("me")));
        QVERIFY( ds.collaboratorIsAssistant(QStringLiteral("claude")));

        QCOMPARE(ds.collaboratorIcon(QStringLiteral("me")),     QString());
        QCOMPARE(ds.collaboratorIcon(QString()),                QString());
        QCOMPARE(ds.collaboratorIcon(QStringLiteral("claude")), QStringLiteral("claude"));

        // No colour override by default.
        QCOMPARE(ds.collaboratorColor(QStringLiteral("claude")), QString());
        QCOMPARE(ds.collaboratorColor(QStringLiteral("me")),     QString());
    }

    void editorList_shape()
    {
        DataStore ds;
        const QVariantList rows = ds.collaborators();
        QCOMPARE(rows.size(), 2);
        const QVariantMap me     = rows.at(0).toMap();
        const QVariantMap claude = rows.at(1).toMap();
        QCOMPARE(me.value(QStringLiteral("id")).toString(),     QStringLiteral("me"));
        QCOMPARE(me.value(QStringLiteral("assistant")).toBool(), false);
        QCOMPARE(claude.value(QStringLiteral("id")).toString(), QStringLiteral("claude"));
        QCOMPARE(claude.value(QStringLiteral("assistant")).toBool(), true);
        QCOMPARE(claude.value(QStringLiteral("icon")).toString(), QStringLiteral("claude"));
    }

    // ── Overrides ──────────────────────────────────────────────────────────
    void setProfile_overridesNameAndColor()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::collaboratorsChanged);

        ds.setCollaboratorProfile(QStringLiteral("claude"),
                                  QStringLiteral("Клод"),
                                  QStringLiteral("#123456"),
                                  QStringLiteral("claude")); // icon unchanged

        QVERIFY(spy.count() >= 1);
        QCOMPARE(ds.collaboratorName(QStringLiteral("claude")),  QStringLiteral("Клод"));
        QCOMPARE(ds.collaboratorColor(QStringLiteral("claude")), QStringLiteral("#123456"));
        // Icon stays the built-in default (we passed the default, so no override).
        QCOMPARE(ds.collaboratorIcon(QStringLiteral("claude")),  QStringLiteral("claude"));

        // The override lives inside the synced settings blob.
        const QJsonObject collabs =
            ds.settings().value(QStringLiteral("collaborators")).toObject();
        QVERIFY(collabs.contains(QStringLiteral("claude")));
        QCOMPARE(collabs.value(QStringLiteral("claude")).toObject()
                        .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Клод"));
    }

    void emptyAuthorId_mapsToMeOverride()
    {
        DataStore ds;
        // Editing via the empty id must land on "me".
        ds.setCollaboratorProfile(QString(), QStringLiteral("Серж"),
                                  QStringLiteral("#abcdef"), QString());
        QCOMPARE(ds.collaboratorName(QString()),            QStringLiteral("Серж"));
        QCOMPARE(ds.collaboratorName(QStringLiteral("me")), QStringLiteral("Серж"));
        QCOMPARE(ds.collaboratorColor(QStringLiteral("me")), QStringLiteral("#abcdef"));

        const QJsonObject collabs =
            ds.settings().value(QStringLiteral("collaborators")).toObject();
        QVERIFY(collabs.contains(QStringLiteral("me")));
    }

    void clearingBackToDefault_removesOverride()
    {
        DataStore ds;
        // Establish an override, then clear name (default) + colour (empty).
        ds.setCollaboratorProfile(QStringLiteral("claude"),
                                  QStringLiteral("Клод"),
                                  QStringLiteral("#123456"),
                                  QStringLiteral("claude"));
        QVERIFY(ds.settings().contains(QStringLiteral("collaborators")));

        // Passing the built-in default name + empty colour + default icon should
        // strip the override entirely, leaving a minimal settings object.
        ds.setCollaboratorProfile(QStringLiteral("claude"),
                                  QStringLiteral("Claude"),
                                  QString(),
                                  QStringLiteral("claude"));
        QCOMPARE(ds.collaboratorName(QStringLiteral("claude")),  QStringLiteral("Claude"));
        QCOMPARE(ds.collaboratorColor(QStringLiteral("claude")), QString());
        // The "collaborators" key is dropped once no overrides remain.
        QVERIFY(!ds.settings().contains(QStringLiteral("collaborators")));
    }

    void iconOverride_persistsWhenDifferentFromDefault()
    {
        DataStore ds;
        // Give "me" (default icon "") a custom icon.
        ds.setCollaboratorProfile(QStringLiteral("me"), QStringLiteral("Я"),
                                  QString(), QStringLiteral("sparkles"));
        QCOMPARE(ds.collaboratorIcon(QStringLiteral("me")), QStringLiteral("sparkles"));
    }

    // ── Sync durability ─────────────────────────────────────────────────────
    void roundTrip_preservesOverrides()
    {
        DataStore ds;
        ds.setCollaboratorProfile(QStringLiteral("claude"),
                                  QStringLiteral("Ассистент"),
                                  QStringLiteral("#0891b2"),
                                  QStringLiteral("claude"));

        const QJsonObject snapshot = ds.toJson();

        DataStore ds2;
        ds2.loadFromJson(snapshot);
        QCOMPARE(ds2.collaboratorName(QStringLiteral("claude")),  QStringLiteral("Ассистент"));
        QCOMPARE(ds2.collaboratorColor(QStringLiteral("claude")), QStringLiteral("#0891b2"));
    }
};

QTEST_MAIN(TestCollaborators)
#include "test_collaborators.moc"
