// Regression test for the "Кнопка раскрытия родительского проекта не работает" bug.
//
// Root cause: ProjectViewModel::childProjects(parentId) returned
// QVariantList<QVariant::fromValue(Project*)>. Qt 6.11 does NOT auto-convert
// QObject*-bearing QVariants inside a QVariantList into a usable JS array
// in the QML scripting engine — `result.length` evaluates to undefined and
// the typical `for (let i = 0; i < arr.length; ++i)` iteration runs zero
// times.
//
// In SidebarContent.qml the projTreeDelegate computes
//   readonly property bool hasChildren: {
//       const ch = projectViewModel.childProjects(projTreeDelegate.projectId)
//       if (!ch) return false
//       for (let i = 0; i < ch.length; ++i) { ... return true }
//       return false
//   }
// Because `ch.length` is undefined, the loop never enters and `hasChildren`
// is always false. The chevron is therefore hidden (`visible: hasChildren`)
// and the MouseArea is disabled — clicking the row navigates to the project
// instead of toggling expansion.
//
// The fix: convert `childProjects` to return QVariantList<QVariantMap> with
// the keys the QML side reads (`id`, `name`, `color`, `icon`, `isArchived`,
// `isInbox`, `parentId`). That shape converts cleanly to a JS array.
//
// This test pins the new shape so a future refactor that reverts to
// QObject*-in-QVariant gets caught.

#include <QtTest>
#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>
#include <QSettings>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "viewmodels/projectviewmodel.h"
#include "model/project.h"

class TestChildProjectsQmlShape : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ChildProjectsShape"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_childprojects_qml_shape"));
    }

    // The core regression check: childProjects must return a QVariantList
    // whose entries are QVariantMaps (not QObject*-bearing QVariants).
    // QVariantMap is what Qt 6.11's QML JS engine treats as a JS object
    // with the map keys as accessible properties.
    void childProjects_returns_variantmap_entries_not_qobject_pointers()
    {
        DataStore store;
        // Build parent + 2 children.
        Project *parent = store.addProject(QStringLiteral("Дом"));
        QVERIFY(parent);
        parent->setColor(QStringLiteral("#fad000"));
        Project *kvartal = store.addProject(QStringLiteral("Квартал"));
        QVERIFY(kvartal);
        kvartal->setColor(QStringLiteral("#888888"));
        kvartal->setParentId(parent->id());
        Project *pokupki = store.addProject(QStringLiteral("Покупки"));
        QVERIFY(pokupki);
        pokupki->setColor(QStringLiteral("#666666"));
        pokupki->setParentId(parent->id());

        ProjectViewModel vm(&store);
        

        const QVariantList children = vm.childProjects(parent->id());
        QCOMPARE(children.size(), 2);

        // Each entry MUST be a QVariantMap, not a QObject*-bearing QVariant.
        for (const QVariant &v : children) {
            QVERIFY2(v.metaType().id() == QMetaType::QVariantMap,
                     qPrintable(QStringLiteral(
                         "childProjects entry has wrong metatype: %1 "
                         "(expected QVariantMap=%2). This indicates a "
                         "regression to the broken QVariant::fromValue(Project*) "
                         "pattern that Qt 6.11 silently breaks in QML.")
                         .arg(v.metaType().id())
                         .arg(QMetaType::QVariantMap)));

            const QVariantMap map = v.toMap();
            // Pin the keys QML reads.
            QVERIFY2(map.contains(QStringLiteral("id")),
                     "childProjects entry must expose 'id' key");
            QVERIFY2(map.contains(QStringLiteral("name")),
                     "childProjects entry must expose 'name' key");
            QVERIFY2(map.contains(QStringLiteral("parentId")),
                     "childProjects entry must expose 'parentId' key");
            QVERIFY2(map.contains(QStringLiteral("isArchived")),
                     "childProjects entry must expose 'isArchived' key");
            QVERIFY2(map.contains(QStringLiteral("isInbox")),
                     "childProjects entry must expose 'isInbox' key");
        }
    }

    // Verify the parent and parentId fields match — the QML rebuild() relies
    // on this to compute the hierarchy.
    void childProjects_parentId_matches_input()
    {
        DataStore store;
        Project *parent = store.addProject(QStringLiteral("Дом"));
        Project *child = store.addProject(QStringLiteral("Квартал"));
        child->setParentId(parent->id());

        ProjectViewModel vm(&store);
        

        const QVariantList children = vm.childProjects(parent->id());
        QCOMPARE(children.size(), 1);
        QCOMPARE(children.first().toMap().value(QStringLiteral("parentId")).toString(),
                 parent->id());
        QCOMPARE(children.first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Квартал"));
    }

    // Verify isArchived / isInbox are accessible. The QML hasChildren guard
    // filters them out:
    //   for (let i = 0; i < ch.length; ++i) {
    //       const c = ch[i]
    //       if (c && !c.isArchived && !c.isInbox) return true
    //   }
    void childProjects_archived_and_inbox_flags_readable()
    {
        DataStore store;
        Project *parent = store.addProject(QStringLiteral("Parent"));
        Project *normal = store.addProject(QStringLiteral("Normal"));
        normal->setParentId(parent->id());

        ProjectViewModel vm(&store);

        // Only non-archived children are visible by default — `m_showArchived`
        // is false in the VM, and refresh() filters archived projects out of
        // `m_projects`. This is fine for the chevron use case: the QML side
        // also filters archived in its hasChildren guard.
        const QVariantList children = vm.childProjects(parent->id());
        QCOMPARE(children.size(), 1);

        const QVariantMap map = children.first().toMap();
        QCOMPARE(map.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Normal"));
        // isArchived / isInbox keys are present and readable as booleans —
        // the QML chevron logic does `!c.isArchived && !c.isInbox`.
        QCOMPARE(map.value(QStringLiteral("isArchived")).toBool(), false);
        QCOMPARE(map.value(QStringLiteral("isInbox")).toBool(), false);
    }

    // Quick sanity: a parent with NO children returns an empty list (not null).
    void childProjects_with_no_children_returns_empty_list()
    {
        DataStore store;
        Project *lonely = store.addProject(QStringLiteral("Lonely"));

        ProjectViewModel vm(&store);
        

        const QVariantList children = vm.childProjects(lonely->id());
        QCOMPARE(children.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestChildProjectsQmlShape)
#include "test_childprojects_qml_shape.moc"
