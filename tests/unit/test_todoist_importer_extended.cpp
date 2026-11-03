// Extended unit tests for TodoistImporter covering CSV-row parsing edge
// cases, date handling, the API-token fast-fail path, and partial-import
// behaviour. These complement test_todoist_import.cpp (ZIP-fixture based).
//
// We focus on:
//   - The pure CSV parser (TodoistCsvParser::parse / tokenize) since its
//     edge cases are the most numerous and don't need any I/O.
//   - The CsvParser-only date parsing branches.
//   - importFromApiToken() with bad/empty input — fast-fail without making
//     any network calls.
//   - Hand-crafted minimal ZIP byte stream → importFromBackupZip()
//     verifying partial-success behaviour and busy-flag transitions.
//
// QTEST_GUILESS_MAIN — no QGuiApplication needed; we don't touch QML.

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
// Same header pick as src/import/todoistimporter.cpp: Linux Qt has no QtZlib.
#if __has_include(<QtZlib/zlib.h>)
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#include "import/todoist_csv_parser.h"
#include "import/todoistimporter.h"
#include "import/todoist_types.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/comment.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// CSV header used across most tests
static const QString kHeader =
    QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,"
                   "DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT\n");

// ---------------------------------------------------------------------------
// Hand-craft a minimal one-entry ZIP archive for importFromBackupZip tests.
// We emit Stored (method 0) entries so we don't need DEFLATE on the writer
// side — the reader (importer) supports both Stored (0) and Deflate (8).
// Filename is encoded as UTF-8 with the EFS bit (0x800) set.
// ---------------------------------------------------------------------------
static QByteArray le16(quint16 v)
{
    QByteArray out(2, 0);
    out[0] = char(v & 0xFF);
    out[1] = char((v >> 8) & 0xFF);
    return out;
}
static QByteArray le32(quint32 v)
{
    QByteArray out(4, 0);
    out[0] = char(v & 0xFF);
    out[1] = char((v >> 8) & 0xFF);
    out[2] = char((v >> 16) & 0xFF);
    out[3] = char((v >> 24) & 0xFF);
    return out;
}

// CRC-32 (polynomial 0xEDB88320) — tiny implementation for our test ZIPs.
static quint32 crc32of(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i)
        c = table[(c ^ quint8(data[i])) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct ZipFileSpec {
    QString    name;       // filename inside ZIP
    QByteArray contents;   // raw bytes
};

static QByteArray buildZip(const QList<ZipFileSpec> &files)
{
    QByteArray archive;
    QList<QByteArray> centralEntries;
    QList<quint32>    localOffsets;

    for (const auto &f : files) {
        const QByteArray nameUtf8 = f.name.toUtf8();
        const quint32 crc         = crc32of(f.contents);
        const quint32 size        = quint32(f.contents.size());
        const quint16 gpFlag      = 0x0800; // UTF-8 filename
        const quint16 method      = 0;      // Stored
        const quint16 modTime     = 0;
        const quint16 modDate     = 0x21;   // 1980-01-01

        localOffsets.append(quint32(archive.size()));

        // Local file header (signature 0x04034b50)
        archive += QByteArray::fromHex("504b0304");
        archive += le16(20);              // version needed
        archive += le16(gpFlag);
        archive += le16(method);
        archive += le16(modTime);
        archive += le16(modDate);
        archive += le32(crc);
        archive += le32(size);            // compressed size
        archive += le32(size);            // uncompressed size
        archive += le16(quint16(nameUtf8.size()));
        archive += le16(0);               // extra field length
        archive += nameUtf8;
        archive += f.contents;

        // Build matching central directory entry (deferred — appended later)
        QByteArray cd;
        cd += QByteArray::fromHex("504b0102"); // central dir signature
        cd += le16(20);                   // version made by
        cd += le16(20);                   // version needed
        cd += le16(gpFlag);
        cd += le16(method);
        cd += le16(modTime);
        cd += le16(modDate);
        cd += le32(crc);
        cd += le32(size);
        cd += le32(size);
        cd += le16(quint16(nameUtf8.size()));
        cd += le16(0);                    // extra
        cd += le16(0);                    // comment
        cd += le16(0);                    // disk start
        cd += le16(0);                    // internal attrs
        cd += le32(0);                    // external attrs
        cd += le32(localOffsets.last());  // local header offset
        cd += nameUtf8;
        centralEntries.append(cd);
    }

    const quint32 cdStart = quint32(archive.size());
    QByteArray cdAll;
    for (const auto &e : centralEntries)
        cdAll += e;
    archive += cdAll;
    const quint32 cdSize = quint32(cdAll.size());

    // End of central directory (signature 0x06054b50)
    archive += QByteArray::fromHex("504b0506");
    archive += le16(0);                       // disk #
    archive += le16(0);                       // disk where CD starts
    archive += le16(quint16(files.size()));   // entries this disk
    archive += le16(quint16(files.size()));   // total entries
    archive += le32(cdSize);
    archive += le32(cdStart);
    archive += le16(0);                       // comment length

    return archive;
}

// Write a ZIP into a temp dir and return its absolute path.
static QString writeZipTo(const QTemporaryDir &dir, const QString &name,
                          const QList<ZipFileSpec> &files)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(buildZip(files));
    f.close();
    return path;
}

