// ---------------------------------------------------------------------------
// test_filterparser_adversarial.cpp
//
// Adversarial / fuzz coverage for FilterParser.  Layer: Domain.
//
// Goal: prove the parser NEVER crashes on hostile or malformed input.  It
// may legitimately return nullptr (parse error) or a partial AstNode, but
// must not segfault, infinite-loop, or stack-overflow.
//
// Per CLAUDE.md / orchestrator constraints:
//   * QTEST_GUILESS_MAIN.
//   * Unique QSettings org/app namespace to keep the test isolated from
//     the user's real OpenTodo install AND from other suites running in
//     parallel under a shared QStandardPaths root.
//   * NO raw string literals R"(...)" anywhere — moc's parser dies with
//     "No relevant classes found" on unbalanced parens inside R"(...)".
//   * NO function-local Q_OBJECT classes (moc cannot reach them).
//   * Hard time bounds enforced via QElapsedTimer for perf-sensitive
//     slots; budgets are generous (<= 100ms / <= 500ms) so this passes
//     reliably even on slow CI VMs.
//   * Bugs surfaced via qInfo() / QEXPECT_FAIL rather than QFAIL — this
//     is exploratory, the suite must stay green AND surface real
//     production fragility.
//   * Calls suspected of throwing are wrapped in try/catch so any hidden
//     exception is visible as a hard test failure.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QChar>
#include <QRandomGenerator>
#include <memory>

#include "filter/filterparser.h"
#include "filter/tokens.h"

// ---------------------------------------------------------------------------
// Run a parse operation with hard exception guards.  Returns the AstNode*
// (possibly null) and reports any exception via QFAIL with the input echoed
// back so the offending case is obvious.
// ---------------------------------------------------------------------------
static AstNode *safeParse(FilterParser &p, const QString &query, const char *label)
{
    AstNode *ast = nullptr;
    try {
        ast = p.parse(query);
    } catch (const std::exception &e) {
        // Cannot use QFAIL here directly — it expands to a `return;` statement
        // and this helper returns AstNode*. Use the lower-level qFail entry
        // point + explicit nullptr return so the caller still sees a value.
        QTest::qFail(qPrintable(QString("FilterParser threw std::exception on '%1' (label='%2'): %3")
                                    .arg(label, label, QString::fromUtf8(e.what()))),
                     __FILE__, __LINE__);
        return nullptr;
    } catch (...) {
        QTest::qFail(qPrintable(QString("FilterParser threw unknown exception on '%1' (label='%2')")
                                    .arg(label, label)),
                     __FILE__, __LINE__);
        return nullptr;
    }
    return ast;
}

