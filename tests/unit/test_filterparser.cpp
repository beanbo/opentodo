#include <QtTest>

#include "filter/filterparser.h"
#include "filter/tokens.h"

// ---------------------------------------------------------------------------
// Helper: parse a string, assert non-null, return the root node.
// Caller owns the returned pointer.
// ---------------------------------------------------------------------------
static AstNode *mustParse(FilterParser &p, const QString &query)
{
    AstNode *ast = p.parse(query);
    if (!ast)
        qWarning() << "parse failed for:" << query << "error:" << p.lastError();
    return ast;
}

class TestFilterParser : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // Single-predicate round-trip: parse returns non-null with correct type
    // -----------------------------------------------------------------------
    void singlePredicates_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("expectedTokenType");

        QTest::newRow("today")            << "today"                   << (int)TokenType::Today;
        QTest::newRow("tomorrow")         << "tomorrow"                << (int)TokenType::Tomorrow;
        QTest::newRow("overdue")          << "overdue"                 << (int)TokenType::Overdue;
        QTest::newRow("no date")          << "no date"                 << (int)TokenType::NoDate;
        QTest::newRow("p1")               << "p1"                      << (int)TokenType::Priority;
        QTest::newRow("p2")               << "p2"                      << (int)TokenType::Priority;
        QTest::newRow("p3")               << "p3"                      << (int)TokenType::Priority;
        QTest::newRow("p4")               << "p4"                      << (int)TokenType::Priority;
        QTest::newRow("#Project")         << "#Project"                << (int)TokenType::Project;
        QTest::newRow("@label")           << "@label"                  << (int)TokenType::Label;
        QTest::newRow("/section")         << "/section"                << (int)TokenType::Section;
        QTest::newRow("search:foo")       << "search: foo"             << (int)TokenType::Search;
        QTest::newRow("5 days")           << "5 days"                  << (int)TokenType::Days;
        QTest::newRow("before date")      << "before 2026-12-01"       << (int)TokenType::Before;
        QTest::newRow("after date")       << "after 2025-01-01"        << (int)TokenType::After;
        QTest::newRow("recurring")        << "recurring"               << (int)TokenType::Recurring;
        QTest::newRow("deadline")         << "deadline"                << (int)TokenType::HasDeadline;
        QTest::newRow("has deadline")     << "has deadline"            << (int)TokenType::HasDeadline;
        QTest::newRow("with deadline")    << "with deadline"           << (int)TokenType::HasDeadline;
        QTest::newRow("дедлайн")          << QString::fromUtf8("дедлайн")           << (int)TokenType::HasDeadline;
        QTest::newRow("с дедлайном")      << QString::fromUtf8("с дедлайном")       << (int)TokenType::HasDeadline;
        QTest::newRow("есть дедлайн")     << QString::fromUtf8("есть дедлайн")      << (int)TokenType::HasDeadline;
        QTest::newRow("completed")        << "completed"               << (int)TokenType::Completed;
        QTest::newRow("created before")   << "created before 2025-01-01" << (int)TokenType::CreatedBefore;
        QTest::newRow("created after")    << "created after 2025-01-01"  << (int)TokenType::CreatedAfter;
    }

    void singlePredicates()
    {
        QFETCH(QString, query);
        QFETCH(int, expectedTokenType);

        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, query));
        QVERIFY2(ast != nullptr, qPrintable(QString("parse returned null for '%1': %2").arg(query, p.lastError())));

        // Root must be a Predicate node
        QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->token.type, expectedTokenType);
    }

    // -----------------------------------------------------------------------
    // Priority values stored in token.value
    // -----------------------------------------------------------------------
    void priorityValues_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("expectedPriority");

        QTest::newRow("p1 value") << "p1" << 1;
        QTest::newRow("p2 value") << "p2" << 2;
        QTest::newRow("p3 value") << "p3" << 3;
        QTest::newRow("p4 value") << "p4" << 4;
    }

    void priorityValues()
    {
        QFETCH(QString, query);
        QFETCH(int, expectedPriority);

        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, query));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.value.toInt(), expectedPriority);
    }

    // -----------------------------------------------------------------------
    // N days value
    // -----------------------------------------------------------------------
    void daysValue()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "7 days"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.value.toInt(), 7);
    }

    // -----------------------------------------------------------------------
    // Operator nodes
    // -----------------------------------------------------------------------
    void andOperator()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today & p1"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::And);
        QCOMPARE(ast->children.size(), 2);
        QCOMPARE((int)ast->children[0]->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->children[0]->token.type, (int)TokenType::Today);
        QCOMPARE((int)ast->children[1]->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->children[1]->token.type, (int)TokenType::Priority);
    }

    void orOperator()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today | p1"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Or);
        QCOMPARE(ast->children.size(), 2);
    }

    void notOperator()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "!today"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Not);
        QCOMPARE(ast->children.size(), 1);
        QCOMPARE((int)ast->children[0]->token.type, (int)TokenType::Today);
    }

    // comma acts as section separator (evaluated as OR)
    void commaOperator()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today, p1"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::SectionSeparator);
        QCOMPARE(ast->children.size(), 2);
    }

    // -----------------------------------------------------------------------
    // Parentheses grouping
    // -----------------------------------------------------------------------
    void parenGrouping()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "(today | tomorrow) & p1"));
        QVERIFY(ast != nullptr);
        // Root should be AND
        QCOMPARE((int)ast->type, (int)AstNodeType::And);
        // Left child should be OR
        QCOMPARE((int)ast->children[0]->type, (int)AstNodeType::Or);
        // Right child should be Priority predicate
        QCOMPARE((int)ast->children[1]->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->children[1]->token.type, (int)TokenType::Priority);
    }

    // -----------------------------------------------------------------------
    // Operator precedence: AND binds tighter than OR
    // "today | p1 & overdue" => "today | (p1 & overdue)"
    // -----------------------------------------------------------------------
    void operatorPrecedence_andBindsTighterThanOr()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today | p1 & overdue"));
        QVERIFY(ast != nullptr);
        // Root: Or
        QCOMPARE((int)ast->type, (int)AstNodeType::Or);
        // Left: today predicate
        QCOMPARE((int)ast->children[0]->token.type, (int)TokenType::Today);
        // Right: And(p1, overdue)
        QCOMPARE((int)ast->children[1]->type, (int)AstNodeType::And);
        QCOMPARE((int)ast->children[1]->children[0]->token.type, (int)TokenType::Priority);
        QCOMPARE((int)ast->children[1]->children[1]->token.type, (int)TokenType::Overdue);
    }

    // -----------------------------------------------------------------------
    // isValid() convenience method
    // -----------------------------------------------------------------------
    void isValid_validQueries()
    {
        FilterParser p;
        QVERIFY(p.isValid("today"));
        QVERIFY(p.isValid("today & p1"));
        QVERIFY(p.isValid("(today | tomorrow) & p1"));
        QVERIFY(p.isValid("!overdue"));
        QVERIFY(p.isValid("#Inbox | @work"));
    }

    // -----------------------------------------------------------------------
    // Invalid / edge-case inputs must return nullptr
    // -----------------------------------------------------------------------
    void invalidInputs_data()
    {
        QTest::addColumn<QString>("query");

        QTest::newRow("empty string")      << "";
        QTest::newRow("only spaces")       << "   ";
        QTest::newRow("unclosed paren")    << "(today";
        // Leading binary operator with nothing on the left
        QTest::newRow("leading &")         << "& today";
    }

    void invalidInputs()
    {
        QFETCH(QString, query);
        FilterParser p;
        AstNode *ast = p.parse(query);
        QVERIFY2(ast == nullptr,
            qPrintable(QString("Expected null for invalid query '%1' but got a node").arg(query)));
        delete ast; // safe: null-delete is a no-op
    }

    // -----------------------------------------------------------------------
    // Text values stored in token.text
    // -----------------------------------------------------------------------
    void projectTokenText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "#MyProject"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.text, QStringLiteral("MyProject"));
    }

    void labelTokenText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "@urgent"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.text, QStringLiteral("urgent"));
    }

    void sectionTokenText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "/backlog"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.text, QStringLiteral("backlog"));
    }

    void beforeDateText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "before 2026-12-01"));
        QVERIFY(ast != nullptr);
        // token.text holds the date value for Before/After tokens
        QCOMPARE(ast->token.text, QStringLiteral("2026-12-01"));
    }

    void afterDateText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "after 2025-01-01"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.text, QStringLiteral("2025-01-01"));
    }

    void createdBeforeDateText()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "created before 2025-01-01"));
        QVERIFY(ast != nullptr);
        QCOMPARE(ast->token.text, QStringLiteral("2025-01-01"));
    }

    // -----------------------------------------------------------------------
    // Chained operators
    // -----------------------------------------------------------------------
    void tripleAnd()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today & p1 & overdue"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::And);
    }

    void tripleOr()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "today | tomorrow | overdue"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Or);
    }

    // -----------------------------------------------------------------------
    // deadline predicate: parsing and negation
    // -----------------------------------------------------------------------
    void deadline_singleWord()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "deadline"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->token.type, (int)TokenType::HasDeadline);
    }

    void deadline_russianMultiWord()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, QString::fromUtf8("с дедлайном")));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->token.type, (int)TokenType::HasDeadline);
    }

    void deadline_negation()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "!deadline"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Not);
        QCOMPARE(ast->children.size(), 1);
        QCOMPARE((int)ast->children[0]->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)ast->children[0]->token.type, (int)TokenType::HasDeadline);
    }

    void deadline_caseInsensitive()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "DEADLINE"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->token.type, (int)TokenType::HasDeadline);

        std::unique_ptr<AstNode> ast2(mustParse(p, "Has Deadline"));
        QVERIFY(ast2 != nullptr);
        QCOMPARE((int)ast2->token.type, (int)TokenType::HasDeadline);
    }

    // -----------------------------------------------------------------------
    // Double negation parses correctly
    // -----------------------------------------------------------------------
    void doubleNot()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(mustParse(p, "!!today"));
        QVERIFY(ast != nullptr);
        QCOMPARE((int)ast->type, (int)AstNodeType::Not);
        QCOMPARE((int)ast->children[0]->type, (int)AstNodeType::Not);
    }
};

QTEST_GUILESS_MAIN(TestFilterParser)
#include "test_filterparser.moc"
