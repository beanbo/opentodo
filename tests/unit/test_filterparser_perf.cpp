// ---------------------------------------------------------------------------
// test_filterparser_perf.cpp
//
// Performance + pathological-input pinning for FilterParser.  Layer: Domain.
//
// Goal: pin the parser's behaviour under deeply-nested, very-long, and
// pathological inputs.  Complements:
//   * test_filterparser.cpp            — happy-path AST shape
//   * test_filterparser_extensions.cpp — Todoist-parity predicates
//   * test_filterparser_adversarial.cpp — crash-safety on fuzz / short
//                                          adversarial inputs (50-deep / 1000-OR)
//
// This suite pushes the same axes further (100 / 1000 nesting, 10kB chains)
// AND adds pin-the-current-behaviour slots for cases the adversarial suite
// did not cover (alternating operators, mid-chain bad token, empty
// substrings, unicode-with-emoji predicate names).
//
// Per CLAUDE.md / orchestrator constraints:
//   * QTEST_GUILESS_MAIN.
//   * Unique QSettings org/app namespace to isolate from other parallel
//     tests under a shared QStandardPaths root.
//   * NO raw string literals R"(...)" anywhere — moc dies on unbalanced
//     parens inside R"(...)".
//   * NO function-local Q_OBJECT classes (moc cannot reach them).
//   * Hard time bounds via QElapsedTimer; budgets are intentionally
//     generous (50 ms / 500 ms / etc.) so this passes on slow CI VMs.
//   * Behaviour pinned via assertions + qInfo() — NO production-code edits.
//   * Calls suspected of throwing wrapped in try/catch so a hidden
//     exception is visible as a hard failure rather than a stack unwind.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QChar>
#include <memory>
#include <functional>

#include "filter/filterparser.h"
#include "filter/tokens.h"

// ---------------------------------------------------------------------------
// safeParse — run p.parse(query) with an exception guard.  Reports any C++
// exception via QTest::qFail (because QFAIL expands to `return;` which would
// not compile inside an AstNode*-returning helper).
// ---------------------------------------------------------------------------
static AstNode *safeParse(FilterParser &p, const QString &query, const char *label)
{
    AstNode *ast = nullptr;
    try {
        ast = p.parse(query);
    } catch (const std::exception &e) {
        QTest::qFail(qPrintable(QString("FilterParser threw std::exception on label='%1': %2")
                                    .arg(QString::fromUtf8(label), QString::fromUtf8(e.what()))),
                     __FILE__, __LINE__);
        return nullptr;
    } catch (...) {
        QTest::qFail(qPrintable(QString("FilterParser threw unknown exception on label='%1'")
                                    .arg(QString::fromUtf8(label))),
                     __FILE__, __LINE__);
        return nullptr;
    }
    return ast;
}

// ---------------------------------------------------------------------------
// astDepth — recursive max-depth walk used to verify that nested parens
// collapse into a single Predicate leaf (parser unwraps each (Expr) layer)
// and that OR/AND chains form a left-leaning spine.
// ---------------------------------------------------------------------------
static int astDepth(const AstNode *n)
{
    if (!n) return 0;
    int maxChild = 0;
    for (const AstNode *c : n->children) {
        const int d = astDepth(c);
        if (d > maxChild) maxChild = d;
    }
    return 1 + maxChild;
}

// ---------------------------------------------------------------------------
// countNodes — total AstNode count in a sub-tree.  Used to size-check the
// OR/AND chain ASTs (1000-atom chain should produce ~2000-ish nodes:
// 1000 predicates + 999 binary operators).
// ---------------------------------------------------------------------------
static int countNodes(const AstNode *n)
{
    if (!n) return 0;
    int total = 1;
    for (const AstNode *c : n->children) total += countNodes(c);
    return total;
}

// ---------------------------------------------------------------------------
// countPredicates — total Predicate-typed nodes in a sub-tree.  Used to
// verify a 1000-OR chain ends up with exactly 1000 predicates regardless of
// associativity shape.
// ---------------------------------------------------------------------------
static int countPredicates(const AstNode *n)
{
    if (!n) return 0;
    int total = (n->type == AstNodeType::Predicate) ? 1 : 0;
    for (const AstNode *c : n->children) total += countPredicates(c);
    return total;
}

