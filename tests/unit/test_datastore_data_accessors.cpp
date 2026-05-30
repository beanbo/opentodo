#include <QtTest>
#include <QVariantList>
#include <QVariantMap>
#include <QDate>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

// ---------------------------------------------------------------------------
// Regression tests for the JS-friendly *Data() accessors (night iters 48-54).
//
// Background: a Q_INVOKABLE returning QList<Task*> does NOT convert to a usable
// JS array in Qt 6.11 (from QML: typeof === "object", Array.isArray === false,
// .length === undefined), so every QML aggregation over allTasks()/
// tasksForToday()/etc. silently iterated zero times. The fix added *Data()
// accessors returning QVariantList<QVariantMap> with camelCase keys mirroring
// the Q_PROPERTY names.
//
// These tests pin that contract so a revert (back to QList<Task*>) or a key
// rename (e.g. camelCase → snake_case) is caught:
//   * the methods compile as returning QVariantList (C++ type-checked here),
//   * each element is a QVariantMap carrying the expected camelCase keys,
//   * filtering (deleted-excluded, today, overdue, per-project, search) is right.
// ---------------------------------------------------------------------------

static DataStore *makeStore()
{
    auto *store = new DataStore;
    Project *inbox = store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    return store;
}

static QVariantMap findByContent(const QVariantList &list, const QString &content)
{
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("content")).toString() == content)
            return m;
    }
    return QVariantMap();
}

static bool containsContent(const QVariantList &list, const QString &content)
{
    return !findByContent(list, content).isEmpty();
}

class TestDataStoreDataAccessors : public QObject
{
    Q_OBJECT

private:
    DataStore *store = nullptr;
    QString    workId;

private slots:

