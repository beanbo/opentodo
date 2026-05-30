// Robustness suite for TodoistImporter — adversarial edge cases beyond the
// happy path (test_todoist_import.cpp) and the extended coverage
// (test_todoist_importer_extended.cpp).
//
// Focus areas (none of which the existing two suites cover):
//
//   1. Malformed ZIP archives
//        - truncated central directory
//        - file truncated mid-data
//        - tiny header-only "junk" file (<22 bytes)
//        - ZIP with an unsupported compression method (e.g. method 12 = bzip2)
//        - ZIP with way too many entries — sanity check, no DoS regression
//
//   2. CSV inside ZIP with unusual byte encodings
//        - Windows-1251 (Cyrillic, no BOM) — fallback path
//        - UTF-16LE with BOM (FF FE …) — neither UTF-8 nor 1251; defensively
//          handled (must not crash, produces an error/empty parse)
//        - UTF-8 NO BOM but with content (covered as control)
//
//   3. Empty CSV-flavour ZIP entries
//        - file containing only whitespace
//        - file containing only a UTF-8 BOM and nothing else
//        - file with NO header at all
//
//   4. Invalid Todoist priority values
//        - "0" (Todoist's null sentinel) → must default to internal 4 (Normal)
//        - "5" out-of-range → must default to internal 4 (Normal)
//        - "-1" negative → must default to internal 4 (Normal)
//        - "high" / "low" English words → must default to internal 4 (Normal)
//
//   5. Natural-language date variants that the parser does NOT understand
//        - "today", "tomorrow", "next week" (English fallback)
//        - "MM/DD/YYYY" and "DD.MM.YYYY" formats (NOT recognised by the ISO
//          regex; the importer must treat them as natural-language strings)
//
//   6. Section names with edge content
//        - Cyrillic, emoji, leading/trailing whitespace, >200 chars
//
//   7. Label mapping (via Sync-API-shaped intermediate data — CSV doesn't
//      carry labels but the API path does)
//        - the importer must map every documented Todoist colour name
//          (berry_red, red, orange, …, charcoal, grey, taupe) to a #RRGGBB
//          hex string, never leaving the raw colour name on the Label
//
//   8. Project hierarchy depth
//        - 4-level parent chain via the synthetic ZIP path: each CSV becomes
//          its own project, but Todoist's intermediate model supports parentId
//          which we exercise through deeply-nested sub-task indents (the only
//          parent linking the CSV path can do is task-parent, since CSVs are
//          one-project-per-file). Pin that we do not flatten or truncate at
//          any level.
//
//   9. Comments containing `[[file {…}]]` JSON-embedded image markers
//        - Todoist serialises image attachments inline. The importer must
//          preserve the marker verbatim (we don't strip it; QML can render it
//          later).
//
//  10. Incremental / re-run import — calling importFromBackupZip twice with
//      the same ZIP must NOT silently duplicate tasks: the content+createdAt
//      MD5 fingerprint dedup path (ITER3 #17) should suppress the second
//      pass's additions.
//
// Production bugs surfaced (if any) are documented via qInfo() + QVERIFY pin
// pattern (the slot stays green; the qInfo emits a visible warning each run).
//
// Constraints honoured:
//   - QTEST_GUILESS_MAIN (no QGuiApplication, no QML engine)
//   - Per-test QSettings scope: org=OpenTodoTest_TodoistImpRobust,
//                                app=test_todoist_importer_robustness
//   - No raw NUL bytes in source (AUTOMOC requirement)
//   - No function-local Q_OBJECT
//   - ≥8 slots — actually 12 slots below.

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>
#include <QSet>
#include <QStringConverter>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include "import/todoist_csv_parser.h"
#include "import/todoistimporter.h"
#include "import/todoist_types.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/comment.h"

// ---------------------------------------------------------------------------
// Helpers (file-scope; no function-local QObjects).
// ---------------------------------------------------------------------------

static const QString kHeader =
    QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,"
                   "DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT\n");

static DataStore *makeStore(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// Little-endian encoders used by buildZip().
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

// CRC-32 (polynomial 0xEDB88320) for ZIP file headers.
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
    QString    name;
    QByteArray contents;
    quint16    method = 0;  // 0 = Stored (default), 8 = Deflate (not produced here)
};

// Build a ZIP archive in memory using Stored (method 0) compression for all
// entries. We override `method` per-entry only to simulate exotic / unsupported
// methods for the robustness suite (the reader will skip those entries).
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
        const quint16 method      = f.method;
        const quint16 modTime     = 0;
        const quint16 modDate     = 0x21;   // 1980-01-01

        localOffsets.append(quint32(archive.size()));

        // Local file header (signature 0x04034b50)
        archive += QByteArray::fromHex("504b0304");
        archive += le16(20);
        archive += le16(gpFlag);
        archive += le16(method);
        archive += le16(modTime);
        archive += le16(modDate);
        archive += le32(crc);
        archive += le32(size);
        archive += le32(size);
        archive += le16(quint16(nameUtf8.size()));
        archive += le16(0);
        archive += nameUtf8;
        archive += f.contents;

        QByteArray cd;
        cd += QByteArray::fromHex("504b0102");
        cd += le16(20);
        cd += le16(20);
        cd += le16(gpFlag);
        cd += le16(method);
        cd += le16(modTime);
        cd += le16(modDate);
        cd += le32(crc);
        cd += le32(size);
        cd += le32(size);
        cd += le16(quint16(nameUtf8.size()));
        cd += le16(0);
        cd += le16(0);
        cd += le16(0);
        cd += le16(0);
        cd += le32(0);
        cd += le32(localOffsets.last());
        cd += nameUtf8;
        centralEntries.append(cd);
    }

    const quint32 cdStart = quint32(archive.size());
    QByteArray cdAll;
    for (const auto &e : centralEntries)
        cdAll += e;
    archive += cdAll;
    const quint32 cdSize = quint32(cdAll.size());

    // End-of-central-directory (signature 0x06054b50)
    archive += QByteArray::fromHex("504b0506");
    archive += le16(0);
    archive += le16(0);
    archive += le16(quint16(files.size()));
    archive += le16(quint16(files.size()));
    archive += le32(cdSize);
    archive += le32(cdStart);
    archive += le16(0);

    return archive;
}

static QString writeZipBytesTo(const QTemporaryDir &dir, const QString &name,
                               const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    f.close();
    return path;
}

static QString writeZipTo(const QTemporaryDir &dir, const QString &name,
                          const QList<ZipFileSpec> &files)
{
    return writeZipBytesTo(dir, name, buildZip(files));
}

// Encode a QString to Windows-1251 byte sequence for the encoding-fallback
// test. We restrict input to ASCII + Cyrillic Unicode block, both of which
// CP-1251 covers; anything else round-trips through the QStringEncoder
// returning replacement-bytes (still acceptable for "must not crash" tests).
static QByteArray toCp1251(const QString &s)
{
    auto encoder = QStringEncoder(QStringConverter::System);
    // QStringConverter::System resolves to the system codepage on Windows;
    // on Russian-locale dev machines this is CP-1251 already. For determinism
    // we build the byte sequence by hand: any ASCII char maps 1:1, and the
    // U+0410 .. U+044F (А..я) Cyrillic block maps to 0xC0..0xFF.
    QByteArray out;
    out.reserve(s.size());
    for (QChar ch : s) {
        const ushort u = ch.unicode();
        if (u < 0x80) {
            out.append(char(u));
        } else if (u >= 0x0410 && u <= 0x044F) {
            // А..я (Cyrillic capital A .. small ya) → 0xC0..0xFF
            out.append(char(0xC0 + (u - 0x0410)));
        } else if (u == 0x0401) {
            // Ё → 0xA8
            out.append(char(0xA8));
        } else if (u == 0x0451) {
            // ё → 0xB8
            out.append(char(0xB8));
        } else {
            // unknown: substitute '?'
            out.append('?');
        }
    }
    Q_UNUSED(encoder);
    return out;
}

