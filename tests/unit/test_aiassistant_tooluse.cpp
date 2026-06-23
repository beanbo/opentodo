// AI assistant tool-use: action extraction from a response + confirmed
// execution against the DataStore. No network — exercises the parse + dispatch
// plumbing directly (the live LLM round-trip is out of scope for a unit test).

#include <QtTest>
#include <QSignalSpy>

#include "services/aiassistant.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

class TestAIAssistantToolUse : public QObject
{
    Q_OBJECT

    static Task *findByContent(DataStore &s, const QString &c)
    {
        for (Task *t : s.allTasks())
            if (t && t->content() == c)
                return t;
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_AIToolUse"));
        QCoreApplication::setApplicationName(QStringLiteral("test_aiassistant_tooluse"));
    }

    void extractActionJson_fenced()
    {
        const QString r = QStringLiteral(
            "Конечно, добавлю!\n```action\n"
            "{\"op\":\"add_task\",\"content\":\"Купить хлеб\",\"priority\":2}\n```");
        const QString j = AIAssistant::extractActionJson(r);
        QVERIFY(j.contains(QStringLiteral("add_task")));
        QVERIFY(j.contains(QStringLiteral("Купить хлеб")));
    }

    void extractActionJson_none()
    {
        QCOMPARE(AIAssistant::extractActionJson(QStringLiteral("Просто совет, без действий.")),
                 QString());
    }

    void extractActionJson_bareTrailingObject()
    {
        const QString r = QStringLiteral("Готово. {\"op\":\"complete_task\",\"content\":\"X\"}");
        QVERIFY(AIAssistant::extractActionJson(r).contains(QStringLiteral("complete_task")));
    }

    void stripActionBlock_removesFence()
    {
        const QString r = QStringLiteral(
            "Текст ответа.\n```action\n{\"op\":\"add_task\",\"content\":\"Y\"}\n```");
        const QString s = AIAssistant::stripActionBlock(r);
        QVERIFY(!s.contains(QStringLiteral("```")));
        QVERIFY(s.contains(QStringLiteral("Текст ответа")));
    }

    void confirmAction_addTask_createsTaskWithPriorityAndProject()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Работа"));
        AIAssistant ai(&store);
        QSignalSpy done(&ai, &AIAssistant::actionExecuted);

        const int before = store.taskCount();
        QVERIFY(ai.confirmAction(QStringLiteral(
            "{\"op\":\"add_task\",\"content\":\"Новая задача\",\"priority\":1,\"project\":\"Работа\"}")));
        QCOMPARE(store.taskCount(), before + 1);
        QCOMPARE(done.count(), 1);

        Task *t = findByContent(store, QStringLiteral("Новая задача"));
        QVERIFY(t);
        QCOMPARE(t->priority(), 1);
        QCOMPARE(t->projectId(), p->id());
    }

    void confirmAction_completeTask()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("Помыть посуду"), p->id());
        AIAssistant ai(&store);
        QVERIFY(ai.confirmAction(QStringLiteral(
            "{\"op\":\"complete_task\",\"content\":\"Помыть посуду\"}")));
        QVERIFY(t->isCompleted());
    }

    void confirmAction_setPriority()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("Задача"), p->id());
        AIAssistant ai(&store);
        QVERIFY(ai.confirmAction(QStringLiteral(
            "{\"op\":\"set_priority\",\"content\":\"Задача\",\"priority\":1}")));
        QCOMPARE(t->priority(), 1);
    }

    void confirmAction_deleteTask()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("P"));
        Task *t = store.addTask(QStringLiteral("Удалить меня"), p->id());
        AIAssistant ai(&store);
        QVERIFY(ai.confirmAction(QStringLiteral(
            "{\"op\":\"delete_task\",\"content\":\"Удалить меня\"}")));
        QVERIFY(t->isDeleted());
    }

    void confirmAction_unknownTask_failsAndEmitsError()
    {
        DataStore store;
        AIAssistant ai(&store);
        QSignalSpy err(&ai, &AIAssistant::errorReceived);
        QVERIFY(!ai.confirmAction(QStringLiteral(
            "{\"op\":\"complete_task\",\"content\":\"Несуществующая\"}")));
        QCOMPARE(err.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestAIAssistantToolUse)
#include "test_aiassistant_tooluse.moc"
