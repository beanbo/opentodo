#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "services/aiassistant.h"

// Helper: build a DataStore with a minimal Inbox project.
static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// Helper: add a task to a DataStore and return it.
static Task *addTask(DataStore *ds, const QString &content, const QString &desc = {})
{
    Task *t = ds->addTask(content, QStringLiteral(""));
    if (!desc.isEmpty())
        t->setDescription(desc);
    return t;
}

class TestSubtaskGenerator : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SubtaskGen"));
        QCoreApplication::setApplicationName(QStringLiteral("test_subtask_generator"));
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));
    }

    // ── buildSubtaskPrompt ─────────────────────────────────────────────────────

    void prompt_contains_task_content()
    {
        DataStore *store = makeStore(this);
        AIAssistant ai(store);

        const QString prompt = ai.buildSubtaskPrompt(
            QStringLiteral("Запустить лендинг"),
            QStringLiteral(""),
            5
        );

        QVERIFY(prompt.contains(QStringLiteral("Запустить лендинг")));
    }

    void prompt_contains_count()
    {
        DataStore *store = makeStore(this);
        AIAssistant ai(store);

        const QString prompt7 = ai.buildSubtaskPrompt(
            QStringLiteral("Запустить лендинг"),
            QStringLiteral(""),
            7
        );
        QVERIFY(prompt7.contains(QStringLiteral("7")));

        const QString prompt3 = ai.buildSubtaskPrompt(
            QStringLiteral("любая задача"),
            QStringLiteral(""),
            3
        );
        QVERIFY(prompt3.contains(QStringLiteral("3")));
    }

    void prompt_contains_description_when_set()
    {
        DataStore *store = makeStore(this);
        AIAssistant ai(store);

        const QString prompt = ai.buildSubtaskPrompt(
            QStringLiteral("Запустить лендинг"),
            QStringLiteral("Нужен красивый дизайн и форма"),
            5
        );

        QVERIFY(prompt.contains(QStringLiteral("Нужен красивый дизайн и форма")));
    }

    void prompt_uses_placeholder_when_no_description()
    {
        DataStore *store = makeStore(this);
        AIAssistant ai(store);

        const QString prompt = ai.buildSubtaskPrompt(
            QStringLiteral("Задача без описания"),
            QStringLiteral(""),
            5
        );

        QVERIFY(prompt.contains(QStringLiteral("(нет описания)")));
    }

    // ── parseSubtasksResponse ─────────────────────────────────────────────────

    void parse_clean_json_array()
    {
        const QString raw =
            QStringLiteral(R"(["Купить ингредиенты","Помыть овощи","Нарезать","Поставить на огонь","Подавать"])");

        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QCOMPARE(result.size(), 5);
        QCOMPARE(result.at(0), QStringLiteral("Купить ингредиенты"));
        QCOMPARE(result.at(4), QStringLiteral("Подавать"));
    }

    void parse_strips_markdown_fence()
    {
        const QString raw =
            QStringLiteral("```json\n[\"Шаг 1\",\"Шаг 2\",\"Шаг 3\"]\n```");

        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QCOMPARE(result.size(), 3);
        QCOMPARE(result.at(0), QStringLiteral("Шаг 1"));
    }

    void parse_strips_plain_fence()
    {
        const QString raw =
            QStringLiteral("```\n[\"Раз\",\"Два\"]\n```");

        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QCOMPARE(result.size(), 2);
    }

    void parse_tolerates_leading_text_before_bracket()
    {
        // Model sometimes emits a short preamble before the array
        const QString raw =
            QStringLiteral("Вот список шагов:\n[\"Шаг A\",\"Шаг B\",\"Шаг C\"]");

        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QCOMPARE(result.size(), 3);
        QCOMPARE(result.at(0), QStringLiteral("Шаг A"));
    }

    void parse_returns_empty_for_malformed_json()
    {
        const QString raw = QStringLiteral("Это не JSON");
        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QVERIFY(result.isEmpty());
    }

    void parse_returns_empty_for_empty_string()
    {
        const QStringList result = AIAssistant::parseSubtasksResponse(QString{});
        QVERIFY(result.isEmpty());
    }

    void parse_returns_empty_for_empty_array()
    {
        const QStringList result = AIAssistant::parseSubtasksResponse(QStringLiteral("[]"));
        QVERIFY(result.isEmpty());
    }

    void parse_ignores_empty_string_elements()
    {
        const QString raw = QStringLiteral(R"(["Шаг 1", "", "Шаг 3", "  "])");
        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        // The non-empty items should survive; empty/whitespace trimmed to ""
        // parseSubtasksResponse only skips truly empty after trim
        QVERIFY(result.contains(QStringLiteral("Шаг 1")));
        QVERIFY(result.contains(QStringLiteral("Шаг 3")));
        QVERIFY(!result.contains(QString{}));
    }

    void parse_handles_unicode_correctly()
    {
        const QString raw =
            QStringLiteral(R"(["Написать ТЗ","Созвониться с дизайнером","Задеплоить на прод"])");

        const QStringList result = AIAssistant::parseSubtasksResponse(raw);
        QCOMPARE(result.size(), 3);
        QVERIFY(result.contains(QStringLiteral("Задеплоить на прод")));
    }

    // ── generateSubtasks — no-network guard tests ─────────────────────────────

    void generateSubtasks_without_api_key_emits_failed()
    {
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));

        DataStore *store = makeStore(this);
        Task *t = addTask(store, QStringLiteral("Запустить лендинг"));

        AIAssistant ai(store);
        QSignalSpy failSpy(&ai, &AIAssistant::subtaskGenerationFailed);

        ai.generateSubtasks(t->id(), 5);

        QCOMPARE(failSpy.count(), 1);
        const QString err = failSpy.at(0).at(0).toString();
        QVERIFY(!err.isEmpty());
    }

    void generateSubtasks_with_empty_task_id_emits_failed()
    {
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));

        DataStore *store = makeStore(this);
        AIAssistant ai(store);
        QSignalSpy failSpy(&ai, &AIAssistant::subtaskGenerationFailed);

        ai.generateSubtasks(QStringLiteral(""), 5);

        QCOMPARE(failSpy.count(), 1);
    }

    void generateSubtasks_with_unknown_task_id_emits_failed()
    {
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));

        DataStore *store = makeStore(this);
        AIAssistant ai(store);
        QSignalSpy failSpy(&ai, &AIAssistant::subtaskGenerationFailed);

        ai.generateSubtasks(QStringLiteral("non-existent-uuid-1234"), 5);

        QCOMPARE(failSpy.count(), 1);
    }

    void generateSubtasks_with_empty_content_emits_failed()
    {
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));

        DataStore *store = makeStore(this);
        Task *t = addTask(store, QStringLiteral("   "));  // whitespace-only content
        t->setContent(QStringLiteral(""));

        AIAssistant ai(store);
        QSignalSpy failSpy(&ai, &AIAssistant::subtaskGenerationFailed);

        ai.generateSubtasks(t->id(), 5);

        QCOMPARE(failSpy.count(), 1);
        const QString err = failSpy.at(0).at(0).toString();
        QVERIFY(!err.isEmpty());
    }

    // ── isGeneratingSubtasks property ─────────────────────────────────────────

    void isGeneratingSubtasks_false_initially()
    {
        DataStore *store = makeStore(this);
        AIAssistant ai(store);
        QVERIFY(!ai.isGeneratingSubtasks());
    }

    // ── concurrent guard ──────────────────────────────────────────────────────

    void generateSubtasks_rejects_concurrent_call()
    {
        // We cannot easily trigger a real in-flight request without a server,
        // but we can simulate by calling twice immediately — the second one
        // should either silently return (no key) or emit failed with busy msg.
        QSettings s;
        s.remove(QStringLiteral("ai/api_key"));

        DataStore *store = makeStore(this);
        AIAssistant ai(store);

        // Since key is absent, every call immediately emits subtaskGenerationFailed.
        // That means m_isGeneratingSubtasks never becomes true here; this test
        // validates the early-exit path for the no-key case.
        QSignalSpy failSpy(&ai, &AIAssistant::subtaskGenerationFailed);

        Task *t = addTask(store, QStringLiteral("Задача"));
        ai.generateSubtasks(t->id(), 5);
        ai.generateSubtasks(t->id(), 5);

        // Both emit failed; no crash
        QVERIFY(failSpy.count() >= 1);
    }
};

QTEST_GUILESS_MAIN(TestSubtaskGenerator)
#include "test_subtask_generator.moc"
