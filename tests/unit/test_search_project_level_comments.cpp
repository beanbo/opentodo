// =============================================================================
// Pin test for iter-night-6: SearchViewModel must surface PROJECT-level
// comments alongside task-level ones.
//
// Before the fix performSearch() iterated `m_store->allTasks()` only and
// queried `commentsForTask(t->id())`. Comments attached to a project (taskId
// empty, projectId set — created by the TodoistImporter for any note whose
// task was filtered out, or by direct `DataStore::addComment(content, "", pid)`
// calls) were never visited, so a global search for a substring that appeared
// verbatim in such a comment returned zero results.
//
// This test pins:
//   1. Project-level comment whose body matches → must produce a comment hit.
//   2. The hit carries `projectId` (NOT `taskId`) so the click handler can
//      route to the project view rather than crashing on an empty taskId.
//   3. Subtitle distinguishes "Комментарий к проекту: <name>" from the
//      task-level "Комментарий к: <task content>" subtitle.
//   4. Mixed scenario: when both a task-level comment AND a project-level
//      comment match, both surface. (Each comment can only have ONE parent;
//      DataStore::addComment enforces it, so this is genuinely two distinct
//      comments.)
//   5. Tombstoned project-level comments are excluded.
//   6. Comments on a tombstoned PROJECT are excluded. (Tombstoned projects
//      are filtered by DataStore::allProjects() — we don't reach
//      `commentsForProject` for them.)
// =============================================================================

#include <QtTest>
#include <QStandardPaths>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/comment.h"
#include "viewmodels/searchviewmodel.h"

namespace {

static bool hasResultOfTypeWithId(const QVariantList &results,
                                  const QString &type, const QString &id)
{
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type
            && m.value(QStringLiteral("id")).toString() == id)
            return true;
    }
    return false;
}

static QVariantMap findResult(const QVariantList &results,
                              const QString &type, const QString &id)
{
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type
            && m.value(QStringLiteral("id")).toString() == id)
            return m;
    }
    return {};
}

static int countByType(const QVariantList &results, const QString &type)
{
    int n = 0;
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type)
            ++n;
    }
    return n;
}

} // anonymous namespace


