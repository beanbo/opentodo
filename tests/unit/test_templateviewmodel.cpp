// TemplateViewModel: save-as-template captures the task subtree, insertTemplate
// materialises it into real tasks, delete removes it, and templates persist to
// templates.json across VM instances.

#include <QtTest>
#include <QFile>
#include <QStandardPaths>

#include "viewmodels/templateviewmodel.h"
#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"

class TestTemplateViewModel : public QObject
{
    Q_OBJECT

    static QString tplPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/templates.json");
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_TemplateVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_templateviewmodel"));
    }

    void init() { QFile::remove(tplPath()); }      // each test starts with no templates
    void cleanup() { QFile::remove(tplPath()); }

    void saveAndInsert_roundTrip()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Proj"));
        Task *parent = store.addTask(QStringLiteral("Parent task"), p->id());
        parent->setPriority(1);
        parent->setLabels(QStringList{QStringLiteral("work")});
        Task *child = store.addTask(QStringLiteral("Child"), p->id());
        child->setParentId(parent->id());

        TemplateViewModel vm(&store);
        const QString tid = vm.saveAsTemplate(QStringLiteral("My template"), {parent->id()});
        QVERIFY(!tid.isEmpty());
        QCOMPARE(vm.count(), 1);

        // Insert into a fresh project.
        Project *p2 = store.addProject(QStringLiteral("Target"));
        vm.insertTemplate(tid, p2->id(), QString());

        const auto top = store.tasksForProject(p2->id());
        QCOMPARE(top.size(), 1);
        Task *inserted = top.first();
        QCOMPARE(inserted->content(), QStringLiteral("Parent task"));
        QCOMPARE(inserted->priority(), 1);
        QCOMPARE(inserted->labels(), QStringList{QStringLiteral("work")});

        // Child materialised as a subtask of the inserted parent.
        const auto subs = store.subtasks(inserted->id());
        QCOMPARE(subs.size(), 1);
        QCOMPARE(subs.first()->content(), QStringLiteral("Child"));
        QCOMPARE(subs.first()->projectId(), p2->id());
    }

    void saveAsTemplate_emptySelection_returnsEmpty()
    {
        DataStore store;
        TemplateViewModel vm(&store);
        QCOMPARE(vm.saveAsTemplate(QStringLiteral("Nothing"), {}), QString());
        QCOMPARE(vm.count(), 0);
    }

    void deleteTemplate_removes()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("T"), p->id());
        TemplateViewModel vm(&store);
        const QString id = vm.saveAsTemplate(QStringLiteral("X"), {t->id()});
        QCOMPARE(vm.count(), 1);
        QVERIFY(vm.deleteTemplate(id));
        QCOMPARE(vm.count(), 0);
    }

    void persistence_reloadsAcrossInstances()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("Persisted"), p->id());
        { TemplateViewModel vm(&store); vm.saveAsTemplate(QStringLiteral("Persisted tpl"), {t->id()}); }
        TemplateViewModel vm2(&store);   // loads templates.json written above
        QCOMPARE(vm2.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestTemplateViewModel)
#include "test_templateviewmodel.moc"
