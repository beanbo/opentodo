#include <QtTest>
#include <QSignalSpy>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/reminder.h"
#include "model/comment.h"
#include "utils/uuid.h"

class TestDataStore : public QObject
{
    Q_OBJECT

private:
    // Helper: creates a DataStore pre-loaded with one Inbox project so addTask works
    DataStore *makeStore()
    {
        auto *ds = new DataStore(this);
        // Provide an inbox project so addTask(content, "") works
        auto *inbox = new Project(ds);
        inbox->setName(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        ds->addExistingProject(inbox);
        return ds;
    }

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    // ── Constructor ───────────────────────────────────────────────────────────

    void constructor_all_lists_empty()
    {
        DataStore ds;
        QCOMPARE(ds.allTasks().size(), 0);
        QCOMPARE(ds.allProjects().size(), 0);
        QCOMPARE(ds.allLabels().size(), 0);
        QCOMPARE(ds.allFilters().size(), 0);
        QCOMPARE(ds.taskCount(), 0);
        QCOMPARE(ds.projectCount(), 0);
        QVERIFY(ds.inboxProject() == nullptr);
    }

    // ── Tasks ─────────────────────────────────────────────────────────────────

    void addTask_returns_task_with_id()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("Hello"), QString());
        QVERIFY(t != nullptr);
        QVERIFY(!t->id().isEmpty());
        QVERIFY(UuidHelper::isValid(t->id()));
        QCOMPARE(t->content(), QStringLiteral("Hello"));
    }

    void taskById_returns_same_object()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("Find me"), QString());
        QCOMPARE(ds->taskById(t->id()), t);
    }

    void taskById_unknown_returns_null()
    {
        QScopedPointer<DataStore> ds(makeStore());
        QVERIFY(ds->taskById(QStringLiteral("nonexistent")) == nullptr);
    }

    void allTasks_excludes_tombstones()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t1 = ds->addTask(QStringLiteral("Active"), QString());
        Task *t2 = ds->addTask(QStringLiteral("Will be deleted"), QString());

        QCOMPARE(ds->allTasks().size(), 2);

        bool removed = ds->removeTask(t2->id());
        QVERIFY(removed);
        QVERIFY(t2->isDeleted());

        QCOMPARE(ds->allTasks().size(), 1);
        QCOMPARE(ds->allTasks().first(), t1);
    }

    void removeTask_marks_deleted_not_destroyed()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("Soft delete"), QString());
        const QString id = t->id();

        ds->removeTask(id);

        // Object still accessible via index; just marked deleted
        Task *found = ds->taskById(id);
        QVERIFY(found != nullptr);
        QVERIFY(found->isDeleted());
    }

    void removeTask_unknown_returns_false()
    {
        QScopedPointer<DataStore> ds(makeStore());
        QVERIFY(!ds->removeTask(QStringLiteral("no-such-id")));
    }

    void tasksForProject_returns_only_matching_non_deleted()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Project *p1 = ds->addProject(QStringLiteral("P1"));
        Project *p2 = ds->addProject(QStringLiteral("P2"));

        Task *t1 = ds->addTask(QStringLiteral("T1"), p1->id());
        Task *t2 = ds->addTask(QStringLiteral("T2"), p1->id());
        Task *t3 = ds->addTask(QStringLiteral("T3"), p2->id());
        Q_UNUSED(t3)

        QCOMPARE(ds->tasksForProject(p1->id()).size(), 2);
        QCOMPARE(ds->tasksForProject(p2->id()).size(), 1);

        ds->removeTask(t2->id());
        QCOMPARE(ds->tasksForProject(p1->id()).size(), 1);
        QCOMPARE(ds->tasksForProject(p1->id()).first(), t1);
    }

    void tasksForSection_correct_after_section_change()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Project *p = ds->addProject(QStringLiteral("Proj"));
        Section *s1 = ds->addSection(QStringLiteral("S1"), p->id());
        Section *s2 = ds->addSection(QStringLiteral("S2"), p->id());

        Task *t = ds->addTask(QStringLiteral("Movable"), p->id());
        t->setSectionId(s1->id());

        QCOMPARE(ds->tasksForSection(s1->id()).size(), 1);
        QCOMPARE(ds->tasksForSection(s2->id()).size(), 0);

        // Move task to s2
        t->setSectionId(s2->id());

        QCOMPARE(ds->tasksForSection(s1->id()).size(), 0);
        QCOMPARE(ds->tasksForSection(s2->id()).size(), 1);
    }

    void addTask_emits_dataChanged()
    {
        QScopedPointer<DataStore> ds(makeStore());
        QSignalSpy spy(ds.data(), &DataStore::dataChanged);
        ds->addTask(QStringLiteral("Signal test"), QString());
        QVERIFY(spy.count() >= 1);
    }

    void addTask_emits_tasksChanged()
    {
        QScopedPointer<DataStore> ds(makeStore());
        QSignalSpy spy(ds.data(), &DataStore::tasksChanged);
        ds->addTask(QStringLiteral("Signal test"), QString());
        QCOMPARE(spy.count(), 1);
    }

    void removeTask_emits_dataChanged()
    {
        QScopedPointer<DataStore> ds(makeStore());
        Task *t = ds->addTask(QStringLiteral("To remove"), QString());
        QSignalSpy spy(ds.data(), &DataStore::dataChanged);
        ds->removeTask(t->id());
        QVERIFY(spy.count() >= 1);
    }

    // ── Projects ──────────────────────────────────────────────────────────────

    void addProject_emits_dataChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addProject(QStringLiteral("Work"));
        QVERIFY(spy.count() >= 1);
    }

    void addProject_emits_projectsChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::projectsChanged);
        ds.addProject(QStringLiteral("Work"));
        QCOMPARE(spy.count(), 1);
    }

    void removeProject_marks_deleted()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Temp"));
        QVERIFY(!p->isDeleted());
        bool ok = ds.removeProject(p->id());
        QVERIFY(ok);
        QVERIFY(p->isDeleted());
        // allProjects() filters tombstones — only inbox-like projects
        // after removal the list shrinks
        QCOMPARE(ds.allProjects().size(), 0);
    }

    void removeProject_inbox_returns_false()
    {
        // Inbox project cannot be deleted
        QScopedPointer<DataStore> ds(makeStore());
        Project *inbox = ds->inboxProject();
        QVERIFY(inbox != nullptr);
        bool ok = ds->removeProject(inbox->id());
        QVERIFY(!ok);
        QVERIFY(!inbox->isDeleted());
    }

    // ITER4 #1 / #3: removing a parent project cascades to its subprojects
    // (and recursively their grandchildren). Without this, orphaned
    // subprojects with parentId pointing at a tombstone stayed in the
    // sidebar and never reconciled away on sync.
    void removeProject_cascades_to_child_subprojects()
    {
        DataStore ds;
        Project *parent     = ds.addProject(QStringLiteral("Parent"));
        Project *child      = ds.addProject(QStringLiteral("Child"));
        child->setParentId(parent->id());
        Project *grandchild = ds.addProject(QStringLiteral("Grandchild"));
        grandchild->setParentId(child->id());

        // Sanity: parent, child, grandchild currently alive.
        QVERIFY(!parent->isDeleted());
        QVERIFY(!child->isDeleted());
        QVERIFY(!grandchild->isDeleted());

        const bool ok = ds.removeProject(parent->id());
        QVERIFY(ok);
        QVERIFY(parent->isDeleted());
        QVERIFY2(child->isDeleted(),
                 "Child subproject must be tombstoned when parent is deleted");
        QVERIFY2(grandchild->isDeleted(),
                 "Grandchild subproject must be tombstoned recursively");

        // allProjects() filters tombstones — list should be empty.
        QCOMPARE(ds.allProjects().size(), 0);
    }

    void projectById_returns_correct()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("FindMe"));
        QCOMPARE(ds.projectById(p->id()), p);
        QVERIFY(ds.projectById(QStringLiteral("bad")) == nullptr);
    }

    // ── Sections ──────────────────────────────────────────────────────────────

    void addSection_emits_dataChanged()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addSection(QStringLiteral("S1"), p->id());
        QVERIFY(spy.count() >= 1);
    }

    void sectionsForProject_correct()
    {
        DataStore ds;
        Project *p1 = ds.addProject(QStringLiteral("P1"));
        Project *p2 = ds.addProject(QStringLiteral("P2"));
        ds.addSection(QStringLiteral("S1"), p1->id());
        ds.addSection(QStringLiteral("S2"), p1->id());
        ds.addSection(QStringLiteral("S3"), p2->id());

        QCOMPARE(ds.sectionsForProject(p1->id()).size(), 2);
        QCOMPARE(ds.sectionsForProject(p2->id()).size(), 1);
    }

    void removeSection_marks_deleted()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Sprint"), p->id());
        QVERIFY(!s->isDeleted());
        ds.removeSection(s->id());
        QVERIFY(s->isDeleted());
        QCOMPARE(ds.sectionsForProject(p->id()).size(), 0);
    }

    void sectionById_returns_correct()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("S"), p->id());
        QCOMPARE(ds.sectionById(s->id()), s);
        QVERIFY(ds.sectionById(QStringLiteral("bad")) == nullptr);
    }

    // ── Labels ────────────────────────────────────────────────────────────────

    void addLabel_emits_dataChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addLabel(QStringLiteral("urgent"));
        QVERIFY(spy.count() >= 1);
    }

    void allLabels_excludes_deleted()
    {
        DataStore ds;
        Label *l1 = ds.addLabel(QStringLiteral("work"));
        Label *l2 = ds.addLabel(QStringLiteral("home"));
        QCOMPARE(ds.allLabels().size(), 2);
        ds.removeLabel(l2->id());
        QCOMPARE(ds.allLabels().size(), 1);
        QCOMPARE(ds.allLabels().first(), l1);
    }

    void labelByName_lookup()
    {
        DataStore ds;
        ds.addLabel(QStringLiteral("Work"));
        Label *found = ds.labelByName(QStringLiteral("work")); // case-insensitive
        QVERIFY(found != nullptr);
        QCOMPARE(found->name(), QStringLiteral("Work"));
    }

    // ── Filters ───────────────────────────────────────────────────────────────

    void addFilter_emits_dataChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addFilter(QStringLiteral("Today"), QStringLiteral("today"));
        QVERIFY(spy.count() >= 1);
    }

    void allFilters_excludes_deleted()
    {
        DataStore ds;
        Filter *f1 = ds.addFilter(QStringLiteral("F1"), QStringLiteral("p1"));
        Filter *f2 = ds.addFilter(QStringLiteral("F2"), QStringLiteral("p2"));
        QCOMPARE(ds.allFilters().size(), 2);
        ds.removeFilter(f2->id());
        QCOMPARE(ds.allFilters().size(), 1);
        QCOMPARE(ds.allFilters().first(), f1);
    }

    // ── Reminders ─────────────────────────────────────────────────────────────

    void addReminder_emits_dataChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addReminder(QStringLiteral("t-1"), QStringLiteral("absolute"));
        QVERIFY(spy.count() >= 1);
    }

    void remindersForTask_correct()
    {
        DataStore ds;
        Reminder *r1 = ds.addReminder(QStringLiteral("task-A"), QStringLiteral("absolute"));
        Reminder *r2 = ds.addReminder(QStringLiteral("task-A"), QStringLiteral("relative"));
        Reminder *r3 = ds.addReminder(QStringLiteral("task-B"), QStringLiteral("absolute"));
        Q_UNUSED(r3)

        QCOMPARE(ds.remindersForTask(QStringLiteral("task-A")).size(), 2);
        QCOMPARE(ds.remindersForTask(QStringLiteral("task-B")).size(), 1);

        ds.removeReminder(r2->id());
        QCOMPARE(ds.remindersForTask(QStringLiteral("task-A")).size(), 1);
        QCOMPARE(ds.remindersForTask(QStringLiteral("task-A")).first(), r1);
    }

    // ── Comments ──────────────────────────────────────────────────────────────

    void addComment_emits_dataChanged()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.addComment(QStringLiteral("Hello"), QStringLiteral("t-1"), QString());
        QVERIFY(spy.count() >= 1);
    }

    void commentsForTask_and_forProject_correct()
    {
        DataStore ds;
        Comment *c1 = ds.addComment(QStringLiteral("Task note"), QStringLiteral("t-1"), QString());
        Comment *c2 = ds.addComment(QStringLiteral("Task note 2"), QStringLiteral("t-1"), QString());
        Comment *c3 = ds.addComment(QStringLiteral("Proj note"), QString(), QStringLiteral("p-1"));
        Q_UNUSED(c1) Q_UNUSED(c2) Q_UNUSED(c3)

        QCOMPARE(ds.commentsForTask(QStringLiteral("t-1")).size(), 2);
        QCOMPARE(ds.commentsForProject(QStringLiteral("p-1")).size(), 1);

        ds.removeComment(c2->id());
        QCOMPARE(ds.commentsForTask(QStringLiteral("t-1")).size(), 1);
    }

    // ── Settings ──────────────────────────────────────────────────────────────

    void settings_emits_dataChanged_on_setSetting()
    {
        DataStore ds;
        QSignalSpy spy(&ds, &DataStore::dataChanged);
        ds.setSetting(QStringLiteral("theme"), QStringLiteral("dark"));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(ds.setting(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
    }

    // ── Full round-trip (toJson / loadFromJson) ────────────────────────────────

    void round_trip_full_store()
    {
        QScopedPointer<DataStore> ds1(makeStore());
        Project *p = ds1->addProject(QStringLiteral("Work"));
        Task *t = ds1->addTask(QStringLiteral("Write tests"), p->id());
        t->setPriority(2);
        t->setLabels({QStringLiteral("dev")});
        Section *s = ds1->addSection(QStringLiteral("Sprint"), p->id());
        ds1->addLabel(QStringLiteral("urgent"));
        ds1->addFilter(QStringLiteral("High"), QStringLiteral("p1"));
        ds1->addReminder(t->id(), QStringLiteral("absolute"));
        ds1->addComment(QStringLiteral("Note"), t->id(), QString());
        Q_UNUSED(s)

        QJsonObject json = ds1->toJson();

        // Load into a fresh store
        DataStore ds2;
        ds2.loadFromJson(json);

        // Counts match
        QCOMPARE(ds2.allTasks().size(), ds1->allTasks().size());
        QCOMPARE(ds2.allProjects().size(), ds1->allProjects().size());
        QCOMPARE(ds2.allLabels().size(), ds1->allLabels().size());
        QCOMPARE(ds2.allFilters().size(), ds1->allFilters().size());

        // Task content preserved
        Task *t2 = ds2.taskById(t->id());
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->content(), QStringLiteral("Write tests"));
        QCOMPARE(t2->priority(), 2);
        QCOMPARE(t2->labels(), QStringList{QStringLiteral("dev")});
    }

    void loadFromJson_does_not_duplicate_entities()
    {
        QScopedPointer<DataStore> ds(makeStore());
        ds->addTask(QStringLiteral("T1"), QString());
        ds->addTask(QStringLiteral("T2"), QString());

        QJsonObject json = ds->toJson();

        // Load twice
        ds->loadFromJson(json);
        ds->loadFromJson(json);

        // Should still have same count (clear is called inside loadFromJson)
        QCOMPARE(ds->allTasks().size(), 2);
    }

    void clear_empties_all_collections()
    {
        QScopedPointer<DataStore> ds(makeStore());
        ds->addTask(QStringLiteral("T"), QString());
        ds->addLabel(QStringLiteral("L"));
        ds->addFilter(QStringLiteral("F"), QStringLiteral("today"));
        QVERIFY(ds->allTasks().size() > 0);

        ds->clear();

        QCOMPARE(ds->allTasks().size(), 0);
        QCOMPARE(ds->allProjects().size(), 0);
        QCOMPARE(ds->allLabels().size(), 0);
        QCOMPARE(ds->allFilters().size(), 0);
        QVERIFY(ds->inboxProject() == nullptr);
    }
};

QTEST_GUILESS_MAIN(TestDataStore)
#include "test_datastore.moc"