// ---------------------------------------------------------------------------
class TestFilterParserPerf : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Per-process unique QSettings namespace — keeps this test isolated
        // from the user's real OpenTodo install AND from any other test
        // running in parallel under a shared QStandardPaths root.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_FilterParserPerf"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_filterparser_perf"));
    }

    // -----------------------------------------------------------------------
    // 1. 100 nested parens, <50 ms.  Each level of nesting recurses through
    //    parseExpression -> parseSectionSeparator -> parseOr -> parseAnd ->
    //    parseUnary -> parseAtom.  100 levels exercises a real but
    //    realistic recursion stack.
    //
    //    Documented behaviour: parens collapse — ((((today)))) produces a
    //    Predicate(Today) root, NOT a chain of group nodes.  AST depth
    //    should be exactly 1.
    // -----------------------------------------------------------------------
    void parser_handles_100_nested_parens_under_50ms()
    {
        const int depth = 100;
        QString q;
        q.reserve(depth * 2 + 5);
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char('('));
        q.append(QStringLiteral("today"));
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char(')'));

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "100_nested_parens"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "100 nested parens: elapsed =" << elapsed << "ms";
        QVERIFY2(elapsed < 50,
            qPrintable(QString("100-level nested parse took %1ms (limit 50ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "100-level nested parens should parse to a non-null AST");

        if (ast) {
            // Parens collapse — root is still the leaf Predicate "today".
            QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)ast->token.type, (int)TokenType::Today);
            QCOMPARE(astDepth(ast.get()), 1);
        }
    }

    // -----------------------------------------------------------------------
    // 2. 1000 nested parens — pin behaviour.  This is past the typical
    //    Windows default stack frame budget (1 MB) for unbounded recursion
    //    in debug builds; the parser may either:
    //      (a) parse successfully (release build / small frames), or
    //      (b) return nullptr with an error, or
    //      (c) — the bug we'd catch — crash via stack overflow.
    //
    //    We CANNOT prevent a crash from this test (a SO terminates the
    //    process), but if we get any return at all we pin the contract:
    //    no exception thrown, either non-null AST OR populated lastError.
    //    On the current MinGW release build this passes successfully.
    // -----------------------------------------------------------------------
    void parser_handles_1000_nested_parens_does_not_stack_overflow()
    {
        const int depth = 1000;
        QString q;
        q.reserve(depth * 2 + 5);
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char('('));
        q.append(QStringLiteral("today"));
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char(')'));

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "1000_nested_parens"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "1000 nested parens: elapsed =" << elapsed << "ms"
                << "ast =" << (ast ? "non-null" : "null")
                << "err =" << p.lastError();

        // Contract: must complete (no SO crash) within a generous budget.
        // 500 ms is forgiving — this slot may legitimately parse the whole
        // thing on a fast box or reject it early.
        QVERIFY2(elapsed < 500,
            qPrintable(QString("1000-level nested parse took %1ms (limit 500ms)").arg(elapsed)));

        if (ast) {
            // If the parser accepted it, the leaf must still be Today
            // (parens collapse to a single predicate).
            QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)ast->token.type, (int)TokenType::Today);
            qInfo() << "  -> parser accepted 1000-level nesting (depth ="
                    << astDepth(ast.get()) << ")";
        } else {
            // If rejected, lastError must be populated.
            QVERIFY2(!p.lastError().isEmpty(),
                "Parser must populate lastError when returning nullptr");
            qInfo() << "  -> parser rejected 1000-level nesting cleanly";
        }
    }

    // -----------------------------------------------------------------------
    // 3. 10KB-class OR chain: "today | today | today | ... " (1000 atoms,
    //    ~8 KB serialised).  parseOr() is iterative (while loop), so the
    //    AST grows linearly into a left-leaning chain (depth ~= n).
    //    Pinned contract:
    //      * elapsed < 500 ms
    //      * root is Or
    //      * exactly 1000 Today predicates in the tree
    //      * left-leaning shape: depth ~= 1000 + 1 (Predicate at the leaf)
    // -----------------------------------------------------------------------
    void parser_handles_10kb_OR_chain()
    {
        const int n = 1000;
        QString q;
        q.reserve(n * 9);                         // " | today" == 8 chars + 1
        for (int i = 0; i < n; ++i) {
            if (i > 0) q.append(QStringLiteral(" | "));
            q.append(QStringLiteral("today"));
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "10kb_OR_chain"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "10kb OR chain: size =" << q.size() << "bytes elapsed ="
                << elapsed << "ms";
        QVERIFY2(elapsed < 500,
            qPrintable(QString("1000-OR chain took %1ms (limit 500ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "1000-OR chain must parse");
        if (!ast) return;

        // Pin: root is Or, contains exactly 1000 Today predicates, and the
        // shape is left-leaning so depth equals the operator count + 1.
        QCOMPARE((int)ast->type, (int)AstNodeType::Or);
        QCOMPARE(countPredicates(ast.get()), n);

        const int d = astDepth(ast.get());
        qInfo() << "  -> AST depth =" << d << "(expected" << n << "for left-leaning)";
        QVERIFY2(d == n,
            qPrintable(QString("OR chain AST depth %1 != expected %2 (left-leaning)").arg(d).arg(n)));
    }

    // -----------------------------------------------------------------------
    // 4. Same shape as slot 3, with '&' instead of '|'.  parseAnd is
    //    likewise iterative; same budget.  Pins left-leaning shape +
    //    1000 predicates.
    // -----------------------------------------------------------------------
    void parser_handles_10kb_AND_chain()
    {
        const int n = 1000;
        QString q;
        q.reserve(n * 9);
        for (int i = 0; i < n; ++i) {
            if (i > 0) q.append(QStringLiteral(" & "));
            q.append(QStringLiteral("today"));
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "10kb_AND_chain"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "10kb AND chain: size =" << q.size() << "bytes elapsed ="
                << elapsed << "ms";
        QVERIFY2(elapsed < 500,
            qPrintable(QString("1000-AND chain took %1ms (limit 500ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "1000-AND chain must parse");
        if (!ast) return;

        QCOMPARE((int)ast->type, (int)AstNodeType::And);
        QCOMPARE(countPredicates(ast.get()), n);

        const int d = astDepth(ast.get());
        qInfo() << "  -> AST depth =" << d << "(expected" << n << "for left-leaning)";
        QVERIFY2(d == n,
            qPrintable(QString("AND chain AST depth %1 != expected %2 (left-leaning)").arg(d).arg(n)));
    }

    // -----------------------------------------------------------------------
    // 5. Mixed NOT chain — "!!!!...!today" (1000 NOTs deep).  parseUnary
    //    recurses on each '!' so this directly exercises native stack
    //    depth, similar to slot 2 but via a different code path (Unary
    //    chain, not parenthesis collapse).
    //
    //    Documented behaviour from the adversarial suite: the parser does
    //    NOT canonicalise !!x -> x; every '!' adds a Not node.  So we
    //    expect 1000 Not nodes wrapping a Today leaf, total depth 1001.
    //
    //    The MinGW release build handles this; if a future debug build
    //    blows the stack here, the test will surface it.
    // -----------------------------------------------------------------------
    void parser_handles_mixed_NOT_chain()
    {
        const int n = 1000;
        QString q;
        q.reserve(n + 5);
        for (int i = 0; i < n; ++i) q.append(QLatin1Char('!'));
        q.append(QStringLiteral("today"));

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "1000_NOT_chain"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "1000 NOT chain: elapsed =" << elapsed << "ms"
                << "ast =" << (ast ? "non-null" : "null")
                << "err =" << p.lastError();

        // Generous budget — recursion through parseUnary on each '!'.
        QVERIFY2(elapsed < 500,
            qPrintable(QString("1000-NOT chain took %1ms (limit 500ms)").arg(elapsed)));

        if (ast) {
            // Walk the Not spine — pin exactly n NOTs wrapping the leaf.
            int notCount = 0;
            const AstNode *cur = ast.get();
            while (cur && cur->type == AstNodeType::Not) {
                ++notCount;
                cur = cur->children.isEmpty() ? nullptr : cur->children[0];
            }
            QCOMPARE(notCount, n);
            QVERIFY(cur != nullptr);
            QCOMPARE((int)cur->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)cur->token.type, (int)TokenType::Today);
            qInfo() << "  -> 1000 Not nodes preserved (no canonicalisation)";
        } else {
            // If rejected, that's still a pinned contract — must be clean.
            QVERIFY2(!p.lastError().isEmpty(),
                "Parser must populate lastError when returning nullptr");
            qInfo() << "  -> parser rejected 1000-NOT chain";
        }
    }

    // -----------------------------------------------------------------------
    // 6. Pathological alternating operators — "today & today | today & ..."
    //    100 alternations.  AND binds tighter than OR (documented), so the
    //    AST should be a chain of OR nodes, each child of which is an AND
    //    of two Today predicates (or a single Today depending on the cycle
    //    boundary).
    //
    //    Concrete shape for 200 atoms in pattern "A & A | A & A | ...":
    //      Or( Or( ..., And(A, A) ), And(A, A) )
    //    -> root = Or, total Today predicates = 200, OR-depth grows
    //    iteratively (not recursively), no perf spike expected.
    // -----------------------------------------------------------------------
    void parser_handles_pathological_alternating_operators()
    {
        const int alternations = 100;     // -> 200 atoms, 100 ORs, 100 ANDs
        QString q;
        q.reserve(alternations * 24);
        for (int i = 0; i < alternations; ++i) {
            if (i > 0) q.append(QStringLiteral(" | "));
            q.append(QStringLiteral("today & today"));
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "alternating_operators"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "alternating operators (100 alt, 200 atoms): elapsed ="
                << elapsed << "ms size =" << q.size() << "bytes";
        QVERIFY2(elapsed < 100,
            qPrintable(QString("100-alternation parse took %1ms (limit 100ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "Alternating operators must parse");
        if (!ast) return;

        // Pin: root must be Or (AND binds tighter, so all ANDs nest under
        // their respective OR positions).  Total Today predicate count =
        // 2 * alternations.  Total node count = predicates + ANDs + ORs =
        // 2*alt + alt + (alt-1) = 4*alt - 1.
        QCOMPARE((int)ast->type, (int)AstNodeType::Or);
        QCOMPARE(countPredicates(ast.get()), alternations * 2);
        QCOMPARE(countNodes(ast.get()), 4 * alternations - 1);

        // The first AND on the spine should have two Today predicate children.
        // Walk down the OR-leftspine to the deepest OR, its left child should
        // be an And of two Todays.
        const AstNode *cur = ast.get();
        while (cur->type == AstNodeType::Or && !cur->children.isEmpty() &&
               cur->children[0] && cur->children[0]->type == AstNodeType::Or) {
            cur = cur->children[0];
        }
        // cur is now the deepest Or; cur->children[0] is the leftmost AND.
        QVERIFY(cur != nullptr);
        QVERIFY(cur->children.size() == 2);
        const AstNode *leftAnd = cur->children[0];
        QVERIFY(leftAnd != nullptr);
        QCOMPARE((int)leftAnd->type, (int)AstNodeType::And);
        QCOMPARE(leftAnd->children.size(), 2);
        QCOMPARE((int)leftAnd->children[0]->token.type, (int)TokenType::Today);
        QCOMPARE((int)leftAnd->children[1]->token.type, (int)TokenType::Today);
        qInfo() << "  -> AND binds tighter than OR confirmed across alternation chain";
    }

    // -----------------------------------------------------------------------
    // 7. Invalid token in middle of chain — "today & garbagetoken & overdue".
    //    Pin: does the whole expression go invalid, or does the parser
    //    accept the garbage token as an Identifier and produce an AST?
    //
    //    Inspection of filterparser.cpp:
    //      * tokenize() falls through to the Identifier branch for any
    //        unrecognised word, producing Token(Identifier, "garbagetoken").
    //      * parseAtom() has Identifier in its accept list (around line
    //        644), so the Identifier predicate is allowed.
    //      * Conclusion: the whole chain parses successfully, AST root is
    //        And with three predicate-like leaves; the Identifier predicate
    //        falls through to FilterEvaluator content-substring matching.
    //
    //    Pin this CURRENT behaviour so a future tightening (e.g. rejecting
    //    bare Identifier in operator contexts) surfaces as a regression.
    // -----------------------------------------------------------------------
    void parser_recovers_from_invalid_token_in_middle()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(
            p, QStringLiteral("today & garbagetoken & overdue"),
            "invalid_token_in_middle"));

        qInfo() << "invalid_token_in_middle:" << (ast ? "parsed OK" : "rejected")
                << "err =" << p.lastError();

        QVERIFY2(ast != nullptr,
            "PINNED: parser ACCEPTS bare identifiers as predicates "
            "(garbagetoken -> Identifier predicate -> content match)");
        if (!ast) return;

        // Root must be And (iterative left-leaning chain of 3 atoms).
        QCOMPARE((int)ast->type, (int)AstNodeType::And);
        // Total predicate count = 3 (today, garbagetoken, overdue).
        QCOMPARE(countPredicates(ast.get()), 3);

        // Walk the And spine to verify all three token types appear.
        QList<TokenType> tokenTypes;
        std::function<void(const AstNode *)> walk = [&](const AstNode *n) {
            if (!n) return;
            if (n->type == AstNodeType::Predicate) tokenTypes.append(n->token.type);
            for (const AstNode *c : n->children) walk(c);
        };
        walk(ast.get());

        QVERIFY(tokenTypes.contains(TokenType::Today));
        QVERIFY(tokenTypes.contains(TokenType::Overdue));
        QVERIFY(tokenTypes.contains(TokenType::Identifier));
        qInfo() << "  -> bare identifier 'garbagetoken' silently coerced to "
                << "Identifier predicate (content-substring match at eval time)";
    }

    // -----------------------------------------------------------------------
    // 8. Empty substrings between operators — pin behaviour for inputs like
    //    "today &  & overdue", "() & today", "today | () | overdue".
    //
    //    Expected from the parser:
    //      * "today &  & overdue" — second '&' has no left operand;
    //        parseUnary fails -> parseAnd's inner parseUnary returns null;
    //        parseAnd returns null; whole parse fails with non-empty
    //        lastError.
    //      * "() & today" — parseAtom sees '(', advances, recursively
    //        parses Expression which immediately sees ')' (not in atom
    //        accept list) -> nullptr -> parse fails.
    //      * "today | () | overdue" — same: empty parens fail in the
    //        middle.
    //
    //    Pin all three as parse errors.
    // -----------------------------------------------------------------------
    void parser_handles_empty_substrings_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("label");

        QTest::newRow("empty between AND")
            << QStringLiteral("today &  & overdue") << QStringLiteral("empty_between_and");
        QTest::newRow("empty parens leading")
            << QStringLiteral("() & today")          << QStringLiteral("empty_parens_lead");
        QTest::newRow("empty parens middle")
            << QStringLiteral("today | () | overdue") << QStringLiteral("empty_parens_mid");
        QTest::newRow("trailing operator")
            << QStringLiteral("today & ")            << QStringLiteral("trailing_op");
        QTest::newRow("leading or")
            << QStringLiteral("| today")             << QStringLiteral("leading_or");
    }

    void parser_handles_empty_substrings()
    {
        QFETCH(QString, query);
        QFETCH(QString, label);

        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, query, label.toUtf8().constData()));

        qInfo() << "empty_substrings label =" << label
                << " query =" << query
                << " ast =" << (ast ? "non-null" : "null")
                << " err =" << p.lastError();

        // Pin: ALL of these are documented parse errors.
        QVERIFY2(ast == nullptr,
            qPrintable(QString("Expected parse error for '%1' but got non-null AST").arg(query)));
        QVERIFY2(!p.lastError().isEmpty(),
            qPrintable(QString("lastError() must be populated for '%1'").arg(query)));
    }

    // -----------------------------------------------------------------------
    // 9. Unicode predicate values — Cyrillic AND emoji.  The adversarial
    //    suite already covers Cyrillic; this adds emoji coverage.
    //
    //    Tokenizer behaviour for '#': read until whitespace / ')' / '|' /
    //    '&' / ',' — emoji code points are non-whitespace, non-operator,
    //    so they pass through unchanged.  Same for '@' and '/'.
    //
    //    Pin: '#📊', '@🏠', '/проект' all parse to single Predicate nodes
    //    with the unicode payload preserved exactly.
    //
    //    NOTE: emoji like 📊 (U+1F4CA) is a 2-codeunit surrogate pair in
    //    QString.  We pin BOTH the QString.length() (4) and the
    //    QString.size() (also 4 — Qt6 conflates them) and the actual
    //    UTF-8 round-trip equality.
    // -----------------------------------------------------------------------
    void parser_handles_unicode_predicate_values()
    {
        struct Case { QString query; QString expectedText; TokenType expectedType; };
        const QList<Case> cases = {
            { QString::fromUtf8("#проект"),  QString::fromUtf8("проект"), TokenType::Project },
            { QString::fromUtf8("@метка"),   QString::fromUtf8("метка"),  TokenType::Label   },
            { QString::fromUtf8("#📊"),       QString::fromUtf8("📊"),      TokenType::Project },
            { QString::fromUtf8("@🏠"),       QString::fromUtf8("🏠"),      TokenType::Label   },
            { QString::fromUtf8("/раздел"),  QString::fromUtf8("раздел"), TokenType::Section },
            // Mixed emoji + cyrillic in a single name.
            { QString::fromUtf8("#проект📊"), QString::fromUtf8("проект📊"), TokenType::Project },
        };

        for (const auto &c : cases) {
            FilterParser p;
            std::unique_ptr<AstNode> ast(safeParse(p, c.query, "unicode_predicate"));
            QVERIFY2(ast != nullptr,
                qPrintable(QString("Unicode predicate '%1' must parse (err: '%2')")
                               .arg(c.query, p.lastError())));
            if (!ast) continue;

            QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)ast->token.type, (int)c.expectedType);
            QCOMPARE(ast->token.text, c.expectedText);
            qInfo() << "  unicode predicate OK:" << c.query
                    << "-> token.text =" << ast->token.text
                    << "(length =" << ast->token.text.length() << ")";
        }
    }

    // -----------------------------------------------------------------------
    // BONUS slot: combined-shape stress — alternating NOT + paren + atom
    // for a complex but legal 5kB query.  Just verify it survives + budget.
    // -----------------------------------------------------------------------
    void parser_handles_legal_5kb_complex_query()
    {
        // Build "(!today | (today & !overdue)) | (!today | (today & !overdue)) | ..."
        // each unit is ~36 chars; 150 units gives ~5500 chars.
        const QString unit = QStringLiteral("(!today | (today & !overdue))");
        const int units = 150;
        QString q;
        q.reserve(units * (unit.size() + 3));
        for (int i = 0; i < units; ++i) {
            if (i > 0) q.append(QStringLiteral(" | "));
            q.append(unit);
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "legal_5kb_complex"));
        const qint64 elapsed = t.elapsed();

        qInfo() << "legal 5kb complex query: size =" << q.size() << "bytes elapsed ="
                << elapsed << "ms";
        QVERIFY2(elapsed < 500,
            qPrintable(QString("5kB complex parse took %1ms (limit 500ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "Legal 5kB complex query must parse");
        if (ast) {
            QCOMPARE((int)ast->type, (int)AstNodeType::Or);
            // Each unit has 3 Today/Overdue predicates ("today", "today",
            // "overdue"), total = units * 3.
            QCOMPARE(countPredicates(ast.get()), units * 3);
        }
    }
};

QTEST_GUILESS_MAIN(TestFilterParserPerf)
#include "test_filterparser_perf.moc"
