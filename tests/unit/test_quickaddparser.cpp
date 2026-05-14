#include <QtTest>
#include "nlp/quickaddparser.h"

class TestQuickAddParser : public QObject
{
    Q_OBJECT

private slots:

    // 1. Plain text — no special tokens
    void plain_text()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Простая задача"));
        QCOMPARE(r.content, QStringLiteral("Простая задача"));
        QCOMPARE(r.projectName, QString());
        QVERIFY(r.labels.isEmpty());
        QCOMPARE(r.priority, 4);
        QCOMPARE(r.dueString, QString());
    }

    // 2. Project extraction from #token
    void project_extraction()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Купить молоко #Дом"));
        QCOMPARE(r.content, QStringLiteral("Купить молоко"));
        QCOMPARE(r.projectName, QStringLiteral("Дом"));
        QVERIFY(r.labels.isEmpty());
    }

    // 3. Multiple @label tokens
    void multiple_labels()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Позвонить @срочно @работа"));
        QCOMPARE(r.content, QStringLiteral("Позвонить"));
        QVERIFY(r.labels.contains(QStringLiteral("срочно")));
        QVERIFY(r.labels.contains(QStringLiteral("работа")));
        QCOMPARE(r.labels.size(), 2);
    }

    // 4. Priority p1
    void priority_p1()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Отчёт p1"));
        QCOMPARE(r.priority, 1);
        QCOMPARE(r.content, QStringLiteral("Отчёт"));
    }

    // 5. Russian date keyword "завтра"
    void russian_due_tomorrow()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Встреча завтра"));
        QVERIFY(!r.dueString.isEmpty());
        QCOMPARE(r.dueString, QStringLiteral("завтра"));
        QCOMPARE(r.content, QStringLiteral("Встреча"));
    }

    // 6. Full combination: content + date + project + label + priority
    void full_combination()
    {
        const auto r = QuickAddParser::parse(
            QStringLiteral("Купить молоко завтра в 18:00 #Дом @срочно p1"));
        QCOMPARE(r.content, QStringLiteral("Купить молоко"));
        QCOMPARE(r.projectName, QStringLiteral("Дом"));
        QVERIFY(r.labels.contains(QStringLiteral("срочно")));
        QCOMPARE(r.priority, 1);
        QVERIFY(!r.dueString.isEmpty());
        // dueString should contain the date keywords
        QVERIFY(r.dueString.contains(QStringLiteral("завтра")));
    }

    // 7. English recurrence + priority
    void english_recurrence_priority()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Yoga every monday p2"));
        QCOMPARE(r.priority, 2);
        QVERIFY(r.dueString.contains(QStringLiteral("every monday")));
        QCOMPARE(r.content, QStringLiteral("Yoga"));
    }

    // 8. Bang-notation priority (!1)
    void bang_priority()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Hello !1"));
        QCOMPARE(r.priority, 1);
        QCOMPARE(r.content, QStringLiteral("Hello"));
    }

    // 9. Empty input
    void empty_input()
    {
        const auto r = QuickAddParser::parse(QStringLiteral(""));
        QCOMPARE(r.content, QString());
        QCOMPARE(r.projectName, QString());
        QVERIFY(r.labels.isEmpty());
        QCOMPARE(r.priority, 4);
        QCOMPARE(r.dueString, QString());
    }

    // 10. Project only, no content
    void project_only()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("#Только проект"));
        QCOMPARE(r.projectName, QStringLiteral("Только"));
        // "проект" is not a date keyword so it becomes content
        QCOMPARE(r.content, QStringLiteral("проект"));
        QCOMPARE(r.dueString, QString());
    }

    // 11. Label only, no content
    void label_only()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("@только_метка"));
        QCOMPARE(r.content, QString());
        QVERIFY(r.labels.contains(QStringLiteral("только_метка")));
        QCOMPARE(r.dueString, QString());
    }

    // 12. Priority token only
    void priority_only()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("p3"));
        QCOMPARE(r.priority, 3);
        QCOMPARE(r.content, QString());
        QCOMPARE(r.dueString, QString());
    }

    // ── Additional edge-case tests ────────────────────────────────────────

    // 13. English "today" as due
    void english_due_today()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Buy groceries today"));
        QCOMPARE(r.content, QStringLiteral("Buy groceries"));
        QCOMPARE(r.dueString, QStringLiteral("today"));
    }

    // 14. Priority p4 (explicit default)
    void priority_p4_explicit()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Low priority task p4"));
        QCOMPARE(r.priority, 4);
        QCOMPARE(r.content, QStringLiteral("Low priority task"));
    }

    // 15. Mixed Russian content + English date
    void russian_content_english_date()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("Задача tomorrow"));
        QCOMPARE(r.content, QStringLiteral("Задача"));
        QCOMPARE(r.dueString, QStringLiteral("tomorrow"));
    }
};

QTEST_GUILESS_MAIN(TestQuickAddParser)
#include "test_quickaddparser.moc"
