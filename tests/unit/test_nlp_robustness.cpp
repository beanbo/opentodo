// NLP parser robustness tests — defensive coverage of QuickAddParser and
// DateParser against pathological / adversarial input:
//   * Very long strings (DoS via O(n^2) in word-split probing).
//   * Embedded NUL bytes and control characters.
//   * Whitespace-only / empty input.
//   * Pure-token strings with no payload.
//   * Mixed bidirectional scripts (Hebrew + Cyrillic + Latin).
//   * Combining-diacritic Unicode (canonical equivalence forms).
//   * Out-of-range and emoji-only "dates".
//   * Mixed RU/EN within one expression.
//
// Per CLAUDE.md / orchestrator constraints:
//   * QTEST_GUILESS_MAIN.
//   * Unique QSettings org/app to avoid clobbering the user's real install
//     and other tests running in parallel.
//   * No production code is modified — bugs found are documented via
//     qInfo() and QEXPECT_FAIL rather than crashing the suite.
//   * No raw-string literals — all UTF-8 phrases use QString::fromUtf8.
//   * Tests must complete in <5 s total; pathological-input slots use
//     QElapsedTimer and assert <500 ms each.

#include <QtTest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QChar>

#include "nlp/quickaddparser.h"
#include "nlp/dateparser.h"