class TestSearchProjectLevelComments : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_SearchProjComm"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_search_project_level_comments"));
    }

    void init()
    {
        QSettings qs;
        qs.remove(QStringLiteral("search"));
        qs.sync();
    }

    void cleanupTestCase()
    {
        QSettings qs;
        qs.remove(QStringLiteral("search"));
        qs.sync();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1) Project-level comment must surface, with projectId field set.
    // ─────────────────────────────────────────────────────────────────────────
    void projectLevelComment_surfacesAndCarriesProjectId()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Работа"));

        // Project-level comment: taskId="", projectId=p->id().
        Comment *projComment =
            store.addComment(QStringLiteral("уникальныйПроектныйКоммент здесь"),
                             QString(), p->id());
        QVERIFY(projComment);
        // Sanity: addComment really did attach it to the PROJECT, not a task.
        QVERIFY(projComment->taskId().isEmpty());
        QCOMPARE(projComment->projectId(), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("уникальныйПроектныйКоммент"));

        // Must surface as type "comment".
        QVERIFY2(hasResultOfTypeWithId(vm.results(),
                                       QStringLiteral("comment"),
                                       projComment->id()),
                 "Project-level comment must appear in global search results.");

        // The result item must carry projectId for routing.
        const QVariantMap m = findResult(vm.results(),
                                         QStringLiteral("comment"),
                                         projComment->id());
        QVERIFY(!m.isEmpty());
        QCOMPARE(m.value(QStringLiteral("projectId")).toString(), p->id());
        QVERIFY2(!m.contains(QStringLiteral("taskId"))
                 || m.value(QStringLiteral("taskId")).toString().isEmpty(),
                 "Project-level comment must NOT carry taskId — routing "
                 "depends on the absence of taskId to dispatch to project.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2) Subtitle disambiguates project-level vs task-level comments.
    // ─────────────────────────────────────────────────────────────────────────
    void projectLevelComment_subtitleSaysProject()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("ИмяПроекта"));
        Comment *c = store.addComment(QStringLiteral("alphaXYZ"),
                                      QString(), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("alphaXYZ"));

        const QVariantMap m = findResult(vm.results(),
                                         QStringLiteral("comment"), c->id());
        QVERIFY(!m.isEmpty());
        const QString subtitle = m.value(QStringLiteral("subtitle")).toString();
        QVERIFY2(subtitle.contains(QStringLiteral("проекту"),
                                   Qt::CaseInsensitive),
                 qPrintable(QStringLiteral(
                     "Subtitle should mention project routing, got: %1")
                     .arg(subtitle)));
        QVERIFY(subtitle.contains(QStringLiteral("ИмяПроекта")));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3) Mixed scenario: task-level + project-level comments BOTH surface.
    // ─────────────────────────────────────────────────────────────────────────
    void mixedTaskAndProjectComments_bothSurface()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Проект"));
        Task *t = store.addTask(QStringLiteral("Задача"), p->id());

        Comment *taskComment =
            store.addComment(QStringLiteral("kappa-shared-token task side"),
                             t->id(), QString());
        Comment *projComment =
            store.addComment(QStringLiteral("kappa-shared-token project side"),
                             QString(), p->id());

        // Sanity on parentage.
        QCOMPARE(taskComment->taskId(),  t->id());
        QVERIFY(taskComment->projectId().isEmpty());
        QVERIFY(projComment->taskId().isEmpty());
        QCOMPARE(projComment->projectId(), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("kappa-shared-token"));

        QCOMPARE(countByType(vm.results(), QStringLiteral("comment")), 2);
        QVERIFY(hasResultOfTypeWithId(vm.results(),
                                      QStringLiteral("comment"),
                                      taskComment->id()));
        QVERIFY(hasResultOfTypeWithId(vm.results(),
                                      QStringLiteral("comment"),
                                      projComment->id()));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4) Tombstoned project-level comment must NOT surface.
    // ─────────────────────────────────────────────────────────────────────────
    void tombstonedProjectLevelComment_isExcluded()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Проект"));
        Comment *c = store.addComment(QStringLiteral("zetaTomb"),
                                      QString(), p->id());

        store.removeComment(c->id());
        QVERIFY(c->isDeleted());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("zetaTomb"));

        QVERIFY(!hasResultOfTypeWithId(vm.results(),
                                       QStringLiteral("comment"), c->id()));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5) Comments on a tombstoned PROJECT must NOT surface in search.
    //
    //    There are two cascading reasons this must hold, and the pin checks
    //    that BOTH guarantee correctness — defence in depth:
    //      (a) DataStore::removeProject cascade-tombstones every project-level
    //          comment (datastore.cpp:767-776), so commentsForProject(pid)
    //          filters them out by `!c->isDeleted()`.
    //      (b) Even if (a) regressed, allProjects() filters tombstoned
    //          projects, so the new project-comment loop never iterates the
    //          dead project at all.
    //    The end-user contract: searching for a token in a deleted project's
    //    comment yields zero results — never a click-target into a
    //    tombstoned project.
    // ─────────────────────────────────────────────────────────────────────────
    void commentOnTombstonedProject_isExcluded()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("DoomedProject"));
        Comment *c = store.addComment(QStringLiteral("orphanZeta"),
                                      QString(), p->id());

        store.removeProject(p->id());
        QVERIFY(p->isDeleted());
        // (a) Cascade-tombstone is the current contract in removeProject —
        // pin it so a future refactor that "decascades" project-level
        // comments here flips this test red and surfaces the regression.
        QVERIFY2(c->isDeleted(),
                 "removeProject must cascade-tombstone project-level comments "
                 "(datastore.cpp:767-776). If this assertion ever flips, "
                 "search will start returning hits that route into a "
                 "tombstoned project.");

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("orphanZeta"));

        // The comment must NOT surface, end-to-end.
        QVERIFY(!hasResultOfTypeWithId(vm.results(),
                                       QStringLiteral("comment"), c->id()));
    }
};

QTEST_GUILESS_MAIN(TestSearchProjectLevelComments)
#include "test_search_project_level_comments.moc"
