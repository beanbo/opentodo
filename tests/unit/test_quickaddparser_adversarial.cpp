// ---------------------------------------------------------------------------
// test_quickaddparser_adversarial.cpp
//
// Adversarial / fuzz coverage for QuickAddParser.  Layer: Domain.
//
// Q2.16-C added test_nlp_robustness.cpp with broad DoS-length / NUL byte /
// RTL / NFC-NFD / year overflow / emoji-only coverage spanning BOTH the
// QuickAddParser AND the DateParser.  This file goes DEEPER on the
// QuickAddParser specifically, exercising its three-stage token grammar:
//
//   1. `#Project` first match  →  result.projectName
//   2. `#labels` subsequent    →  result.extraLabels
//   3. `@labels` all           →  result.labels
//   4. `p1`-`p4` / `!1`-`!4`   →  result.priority
//   5. remaining → DateParser  →  result.dueString + result.content
//
// Each token type uses a different regex with subtle word-boundary lookahead/
// lookbehind rules.  This file probes the boundary cases that the existing
// test suite misses:
//   * Token immediately followed by another sigil (`p1#`)
//   * Repeated priority tokens (which wins?)
//   * Multi-token projects vs. labels for inbox-less Todoist convention
//   * Project name with embedded whitespace
//   * Special characters inside labels: hyphen, period, slash
//   * Lone sigils (`#`, `@`, `p`) alone — empty tokens must not crash
//   * Cross-language token recognition (Russian content + English tokens)
//   * Priority tokens in mid-sentence (`the cat p1 has 9 lives`)
//   * Very-long single token (3 KB project name) — performance
//   * Many tokens in one string — all collected
//   * Pipe-separated tokens — `|` is not a sigil, so it's content
//   * Russian date phrase that LOOKS like a token (`завтра`)
//   * Mixed-case priority (`P1` vs `p1` vs `PRIORITY 1`)
//   * Recurrence rule interleaved with project + priority
//
// Per CLAUDE.md / orchestrator constraints:
//   * QTEST_GUILESS_MAIN — no QGuiApplication.
//   * Unique per-process QSettings org/app namespace
//     (`OpenTodoTest_QuickAddAdversarial` / `test_quickaddparser_adversarial`)
//     to keep this isolated from the user's real install AND from any other
//     test running in parallel under shared QStandardPaths.
//   * NO raw-string literals R"(...)" anywhere — moc's parser dies with
//     "No relevant classes found" on unbalanced parens inside R"(...)".
//   * NO function-local Q_OBJECT classes (moc cannot reach them).
//   * Production-code edits are AVOIDED; quirks are documented via qInfo()
//     rather than QFAIL — the suite must stay green AND surface real
//     production fragility.  Only a true crash (segfault / infinite loop)
//     would justify a parser fix; none was found.
//   * Hard time bounds via QElapsedTimer for perf-sensitive slots
//     (3 KB single-token parse must finish in <500 ms).
//   * All UI-facing strings are Russian where applicable.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QChar>

#include "nlp/quickaddparser.h"