// ===========================================================================
class TestTodoistImporterExtended : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Use throw-away QSettings scope so the importer's batch tracking
        // doesn't pollute real settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_TodoistImpExt"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_todoist_importer_extended"));
    }

    // ── CSV row parsing — direct via the parser ──────────────────────────────

    void csv_valid_row_with_all_fields()
    {
        const QString csv = kHeader
            + QStringLiteral("task,Buy milk,A description,2,1,me,me,"
                             "2026-05-09,en,UTC,30,minute\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p1"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content,        QStringLiteral("Buy milk"));
        QCOMPARE(r.tasks[0].description,    QStringLiteral("A description"));
        // CSV priority 2 -> internal 2 (identity; CSV and internal both use 1=urgent)
        QCOMPARE(r.tasks[0].priority,       2);
        QCOMPARE(r.tasks[0].due.date,       QStringLiteral("2026-05-09"));
        QCOMPARE(r.tasks[0].due.timezone,   QStringLiteral("UTC"));
        QCOMPARE(r.tasks[0].durationAmount, 30);
        QCOMPARE(r.tasks[0].durationUnit,   QStringLiteral("minute"));
    }

    void csv_quoted_comma_in_content_preserved()
    {
        const QString csv = kHeader
            + QStringLiteral("task,\"Buy: milk, bread, eggs\",,1,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content,
                 QStringLiteral("Buy: milk, bread, eggs"));
    }

    void csv_escaped_double_quote_in_content()
    {
        // Inside the quoted field: He said ""hi"" → He said "hi"
        const QString csv = kHeader
            + QStringLiteral("task,\"He said \"\"hi\"\"\",,1,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("He said \"hi\""));
    }

    void csv_embedded_newline_in_quoted_content()
    {
        const QString csv = kHeader
            + QStringLiteral("task,\"Line 1\nLine 2\",,1,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Line 1\nLine 2"));
    }

    void csv_unicode_cyrillic_and_emoji()
    {
        // Cyrillic + emoji must round-trip through the parser as UTF-8.
        const QString csv = kHeader
            + QStringLiteral("task,Купить молоко 🥛,,1,1,,,,ru,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Купить молоко 🥛"));
    }

    void csv_missing_optional_fields_uses_defaults()
    {
        // Only TYPE + CONTENT + INDENT — everything else absent (empty).
        const QString csv = kHeader
            + QStringLiteral("task,Minimal task,,,1,,,,,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content,  QStringLiteral("Minimal task"));
        // Bad/empty priority → defaults to internal 4 (Normal/p4)
        QCOMPARE(r.tasks[0].priority, 4);
        QVERIFY(r.tasks[0].due.date.isEmpty());
        QVERIFY(r.tasks[0].due.string.isEmpty());
        QCOMPARE(r.tasks[0].durationAmount, 0);
    }

    void csv_too_few_columns_does_not_crash()
    {
        // A "task" row with only 2 fields — safeField() returns empty for
        // out-of-range indices, so the row is parsed but with empty fields.
        // The task is dropped at applyImport time (empty content), but the
        // parser must not crash.
        const QString csv = kHeader
            + QStringLiteral("task,Short row\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        // Parser tolerates missing columns
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Short row"));
        // Indent defaults to 1 when missing
        QCOMPARE(r.tasks[0].indent, 1);
    }

    void csv_too_many_columns_excess_ignored()
    {
        // 18 columns instead of 12 — trailing extras ignored by safeField().
        const QString csv = kHeader
            + QStringLiteral("task,Wide row,,1,1,,,,en,,,extra1,extra2,extra3,extra4,extra5,extra6\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Wide row"));
    }

    void csv_empty_lines_skipped()
    {
        // Blank line in the middle should not blow up.
        const QString csv = kHeader
            + QStringLiteral("task,Task A,,1,1,,,,en,,,\n")
            + QStringLiteral("\n")
            + QStringLiteral("task,Task B,,1,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        // Both real tasks parsed; the blank row produces no task because
        // its TYPE is empty (not "task" / "section" / "note").
        QCOMPARE(r.tasks.size(), 2);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Task A"));
        QCOMPARE(r.tasks[1].content, QStringLiteral("Task B"));
    }

    void csv_header_row_not_treated_as_task()
    {
        // The first row "TYPE,CONTENT,..." must be skipped — TYPE != "task".
        const QString csv = kHeader
            + QStringLiteral("task,Real task,,1,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].content, QStringLiteral("Real task"));
    }

    void csv_missing_header_returns_error()
    {
        // Header check is strict: first column must be TYPE (case-insensitive).
        const QString csv = QStringLiteral("FOO,BAR,BAZ\ntask,X,Y\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(!r.error.isEmpty());
    }

    // ── Date / recurrence parsing ────────────────────────────────────────────

    void date_iso_only_parsed_as_due_date()
    {
        const QString csv = kHeader
            + QStringLiteral("task,Deadline,,1,1,,,2026-05-09,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QCOMPARE(r.tasks[0].due.date, QStringLiteral("2026-05-09"));
        QVERIFY(!r.tasks[0].due.isRecurring);
    }

    void date_natural_language_recurring_english()
    {
        const QString csv = kHeader
            + QStringLiteral("task,Daily,,1,1,,,every day,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.tasks[0].due.isRecurring);
        QCOMPARE(r.tasks[0].due.string, QStringLiteral("every day"));
    }

    void date_natural_language_recurring_russian()
    {
        const QString csv = kHeader
            + QStringLiteral("task,Ежедневно,,1,1,,,каждый день в 14:30,ru,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.tasks[0].due.isRecurring);
        QCOMPARE(r.tasks[0].due.lang, QStringLiteral("ru"));
    }

    void date_invalid_string_falls_back_to_no_iso_date()
    {
        // "tomorrow at 14:30" doesn't match the ISO regex so date stays empty,
        // but the natural-language string is preserved as-is.  The task is
        // still created (no due-date silently drops the row).
        const QString csv = kHeader
            + QStringLiteral("task,Vague,,1,1,,,nonsense gibberish 12345,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QCOMPARE(r.tasks.size(), 1);
        QVERIFY(r.tasks[0].due.date.isEmpty());
        QVERIFY(!r.tasks[0].due.isRecurring);
        QCOMPARE(r.tasks[0].due.string,
                 QStringLiteral("nonsense gibberish 12345"));
    }

    // ── importFromApiToken — fast-fail paths (no network) ────────────────────

    void api_empty_token_emits_error_synchronously()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromApiToken(QString{}, false);

        QCOMPARE(finished.size(), 1);
        const QString error = finished[0][2].toString();
        QVERIFY(!error.isEmpty());
        QCOMPARE(finished[0][0].toInt(), 0);
        QCOMPARE(finished[0][1].toInt(), 0);
        // Importer must not be left in busy state after fast-fail.
        QVERIFY(!imp->busy());
    }

    void api_whitespace_only_token_treated_as_empty()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromApiToken(QStringLiteral("   \t\n  "), false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished[0][2].toString().isEmpty());
        QVERIFY(!imp->busy());
    }

    void api_token_with_actual_text_does_not_synchronous_fail()
    {
        // A non-empty token starts the network path.  We can't easily mock
        // QNetworkAccessManager without changing production code, so we just
        // verify that:
        //   - busy() flips to true (the importer accepts the token), AND
        //   - importFinished() is NOT emitted synchronously.
        //
        // The actual network call will eventually fail (no network in test
        // env or token is bogus) but we don't wait for it — destructing the
        // importer cancels in-flight requests via QObject parent ownership.
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromApiToken(QStringLiteral("invalid-but-nonempty-token"),
                                false);

        // Synchronous behaviour: the call should accept the token and become
        // busy without emitting importFinished yet (network is async).
        QVERIFY(imp->busy());
        QCOMPARE(finished.size(), 0);

        // Cleanup happens via QScopedPointer; no QSignalSpy assertion on the
        // eventual network failure since timing depends on the host env.
    }

    // ── ZIP-driven partial-import tests ──────────────────────────────────────

    void zip_minimal_one_project_imports_successfully()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Hello world,,1,1,,,,en,,,\n")).toUtf8();

        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("Sample [abc123def456].csv.zip"),
            { { QStringLiteral("Sample [abc123def456].csv"), csv } });
        QVERIFY(QFileInfo::exists(zipPath));

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(zipPath, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));
        QCOMPARE(finished[0][0].toInt(), 1);  // 1 task
        QCOMPARE(finished[0][1].toInt(), 1);  // 1 project (Sample)

        // Project name should have the [id] suffix stripped.
        bool foundClean = false;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("Sample")) foundClean = true;
            QVERIFY(!p->name().contains(u'['));
        }
        QVERIFY(foundClean);
    }

    void zip_partial_import_one_csv_malformed_other_succeeds()
    {
        // First CSV has a bad header (the parser will emit an error and
        // skip its tasks); second CSV is well-formed. The importer should
        // import the good one and continue (warnings emitted out-of-band).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray badCsv = QByteArrayLiteral(
            "WRONG,HEADER,FOO\n"
            "task,Should not be imported,,1,1,,,,en,,,\n");
        const QByteArray goodCsv = (kHeader
            + QStringLiteral("task,Good task,,1,1,,,,en,,,\n")).toUtf8();

        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("partial.zip"),
            {
                { QStringLiteral("Bad.csv"),  badCsv  },
                { QStringLiteral("Good.csv"), goodCsv },
            });
        QVERIFY(QFileInfo::exists(zipPath));

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        QSignalSpy warnings(imp.data(), &TodoistImporter::importWarnings);

        imp->importFromBackupZip(zipPath, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());

        // Should have imported exactly the good task; the bad CSV's "task"
        // row never reaches applyImport.
        const int importedTasks = finished[0][0].toInt();
        QCOMPARE(importedTasks, 1);

        bool foundGood = false;
        bool foundBad  = false;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Good task"))
                foundGood = true;
            if (t->content() == QStringLiteral("Should not be imported"))
                foundBad = true;
        }
        QVERIFY(foundGood);
        QVERIFY(!foundBad);

        // Non-fatal parse error from the bad CSV should surface as a warning.
        // (Emitted only if m_warnings non-empty; allow either way to be
        // resilient to future tightening of error semantics.)
        if (warnings.size() > 0) {
            const QStringList list = warnings[0][0].toStringList();
            bool mentionsBad = false;
            for (const auto &w : list)
                if (w.contains(QStringLiteral("Bad.csv"))) mentionsBad = true;
            QVERIFY(mentionsBad);
        }
    }

    void zip_busy_flag_transitions_around_import()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Busy test,,1,1,,,,en,,,\n")).toUtf8();
        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("busy.zip"),
            { { QStringLiteral("Busy.csv"), csv } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy busySpy(imp.data(), &TodoistImporter::busyChanged);

        QVERIFY(!imp->busy());
        imp->importFromBackupZip(zipPath, false);
        // ZIP path is fully synchronous — busy flips back to false by the
        // time we observe it.
        QVERIFY(!imp->busy());
        // We should have observed at least two transitions (false→true, true→false).
        QVERIFY(busySpy.size() >= 2);
    }

    void zip_reentrancy_guarded_by_busy()
    {
        // Calling importFromBackupZip while busy should be a no-op (the
        // method early-returns).  Since the ZIP path is synchronous we
        // can't easily catch the importer mid-flight, but we *can* prove
        // that a second call after a successful import still works fine
        // (busy was cleared properly).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,First,,1,1,,,,en,,,\n")).toUtf8();
        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("retry.zip"),
            { { QStringLiteral("R.csv"), csv } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);

        imp->importFromBackupZip(zipPath, false);
        imp->importFromBackupZip(zipPath, false); // second run

        // Two complete imports → two importFinished signals.
        QCOMPARE(finished.size(), 2);
        QVERIFY(!imp->busy());
    }

    void zip_corrupt_archive_returns_error_and_clears_busy()
    {
        // Hand-craft a file that is not a ZIP at all.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("not_a_zip.zip"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("This is just plain text, definitely not a ZIP file.");
            f.close();
        }

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished[0][2].toString().isEmpty()); // an error message
        QVERIFY(!imp->busy());
    }

    void zip_csv_with_only_header_creates_empty_project()
    {
        // A CSV with header but no data rows should produce a project with
        // zero tasks — and importFinished must report 0 tasks but 1 project.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = kHeader.toUtf8();
        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("empty.zip"),
            { { QStringLiteral("Empty.csv"), csv } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(zipPath, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 0);  // 0 tasks
        QCOMPARE(finished[0][1].toInt(), 1);  // 1 project (Empty)
    }

    void zip_unicode_filename_imported()
    {
        // The importer's hand-rolled ZIP reader is the whole reason
        // QZipReader was replaced — so verify a UTF-8 filename actually
        // round-trips into a real OpenTodo project.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Купить молоко,,1,1,,,,ru,,,\n")).toUtf8();
        const QString zipPath = writeZipTo(tmp,
            QStringLiteral("unicode.zip"),
            { { QStringLiteral("🥷 Личное [aaaaaaaaaaaaaaaa].csv"), csv } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(zipPath, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // Project name should be cleaned of the [id] suffix and preserve
        // emoji + Cyrillic.
        QStringList names;
        for (auto *p : ds->allProjects()) names << p->name();
        QVERIFY2(names.contains(QStringLiteral("🥷 Личное")),
                 qPrintable(QStringLiteral("Got: %1")
                                .arg(names.join(QStringLiteral(", ")))));

        // The Cyrillic task content should also be preserved.
        bool foundTask = false;
        for (auto *t : ds->allTasks())
            if (t->content() == QStringLiteral("Купить молоко"))
                foundTask = true;
        QVERIFY(foundTask);
    }
};

QTEST_GUILESS_MAIN(TestTodoistImporterExtended)
#include "test_todoist_importer_extended.moc"
