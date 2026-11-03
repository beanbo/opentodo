// Tests for ClaudeCodeLauncher's command-line construction (Infra layer).
//
// launch() itself only does anything on Windows, but the prompt sanitising
// and the wt.exe / cmd.exe argument lists are pure static helpers compiled on
// every platform, so the quoting rules are pinned here on any CI host. Nothing
// is spawned.

#include <QtTest>
#include <QRegularExpression>

#include "services/claudecodelauncher.h"

namespace {

// Windows Terminal's subcommand delimiter (AppCommandlineArgs.cpp,
// _commandDelimiterRegex): a ';' at the start or not preceded by '\'.
const QRegularExpression kWtDelimiter(QStringLiteral("(^;|[^\\\\];)"));

}  // namespace

class TestClaudeCodeLauncher : public QObject
{
    Q_OBJECT

private slots:
    void prompt_flattensAndNeutralisesCmdMetacharacters()
    {
        const QString p = ClaudeCodeLauncher::buildPrompt(
            QStringLiteral("Fix \"login\"\r\nsecond line\x1b[31m 100% done %PATH%"),
            QStringLiteral("details\there"));

        // One line, no control characters except tab.
        QVERIFY(!p.contains(QLatin1Char('\n')));
        QVERIFY(!p.contains(QLatin1Char('\r')));
        QVERIFY(!p.contains(QChar(0x1b)));
        QVERIFY(p.contains(QLatin1Char('\t')));
        // A '"' would toggle cmd.exe's quote state; '%' is expanded by cmd
        // even inside quotes.
        QVERIFY(!p.contains(QLatin1Char('"')));
        QVERIFY(!p.contains(QLatin1Char('%')));
        QVERIFY(p.contains(QStringLiteral("Fix 'login'")));
        QVERIFY(p.contains(QStringLiteral("100") + QChar(0xFF05)));
        QVERIFY(p.contains(QChar(0xFF05) + QStringLiteral("PATH") + QChar(0xFF05)));
        // Content, description and the CLI hint all survive.
        QVERIFY(p.startsWith(QStringLiteral("Fix 'login'  second line")));
        QVERIFY(p.contains(QStringLiteral("details\there")));
        QVERIFY(p.contains(QStringLiteral("opentodo-cli list --json")));
        // Always contains a space, so Qt and wt always wrap it in quotes.
        QVERIFY(p.contains(QLatin1Char(' ')));
    }

    void prompt_emptyDescriptionIsOmitted()
    {
        const QString p = ClaudeCodeLauncher::buildPrompt(QStringLiteral("Task"),
                                                          QStringLiteral("   "));
        QVERIFY(p.startsWith(QStringLiteral("Task (This repo is linked")));
    }

    void prompt_neverStartsWithDash()
    {
        // The prompt is one argv element of the claude CLI; a leading '-'
        // would let task text be parsed as a CLI option.
        const QString p = ClaudeCodeLauncher::buildPrompt(
            QStringLiteral("--allowedTools=Bash do things"), QString());
        QVERIFY(!p.startsWith(QLatin1Char('-')));
        QVERIFY(p.startsWith(QStringLiteral(" --allowedTools=Bash do things")));

        const QString plain = ClaudeCodeLauncher::buildPrompt(QStringLiteral("- item"), QString());
        QVERIFY(!plain.startsWith(QLatin1Char('-')));
    }

    void prompt_isCappedBelowCmdLineLimit()
    {
        // cmd.exe refuses command lines over 8191 characters.
        const QString p = ClaudeCodeLauncher::buildPrompt(
            QStringLiteral("Title"), QString(20000, QLatin1Char('a')));
        QVERIFY2(p.size() <= 6000, qPrintable(QString::number(p.size())));
        QVERIFY(p.contains(QChar(0x2026)));
        QVERIFY(p.endsWith(QStringLiteral("to capture follow-ups.)")));

        // The cut never leaves half a surrogate pair behind.
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");  // U+1F600
        QCOMPARE(emoji.size(), 2);
        QString many;
        for (int i = 0; i < 4000; ++i)
            many += emoji;
        for (int shift = 0; shift < 2; ++shift) {
            const QString q = ClaudeCodeLauncher::buildPrompt(
                QString(shift, QLatin1Char('x')) + many, QString());
            const int ell = q.indexOf(QChar(0x2026));
            QVERIFY(ell > 0);
            QVERIFY(!q.at(ell - 1).isHighSurrogate());
        }
    }

    void wtArgs_layoutAndCallPrefix()
    {
        const QStringList a = ClaudeCodeLauncher::windowsTerminalArgs(
            QStringLiteral("C:\\Repos\\My App"), QStringLiteral("C:\\Windows\\system32\\cmd.exe"),
            QStringLiteral("C:\\Users\\First Last\\AppData\\Roaming\\npm\\claude.cmd"),
            QStringLiteral("do it"));
        const QStringList expected = {
            QStringLiteral("-d"), QStringLiteral("C:\\Repos\\My App"),
            QStringLiteral("C:\\Windows\\system32\\cmd.exe"), QStringLiteral("/k"),
            // `call` keeps a quoted (space-containing) claude path from being
            // the first thing after /k, which would trip cmd's quote stripping.
            QStringLiteral("call"),
            QStringLiteral("C:\\Users\\First Last\\AppData\\Roaming\\npm\\claude.cmd"),
            QStringLiteral("do it"),
        };
        QCOMPARE(a, expected);
    }

    void wtArgs_escapeSubcommandDelimiters()
    {
        const QString prompt = ClaudeCodeLauncher::buildPrompt(
            QStringLiteral(";lead; mid;; and \\; already"), QString());
        const QStringList a = ClaudeCodeLauncher::windowsTerminalArgs(
            QStringLiteral("C:\\a;b"), QStringLiteral("C:\\Windows\\system32\\cmd.exe"),
            QStringLiteral("C:\\x\\claude.cmd"), prompt);
        for (const QString &arg : a)
            QVERIFY2(!kWtDelimiter.match(arg).hasMatch(), qPrintable(arg));

        // wt turns "\;" back into ";" (Commandline::AddArg) — the round trip
        // must restore the original text exactly.
        auto wtUnescape = [](QString s) { return s.replace(QStringLiteral("\\;"),
                                                           QStringLiteral(";")); };
        QCOMPARE(wtUnescape(a.at(1)), QStringLiteral("C:\\a;b"));
        QCOMPARE(wtUnescape(a.last()), prompt);
    }

    void cmdStartArgs_keepRepoOffTheCommandLine()
    {
        const QStringList a = ClaudeCodeLauncher::cmdStartArgs(
            QStringLiteral("C:\\Windows\\system32\\cmd.exe"),
            QStringLiteral("C:\\x\\claude.cmd"), QStringLiteral("p q"));
        const QStringList expected = {
            QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("Claude Code"),
            QStringLiteral("C:\\Windows\\system32\\cmd.exe"), QStringLiteral("/k"),
            QStringLiteral("call"), QStringLiteral("C:\\x\\claude.cmd"), QStringLiteral("p q"),
        };
        QCOMPARE(a, expected);
        QVERIFY(!a.contains(QStringLiteral("/D")));
    }
};

QTEST_GUILESS_MAIN(TestClaudeCodeLauncher)
#include "test_claudecodelauncher.moc"