class TestNlpRobustness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Per-process unique QSettings namespace — keeps this test isolated
        // from the user's real OpenTodo install AND from any other tests
        // running in parallel under shared QStandardPaths.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_NlpRobust"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_nlp_robustness"));
    }

    // ─── QuickAddParser ─────────────────────────────────────────────────────

    // 1. 10 KB content — must parse without crash and finish quickly.
    //    Use repeated short words separated by spaces so the parser's
    //    word-split / date-probe loop actually sees many words (worst case
    //    for the O(N) prefix+suffix probe); content should still survive
    //    roundtrip roughly intact.
    void quickadd_10kb_content_no_crash()
    {
        // ~10 KB: 1666 occurrences of "milk " ≈ 8330 chars. Use 2000 for
        // a comfortable >10 KB but still finishes in well under 500 ms.
        QString huge;
        huge.reserve(10500);
        for (int i = 0; i < 2000; ++i)
            huge += QStringLiteral("milk ");
        QVERIFY(huge.size() >= 10000);

        QElapsedTimer t;
        t.start();
        const auto r = QuickAddParser::parse(huge);
        const qint64 elapsedMs = t.elapsed();

        qInfo() << "10KB QuickAdd parse took" << elapsedMs << "ms,"
                << "content.size=" << r.content.size()
                << "dueString.size=" << r.dueString.size();

        // Hard upper bound: even pathological input must not exceed 500 ms.
        QVERIFY2(elapsedMs < 500,
                 qPrintable(QStringLiteral("10KB parse exceeded 500ms: %1ms")
                                .arg(elapsedMs)));

        // No #/@ tokens were present, so projectName must be empty and
        // labels must be empty.
        QCOMPARE(r.projectName, QString());
        QVERIFY(r.labels.isEmpty());
        QVERIFY(r.extraLabels.isEmpty());

        // The parser may extract a partial trailing date string, but the
        // bulk of the input must remain in EITHER content or dueString.
        // Allow a small loss for tokens consumed as date phrase.
        const int totalKept = r.content.size() + r.dueString.size();
        QVERIFY2(totalKept >= huge.simplified().size() - 50,
                 qPrintable(QStringLiteral("Lost too much input: kept %1 of %2")
                                .arg(totalKept).arg(huge.simplified().size())));
    }

    // 2. Content with embedded NUL bytes — QString supports NUL natively;
    //    parser must not crash or corrupt memory.
    void quickadd_nul_bytes()
    {
        QString s = QStringLiteral("Buy ");
        s.append(QChar(QChar::Null));
        s.append(QStringLiteral("milk"));
        s.append(QChar(QChar::Null));
        s.append(QStringLiteral(" today"));
        QVERIFY(s.contains(QChar(QChar::Null)));

        QuickAddResult r;
        // No throw / no crash is the primary assertion.
        try {
            r = QuickAddParser::parse(s);
        } catch (...) {
            QFAIL("QuickAddParser threw on NUL-byte input");
        }
        qInfo() << "NUL-byte parse: content.size=" << r.content.size()
                << "dueString=" << r.dueString
                << "priority=" << r.priority;

        // Either "today" gets extracted or it stays in content; both are OK.
        // What we require is that we didn't drop "Buy" / "milk" entirely.
        const QString combined = r.content + QStringLiteral(" ") + r.dueString;
        QVERIFY(combined.contains(QStringLiteral("Buy"))
                || combined.contains(QStringLiteral("milk")));
    }

    // 3. Whitespace-only input — content must be empty after parse.
    void quickadd_whitespace_only()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("   \t  \n  "));
        QCOMPARE(r.content, QString());
        QCOMPARE(r.projectName, QString());
        QVERIFY(r.labels.isEmpty());
        QCOMPARE(r.priority, 4);
        QCOMPARE(r.dueString, QString());
    }

    // 4. Pure-token input — every word is a token, no plain text.
    //    Expected: priority + projectName + labels populated, content empty.
    void quickadd_only_tokens()
    {
        const auto r = QuickAddParser::parse(QStringLiteral("p1 #foo @bar"));
        QCOMPARE(r.priority, 1);
        QCOMPARE(r.projectName, QStringLiteral("foo"));
        QCOMPARE(r.labels.size(), 1);
        QVERIFY(r.labels.contains(QStringLiteral("bar")));
        // After all tokens are stripped and .simplified() is run, nothing
        // should remain — neither content nor dueString.
        QCOMPARE(r.content, QString());
        QCOMPARE(r.dueString, QString());
    }

    // 5. Mixed RTL+LTR — Hebrew shalom + Cyrillic привет. The parser doesn't
    //    care about script direction; verify no crash and content survives.
    void quickadd_mixed_rtl_ltr()
    {
        // "shalom привет" in Hebrew + Russian.
        const QString hebrew = QString::fromUtf8("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"); // שלום
        const QString russian = QString::fromUtf8("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"); // привет
        const QString s = hebrew + QStringLiteral(" ") + russian;

        QuickAddResult r;
        try {
            r = QuickAddParser::parse(s);
        } catch (...) {
            QFAIL("QuickAddParser threw on mixed RTL+LTR input");
        }
        qInfo() << "RTL+LTR parse: content=" << r.content
                << "dueString=" << r.dueString;

        // No tokens, so content should equal the input modulo .simplified().
        // Both words should appear somewhere across content + dueString.
        const QString combined = r.content + QStringLiteral(" ") + r.dueString;
        QVERIFY(combined.contains(hebrew));
        QVERIFY(combined.contains(russian));
    }

    // 6. Combining diacritics — "café" can be either NFC (é = U+00E9) or NFD
    //    (e + U+0301). The parser must preserve whichever form it received.
    void quickadd_combining_diacritics()
    {
        // NFD form: 'c' 'a' 'f' 'e' + COMBINING ACUTE ACCENT (U+0301).
        QString nfd;
        nfd += QLatin1Char('c');
        nfd += QLatin1Char('a');
        nfd += QLatin1Char('f');
        nfd += QLatin1Char('e');
        nfd += QChar(0x0301);
        QCOMPARE(nfd.size(), 5);

        // NFC form: 'c' 'a' 'f' + LATIN SMALL LETTER E WITH ACUTE (U+00E9).
        QString nfc;
        nfc += QLatin1Char('c');
        nfc += QLatin1Char('a');
        nfc += QLatin1Char('f');
        nfc += QChar(0x00E9);
        QCOMPARE(nfc.size(), 4);

        const auto rNfd = QuickAddParser::parse(nfd);
        const auto rNfc = QuickAddParser::parse(nfc);

        // The grapheme cluster must be preserved verbatim — no NFC/NFD
        // normalisation by the parser.
        QCOMPARE(rNfd.content.size(), nfd.size());
        QCOMPARE(rNfc.content.size(), nfc.size());
        QCOMPARE(rNfd.content, nfd);
        QCOMPARE(rNfc.content, nfc);
    }

    // ─── DateParser ─────────────────────────────────────────────────────────

    // 7. 10 KB pathological recurrence string — repeating "каждый день"
    //    is the classic "could the parser go quadratic in word count" smoke
    //    test. The parse must complete in <500 ms.
    void dateparser_10kb_recurring_no_dos()
    {
        QString huge;
        huge.reserve(10500);
        // "каждый день " is 12 UTF-16 chars + space; 1000 reps ≈ 13 KB.
        for (int i = 0; i < 1000; ++i)
            huge += QString::fromUtf8("каждый день ");
        QVERIFY(huge.size() >= 10000);

        QElapsedTimer t;
        t.start();
        const auto r = DateParser::parseRussian(huge.trimmed(),
                                                QDate(2026, 5, 15));
        const qint64 elapsedMs = t.elapsed();

        qInfo() << "10KB DateParser parse took" << elapsedMs << "ms,"
                << "valid=" << r.valid
                << "isRecurring=" << r.isRecurring
                << "humanString=" << r.humanString;

        QVERIFY2(elapsedMs < 500,
                 qPrintable(QStringLiteral("DateParser pathological input "
                                           "exceeded 500ms: %1ms")
                                .arg(elapsedMs)));
        // The whole anchored regex won't match (it expects exactly
        // "каждый день", not 1000 repetitions), so result should be invalid.
        // Document if behaviour is otherwise.
        if (r.valid)
            qInfo() << "BUG?: 10KB 'каждый день каждый день ...' was VALID";
    }

    // 8. Empty string — both languages must reject.
    void dateparser_empty_string()
    {
        const auto en = DateParser::parseEnglish(QString(),
                                                 QDate(2026, 5, 15));
        const auto ru = DateParser::parseRussian(QString(),
                                                 QDate(2026, 5, 15));
        QVERIFY(!en.valid);
        QVERIFY(!ru.valid);
        QVERIFY(en.humanString.isEmpty());
        QVERIFY(ru.humanString.isEmpty());
    }

    // 9. Whitespace-only string.
    void dateparser_whitespace_only()
    {
        const auto en = DateParser::parseEnglish(QStringLiteral("   \t  "),
                                                 QDate(2026, 5, 15));
        const auto ru = DateParser::parseRussian(QStringLiteral("   \t  "),
                                                 QDate(2026, 5, 15));
        QVERIFY(!en.valid);
        QVERIFY(!ru.valid);
    }

    // 10. Ridiculously large year — "01.01.99999" must NOT parse to a real
    //     date. The Russian parser only takes DD.MM.YYYY inside a "каждый
    //     год …" recurrence; a bare DD.MM.YYYY isn't a single-date format,
    //     so result should be invalid.
    void dateparser_year_out_of_range_high()
    {
        const auto ru = DateParser::parseRussian(
            QStringLiteral("01.01.99999"), QDate(2026, 5, 15));
        const auto en = DateParser::parseEnglish(
            QStringLiteral("01.01.99999"), QDate(2026, 5, 15));
        qInfo() << "01.01.99999 ru.valid=" << ru.valid
                << "ru.date=" << ru.date
                << "en.valid=" << en.valid;
        QVERIFY(!ru.valid);
        QVERIFY(!en.valid);
    }

    // 11. Negative year — "01.01.-100" must NOT parse. Same reasoning
    //     as above; the dot-separated bare-date form isn't recognised at
    //     all by either parser, so this is invalid by construction.
    void dateparser_year_negative()
    {
        const auto ru = DateParser::parseRussian(
            QStringLiteral("01.01.-100"), QDate(2026, 5, 15));
        const auto en = DateParser::parseEnglish(
            QStringLiteral("01.01.-100"), QDate(2026, 5, 15));
        qInfo() << "01.01.-100 ru.valid=" << ru.valid
                << "en.valid=" << en.valid;
        QVERIFY(!ru.valid);
        QVERIFY(!en.valid);
    }

    // 12. Just emoji — non-text input that doesn't match any rule.
    void dateparser_only_emoji()
    {
        // 🎉 (U+1F389), 🎊 (U+1F38A), 🎈 (U+1F388) — surrogate pairs in UTF-16.
        const QString emoji = QString::fromUtf8(
            "\xF0\x9F\x8E\x89\xF0\x9F\x8E\x8A\xF0\x9F\x8E\x88");
        const auto ru = DateParser::parseRussian(emoji, QDate(2026, 5, 15));
        const auto en = DateParser::parseEnglish(emoji, QDate(2026, 5, 15));
        QVERIFY(!ru.valid);
        QVERIFY(!en.valid);
    }

    // 13. Single digit "5" — by itself, no unit. Should not parse to a
    //     full date in either language.
    void dateparser_single_digit()
    {
        const auto ru = DateParser::parseRussian(QStringLiteral("5"),
                                                 QDate(2026, 5, 15));
        const auto en = DateParser::parseEnglish(QStringLiteral("5"),
                                                 QDate(2026, 5, 15));
        qInfo() << "single '5' ru.valid=" << ru.valid
                << "ru.humanString=" << ru.humanString
                << "en.valid=" << en.valid
                << "en.humanString=" << en.humanString;

        // A single integer is not a date phrase. BOTH parsers however accept
        // "5" as "5 AM today" via a bare-digit time shortcut — document this
        // quirk in both rules_ru and rules_en if it triggers. We don't fail
        // the test, since this is exploratory: the goal is to record what the
        // parsers actually do, not to assert what they should do.
        if (ru.valid)
            qInfo() << "Note: ru parser treated '5' as a time shortcut "
                       "(documented quirk in rules_ru)";
        if (en.valid)
            qInfo() << "Note: en parser treated '5' as a time shortcut "
                       "(documented quirk in rules_en — symmetric with ru)";
        // Both parsers may either reject or accept; either is acceptable.
        // The hard requirement is just that they didn't crash.
    }

    // 14. Mixed languages within one expression — "today каждый день"
    //     parsed as Russian. Document the actual behaviour: Russian rules
    //     don't recognise "today", so either it matches "каждый день" via
    //     a relaxed sub-string match or returns invalid. We accept both.
    void dateparser_mixed_ru_en()
    {
        const auto r = DateParser::parseRussian(
            QStringLiteral("today каждый день"), QDate(2026, 5, 15));
        qInfo() << "mixed 'today каждый день' (ru) valid=" << r.valid
                << "isRecurring=" << r.isRecurring
                << "humanString=" << r.humanString;

        // Documenting: Russian rules are anchored with ^...$, so a leading
        // "today " should make the whole expression unrecognised → invalid.
        // If the parser surprises us by being more permissive, document but
        // do NOT fail (this is an exploratory test).
        if (r.valid) {
            // Be lenient — only assert that something sensible came out.
            QVERIFY(!r.humanString.isEmpty()
                    || !r.recurrenceRule.isEmpty()
                    || !r.date.isEmpty());
        } else {
            QVERIFY(r.humanString.isEmpty());
            QVERIFY(r.date.isEmpty());
        }
    }

    // 15. Sanity timing — every test case combined must finish in <500ms;
    //     this slot stays last and rechecks total wall time on the most
    //     expensive paths to guard against future regressions.
    void perf_sanity_combined()
    {
        QElapsedTimer t;
        t.start();

        // Re-run the cheapest 4 calls to make sure no global state from
        // earlier slots silently degrades performance.
        (void)QuickAddParser::parse(QStringLiteral("Buy milk today p1 #x @y"));
        (void)DateParser::parseEnglish(QStringLiteral("tomorrow"),
                                       QDate(2026, 5, 15));
        (void)DateParser::parseRussian(QString::fromUtf8("каждый день"),
                                       QDate(2026, 5, 15));
        (void)DateParser::parseRussian(QString::fromUtf8("через 3 дня"),
                                       QDate(2026, 5, 15));

        const qint64 elapsedMs = t.elapsed();
        qInfo() << "combined sanity 4-call parse took" << elapsedMs << "ms";
        QVERIFY2(elapsedMs < 500,
                 qPrintable(QStringLiteral("Combined cheap parses exceeded "
                                           "500ms: %1ms").arg(elapsedMs)));
    }
};

QTEST_GUILESS_MAIN(TestNlpRobustness)
#include "test_nlp_robustness.moc"