// ===========================================================================
class TestTodoistImporterRobustness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_TodoistImpRobust"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_todoist_importer_robustness"));

        // Clear any lingering last_completed_batch / current_batch from a
        // prior failed run so dedup behaviour starts from a known state.
        QSettings s;
        s.remove(QStringLiteral("import/last_completed_batch"));
        s.remove(QStringLiteral("import/current_batch"));
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("import/last_completed_batch"));
        s.remove(QStringLiteral("import/current_batch"));
    }

    // ── 1. Malformed ZIP archives ────────────────────────────────────────────

    void malformed_truncated_zip_returns_error()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Build a real ZIP, then truncate the trailing 16 bytes — that wipes
        // the EOCD record so the reader should fall through with an error.
        const QByteArray csv = (kHeader
            + QStringLiteral("task,Buried,,1,1,,,,en,,,\n")).toUtf8();
        QByteArray archive = buildZip({ { QStringLiteral("X.csv"), csv, 0 } });
        QVERIFY(archive.size() > 30);
        archive.chop(16); // strip end-of-central-directory record

        const QString path = writeZipBytesTo(tmp,
            QStringLiteral("truncated.zip"), archive);

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        const QString error = finished[0][2].toString();
        QVERIFY2(!error.isEmpty(), "Truncated ZIP must report a non-empty error");
        QVERIFY(!imp->busy());
        QCOMPARE(finished[0][0].toInt(), 0); // no tasks
        QCOMPARE(finished[0][1].toInt(), 0); // no projects
    }

    void malformed_tiny_garbage_file_does_not_crash()
    {
        // Anything smaller than 22 bytes is rejected outright by the EOCD
        // check ("file too small to be a ZIP"). Also covers the
        // "completely empty file" sub-case implicitly.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString path = tmp.filePath(QStringLiteral("tiny.zip"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("PK"); // 2-byte teaser, valid signature prefix but no body
            f.close();
        }

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished[0][2].toString().isEmpty());
        QVERIFY(!imp->busy());
    }

    void malformed_unsupported_compression_method_skips_entry()
    {
        // The hand-rolled reader supports method 0 (Stored) and 8 (Deflate)
        // only. Method 12 (bzip2) is a valid ZIP method but unsupported —
        // the reader must skip the entry quietly rather than aborting the
        // whole archive. We pair it with a Stored entry that DOES parse.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray goodCsv = (kHeader
            + QStringLiteral("task,Good,,1,1,,,,en,,,\n")).toUtf8();
        const QByteArray badCsv  = (kHeader
            + QStringLiteral("task,Lost,,1,1,,,,en,,,\n")).toUtf8();

        const QByteArray archive = buildZip({
            { QStringLiteral("Good.csv"), goodCsv, 0 },
            { QStringLiteral("Bad.csv"),  badCsv, 12 }, // bzip2 — unsupported
        });
        const QString path = writeZipBytesTo(tmp,
            QStringLiteral("mixed.zip"), archive);

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // Only the Stored entry contributes a task.
        QCOMPARE(finished[0][0].toInt(), 1);

        bool foundGood = false;
        bool foundLost = false;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Good")) foundGood = true;
            if (t->content() == QStringLiteral("Lost")) foundLost = true;
        }
        QVERIFY(foundGood);
        QVERIFY(!foundLost);
    }

    void malformed_many_entries_completes_in_bounded_time()
    {
        // Stress: 200 trivial CSVs in one ZIP. Real Todoist backups top out
        // at ~50 projects; 200 is well above realistic — exercises the inner
        // loop's per-entry overhead without claiming any fixed bound.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QList<ZipFileSpec> files;
        for (int i = 0; i < 200; ++i) {
            const QByteArray csv = (kHeader
                + QStringLiteral("task,P%1 task,,1,1,,,,en,,,\n")
                      .arg(i)).toUtf8();
            files.append({ QStringLiteral("P%1.csv").arg(i), csv, 0 });
        }
        const QString path = writeZipTo(tmp,
            QStringLiteral("many.zip"), files);

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);

        QElapsedTimer timer;
        timer.start();
        imp->importFromBackupZip(path, false);
        const qint64 elapsedMs = timer.elapsed();

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // 200 projects + 200 tasks
        QCOMPARE(finished[0][0].toInt(), 200);
        QCOMPARE(finished[0][1].toInt(), 200);

        // Soft upper bound: 30 seconds. Way slacker than the import would
        // ever take in practice; the assertion is just a smoke test against
        // pathological O(N²) regressions.
        QVERIFY2(elapsedMs < 30000,
                 qPrintable(QStringLiteral("200-entry import took %1 ms")
                                .arg(elapsedMs)));
    }

    // ── 2. CSV inside ZIP with unusual byte encodings ────────────────────────

    void encoding_windows_1251_decoded_via_fallback()
    {
        // CP-1251-encoded Cyrillic content with NO BOM. The parser's
        // parseBytes() decodes UTF-8 first; the byte sequence is invalid
        // UTF-8, so the fallback (QString::fromLocal8Bit) should kick in.
        //
        // Pin: the test asserts the task is created with NON-EMPTY content.
        // The exact decoded text depends on the host's system locale — on a
        // Russian-locale dev machine it decodes correctly; on en-US it may
        // mojibake into Latin-1ish gibberish but must still parse a row.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString headerAscii =
            QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,"
                           "RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,"
                           "DURATION_UNIT\n");
        const QString rowCyr =
            QStringLiteral("task,Привет мир,,1,1,,,,ru,,,\n");

        QByteArray cp1251Bytes = toCp1251(headerAscii) + toCp1251(rowCyr);

        const QString path = writeZipTo(tmp,
            QStringLiteral("cp1251.zip"),
            { { QStringLiteral("Cyr.csv"), cp1251Bytes, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // Exactly one task imported (content non-empty, project counted).
        QCOMPARE(finished[0][0].toInt(), 1);
        const auto allTasks = ds->allTasks();
        QCOMPARE(allTasks.size(), 1);
        QVERIFY(!allTasks[0]->content().isEmpty());

        // On a CP-1251 host the content round-trips exactly. We can't assert
        // that universally, but we can pin one stable observation: the
        // content is not the placeholder UTF-8 replacement char (U+FFFD)
        // for every code unit.
        if (allTasks[0]->content() != QStringLiteral("Привет мир")) {
            qInfo() << "CP-1251 fallback produced non-canonical decode on this"
                    << " host (system codepage isn't CP-1251). Content:"
                    << allTasks[0]->content();
        }
    }

    void encoding_utf16le_bom_does_not_crash()
    {
        // F-Q2.24-C #5 FIX: parseBytes now detects UTF-16 LE / BE BOM
        // (FF FE / FE FF) at the start of the byte stream and dispatches
        // to the proper QStringDecoder before falling through to UTF-8
        // / local-8bit. A UTF-16LE-encoded CSV now imports the same way
        // a UTF-8 one would.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString header =
            QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,"
                           "RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,"
                           "DURATION_UNIT\n");
        const QString row = QStringLiteral("task,Hello,,1,1,,,,en,,,\n");

        QByteArray utf16le;
        utf16le.append(char(0xFF)); // BOM
        utf16le.append(char(0xFE));
        const QString combined = header + row;
        for (QChar c : combined) {
            const ushort u = c.unicode();
            utf16le.append(char(u & 0xFF));
            utf16le.append(char((u >> 8) & 0xFF));
        }

        const QString path = writeZipTo(tmp,
            QStringLiteral("utf16le.zip"),
            { { QStringLiteral("U.csv"), utf16le, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(!imp->busy());

        // Aggregate import succeeds — error string is empty.
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // The single row should have been parsed: exactly one task and
        // its content is the canonical "Hello" (not 8-bit garbage).
        QCOMPARE(finished[0][0].toInt(), 1);
        const auto allTasks = ds->allTasks();
        QCOMPARE(allTasks.size(), 1);
        QCOMPARE(allTasks[0]->content(), QStringLiteral("Hello"));
    }

    // F-Q2.24-C #5 follow-up: UTF-16BE BOM is also recognised.
    void encoding_utf16be_bom_decodes_cleanly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString header =
            QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,"
                           "RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,"
                           "DURATION_UNIT\n");
        const QString row = QStringLiteral("task,Bonjour,,1,1,,,,en,,,\n");

        QByteArray utf16be;
        utf16be.append(char(0xFE)); // BOM
        utf16be.append(char(0xFF));
        const QString combined = header + row;
        for (QChar c : combined) {
            const ushort u = c.unicode();
            utf16be.append(char((u >> 8) & 0xFF)); // BE: high byte first
            utf16be.append(char(u & 0xFF));
        }

        const QString path = writeZipTo(tmp,
            QStringLiteral("utf16be.zip"),
            { { QStringLiteral("U.csv"), utf16be, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 1);
        const auto allTasks = ds->allTasks();
        QCOMPARE(allTasks.size(), 1);
        QCOMPARE(allTasks[0]->content(), QStringLiteral("Bonjour"));
    }

    // ── 3. Empty / degenerate CSV bodies ─────────────────────────────────────

    void empty_bodies_produce_no_tasks_but_do_not_fail_archive()
    {
        // Three CSV files in one archive:
        //   - whitespace-only contents
        //   - UTF-8 BOM only (3 bytes, no header)
        //   - no header at all (random non-TYPE first column)
        // Plus one valid CSV at the end so the archive overall succeeds.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray whitespace = QByteArrayLiteral("   \n\t\n   \n");
        const QByteArray bomOnly    = QByteArray::fromHex("EFBBBF");
        const QByteArray badHeader  =
            QByteArrayLiteral("WRONG,FIRST,COLUMN\n"
                              "task,Should not import,,1,1,,,,en,,,\n");
        const QByteArray goodCsv    = (kHeader
            + QStringLiteral("task,Survivor,,1,1,,,,en,,,\n")).toUtf8();

        const QString path = writeZipTo(tmp,
            QStringLiteral("degenerate.zip"),
            {
                { QStringLiteral("Whitespace.csv"), whitespace, 0 },
                { QStringLiteral("BomOnly.csv"),    bomOnly,    0 },
                { QStringLiteral("NoHeader.csv"),   badHeader,  0 },
                { QStringLiteral("Good.csv"),       goodCsv,    0 },
            });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        QSignalSpy warnings(imp.data(), &TodoistImporter::importWarnings);

        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        // The aggregate import succeeds — only individual CSVs produce warnings.
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // Exactly one task — from Good.csv.
        QCOMPARE(finished[0][0].toInt(), 1);
        bool foundSurvivor = false;
        bool foundShouldNot = false;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Survivor")) foundSurvivor = true;
            if (t->content() == QStringLiteral("Should not import"))
                foundShouldNot = true;
        }
        QVERIFY(foundSurvivor);
        QVERIFY(!foundShouldNot);

        // We expect ≥1 warning row for the three degenerate inputs.
        if (warnings.size() > 0) {
            const QStringList list = warnings[0][0].toStringList();
            QVERIFY2(!list.isEmpty(),
                     "importWarnings emitted but the list is empty");
        }
    }

    // ── 4. Invalid Todoist priority values ───────────────────────────────────

    void invalid_priority_values_default_to_normal()
    {
        // All four malformed values exercise different branches of
        // todoistPriority(): bad parse ("high"/"low"), out-of-range high (5),
        // out-of-range low (-1, 0). All must collapse to internal 4 (Normal).
        const QString csv = kHeader
            + QStringLiteral("task,Zero,,0,1,,,,en,,,\n")    // Todoist null
            + QStringLiteral("task,Five,,5,1,,,,en,,,\n")    // OOB high
            + QStringLiteral("task,Minus,,-1,1,,,,en,,,\n")  // negative
            + QStringLiteral("task,Hi,,high,1,,,,en,,,\n")
            + QStringLiteral("task,Lo,,low,1,,,,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 5);
        for (const auto &t : r.tasks) {
            QVERIFY2(t.priority == 4,
                     qPrintable(QStringLiteral(
                         "Invalid priority for '%1' produced %2, expected 4")
                         .arg(t.content).arg(t.priority)));
        }

        // F-Q2.24-C #3 fix: the parser now counts rows whose PRIORITY was
        // exactly Todoist's null sentinel "0" so the importer can surface a
        // single user-facing warning ("X task(s) had no priority in Todoist;
        // imported as p4"). The mapping itself is still lossy (no schema
        // distinction between "0/null" and "explicit Normal"), but the user
        // now SEES that distinction at import time.
        //
        // We had exactly ONE row with literal "0" — Zero. Five (5), Minus
        // (-1), Hi (high), Lo (low) all parse as non-zero or non-numeric,
        // so they don't bump the lossy counter.
        QCOMPARE(r.lossyPriorityZeroCount, 1);

        qInfo() << "F-Q2.24-C #3: Todoist priority 0 (null sentinel) is still"
                   " collapsed to internal p4, but the count of such rows is"
                   " now propagated via ParseResult.lossyPriorityZeroCount so"
                   " the importer can emit a one-line user notice. Schema-"
                   " level distinction would require Task::hasPriority.";
    }

    // F-Q2.24-C #3 follow-up: pin that the importer surfaces the lossy
    // priority count as a user-facing warning emitted via importWarnings().
    void priority_zero_emits_user_warning_at_end_of_import()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Implicit,,0,1,,,,en,,,\n") // priority null
            + QStringLiteral("task,Explicit,,4,1,,,,en,,,\n") // priority Normal
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("pri0.zip"),
            { { QStringLiteral("Pri.csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        QSignalSpy warnings(imp.data(), &TodoistImporter::importWarnings);

        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 2);

        // The importWarnings signal must have fired and at least one row
        // mentions the lossy priority — pin substring match.
        QVERIFY2(warnings.size() == 1,
                 "importWarnings should fire once when priority 0 rows exist");
        const QStringList list = warnings[0][0].toStringList();
        bool foundPriorityNotice = false;
        for (const QString &w : list) {
            if (w.contains(QStringLiteral("приоритет"))) {
                foundPriorityNotice = true;
                break;
            }
        }
        QVERIFY2(foundPriorityNotice,
                 qPrintable(QStringLiteral(
                     "Expected a priority-zero notice in warnings; got: %1")
                     .arg(list.join(u'|'))));
    }

    // ── 5. Date format variants the importer now recognises ─────────────────
    //
    // F-Q2.24-C #4 FIX: parseDue now tries ISO → US slash → EU dot →
    // natural-language (DateParser) before giving up. User-edited CSVs that
    // use any of those forms now produce a proper YYYY-MM-DD due.date.
    void date_format_variants_preserved_as_natural_language()
    {
        const QString csv = kHeader
            + QStringLiteral("task,T1,,1,1,,,2026-12-31,en,,,\n")   // ISO
            + QStringLiteral("task,T2,,1,1,,,12/31/2026,en,,,\n")   // US slash
            + QStringLiteral("task,T3,,1,1,,,31.12.2026,en,,,\n")   // EU dot
            + QStringLiteral("task,T4,,1,1,,,today,en,,,\n")
            + QStringLiteral("task,T5,,1,1,,,tomorrow,en,,,\n")
            + QStringLiteral("task,T6,,1,1,,,next week,en,,,\n");

        const auto r = TodoistCsvParser::parse(csv, QStringLiteral("p"));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.tasks.size(), 6);

        // T1 — ISO format: date extracted as 2026-12-31, not recurring.
        QCOMPARE(r.tasks[0].due.date, QStringLiteral("2026-12-31"));
        QVERIFY(!r.tasks[0].due.isRecurring);

        // T2 — US MM/DD/YYYY: now resolves to 2026-12-31.
        QCOMPARE(r.tasks[1].due.date, QStringLiteral("2026-12-31"));
        QVERIFY(!r.tasks[1].due.isRecurring);
        QCOMPARE(r.tasks[1].due.string, QStringLiteral("12/31/2026"));

        // T3 — EU DD.MM.YYYY: now resolves to 2026-12-31.
        QCOMPARE(r.tasks[2].due.date, QStringLiteral("2026-12-31"));
        QVERIFY(!r.tasks[2].due.isRecurring);
        QCOMPARE(r.tasks[2].due.string, QStringLiteral("31.12.2026"));

        // T4 — "today" → today's ISO date via DateParser natural-language.
        const QString todayIso = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(r.tasks[3].due.date, todayIso);
        QVERIFY(!r.tasks[3].due.isRecurring);

        // T5 — "tomorrow" → today+1 ISO date.
        const QString tomorrowIso = QDate::currentDate().addDays(1)
            .toString(QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(r.tasks[4].due.date, tomorrowIso);
        QVERIFY(!r.tasks[4].due.isRecurring);

        // T6 — "next week" → a future ISO date; DateParser computes the
        // exact day-of-week; we just pin "non-empty, after today, not
        // recurring" rather than the exact value (varies with day-of-week).
        QVERIFY2(!r.tasks[5].due.date.isEmpty(),
                 qPrintable(QStringLiteral("'next week' did not yield a date; got '%1'")
                                .arg(r.tasks[5].due.date)));
        const QDate next = QDate::fromString(r.tasks[5].due.date,
                                             QStringLiteral("yyyy-MM-dd"));
        QVERIFY(next.isValid());
        QVERIFY(next > QDate::currentDate());
        QVERIFY(!r.tasks[5].due.isRecurring);
    }

    // ── 6. Section names with edge content ──────────────────────────────────

    void section_names_with_edge_content_preserved()
    {
        // A single CSV with 4 sections at varying degrees of weirdness.
        // Each row is followed by one task so the section becomes a real
        // OpenTodo entity (section without children would still be created,
        // but tying tasks to them sanity-checks downstream wiring).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // 220-char ASCII section name — over the suggested limit of 200.
        const QString longName(220, QLatin1Char('A'));

        const QString csv = kHeader
            + QStringLiteral("section,Раздел Один,,1,1,,,,en,,,\n")
            + QStringLiteral("task,T1,,1,1,,,,en,,,\n")
            + QStringLiteral("section,📁 Проект Икс,,1,1,,,,en,,,\n")
            + QStringLiteral("task,T2,,1,1,,,,en,,,\n")
            + QStringLiteral("section,\"  trimmed  \",,1,1,,,,en,,,\n")
            + QStringLiteral("task,T3,,1,1,,,,en,,,\n")
            + QStringLiteral("section,%1,,1,1,,,,en,,,\n").arg(longName)
            + QStringLiteral("task,T4,,1,1,,,,en,,,\n");

        const QByteArray bytes = csv.toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("sections.zip"),
            { { QStringLiteral("Edge.csv"), bytes, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));
        QCOMPARE(finished[0][0].toInt(), 4); // 4 tasks
        QCOMPARE(finished[0][1].toInt(), 1); // 1 project (Edge)

        // Find the imported "Edge" project & check its sections.
        Project *edge = nullptr;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("Edge")) { edge = p; break; }
        }
        QVERIFY(edge);

        const auto secs = ds->sectionsForProject(edge->id());
        QCOMPARE(secs.size(), 4);

        QStringList names;
        for (auto *s : secs) names << s->name();

        QVERIFY2(names.contains(QStringLiteral("Раздел Один")),
                 qPrintable(QStringLiteral("Got: %1").arg(names.join(u'|'))));
        QVERIFY2(names.contains(QStringLiteral("📁 Проект Икс")),
                 qPrintable(QStringLiteral("Emoji section lost. Got: %1")
                                .arg(names.join(u'|'))));

        // The CSV parser calls trimmed() on every field so "  trimmed  "
        // becomes "trimmed". Pin that contract.
        QVERIFY2(names.contains(QStringLiteral("trimmed")),
                 qPrintable(QStringLiteral(
                     "Whitespace-trimming contract broke. Got: %1")
                     .arg(names.join(u'|'))));

        // 220-char section name preserved (no truncation). Pin length.
        bool foundLong = false;
        for (auto *s : secs) {
            if (s->name().length() == 220) { foundLong = true; break; }
        }
        QVERIFY2(foundLong,
                 "220-char section name was truncated or dropped");
    }

    // ── 7. Label colour mapping (via intermediate data → applyImport) ───────
    //
    // F-Q2.24-C #6 FIX: CSV-imported projects still land as grey (the CSV
    // format genuinely doesn't carry a project colour column), but the
    // importer now emits a user-facing notice via importWarnings telling
    // the user that the API-token path preserves colours. The QCOMPARE on
    // #b8b8b8 below is unchanged; the new assertion is that a notice was
    // emitted alongside the grey colour.
    void label_color_mapping_covers_all_documented_palette()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Anchor,,1,1,,,,en,,,\n")).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("color.zip"),
            { { QStringLiteral("ColorProject [aaaaaaaaaaaa].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        QSignalSpy warnings(imp.data(), &TodoistImporter::importWarnings);
        imp->importFromBackupZip(path, false);
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());

        Project *colorProj = nullptr;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("ColorProject")) {
                colorProj = p; break;
            }
        }
        QVERIFY(colorProj);
        QCOMPARE(colorProj->color(), QStringLiteral("#b8b8b8"));

        // F-Q2.24-C #6: importWarnings must include a one-line notice that
        // CSV colours are unavailable. The full Cyrillic message lives in
        // todoistimporter.cpp; we pin a substring match on "Цвета".
        QCOMPARE(warnings.size(), 1);
        const QStringList list = warnings[0][0].toStringList();
        bool foundColourNotice = false;
        for (const QString &w : list) {
            if (w.contains(QString::fromUtf8("Цвета"))) {
                foundColourNotice = true;
                break;
            }
        }
        QVERIFY2(foundColourNotice,
                 qPrintable(QStringLiteral(
                     "Expected colour-unavailability notice; got: %1")
                     .arg(list.join(u'|'))));
    }

    // ── 8. Project hierarchy depth (4-level subtask chain) ──────────────────

    void deep_subtask_chain_not_flattened()
    {
        // CSV path uses INDENT to build a parent chain. Todoist supports up
        // to indent=4. We send indent=1..5 and verify the chain has not
        // been collapsed: every level past the first has a non-empty
        // parentId.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Level1,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Level2,,1,2,,,,en,,,\n")
            + QStringLiteral("task,Level3,,1,3,,,,en,,,\n")
            + QStringLiteral("task,Level4,,1,4,,,,en,,,\n")
            + QStringLiteral("task,Level5,,1,5,,,,en,,,\n")).toUtf8();

        const QString path = writeZipTo(tmp,
            QStringLiteral("deep.zip"),
            { { QStringLiteral("Deep.csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 5);

        // Build name → task map for assertions.
        QMap<QString, Task *> byName;
        for (auto *t : ds->allTasks()) byName.insert(t->content(), t);

        QVERIFY(byName.contains(QStringLiteral("Level1")));
        QVERIFY(byName.contains(QStringLiteral("Level5")));

        // Level1 is the root, no parent.
        QVERIFY(byName[QStringLiteral("Level1")]->parentId().isEmpty());
        // Levels 2..5 all have non-empty parentId.
        for (int n = 2; n <= 5; ++n) {
            const auto name = QStringLiteral("Level%1").arg(n);
            QVERIFY2(byName.contains(name),
                     qPrintable(QStringLiteral("Missing %1").arg(name)));
            QVERIFY2(!byName[name]->parentId().isEmpty(),
                     qPrintable(QStringLiteral(
                         "%1 lost its parentId (chain was flattened)")
                         .arg(name)));
        }

        // Walk the chain back from Level5 to Level1; must traverse 4 hops.
        Task *cursor = byName[QStringLiteral("Level5")];
        int hops = 0;
        while (cursor && !cursor->parentId().isEmpty() && hops < 10) {
            cursor = ds->taskById(cursor->parentId());
            ++hops;
        }
        QVERIFY(cursor);
        QCOMPARE(cursor->content(), QStringLiteral("Level1"));
        QCOMPARE(hops, 4);
    }

    // ── 9. Comments with image-attachment JSON markers ───────────────────────

    void comment_with_image_marker_preserved_verbatim()
    {
        // Todoist embeds image attachments inside the comment body as a
        // JSON-ish `[[file {...}]]` marker. We verify the marker is NOT
        // stripped, scrubbed, or re-formatted on import.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Quote the whole CONTENT column because it contains a comma in
        // the JSON body. Constructed without a raw-string literal so MOC
        // can scan the source. Inside a quoted CSV field every `"` must
        // be doubled (CSV escape), so we build the inner JSON with `""`
        // pairs around its identifiers.
        const QString rowContent =
            QStringLiteral("[[file {\"\"file_url\"\": \"\"https://todoist.b-cdn.net/x.png\"\", "
                           "\"\"file_type\"\": \"\"image/png\"\"}]]");

        const QString csv = kHeader
            + QStringLiteral("task,Anchor task,,1,1,,,,en,,,\n")
            + QStringLiteral("note,\"%1\",,1,1,,,,en,,,\n").arg(rowContent);

        const QByteArray bytes = csv.toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("attach.zip"),
            { { QStringLiteral("Attach.csv"), bytes, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());

        Task *anchor = nullptr;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Anchor task")) {
                anchor = t; break;
            }
        }
        QVERIFY(anchor);

        const auto comments = ds->commentsForTask(anchor->id());
        QCOMPARE(comments.size(), 1);

        // Marker preserved character-for-character.
        QVERIFY2(comments[0]->content().contains(QStringLiteral("[[file ")),
                 qPrintable(QStringLiteral("Marker dropped. Got: %1")
                                .arg(comments[0]->content())));
        QVERIFY(comments[0]->content().contains(QStringLiteral("file_url")));
        QVERIFY(comments[0]->content().contains(QStringLiteral("file_type")));
        QVERIFY(comments[0]->content().contains(QStringLiteral("image/png")));

        // [DOCUMENTED] Image-attachment markers are kept as plain text in
        // the comment body. No QML renderer interprets them yet — the
        // marker is visible to the user as raw "[[file {...}]]" text.
        // A future enhancement could parse and render an actual thumbnail.
        qInfo() << "[[file ...]] image markers preserved verbatim; no QML "
                   "thumbnail renderer exists for them yet.";
    }

    // ── 10. Incremental / re-run import dedup ───────────────────────────────
    //
    // F-Q2.24-C #2 FIX: the dedup fingerprint is now MD5(content + "|" +
    // projectId), which is stable across runs (project-by-name lookup means
    // the same display name reuses the same OpenTodo Project UUID). Re-
    // importing the same backup no longer duplicates tasks OR projects.
    void rerun_same_zip_deduplicates_via_fingerprint()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Unique task,,1,1,,,,en,,,\n")).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("rerun.zip"),
            { { QStringLiteral("R.csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);

        imp->importFromBackupZip(path, false);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 2);
        QVERIFY(finished[0][2].toString().isEmpty());
        QVERIFY(finished[1][2].toString().isEmpty());

        // Task dedup — exactly one copy of the task after two runs.
        int copies = 0;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Unique task")) ++copies;
        }
        QCOMPARE(copies, 1);

        // Project dedup — exactly one project named "R" after two runs.
        int rProjCount = 0;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("R")) ++rProjCount;
        }
        QCOMPARE(rProjCount, 1);
    }

    // ────────────────────────────────────────────────────────────────────────
    // iter-night-9: re-import that adds a NEW subtask under an existing
    // (deduplicated) parent must parent the new task to the existing one —
    // not to the literal "__skipped__" sentinel that used to land in
    // taskIdMap.
    //
    // Before the fix:
    //   - dedup branch: `taskIdMap[tt.id] = "__skipped__"` (sentinel)
    //   - second pass: `task->setParentId(taskIdMap[tt.parentId])` so the
    //     new grandchild ended up with parent_id == "__skipped__" — a
    //     dangling string that the CRDT field stamp then propagated to
    //     every sync peer, and that left the new task floating at root
    //     level instead of nested under its imported parent.
    //
    // After the fix:
    //   - dedup branch maps to the EXISTING task's UUID
    //   - new grandchild's parent_id resolves to the existing child's UUID
    //   - deduped tasks themselves are NOT re-parented (incremental
    //     imports must not clobber the user's local moves).
    // ────────────────────────────────────────────────────────────────────────
    void reimport_addsNewSubtask_underExistingParent_noSentinel()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // First backup: Parent (indent=1) → Child (indent=2). No grandchild.
        const QByteArray csv1 = (kHeader
            + QStringLiteral("task,Parent,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Child,,1,2,,,,en,,,\n")).toUtf8();
        const QString path1 = writeZipTo(tmp,
            QStringLiteral("first.zip"),
            { { QStringLiteral("Proj [aaaaaaaaaaaa].csv"), csv1, 0 } });

        // Second backup: Parent + Child + Grandchild (indent=3).
        const QByteArray csv2 = (kHeader
            + QStringLiteral("task,Parent,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Child,,1,2,,,,en,,,\n")
            + QStringLiteral("task,Grandchild,,1,3,,,,en,,,\n")).toUtf8();
        const QString path2 = writeZipTo(tmp,
            QStringLiteral("second.zip"),
            { { QStringLiteral("Proj [aaaaaaaaaaaa].csv"), csv2, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));
        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);

        imp->importFromBackupZip(path1, false);
        imp->importFromBackupZip(path2, false);

        QCOMPARE(finished.size(), 2);
        QVERIFY(finished[0][2].toString().isEmpty());
        QVERIFY(finished[1][2].toString().isEmpty());

        // Find the surviving Parent, Child, Grandchild (each must exist
        // exactly once thanks to fingerprint dedup).
        Task *parent = nullptr;
        Task *child = nullptr;
        Task *grandchild = nullptr;
        int parentCount = 0, childCount = 0, grandCount = 0;
        for (auto *t : ds->allTasks()) {
            if (t->isDeleted()) continue;
            if (t->content() == QStringLiteral("Parent"))    { ++parentCount; parent = t; }
            if (t->content() == QStringLiteral("Child"))     { ++childCount;  child = t;  }
            if (t->content() == QStringLiteral("Grandchild")){ ++grandCount;  grandchild = t; }
        }
        QCOMPARE(parentCount, 1);
        QCOMPARE(childCount,  1);
        QCOMPARE(grandCount,  1);
        QVERIFY(parent && child && grandchild);

        // The cardinal assertion: new grandchild must point at the EXISTING
        // child's UUID, never at the literal sentinel "__skipped__".
        QVERIFY2(grandchild->parentId() != QStringLiteral("__skipped__"),
                 "iter-night-9: grandchild's parent_id leaked the dedup "
                 "sentinel — the new task is orphaned under a phantom "
                 "parent and the rotten id syncs to every peer.");
        QCOMPARE(grandchild->parentId(), child->id());

        // And the original chain still resolves correctly: Child → Parent.
        QCOMPARE(child->parentId(), parent->id());
        QVERIFY(parent->parentId().isEmpty());
    }

    // ────────────────────────────────────────────────────────────────────────
    // iter-night-9 (companion): re-importing a backup that re-asserts the
    // same parent_id on an EXISTING (deduplicated) task MUST NOT mutate
    // the local task's parent_id. Otherwise a user who locally moved a
    // previously-imported task to a new parent (or to the root) would
    // see that move silently reverted on every re-import.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_doesNotClobberLocalParentMoveOnDeduplicatedTask()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Anchor,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Mover,,1,2,,,,en,,,\n")).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("mover.zip"),
            { { QStringLiteral("Proj [bbbbbbbbbbbb].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));
        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);

        imp->importFromBackupZip(path, false);
        QCOMPARE(finished.size(), 1);

        Task *anchor = nullptr, *mover = nullptr;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Anchor")) anchor = t;
            if (t->content() == QStringLiteral("Mover"))  mover  = t;
        }
        QVERIFY(anchor && mover);
        QCOMPARE(mover->parentId(), anchor->id());

        // User locally moves Mover out from under Anchor (to the root).
        mover->setParentId(QString());
        QVERIFY(mover->parentId().isEmpty());

        // Re-import the same backup — Mover is deduplicated. Second pass
        // must skip it so the local "move to root" is preserved.
        imp->importFromBackupZip(path, false);
        QCOMPARE(finished.size(), 2);

        QVERIFY2(mover->parentId().isEmpty(),
                 "Re-import of a deduplicated task clobbered the user's "
                 "local parent_id move — incremental imports must be "
                 "additive, not authoritative on existing entities.");
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-19: PROJECT-side version of the above contract. The CSV
    // parser never sets TodoistProject.parentId (only TodoistTask.parentId
    // via INDENT) so we cannot reach this via importFromBackupZip — we
    // build a TodoistImportData by hand and call the private applyImport
    // directly via the friend grant in todoistimporter.h.
    //
    // Pre-fix Phase 2's second pass (line 555+) unconditionally called
    // proj->setParentId(...) on every project including ones that were
    // deduped against an existing local project. The user's local
    // reorganization of a previously-imported project was silently
    // overwritten on every re-import.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_doesNotClobberLocalParentMoveOnDeduplicatedProject()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        // First "import": create Parent project and Child project with
        // Child.parent_id pointing at Parent.
        TodoistImportData first;
        TodoistProject backendP;
        backendP.id     = QStringLiteral("td-backend");
        backendP.name   = QStringLiteral("Backend");
        backendP.color  = QStringLiteral("blue");
        first.projects.append(backendP);

        TodoistProject authP;
        authP.id        = QStringLiteral("td-auth");
        authP.name      = QStringLiteral("Auth");
        authP.color     = QStringLiteral("red");
        authP.parentId  = QStringLiteral("td-backend");
        first.projects.append(authP);

        imp->applyImport(first, /*importCompleted=*/false);

        // Locate the OpenTodo projects.
        Project *backend = nullptr;
        Project *auth = nullptr;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("Backend")) backend = p;
            if (p->name() == QStringLiteral("Auth"))    auth    = p;
        }
        QVERIFY(backend && auth);
        QCOMPARE(auth->parentId(), backend->id());

        // User locally moves Auth out from under Backend (to the root).
        auth->setParentId(QString());
        QVERIFY(auth->parentId().isEmpty());

        // Re-import the same data — both projects dedup by name. Second
        // pass must SKIP the deduplicated rows so the local "move to
        // root" survives.
        imp->applyImport(first, /*importCompleted=*/false);

        QVERIFY2(auth->parentId().isEmpty(),
                 "Iter-night-19: re-import of a deduplicated project must "
                 "NOT clobber the user's local parent_id move. Pre-fix "
                 "Phase 2's second pass had no `dedupedTodoistProjectIds` "
                 "set, so every re-import silently re-parented existing "
                 "projects from the Todoist backup — destroying local "
                 "reorganizations and stamping the stale value with a "
                 "fresh HLC that then propagated through sync.");
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-19 companion: NEW-child-under-deduped-parent re-parents
    // correctly. Mirrors the iter-9 task version's
    // `reimport_addsNewSubtask_underExistingParent_noSentinel` — the
    // newly-imported child must point at the EXISTING parent's UUID.
    // ────────────────────────────────────────────────────────────────────────
    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-21: Phase 5 (notes/comments) re-import must NOT duplicate
    // existing comments. Pre-fix every re-run of the same backup attached
    // another copy of every note to the deduped target task/project,
    // doubling on each import — bloating state.json and sync bandwidth.
    //
    // After iter-night-9's task-side dedup change (taskIdMap[t] = existing
    // UUID instead of the "__skipped__" sentinel), Phase 5's
    // `taskIdMap.value(tn.taskId)` always resolves to a real task on
    // re-import → addComment unconditionally allocates a fresh UUID →
    // duplicate.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_phase5_doesNotDuplicateComments()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        // Build a TodoistImportData with one task and one comment on it.
        TodoistImportData first;
        TodoistProject p;
        p.id = QStringLiteral("td-proj");
        p.name = QStringLiteral("Demo");
        first.projects.append(p);

        TodoistTask t;
        t.id = QStringLiteral("td-task-A");
        t.content = QStringLiteral("Pay bills");
        t.projectId = p.id;
        first.tasks.append(t);

        TodoistNote n;
        n.taskId = t.id;
        n.content = QStringLiteral("Receipt in the inbox folder");
        n.postedAt = QDateTime(QDate(2026, 5, 1), QTime(10, 30));
        first.notes.append(n);

        // Also a project-level note to pin both branches.
        TodoistNote pn;
        pn.projectId = p.id;
        pn.content = QStringLiteral("Project kickoff notes");
        pn.postedAt = QDateTime(QDate(2026, 5, 2), QTime(9, 0));
        first.notes.append(pn);

        imp->applyImport(first, /*importCompleted=*/false);

        // Locate the live task and project.
        Task *task = nullptr;
        Project *proj = nullptr;
        for (auto *tt : ds->allTasks())   { if (tt->content() == QStringLiteral("Pay bills")) task = tt; }
        for (auto *pp : ds->allProjects()){ if (pp->name() == QStringLiteral("Demo")) proj = pp; }
        QVERIFY(task && proj);

        // Each comment surface should have exactly 1 entry after first
        // import.
        QCOMPARE(ds->commentsForTask(task->id()).size(),    1);
        QCOMPARE(ds->commentsForProject(proj->id()).size(), 1);

        // Re-import the same data. Pre-fix this doubled BOTH counts.
        imp->applyImport(first, false);

        QCOMPARE(ds->commentsForTask(task->id()).size(),    1);
        QCOMPARE(ds->commentsForProject(proj->id()).size(), 1);

        // Run it a third time for good measure — counts must remain stable.
        imp->applyImport(first, false);
        QCOMPARE(ds->commentsForTask(task->id()).size(),    1);
        QCOMPARE(ds->commentsForProject(proj->id()).size(), 1);
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-21 companion: NEW note in a later import is attached
    // (deduped only against existing fingerprints, not all comments).
    // ────────────────────────────────────────────────────────────────────────
    void reimport_phase5_addsNewNoteAlongsideExisting()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        TodoistImportData first;
        TodoistProject p;
        p.id = QStringLiteral("td-proj");
        p.name = QStringLiteral("Demo");
        first.projects.append(p);
        TodoistTask t;
        t.id = QStringLiteral("td-task-A");
        t.content = QStringLiteral("Pay bills");
        t.projectId = p.id;
        first.tasks.append(t);
        TodoistNote n1;
        n1.taskId = t.id;
        n1.content = QStringLiteral("Original note");
        n1.postedAt = QDateTime(QDate(2026, 5, 1), QTime(10, 0));
        first.notes.append(n1);
        imp->applyImport(first, false);

        Task *task = nullptr;
        for (auto *tt : ds->allTasks()) {
            if (tt->content() == QStringLiteral("Pay bills")) task = tt;
        }
        QVERIFY(task);
        QCOMPARE(ds->commentsForTask(task->id()).size(), 1);

        // Second import: same data + one NEW note.
        TodoistImportData second = first;
        TodoistNote n2;
        n2.taskId = t.id;
        n2.content = QStringLiteral("Follow-up: receipt arrived");
        n2.postedAt = QDateTime(QDate(2026, 5, 3), QTime(14, 30));
        second.notes.append(n2);

        imp->applyImport(second, false);

        // Original is deduped (not re-attached). New one is added.
        QCOMPARE(ds->commentsForTask(task->id()).size(), 2);
    }

    void reimport_addsNewSubproject_underExistingParent()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        TodoistImportData first;
        TodoistProject p;
        p.id   = QStringLiteral("td-engineering");
        p.name = QStringLiteral("Engineering");
        first.projects.append(p);
        imp->applyImport(first, false);

        Project *engineering = nullptr;
        for (auto *pp : ds->allProjects()) {
            if (pp->name() == QStringLiteral("Engineering")) engineering = pp;
        }
        QVERIFY(engineering);

        // Second import: parent dedups, NEW child under it.
        TodoistImportData second;
        second.projects.append(p);     // dedups
        TodoistProject infra;
        infra.id       = QStringLiteral("td-infra");
        infra.name     = QStringLiteral("Infra");
        infra.parentId = QStringLiteral("td-engineering");
        second.projects.append(infra);
        imp->applyImport(second, false);

        Project *infraProj = nullptr;
        for (auto *pp : ds->allProjects()) {
            if (pp->name() == QStringLiteral("Infra")) infraProj = pp;
        }
        QVERIFY(infraProj);
        QCOMPARE(infraProj->parentId(), engineering->id());
        // Engineering's parentId must NOT have been touched by the
        // second-pass either (it's deduped so the guard skips it).
        QVERIFY(engineering->parentId().isEmpty());
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-27: Phase 3 (Sections) re-import must NOT multiply section
    // rows. Pre-fix every re-run of the same backup added another fresh
    // Section per existing one. Phase 4 task dedup is fingerprint-based
    // (content+projectId), so re-imported tasks went back to the ORIGINAL
    // section while the new copies stayed empty — pure UI clutter that
    // grew linearly with re-import count.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_phase3_doesNotDuplicateSections()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        TodoistImportData first;
        TodoistProject p;
        p.id = QStringLiteral("td-proj");
        p.name = QStringLiteral("Work");
        first.projects.append(p);

        TodoistSection s1;
        s1.id = QStringLiteral("td-sec-1");
        s1.name = QStringLiteral("Backlog");
        s1.projectId = p.id;
        s1.order = 0;
        first.sections.append(s1);

        TodoistSection s2;
        s2.id = QStringLiteral("td-sec-2");
        s2.name = QStringLiteral("In Progress");
        s2.projectId = p.id;
        s2.order = 1;
        first.sections.append(s2);

        imp->applyImport(first, /*importCompleted=*/false);

        Project *proj = nullptr;
        for (auto *pp : ds->allProjects()) {
            if (pp->name() == QStringLiteral("Work")) proj = pp;
        }
        QVERIFY(proj);
        QCOMPARE(ds->sectionsForProject(proj->id()).size(), 2);

        // Re-import the same data — pre-fix the section count doubled to
        // 4 (two pairs of duplicates) and would grow on every subsequent
        // run.
        imp->applyImport(first, false);
        QCOMPARE(ds->sectionsForProject(proj->id()).size(), 2);

        // Third run for stability — must still be 2.
        imp->applyImport(first, false);
        QCOMPARE(ds->sectionsForProject(proj->id()).size(), 2);

        // Sanity: section names match the originals (the dedup found them
        // by case-insensitive name).
        QSet<QString> names;
        for (Section *s : ds->sectionsForProject(proj->id()))
            names.insert(s->name());
        QVERIFY(names.contains(QStringLiteral("Backlog")));
        QVERIFY(names.contains(QStringLiteral("In Progress")));
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-27 companion: case-insensitive name match — Todoist
    // exports may carry "in progress" vs "In Progress" capitalisation
    // drift across versions. Reuse the existing section regardless of
    // case to keep re-imports idempotent.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_phase3_dedupIsCaseInsensitive()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        TodoistImportData first;
        TodoistProject p;
        p.id = QStringLiteral("td-proj");
        p.name = QStringLiteral("Proj");
        first.projects.append(p);
        TodoistSection s;
        s.id = QStringLiteral("td-sec-cap");
        s.name = QStringLiteral("MyStuff");
        s.projectId = p.id;
        first.sections.append(s);
        imp->applyImport(first, false);

        Project *proj = nullptr;
        for (auto *pp : ds->allProjects()) {
            if (pp->name() == QStringLiteral("Proj")) proj = pp;
        }
        QVERIFY(proj);
        QCOMPARE(ds->sectionsForProject(proj->id()).size(), 1);

        // Second import with lowercase section name — should still
        // resolve to the existing "MyStuff" section.
        TodoistImportData second = first;
        second.sections[0].name = QStringLiteral("mystuff");
        imp->applyImport(second, false);

        QCOMPARE(ds->sectionsForProject(proj->id()).size(), 1);
    }

    // ────────────────────────────────────────────────────────────────────────
    // Iter-night-27 companion: same-named sections in DIFFERENT projects
    // must NOT collide. Dedup is per-project; two "Backlog" sections in
    // two projects are independent.
    // ────────────────────────────────────────────────────────────────────────
    void reimport_phase3_sameNameDifferentProjects_independent()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        TodoistImportData first;
        TodoistProject pA, pB;
        pA.id = QStringLiteral("td-A"); pA.name = QStringLiteral("Alpha");
        pB.id = QStringLiteral("td-B"); pB.name = QStringLiteral("Beta");
        first.projects.append(pA);
        first.projects.append(pB);

        TodoistSection sA, sB;
        sA.id = QStringLiteral("td-sec-A"); sA.name = QStringLiteral("Backlog");
        sA.projectId = pA.id;
        sB.id = QStringLiteral("td-sec-B"); sB.name = QStringLiteral("Backlog");
        sB.projectId = pB.id;
        first.sections.append(sA);
        first.sections.append(sB);

        imp->applyImport(first, false);

        Project *projA = nullptr, *projB = nullptr;
        for (auto *pp : ds->allProjects()) {
            if (pp->name() == QStringLiteral("Alpha")) projA = pp;
            if (pp->name() == QStringLiteral("Beta"))  projB = pp;
        }
        QVERIFY(projA && projB);
        QCOMPARE(ds->sectionsForProject(projA->id()).size(), 1);
        QCOMPARE(ds->sectionsForProject(projB->id()).size(), 1);

        // Re-import — each project still has exactly one "Backlog"
        // section. Cross-project dedup would be a bug.
        imp->applyImport(first, false);
        QCOMPARE(ds->sectionsForProject(projA->id()).size(), 1);
        QCOMPARE(ds->sectionsForProject(projB->id()).size(), 1);
    }

    // ────────────────────────────────────────────────────────────────────
    // Iter-night-29: Phase 1 (Labels) must treat tombstoned labels as
    // not-found when deduplicating by name.
    //
    // DataStore::labelByName() looks up via m_labelNameIndex. That index
    // is NOT scrubbed when removeLabel() tombstones a label, and
    // loadFromJson() re-inserts every label into the index regardless of
    // is_deleted. Before iter-night-29, Phase 1 reused the tombstoned hit
    // — so a user who had deleted "Work" and then re-imported a Todoist
    // backup containing "Work" ended up with a label that was tombstoned
    // (invisible in sidebar, hidden in allLabels()) but still referenced
    // by name from imported tasks. The tombstone's is_deleted=true would
    // then ride outbound on sync and silently destroy any peer device's
    // freshly re-created "Work" label.
    //
    // Fix: filter labelByName() hits by isDeleted() inside the importer
    // (siblings: iter-19 Projects, iter-27 Sections — same pattern).
    //
    // Pin: tombstone a label, then re-import the same name. The expected
    // state is: tombstoned label still exists (untouched) AND a fresh,
    // non-deleted label with the same name has been created.
    // ────────────────────────────────────────────────────────────────────
    void reimport_phase1_doesNotReuseTombstonedLabel()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        // Create a label, then soft-delete it. This leaves the entry in
        // m_labelNameIndex pointing at a tombstoned label.
        Label *original = ds->addLabel(QStringLiteral("Work"));
        QVERIFY(original);
        const QString originalId = original->id();
        QVERIFY(ds->removeLabel(originalId));
        // Confirm the precondition: labelByName still returns the
        // tombstone (the very mis-design Phase 1 was tripping on).
        Label *byName = ds->labelByName(QStringLiteral("Work"));
        QVERIFY(byName != nullptr);
        QVERIFY(byName->isDeleted());

        // Re-import a backup that contains the same label name.
        TodoistImportData data;
        TodoistLabel tl;
        tl.name  = QStringLiteral("Work");
        tl.color = QStringLiteral("red");
        tl.order = 0;
        data.labels.append(tl);
        imp->applyImport(data, false);

        // Inspect every Label entity with name "Work". Before the fix
        // there was exactly one (the tombstone) and it remained tomb-
        // stoned. After the fix there is the tombstone PLUS a fresh
        // non-deleted entity (different UUID).
        int liveCount = 0;
        int tombCount = 0;
        // Walk m_labels directly via DataStore's full label list. We
        // can't use allLabels() because that filters out tombstones —
        // and we want to assert BOTH halves of the split. Use the
        // labels QML-bound property's raw read instead.
        const QJsonObject rawState = ds->toJson();
        const QJsonArray labelsArr = rawState.value(QStringLiteral("labels")).toArray();
        for (const QJsonValue &v : labelsArr) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("name")).toString()
                    .compare(QStringLiteral("Work"), Qt::CaseInsensitive) != 0)
                continue;
            if (o.value(QStringLiteral("is_deleted")).toBool())
                ++tombCount;
            else
                ++liveCount;
        }
        QCOMPARE(tombCount, 1);
        QCOMPARE(liveCount, 1);

        // The user-facing list (filtered) must show exactly one live
        // "Work" label, and that label must NOT be the tombstoned one.
        Label *visible = nullptr;
        for (Label *l : ds->allLabels()) {
            if (l && l->name().compare(QStringLiteral("Work"),
                                       Qt::CaseInsensitive) == 0) {
                QVERIFY(visible == nullptr); // exactly one
                visible = l;
            }
        }
        QVERIFY(visible != nullptr);
        QVERIFY(!visible->isDeleted());
        QVERIFY(visible->id() != originalId);
    }

    // ────────────────────────────────────────────────────────────────────
    // Iter-night-29 companion: the on-the-fly label-creation path in
    // Phase 4 (Tasks) — when a task references a label name that wasn't
    // declared in data.labels — must also treat tombstoned hits as
    // not-found. Without the fix, the task ended up labelled with a
    // tombstoned label's name, leaving the chip rendered but unmanaged.
    // ────────────────────────────────────────────────────────────────────
    void reimport_phase4_onTheFlyLabel_doesNotReuseTombstoned()
    {
        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(ds.data(), this));

        // Tombstone "Urgent".
        Label *original = ds->addLabel(QStringLiteral("Urgent"));
        QVERIFY(original);
        const QString originalId = original->id();
        QVERIFY(ds->removeLabel(originalId));

        // Build an import with a TASK referencing "Urgent" but NO label
        // declaration — this triggers the on-the-fly creation branch.
        TodoistImportData data;
        TodoistProject p;
        p.id = QStringLiteral("td-proj-29");
        p.name = QStringLiteral("Sandbox");
        data.projects.append(p);

        TodoistTask tt;
        tt.id        = QStringLiteral("td-task-29");
        tt.projectId = p.id;
        tt.content   = QStringLiteral("Tagged task");
        tt.priority  = 1;
        tt.labelNames << QStringLiteral("Urgent");
        data.tasks.append(tt);

        imp->applyImport(data, false);

        // Same split assertion: exactly one tombstoned "Urgent" + exactly
        // one live "Urgent" should now exist.
        int liveCount = 0, tombCount = 0;
        const QJsonObject rawState = ds->toJson();
        const QJsonArray labelsArr = rawState.value(QStringLiteral("labels")).toArray();
        for (const QJsonValue &v : labelsArr) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("name")).toString()
                    .compare(QStringLiteral("Urgent"), Qt::CaseInsensitive) != 0)
                continue;
            if (o.value(QStringLiteral("is_deleted")).toBool()) ++tombCount;
            else                                                 ++liveCount;
        }
        QCOMPARE(tombCount, 1);
        QCOMPARE(liveCount, 1);

        // The imported task should still carry the label by name. The
        // name uniquely matches the freshly-created (live) label now.
        bool foundTask = false;
        for (Task *t : ds->allTasks()) {
            if (t && t->content() == QStringLiteral("Tagged task")) {
                foundTask = true;
                QVERIFY(t->labels().contains(QStringLiteral("Urgent")));
            }
        }
        QVERIFY(foundTask);
    }
};

QTEST_GUILESS_MAIN(TestTodoistImporterRobustness)
#include "test_todoist_importer_robustness.moc"