class TestQuickAddParserAdversarial : public QObject
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
            QStringLiteral("OpenTodoTest_QuickAddAdversarial"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_quickaddparser_adversarial"));
    }

    // -----------------------------------------------------------------------
    // 1. Token at boundary: `p1#`
    //
    // Priority regex: (?:^|(?<=\s))p([1-4])(?=\s|$)
    //   * Requires `p1` to be followed by whitespace OR end-of-string.
    //   * `p1#` has `#` after — so priority should NOT match.
    //
    // Project regex: (?:^|\s)#([^\s#@!]+)
    //   * Requires `#` to be preceded by whitespace OR start-of-string.
    //   * In `p1#` the `#` is preceded by `1` — so project should NOT match.
    //
    // Net contract: `p1#` is treated entirely as content.  Documents that
    // sigils must have a whitespace boundary on the contact side.
    // -----------------------------------------------------------------------
    void boundary_priority_hash_no_space()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("p1#"));
        qInfo() << "p1# parse: priority=" << r.priority
                << "projectName=" << r.projectName
                << "content=" << r.content;

        // Neither token form has a whitespace boundary on the touching side,
        // so neither matches.  `p1#` stays as content (after .simplified()).
        QCOMPARE(r.priority, 4);
        QCOMPARE(r.projectName, QString());
        QVERIFY(r.content.contains(QStringLiteral("p1#")));
    }

    // -----------------------------------------------------------------------
    // 2. Repeated priority: `p1 p2 p3` — which wins?
    //
    // The priority regex uses .match() (single match), so it matches the
    // FIRST occurrence — `p1` — and removes it.  The remaining `p2 p3` is
    // not re-scanned: it falls through to content.
    //
    // Documents the actual contract: FIRST priority wins.
    // -----------------------------------------------------------------------
    void repeated_priority_first_wins()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("p1 p2 p3"));
        qInfo() << "p1 p2 p3 parse: priority=" << r.priority
                << "content=" << r.content
                << "dueString=" << r.dueString;

        // First priority wins.  The remaining "p2 p3" stays inside content
        // (or possibly dueString if DateParser happens to bite it; both are
        // documented outcomes — neither should fail).
        QCOMPARE(r.priority, 1);
        const QString combined = r.content + QStringLiteral(" ") + r.dueString;
        QVERIFY(combined.contains(QStringLiteral("p2")));
        QVERIFY(combined.contains(QStringLiteral("p3")));
    }

    // -----------------------------------------------------------------------
    // 3. Multiple `#project`: first wins, subsequent become labels.
    //
    // Per F-4.26: any #token after the first is added to result.extraLabels.
    // This matches the Todoist inbox-less convention where the user might
    // type `task #foo #bar` meaning project=foo + tag=bar.
    // -----------------------------------------------------------------------
    void multiple_project_first_wins_rest_labels()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("task #foo #bar"));
        qInfo() << "task #foo #bar parse: projectName=" << r.projectName
                << "extraLabels=" << r.extraLabels
                << "labels=" << r.labels
                << "content=" << r.content;

        QCOMPARE(r.projectName, QStringLiteral("foo"));
        // Subsequent #tokens are stored as extraLabels (NOT labels).
        QVERIFY(r.extraLabels.contains(QStringLiteral("bar")));
        QCOMPARE(r.content, QStringLiteral("task"));
    }

    // -----------------------------------------------------------------------
    // 4. Project name with spaces — `#Моя работа`
    //
    // The #-regex is `#([^\s#@!]+)` — captures NON-whitespace only.  So
    // `#Моя работа` captures `Моя` and leaves ` работа` inside content.
    // Documents that quoted multi-word project names are NOT supported.
    // -----------------------------------------------------------------------
    void project_name_with_space_first_word_only()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("#Моя работа"));
        qInfo() << "#Моя работа parse: projectName=" << r.projectName
                << "content=" << r.content;

        // Greedy [^\s#@!]+ stops at the space.
        QCOMPARE(r.projectName, QStringLiteral("Моя"));
        QVERIFY(r.content.contains(QStringLiteral("работа")));
    }

    // -----------------------------------------------------------------------
    // 5. Label with special characters: hyphen / period / slash.
    //
    // The label regex is `@([^\s@#!]+)` — any char other than whitespace,
    // @, #, ! is allowed.  So hyphen, period, slash, comma, colon, parens,
    // etc. all become PART OF the label name.  Documents that there is NO
    // word-boundary terminator for labels beyond the three sigils.
    // -----------------------------------------------------------------------
    void label_with_special_chars_kept_inside()
    {
        // Hyphen — should be included in the label name.
        const auto rDash = QuickAddParser::parse(
            QStringLiteral("Задача @метка-с-дефисом"));
        qInfo() << "@метка-с-дефисом parse: labels=" << rDash.labels;
        QCOMPARE(rDash.labels.size(), 1);
        QVERIFY(rDash.labels.contains(QStringLiteral("метка-с-дефисом")));

        // Period — also part of the label name.
        const auto rDot = QuickAddParser::parse(
            QStringLiteral("Задача @метка.с.точкой"));
        qInfo() << "@метка.с.точкой parse: labels=" << rDot.labels;
        QCOMPARE(rDot.labels.size(), 1);
        QVERIFY(rDot.labels.contains(QStringLiteral("метка.с.точкой")));

        // Slash — also part of the label name.  (Documents that slash is
        // NOT a path separator at this level; the regex simply doesn't
        // exclude it.)
        const auto rSlash = QuickAddParser::parse(
            QStringLiteral("Задача @метка/с/слешем"));
        qInfo() << "@метка/с/слешем parse: labels=" << rSlash.labels;
        QCOMPARE(rSlash.labels.size(), 1);
        QVERIFY(rSlash.labels.contains(QStringLiteral("метка/с/слешем")));
    }

    // -----------------------------------------------------------------------
    // 6. Escaped / repeated sigils: `@@@`, `\@метка`.
    //
    // `@@@` — the regex requires `@` followed by [^\s@#!]+, but `@@@` has
    // no [^\s@#!] char after the leading `@`.  So no label match.  The
    // entire string falls through to content.
    //
    // `\@метка` — backslash is not a sigil.  Regex `(?:^|\s)@([^\s@#!]+)`
    // requires `@` to be preceded by whitespace or start-of-string.  Here
    // `@` is preceded by `\`, so no match.  Documents that there is NO
    // escape mechanism — the entire phrase ends up as content.
    // -----------------------------------------------------------------------
    void escaped_or_repeated_sigils()
    {
        // Triple @ — no valid label.
        const auto rTripleAt = QuickAddParser::parse(QStringLiteral("@@@"));
        qInfo() << "@@@ parse: labels=" << rTripleAt.labels
                << "content=" << rTripleAt.content;
        QVERIFY(rTripleAt.labels.isEmpty());
        QVERIFY(rTripleAt.content.contains(QChar(QLatin1Char('@'))));

        // Backslash-@-text — no escape mechanism in the parser.  The `\`
        // is content (not a sigil prefix), and the regex `(?:^|\s)@...`
        // requires whitespace or start before `@` — neither holds here, so
        // the @-token does NOT match.
        const auto rBackslash = QuickAddParser::parse(
            QString::fromUtf8("\\@\xD0\xBC\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0"));
        qInfo() << "\\@метка parse: labels=" << rBackslash.labels
                << "content=" << rBackslash.content;
        // The label should NOT be extracted because \ is between start and @.
        QVERIFY(rBackslash.labels.isEmpty());
        // Either the whole "\@метка" sits in content, or it sits inside
        // dueString (DateParser shouldn't bite this) — both are acceptable
        // and indicate no crash.
        const QString combined = rBackslash.content + QStringLiteral(" ")
                                  + rBackslash.dueString;
        QVERIFY(combined.contains(QChar(QLatin1Char('\\'))));
    }

    // -----------------------------------------------------------------------
    // 7. Empty tokens — `@`, `#`, `p` alone.
    //
    // The regex always requires at least one [^\s#@!]+ char after the
    // sigil.  So a lone `@` / `#` does NOT match any token.  And `p`
    // without a digit doesn't match the priority regex either.
    //
    // All three should fall through to content with no field populated
    // and absolutely no crash.
    // -----------------------------------------------------------------------
    void empty_lone_sigils()
    {
        // Lone @ — no label match, no crash.
        const auto rAt = QuickAddParser::parse(QStringLiteral("@"));
        qInfo() << "lone @ parse: labels=" << rAt.labels
                << "content=" << rAt.content;
        QVERIFY(rAt.labels.isEmpty());
        QCOMPARE(rAt.projectName, QString());
        QVERIFY(rAt.content.contains(QChar(QLatin1Char('@')))
                || rAt.dueString.contains(QChar(QLatin1Char('@'))));

        // Lone # — no project, no crash.
        const auto rHash = QuickAddParser::parse(QStringLiteral("#"));
        qInfo() << "lone # parse: projectName=" << rHash.projectName
                << "extraLabels=" << rHash.extraLabels
                << "content=" << rHash.content;
        QCOMPARE(rHash.projectName, QString());
        QVERIFY(rHash.extraLabels.isEmpty());
        QVERIFY(rHash.content.contains(QChar(QLatin1Char('#')))
                || rHash.dueString.contains(QChar(QLatin1Char('#'))));

        // Lone p — no priority match.  "p" by itself ends up in content.
        const auto rP = QuickAddParser::parse(QStringLiteral("p"));
        qInfo() << "lone p parse: priority=" << rP.priority
                << "content=" << rP.content;
        QCOMPARE(rP.priority, 4);
        QVERIFY(rP.content == QStringLiteral("p")
                || rP.dueString.contains(QStringLiteral("p")));
    }

    // -----------------------------------------------------------------------
    // 8. All-Cyrillic content with English tokens.
    //
    // The parser is language-agnostic at the sigil level — `@grocery`,
    // `p1`, `#Магазин` should all be picked up regardless of surrounding
    // Russian text.  And `завтра` should be recognised as the date phrase
    // by the Russian DateParser at the end.
    // -----------------------------------------------------------------------
    void cyrillic_content_english_tokens_cross_boundary()
    {
        const auto r = QuickAddParser::parse(QStringLiteral(
            "Купить молоко @grocery p1 #Магазин завтра в 9"));
        qInfo() << "cross-boundary parse: priority=" << r.priority
                << "projectName=" << r.projectName
                << "labels=" << r.labels
                << "content=" << r.content
                << "dueString=" << r.dueString;

        QCOMPARE(r.priority, 1);
        QCOMPARE(r.projectName, QStringLiteral("Магазин"));
        QVERIFY(r.labels.contains(QStringLiteral("grocery")));
        QCOMPARE(r.content, QStringLiteral("Купить молоко"));
        QVERIFY(r.dueString.contains(QStringLiteral("завтра")));
    }

    // -----------------------------------------------------------------------
    // 9. Priority token in the middle of a sentence: `the cat p1 has 9 lives`
    //
    // The priority regex uses lookbehind `(?<=\s)` and lookahead `(?=\s|$)`,
    // so it matches a `p1` that is surrounded by spaces ANYWHERE in the
    // string, not just at the boundaries.  This is intentional — the user
    // might type a priority anywhere mid-sentence.
    // -----------------------------------------------------------------------
    void priority_mid_sentence()
    {
        const auto r = QuickAddParser::parse(
            QStringLiteral("the cat p1 has 9 lives"));
        qInfo() << "mid-sentence parse: priority=" << r.priority
                << "content=" << r.content
                << "dueString=" << r.dueString;

        QCOMPARE(r.priority, 1);
        // After removing " p1" the working buffer is "the cat has 9 lives".
        // The DateParser may or may not bite "9 lives" as a date phrase —
        // document both outcomes.
        const QString combined = r.content + QStringLiteral(" ") + r.dueString;
        QVERIFY(combined.contains(QStringLiteral("the")));
        QVERIFY(combined.contains(QStringLiteral("cat")));
        QVERIFY(combined.contains(QStringLiteral("lives")));
    }

    // -----------------------------------------------------------------------
    // 10. Very long single token — 3 KB project name.
    //
    // `#` + "abc" * 1000 ≈ 3001 chars in one token.  The regex
    // [^\s#@!]+ is linear in the length of the captured group, and the
    // whole match is O(N).  Must complete quickly and produce the full
    // project name as captured.
    // -----------------------------------------------------------------------
    void very_long_single_token_3kb_project()
    {
        QString token;
        token.reserve(3100);
        token += QLatin1Char('#');
        for (int i = 0; i < 1000; ++i)
            token += QStringLiteral("abc");
        QCOMPARE(token.size(), 3001);

        QElapsedTimer t;
        t.start();
        const auto r = QuickAddParser::parse(token);
        const qint64 elapsedMs = t.elapsed();
        qInfo() << "3KB single-token parse took" << elapsedMs << "ms,"
                << "projectName.size=" << r.projectName.size();

        // Must finish quickly — generous 500ms bound for slow CI VMs.
        QVERIFY2(elapsedMs < 500,
                 qPrintable(QStringLiteral("3KB token parse exceeded 500ms: %1ms")
                                .arg(elapsedMs)));

        // The whole token after `#` should land in projectName.
        QCOMPARE(r.projectName.size(), 3000);
        // No content left over.
        QCOMPARE(r.content, QString());
    }

    // -----------------------------------------------------------------------
    // 11. Many tokens — `p1 #a @b @c @d ... @z`
    //
    // Verifies that the @-token loop collects every label, not just the
    // first.  The parser uses globalMatch() for both @ and # tokens.
    // -----------------------------------------------------------------------
    void many_labels_in_one_string()
    {
        QString s = QStringLiteral("p1 #a");
        const int N = 25; // b..z
        for (int i = 0; i < N; ++i) {
            s += QStringLiteral(" @");
            s += QChar(QLatin1Char('b' + i));
        }
        qInfo() << "many-labels input size:" << s.size();

        const auto r = QuickAddParser::parse(s);
        qInfo() << "many-labels parse: labels.size=" << r.labels.size()
                << "priority=" << r.priority
                << "projectName=" << r.projectName;

        QCOMPARE(r.priority, 1);
        QCOMPARE(r.projectName, QStringLiteral("a"));
        // All N labels should be collected.
        QCOMPARE(r.labels.size(), N);
        QVERIFY(r.labels.contains(QStringLiteral("b")));
        QVERIFY(r.labels.contains(QStringLiteral("z")));
    }

    // -----------------------------------------------------------------------
    // 12. Pipe-separated tokens: `#a | #b`
    //
    // The `|` character has no special meaning in QuickAddParser.  It is
    // not a sigil, not whitespace.  Each #token is still picked up via the
    // standard whitespace-boundary rule, and `|` ends up in content (or
    // is consumed by .simplified()).
    //
    // Documents that there is NO Boolean-OR syntax at the quick-add level
    // (unlike the filter DSL).
    // -----------------------------------------------------------------------
    void pipe_separated_tokens_pipe_is_content()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("#a | #b"));
        qInfo() << "#a | #b parse: projectName=" << r.projectName
                << "extraLabels=" << r.extraLabels
                << "content=" << r.content;

        // First #a is project, second #b is extra label.
        QCOMPARE(r.projectName, QStringLiteral("a"));
        QVERIFY(r.extraLabels.contains(QStringLiteral("b")));
        // `|` survives in content (after .simplified()).
        QVERIFY(r.content.contains(QChar(QLatin1Char('|'))));
    }

    // -----------------------------------------------------------------------
    // 13. Date phrase that looks like a token: `buy milk p1 завтра`
    //
    // Priority `p1` must extract correctly; `завтра` must be recognised
    // by the Russian DateParser as a one-word due phrase.  Critical
    // regression coverage: the priority regex must not accidentally
    // swallow `завтра` (it doesn't — wrong characters).
    // -----------------------------------------------------------------------
    void priority_with_russian_date_phrase()
    {
        const auto r = QuickAddParser::parse(
            QStringLiteral("buy milk p1 завтра"));
        qInfo() << "buy milk p1 завтра parse: priority=" << r.priority
                << "content=" << r.content
                << "dueString=" << r.dueString;

        QCOMPARE(r.priority, 1);
        QCOMPARE(r.content, QStringLiteral("buy milk"));
        QVERIFY(r.dueString.contains(QStringLiteral("завтра")));
    }

    // -----------------------------------------------------------------------
    // 14. Mixed-case priority — `P1`, `p1`, `PRIORITY 1`.
    //
    // The priority regex has CaseInsensitiveOption, so `P1` / `p1` /
    // `P1` all match.  But `PRIORITY 1` is a multi-token English phrase —
    // the regex looks for the literal `p[1-4]` pattern, NOT the word
    // "PRIORITY", so `PRIORITY 1` does NOT extract a priority (it falls
    // through to content where it may or may not bite into DateParser
    // for the `1`).
    // -----------------------------------------------------------------------
    void mixed_case_priority_forms()
    {
        // `P1` — uppercase, case-insensitive flag matches.
        const auto rUpper = QuickAddParser::parse(QStringLiteral("Task P1"));
        qInfo() << "Task P1 parse: priority=" << rUpper.priority
                << "content=" << rUpper.content;
        QCOMPARE(rUpper.priority, 1);
        QCOMPARE(rUpper.content, QStringLiteral("Task"));

        // `p1` — lowercase, baseline case.
        const auto rLower = QuickAddParser::parse(QStringLiteral("Task p1"));
        QCOMPARE(rLower.priority, 1);
        QCOMPARE(rLower.content, QStringLiteral("Task"));

        // `PRIORITY 1` — word "PRIORITY" + space + digit.  The regex looks
        // for `p[1-4]` not `priority `, so no match.  Document the actual
        // behaviour: priority stays at default 4 and PRIORITY/1 fall
        // through to content (or dueString if DateParser bites "1").
        const auto rWord = QuickAddParser::parse(
            QStringLiteral("Task PRIORITY 1"));
        qInfo() << "Task PRIORITY 1 parse: priority=" << rWord.priority
                << "content=" << rWord.content
                << "dueString=" << rWord.dueString;
        // The literal word "PRIORITY" is not a token form, so priority
        // stays at default 4.
        QCOMPARE(rWord.priority, 4);
        const QString combined = rWord.content + QStringLiteral(" ")
                                  + rWord.dueString;
        QVERIFY(combined.contains(QStringLiteral("PRIORITY")));
    }

    // -----------------------------------------------------------------------
    // 15. Recurrence rule with token interleaving: `каждый день #Привычка p2`
    //
    // All three pieces must be extracted INDEPENDENTLY:
    //   * priority = 2
    //   * projectName = "Привычка"
    //   * dueString = "каждый день"
    //
    // This is the canonical use case for a daily-habit task in Russian.
    // Regression coverage to make sure the date parser sees "каждый день"
    // after the # and p tokens are removed.
    // -----------------------------------------------------------------------
    void recurrence_with_interleaved_tokens()
    {
        const auto r = QuickAddParser::parse(
            QStringLiteral("каждый день #Привычка p2"));
        qInfo() << "recurrence parse: priority=" << r.priority
                << "projectName=" << r.projectName
                << "content=" << r.content
                << "dueString=" << r.dueString;

        QCOMPARE(r.priority, 2);
        QCOMPARE(r.projectName, QStringLiteral("Привычка"));
        // After removing #Привычка and p2, the remaining text is
        // "каждый день" — the Russian DateParser should recognise it.
        QVERIFY(r.dueString.contains(QStringLiteral("каждый")));
        QVERIFY(r.dueString.contains(QStringLiteral("день")));
        // No leftover content.
        QCOMPARE(r.content, QString());
    }
};

QTEST_GUILESS_MAIN(TestQuickAddParserAdversarial)
#include "test_quickaddparser_adversarial.moc"