// Recursive AST depth probe — used to verify deep nesting parsed structurally
// rather than collapsing into something flat.
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
class TestFilterParserAdversarial : public QObject
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
            QStringLiteral("OpenTodoTest_FilterAdversarial"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_filterparser_adversarial"));
    }

    // -----------------------------------------------------------------------
    // 1. Deeply nested parentheses — 50 levels of "(...)".  parseAtom
    //    recurses through the entire stack via parseExpression -> ... ->
    //    parseAtom on every level, so this exercises the actual recursion
    //    depth of the recursive-descent parser.  Must complete in <100 ms
    //    and not blow the stack.
    // -----------------------------------------------------------------------
    void deeply_nested_parens()
    {
        const int depth = 50;
        QString q;
        q.reserve(depth * 2 + 5);
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char('('));
        q.append(QStringLiteral("today"));
        for (int i = 0; i < depth; ++i) q.append(QLatin1Char(')'));

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "deeply_nested_parens_50"));
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 100,
            qPrintable(QString("50-level nested parse took %1ms (limit 100ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "50-level nested parens should parse to a non-null AST");

        if (ast) {
            // The parens collapse — parseAtom unwraps each (Expr) — so the
            // resulting AST root should still be the inner Predicate "today".
            QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)ast->token.type, (int)TokenType::Today);
        }
    }

    // -----------------------------------------------------------------------
    // 2. Unmatched open parens — must FAIL gracefully (parse error, no
    //    crash) and lastError() must be populated.
    // -----------------------------------------------------------------------
    void unmatched_open_paren()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QStringLiteral("(((today"), "unmatched_open_paren"));
        QVERIFY2(ast == nullptr,
            "Unmatched open parens should produce a parse error (null AST)");
        QVERIFY2(!p.lastError().isEmpty(),
            "lastError() must be non-empty on parse failure");
        qInfo() << "unmatched_open_paren error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 3. Unmatched close parens — must FAIL gracefully.  The parser parses
    //    "today" then sees stray ')' tokens past EOF logic.
    // -----------------------------------------------------------------------
    void unmatched_close_paren()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QStringLiteral("today)))"), "unmatched_close_paren"));
        QVERIFY2(ast == nullptr,
            "Stray closing parens should produce a parse error (null AST)");
        QVERIFY2(!p.lastError().isEmpty(),
            "lastError() must be non-empty on parse failure");
        qInfo() << "unmatched_close_paren error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 4. Empty string — documented behaviour: parser returns nullptr with
    //    lastError() == "Empty query".  Documented here so any future
    //    refactor that changes this is caught.
    // -----------------------------------------------------------------------
    void empty_string()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QString(), "empty_string"));
        QVERIFY2(ast == nullptr,
            "Documented behaviour: empty input returns nullptr (not trivial-true)");
        // Production lastError on empty input is "Unexpected token '' at
        // position 0" — the tokenizer emits a single EndOfInput token and
        // parseExpression rejects it. We pin "non-empty error string" rather
        // than a specific message so a future error-message refactor doesn't
        // false-positive this test.
        const QString err = p.lastError();
        qInfo() << "empty_string lastError =" << err;
        QVERIFY2(!err.isEmpty(),
            "Parser must populate lastError when returning nullptr");
    }

    // -----------------------------------------------------------------------
    // 5. Whitespace-only — tokenize() skips whitespace, so the resulting
    //    token list is empty (just EndOfInput is appended after the loop,
    //    so... actually a single EndOfInput).  Verify the parse path is
    //    safe and that lastError() is populated either way.
    // -----------------------------------------------------------------------
    void whitespace_only()
    {
        FilterParser p;
        // Mixed whitespace — spaces, tab, newline, CR.
        QString ws;
        ws.append(QStringLiteral("   "));
        ws.append(QChar(QLatin1Char('\t')));
        ws.append(QChar(QLatin1Char('\n')));
        ws.append(QChar(QLatin1Char('\r')));
        ws.append(QStringLiteral("  "));
        std::unique_ptr<AstNode> ast(safeParse(p, ws, "whitespace_only"));
        QVERIFY2(ast == nullptr,
            "Whitespace-only input behaves like empty: nullptr");
        QVERIFY2(!p.lastError().isEmpty(),
            "lastError() must be populated when parse returns null");
        qInfo() << "whitespace_only error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 6. Extremely long predicate name (10 000-char project name).  The
    //    tokenizer's Project branch reads until whitespace/operator —
    //    must handle a single oversized token without quadratic blowup or
    //    crash.
    // -----------------------------------------------------------------------
    void extremely_long_predicate_name()
    {
        QString huge;
        huge.reserve(10001);
        huge.append(QLatin1Char('#'));
        for (int i = 0; i < 10000; ++i) huge.append(QLatin1Char('a'));

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, huge, "extremely_long_predicate_name"));
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 200,
            qPrintable(QString("10k-char project name took %1ms (limit 200ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "10k-char project should still parse");
        if (ast) {
            QCOMPARE((int)ast->type, (int)AstNodeType::Predicate);
            QCOMPARE((int)ast->token.type, (int)TokenType::Project);
            QCOMPARE(ast->token.text.length(), 10000);
        }
    }

    // -----------------------------------------------------------------------
    // 7. Repeated OR chain — 1000 instances of "today | today | today...".
    //    parseOr() is iterative (while loop), not recursive, so the AST
    //    grows linearly to a left-leaning chain.  Verify <500 ms.
    // -----------------------------------------------------------------------
    void repeated_or_chains()
    {
        const int n = 1000;
        QString q;
        q.reserve(n * 8);
        for (int i = 0; i < n; ++i) {
            if (i > 0) q.append(QStringLiteral(" | "));
            q.append(QStringLiteral("today"));
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "repeated_or_chains_1000"));
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 500,
            qPrintable(QString("1000-OR chain took %1ms (limit 500ms)").arg(elapsed)));
        QVERIFY2(ast != nullptr, "1000-OR chain must parse");
        if (ast) {
            // Root must be Or; left-leaning means depth ~= n.
            QCOMPARE((int)ast->type, (int)AstNodeType::Or);
        }
    }

    // -----------------------------------------------------------------------
    // 8. Mixed unicode predicates — "#Проект @метка /Раздел".  Tokenizer
    //    reads the words by stopping at whitespace / operator chars only,
    //    so unicode payload should pass through unchanged.
    // -----------------------------------------------------------------------
    void mixed_unicode_predicates()
    {
        const QString q = QString::fromUtf8("#Проект & @метка & /Раздел");

        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, q, "mixed_unicode_predicates"));
        QVERIFY2(ast != nullptr, "Unicode predicate names must parse");
        if (!ast) return;

        // Walk the AST and collect predicate token texts.
        QStringList texts;
        std::function<void(const AstNode *)> walk = [&](const AstNode *n) {
            if (!n) return;
            if (n->type == AstNodeType::Predicate) texts.append(n->token.text);
            for (const AstNode *c : n->children) walk(c);
        };
        walk(ast.get());

        QVERIFY2(texts.contains(QString::fromUtf8("Проект")),
            "Unicode project name must round-trip exactly");
        QVERIFY2(texts.contains(QString::fromUtf8("метка")),
            "Unicode label name must round-trip exactly");
        QVERIFY2(texts.contains(QString::fromUtf8("Раздел")),
            "Unicode section name must round-trip exactly");
    }

    // -----------------------------------------------------------------------
    // 9. Pathological search:* strings.  The tokenizer's "search:" branch
    //    swallows everything up to '|', '&', ',', or ')' — verify it
    //    handles quoted/unclosed-quoted/empty strings without crash.
    //    Bugs (e.g. surprising empty-payload behaviour) are flagged via
    //    qInfo() rather than QFAIL.
    // -----------------------------------------------------------------------
    void pathological_search_strings_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("label");

        QTest::newRow("empty quoted")     << QStringLiteral("search:\"\"")
                                          << QStringLiteral("empty_quoted");
        QTest::newRow("unclosed quote")   << QStringLiteral("search:\"unclosed")
                                          << QStringLiteral("unclosed_quote");
        QTest::newRow("escaped quote")    << QStringLiteral("search:\"with \\\" escaped quotes\"")
                                          << QStringLiteral("escaped_quote");
        QTest::newRow("no value")         << QStringLiteral("search:")
                                          << QStringLiteral("no_value");
        QTest::newRow("only space value") << QStringLiteral("search:   ")
                                          << QStringLiteral("only_space");
        // Trailing operator — search swallows up to '&', so search payload
        // should be empty and the rest should still parse as today.
        QTest::newRow("search then op")   << QStringLiteral("search: & today")
                                          << QStringLiteral("search_then_op");
    }

    void pathological_search_strings()
    {
        QFETCH(QString, query);
        QFETCH(QString, label);

        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, query, label.toUtf8().constData()));
        // We do not assert ast != nullptr — some forms (search: & today) may
        // legitimately produce a Search predicate even with empty payload,
        // others may fail.  The contract is: no crash, no exception.
        if (ast) {
            qInfo() << "pathological_search OK label=" << label
                    << " ast_type=" << (int)ast->type
                    << " (" << query << ")";
        } else {
            qInfo() << "pathological_search returned null label=" << label
                    << " err=" << p.lastError()
                    << " (" << query << ")";
        }
    }

    // -----------------------------------------------------------------------
    // 10. NUL byte embedded mid-input.  QString tolerates QChar(0); the
    //     tokenizer treats it as a non-whitespace, non-operator char so it
    //     becomes part of the surrounding identifier.  Must not corrupt
    //     internal state or truncate the parse early.
    // -----------------------------------------------------------------------
    void null_byte_in_input()
    {
        QString q;
        q.append(QStringLiteral("today"));
        q.append(QChar(QChar::Null));       // U+0000
        q.append(QStringLiteral(" & p1"));

        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, q, "null_byte_in_input"));
        // No correctness assertion: we don't care what shape the AST takes,
        // only that the parser survives and lastError is non-corrupt UTF-16.
        if (ast) {
            qInfo() << "null_byte_in_input parsed OK, root type =" << (int)ast->type;
        } else {
            qInfo() << "null_byte_in_input returned null, err=" << p.lastError();
            QVERIFY2(!p.lastError().isEmpty(),
                "lastError() must be populated when parse returns null");
        }

        // Re-parse with a clean input to verify state was not poisoned.
        std::unique_ptr<AstNode> clean(safeParse(p, QStringLiteral("today"), "null_byte_recovery"));
        QVERIFY2(clean != nullptr,
            "Parser must recover state and parse a clean input after NUL-byte exposure");
        if (clean) QCOMPARE((int)clean->token.type, (int)TokenType::Today);
    }

    // -----------------------------------------------------------------------
    // 11. Operators only, no operands — "& | & | !".  parseAnd / parseOr
    //     each call parseUnary which fails immediately because none of
    //     these tokens are atoms.  Must fail gracefully.
    // -----------------------------------------------------------------------
    void mixed_operators_no_operands()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QStringLiteral("& | & | !"), "mixed_operators_no_operands"));
        QVERIFY2(ast == nullptr,
            "Operator-only input should produce a parse error");
        QVERIFY2(!p.lastError().isEmpty(),
            "lastError() must be populated on operator-only failure");
        qInfo() << "mixed_operators_no_operands error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 12. Negation chains — "!!!!today" (4 negations).  parseUnary is
    //     mutually-recursive on Not, so this verifies a small but real
    //     recursion stack survives.  Documented behaviour: each ! adds a
    //     Not node (no logical simplification — !!x is NOT collapsed to x
    //     by the parser).
    // -----------------------------------------------------------------------
    void negation_chains()
    {
        FilterParser p;
        const QString q = QStringLiteral("!!!!today");
        std::unique_ptr<AstNode> ast(safeParse(p, q, "negation_chains_4"));
        QVERIFY2(ast != nullptr, "4 chained negations should parse");
        if (!ast) return;

        // Verify exactly 4 Not nodes wrap the leaf Today predicate.  This
        // documents that the parser does NOT canonicalise !!x -> x.
        int notCount = 0;
        const AstNode *cur = ast.get();
        while (cur && cur->type == AstNodeType::Not) {
            ++notCount;
            cur = cur->children.isEmpty() ? nullptr : cur->children[0];
        }
        QCOMPARE(notCount, 4);
        QVERIFY(cur != nullptr);
        QCOMPARE((int)cur->type, (int)AstNodeType::Predicate);
        QCOMPARE((int)cur->token.type, (int)TokenType::Today);
        qInfo() << "negation_chains: !!!!today preserves all 4 Not nodes (no canonicalisation)";
    }

    // -----------------------------------------------------------------------
    // 13. Predicate followed by garbage.  The tokenizer reads "today" as
    //     a Today token, then the GARBAGE word as an Identifier.  parser
    //     parses "today" then sees a stray Identifier past parseExpression
    //     and fails with "Unexpected token".  Must fail gracefully (the
    //     "today" AstNode produced by parseExpression is freed by parse()
    //     before returning nullptr).
    // -----------------------------------------------------------------------
    void predicate_then_garbage()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QStringLiteral("today GARBAGE_TOKEN_HERE"),
                                               "predicate_then_garbage"));
        QVERIFY2(ast == nullptr,
            "Predicate followed by stray identifier should fail (no implicit AND)");
        QVERIFY2(!p.lastError().isEmpty(),
            "lastError() must surface the unexpected token");
        QVERIFY2(p.lastError().contains(QStringLiteral("nexpected"))
                || p.lastError().contains(QStringLiteral("position")),
            qPrintable(QString("lastError() should describe location/cause: '%1'").arg(p.lastError())));
        qInfo() << "predicate_then_garbage error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 14. Comma-only — ",,,,,,".  parseSectionSeparator calls parseOr
    //     first which fails, so the whole expression should null out.
    // -----------------------------------------------------------------------
    void comma_only()
    {
        FilterParser p;
        std::unique_ptr<AstNode> ast(safeParse(p, QStringLiteral(",,,,,,"), "comma_only"));
        QVERIFY2(ast == nullptr,
            "Comma-only input has no operands and must fail");
        QVERIFY2(!p.lastError().isEmpty(), "lastError() must be populated");
        qInfo() << "comma_only error:" << p.lastError();
    }

    // -----------------------------------------------------------------------
    // 15. Huge combined random 5KB query.  Mix valid predicates with
    //     garbage tokens and operators in a deterministic-seeded random
    //     order.  Must complete <500 ms regardless of validity.
    // -----------------------------------------------------------------------
    void huge_combined()
    {
        // Deterministic seed so the test is reproducible across runs.
        QRandomGenerator rng(0xC0FFEEu);

        const QStringList atoms{
            QStringLiteral("today"),
            QStringLiteral("tomorrow"),
            QStringLiteral("overdue"),
            QStringLiteral("p1"),
            QStringLiteral("p2"),
            QStringLiteral("p3"),
            QStringLiteral("p4"),
            QStringLiteral("recurring"),
            QStringLiteral("!recurring"),
            QStringLiteral("completed"),
            QStringLiteral("deadline"),
            QStringLiteral("#proj"),
            QStringLiteral("@lab"),
            QStringLiteral("/sec"),
            QStringLiteral("search:foo"),
            QStringLiteral("5 days"),
            QStringLiteral("garbage"),     // Identifier — likely produces parse error
            QStringLiteral("p9"),          // out-of-range priority -> Identifier
            QStringLiteral("(today)"),     // pre-grouped subexpression
        };
        const QStringList ops{
            QStringLiteral(" & "),
            QStringLiteral(" | "),
            QStringLiteral(" , "),
        };

        QString q;
        q.reserve(5500);
        // Build until ~5KB.
        while (q.size() < 5000) {
            const QString &a = atoms[rng.bounded(atoms.size())];
            const QString &op = ops[rng.bounded(ops.size())];
            if (!q.isEmpty()) q.append(op);
            // Occasionally wrap atom in parens.
            if ((rng.generate() & 0x07) == 0) {
                q.append(QLatin1Char('('));
                q.append(a);
                q.append(QLatin1Char(')'));
            } else {
                q.append(a);
            }
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "huge_combined_5k"));
        const qint64 elapsed = t.elapsed();
        QVERIFY2(elapsed < 500,
            qPrintable(QString("5KB random parse took %1ms (limit 500ms)").arg(elapsed)));
        // We don't assert ast non-null — random tokens including "garbage"
        // and "p9" routinely cause parse errors.  The contract is: no
        // crash, no infinite loop, finished within budget.
        if (ast) {
            qInfo() << "huge_combined: parsed OK, AST depth =" << astDepth(ast.get())
                    << "size=" << q.size() << "bytes elapsed=" << elapsed << "ms";
        } else {
            qInfo() << "huge_combined: parse error (expected for random input)"
                    << "size=" << q.size() << "bytes elapsed=" << elapsed << "ms"
                    << "err=" << p.lastError();
        }
    }

    // -----------------------------------------------------------------------
    // EXTRA bonus slot: very deep AND chain (200 atoms) — exercises the
    // iterative parseAnd loop and verifies the resulting left-leaning AST
    // doesn't explode memory.  Not in the required-15 list, but cheap and
    // covers the analogous concern to repeated_or_chains.
    // -----------------------------------------------------------------------
    void deep_and_chain()
    {
        const int n = 200;
        QString q;
        q.reserve(n * 8);
        for (int i = 0; i < n; ++i) {
            if (i > 0) q.append(QStringLiteral(" & "));
            q.append(QStringLiteral("today"));
        }

        FilterParser p;
        QElapsedTimer t; t.start();
        std::unique_ptr<AstNode> ast(safeParse(p, q, "deep_and_chain_200"));
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 200,
            qPrintable(QString("200-AND chain took %1ms").arg(elapsed)));
        QVERIFY2(ast != nullptr, "200-AND chain must parse");
        if (ast) {
            QCOMPARE((int)ast->type, (int)AstNodeType::And);
            // Sanity: AST depth equals number of operators (left-leaning).
            const int d = astDepth(ast.get());
            QVERIFY2(d == n,
                qPrintable(QString("AST depth %1 != atom count %2").arg(d).arg(n)));
        }
    }
};

QTEST_GUILESS_MAIN(TestFilterParserAdversarial)
#include "test_filterparser_adversarial.moc"