    void init()
    {
        store = makeStore();
        Project *work = store->addProject(QStringLiteral("Work"));
        workId = work->id();

        const QString today     = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString yesterday = QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));

        // t1: active, pinned, priority 2, one label
        Task *t1 = store->addTask(QStringLiteral("Task one"), workId);
        t1->setPriority(2);
        t1->setLabels(QStringList{QStringLiteral("urgent")});
        t1->setIsPinned(true);

        // t2: completed
        Task *t2 = store->addTask(QStringLiteral("Task two"), workId);
        t2->setIsCompleted(true);

        // t3: soft-deleted (must be excluded everywhere)
        Task *t3 = store->addTask(QStringLiteral("Deleted task"), workId);
        t3->setIsDeleted(true);

        // t4: due today
        Task *t4 = store->addTask(QStringLiteral("Today task"), workId);
        t4->setDueDate(today);

        // t5: overdue (due yesterday, not completed)
        Task *t5 = store->addTask(QStringLiteral("Overdue task"), workId);
        t5->setDueDate(yesterday);
    }

    void cleanup()
    {
        delete store;
        store = nullptr;
    }

    // ── allTasksData ────────────────────────────────────────────────────────

    void allTasksData_returnsVariantListExcludingDeleted()
    {
        const QVariantList data = store->allTasksData();
        // 5 created, 1 soft-deleted excluded → 4 (Inbox has no tasks).
        QCOMPARE(data.size(), 4);
        QVERIFY(!containsContent(data, QStringLiteral("Deleted task")));
        QVERIFY(containsContent(data, QStringLiteral("Task one")));
    }

    void allTasksData_eachEntryIsMapWithCamelCaseKeys()
    {
        const QVariantList data = store->allTasksData();
        const QVariantMap m = findByContent(data, QStringLiteral("Task one"));
        QVERIFY(!m.isEmpty());
        // The QML aggregations read exactly these camelCase keys — pin them.
        for (const char *key : { "id", "content", "projectId", "sectionId",
                                 "parentId", "priority", "labels", "isCompleted",
                                 "isDeleted", "createdAt", "updatedAt",
                                 "completedAt", "dueDate", "dueDatetime",
                                 "deadline", "isPinned" }) {
            QVERIFY2(m.contains(QString::fromLatin1(key)),
                     QByteArray("missing key: ") + key);
        }
    }

    void allTasksData_valuesRoundTrip()
    {
        const QVariantList data = store->allTasksData();
        const QVariantMap m = findByContent(data, QStringLiteral("Task one"));
        QCOMPARE(m.value(QStringLiteral("projectId")).toString(), workId);
        QCOMPARE(m.value(QStringLiteral("priority")).toInt(), 2);
        QCOMPARE(m.value(QStringLiteral("isCompleted")).toBool(), false);
        QCOMPARE(m.value(QStringLiteral("isPinned")).toBool(), true);
        QCOMPARE(m.value(QStringLiteral("labels")).toStringList(),
                 QStringList{QStringLiteral("urgent")});
    }

    // ── tasksForTodayData / overdueTasksData ─────────────────────────────────

    void tasksForTodayData_matchesTodayOnly()
    {
        const QVariantList data = store->tasksForTodayData();
        QVERIFY(containsContent(data, QStringLiteral("Today task")));
        QVERIFY(!containsContent(data, QStringLiteral("Overdue task")));
        QVERIFY(!containsContent(data, QStringLiteral("Deleted task")));
    }

    void overdueTasksData_matchesOverdueOnly()
    {
        const QVariantList data = store->overdueTasksData();
        QVERIFY(containsContent(data, QStringLiteral("Overdue task")));
        QVERIFY(!containsContent(data, QStringLiteral("Today task")));
        QVERIFY(!containsContent(data, QStringLiteral("Deleted task")));
    }

    // ── tasksForProjectData ──────────────────────────────────────────────────

    void tasksForProjectData_returnsProjectTopLevelTasks()
    {
        const QVariantList data = store->tasksForProjectData(workId);
        // All non-deleted Work tasks are top-level (no subtasks created).
        QCOMPARE(data.size(), 4);
        QVERIFY(containsContent(data, QStringLiteral("Task one")));
        QVERIFY(!containsContent(data, QStringLiteral("Deleted task")));
    }

    void tasksForProjectData_excludesSubtasks()
    {
        // Make "Task two" a subtask of "Task one" — it should drop out of the
        // top-level project list (mirrors tasksForProject filtering).
        const QVariantList all = store->allTasksData();
        const QString parentId = findByContent(all, QStringLiteral("Task one"))
                                     .value(QStringLiteral("id")).toString();
        const QString childId  = findByContent(all, QStringLiteral("Task two"))
                                     .value(QStringLiteral("id")).toString();
        store->taskById(childId)->setParentId(parentId);

        const QVariantList data = store->tasksForProjectData(workId);
        QVERIFY(!containsContent(data, QStringLiteral("Task two")));
        QVERIFY(containsContent(data, QStringLiteral("Task one")));
    }

    // ── searchTasksData ──────────────────────────────────────────────────────

    void searchTasksData_matchesQuery()
    {
        const QVariantList data = store->searchTasksData(QStringLiteral("Overdue"));
        QVERIFY(containsContent(data, QStringLiteral("Overdue task")));
        QVERIFY(!containsContent(data, QStringLiteral("Task one")));
    }

    // ── allProjectsData ──────────────────────────────────────────────────────

    void allProjectsData_returnsMapsWithCamelCaseKeys()
    {
        const QVariantList data = store->allProjectsData();
        QVERIFY(data.size() >= 2); // Inbox + Work

        QVariantMap work;
        for (const QVariant &v : data) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("name")).toString() == QStringLiteral("Work"))
                work = m;
        }
        QVERIFY(!work.isEmpty());
        for (const char *key : { "id", "name", "color", "icon", "parentId",
                                 "isFavorite", "isInbox", "isArchived",
                                 "isDeleted" }) {
            QVERIFY2(work.contains(QString::fromLatin1(key)),
                     QByteArray("missing project key: ") + key);
        }
        QCOMPARE(work.value(QStringLiteral("id")).toString(), workId);
        QCOMPARE(work.value(QStringLiteral("isInbox")).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TestDataStoreDataAccessors)
#include "test_datastore_data_accessors.moc"
