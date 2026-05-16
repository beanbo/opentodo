// Robustness / edge-case suite for TaskExporter (CSV / Markdown / per-project JSON).
//
// Companion to test_taskexporter.cpp (which pins basic CSV escaping). This
// suite drives adversarial payloads that the original suite does not exercise:
//   - bare CR and CRLF inside content (CSV row-break risk)
//   - embedded NUL bytes in title / description (constructed via QChar(0)
//     at runtime so this source file stays NUL-free and AUTOMOC can scan it)
//   - 10 KB single-task payload (no truncation, no quadratic blow-up)
//   - Markdown special chars (preserved verbatim, no auto-escaping)
//   - all-empty entity (only id) -- no crash, header-only-ish output
//   - 4-deep parent chain (parent -> child -> grandchild -> great-grandchild)
//   - export-while-mutating in same thread (sanity, since DataStore is not
//     thread-safe -- we exercise the only sane interleaving: emit-but-keep-going)
//   - filename collision in same second (timestamp-only naming)
//   - format consistency across CSV / MD / JSON for the same input
//   - Unicode normalisation: NFC vs NFD survive byte-equal through JSON
//
// Bugs surfaced (and confirmed) live in qInfo() comments next to the slot,
// per the project convention of "document, don't QFAIL on observed quirks".
//
// IMPORTANT: this file MUST NOT contain literal NUL bytes (0x00) in source.
// Qt's AUTOMOC refuses to scan a translation unit that contains a NUL byte,
// so the .moc file is never generated and the link fails with
//   "undefined reference to vtable" / "test_*.moc: No such file or directory".
// To inject a NUL into a QString at runtime use QChar(QChar::Null) and
// concatenate/append -- never embed "\0" in a C string literal.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>
#include <QStringDecoder>
#include <QElapsedTimer>
#include <QThread>
#include <QSet>

#include "services/taskexporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/comment.h"

class TestTaskExporterRobustness : public QObject
{
    Q_OBJECT

private:
    static QString readUtf8(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }

    static QByteArray readBytes(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }

    // QTextStream + QIODevice::Text on Windows converts every '\n' WRITE into
    // '\r\n' on disk. For row-counting we collapse the OS-EOL pairs back to
    // LF, leaving any *embedded* CR bytes intact so CR-related bugs stay
    // visible in the assertions.
    static QString normaliseEol(QString s)
    {
        return s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }

    // Build a string with an embedded NUL byte at runtime, so the source
    // file stays NUL-free (AUTOMOC requirement). Equivalent to
    // QStringLiteral("AAA\0BBB") but without putting 0x00 in the .cpp.
    static QString withNul(const QString &left, const QString &right)
    {
        QString s = left;
        s.append(QChar(QChar::Null));
        s.append(right);
        return s;
    }

private slots:
    void initTestCase()
    {
        // Per-test org/app keeps the test-mode AppDataLocation isolated from
        // other taskexporter tests under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_TaskExportRobust"));
        QCoreApplication::setApplicationName(QStringLiteral("test_taskexporter_robustness"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // =====================================================================
    //  Slot 1 -- Newline variants in content (bare CR, CRLF, LF)
    //
    //  RFC 4180 sec 2.6 requires fields containing CR, LF, double-quote or
    //  comma to be enclosed in double-quotes. csvEscape now checks all four
    //  triggers, so a bare CR in content is correctly wrapped.
    //
    //  Previously pinned as a bug -- now flipped to assert the correct
    //  RFC-4180 behaviour. Fix: csvEscape predicate gained
    //  `s.contains(QChar('\r'))` in taskexporter.cpp.
    // =====================================================================

    void csv_bare_cr_in_content_is_not_quoted_pin_bug()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("CR test"));
        // Bare CR with no LF. RFC 4180 sec 2.6 says fields containing CR or
        // LF MUST be enclosed in double-quotes.
        ds.addTask(QStringLiteral("Line1\rLine2"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString raw = readUtf8(path);
        // The cell MUST be wrapped in quotes now (RFC 4180 compliance).
        QVERIFY2(raw.contains(QStringLiteral("\"Line1\rLine2\"")),
                 "Bare CR in CSV content must be quoted (RFC 4180 sec 2.6).");

        // CRLF case: the LF half of CRLF triggers the predicate independently,
        // so CRLF is also correctly quoted. Confirm.
        DataStore ds2;
        Project *p2 = ds2.addProject(QStringLiteral("CRLF test"));
        ds2.addTask(QStringLiteral("Line1\r\nLine2"), p2->id());
        TaskExporter exp2(&ds2);
        const QString path2 = exp2.exportToCsv(tmp.path());
        const QString raw2 = readUtf8(path2);
        QVERIFY2(raw2.contains(QStringLiteral("\"Line1")) &&
                 raw2.contains(QStringLiteral("Line2\"")),
                 "CRLF in content should be quoted (LF half triggers it)");
    }

    // =====================================================================
    //  Slot 2 -- Embedded NUL bytes in title and description
    //
    //  QString stores NUL freely; QTextStream's UTF-8 encoder happily writes
    //  it as a single 0x00 byte. CSV consumers / spreadsheets typically
    //  treat 0x00 as end-of-string. Verify exporter doesn't drop or
    //  truncate, and document the consumer-side risk.
    //
    //  The NUL byte itself is constructed at runtime via QChar(QChar::Null)
    //  -- see the withNul() helper. The source file contains zero NUL bytes.
    // =====================================================================

    void csv_and_json_preserve_embedded_nul_bytes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("NUL"));
        // Build "AAA<NUL>BBB" at runtime -- the QChar(0) injection keeps the
        // source file free of literal 0x00 bytes (AUTOMOC requirement).
        const QString contentWithNul = withNul(QStringLiteral("AAA"),
                                                QStringLiteral("BBB"));
        QCOMPARE(contentWithNul.size(), 7);
        QCOMPARE(contentWithNul.at(3), QChar(QChar::Null));

        Task *t = ds.addTask(contentWithNul, p->id());
        t->setDescription(contentWithNul);

        TaskExporter exp(&ds);

        // CSV
        const QString csvPath = exp.exportToCsv(tmp.path());
        QVERIFY(!csvPath.isEmpty());
        const QByteArray csvBytes = readBytes(csvPath);
        // The 0x00 byte should be present at least twice (content + description).
        const int nulCount = csvBytes.count('\0');
        QVERIFY2(nulCount >= 2, "NUL bytes should pass through CSV encoder");

        // JSON -- exportProjectToJson via Task::toJson + QJsonDocument
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!jsonPath.isEmpty());
        const QByteArray jsonBytes = readBytes(jsonPath);
        // QJsonDocument escapes NUL as a six-char ASCII escape
        // (backslash-u-0-0-0-0), NOT as a literal 0x00 byte. So we
        // should see that escape sequence and zero raw NULs in the file.
        QCOMPARE(jsonBytes.count('\0'), 0);
        QVERIFY2(jsonBytes.contains("\\u0000"),
                 "JSON should escape NUL as \\u0000 (no literal 0x00 bytes)");

        qInfo("Note: CSV writes literal 0x00 bytes for embedded NULs -- most "
              "spreadsheet tools (Excel, LibreOffice Calc) silently truncate "
              "the cell at the first NUL. JSON path correctly escapes to "
              "the \\u0000 form. Decide later if CSV should mirror that.");
    }

    // =====================================================================
    //  Slot 3 -- Very long content (10 KB single task)
    //
    //  Pins that the exporter does not truncate, does not blow up on QString
    //  reallocation, and that QTextStream writes the full payload through.
    //  Also serves as a soft perf canary (>5 s = something quadratic).
    // =====================================================================

    void csv_md_json_handle_10kb_single_task_payload()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Big"));
        const QString huge = QString(10 * 1024, QChar('A'));
        ds.addTask(huge, p->id());

        TaskExporter exp(&ds);

        QElapsedTimer timer; timer.start();
        const QString csvPath = exp.exportToCsv(tmp.path());
        const QString mdPath  = exp.exportToMarkdown(tmp.path());
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        const qint64 ms = timer.elapsed();

        QVERIFY(!csvPath.isEmpty());
        QVERIFY(!mdPath.isEmpty());
        QVERIFY(!jsonPath.isEmpty());

        // CSV / MD / JSON each contain the full 10 KB run.
        const QString csv = readUtf8(csvPath);
        const QString md  = readUtf8(mdPath);
        const QString json = readUtf8(jsonPath);
        QVERIFY2(csv.contains(huge),  "CSV truncated 10 KB content");
        QVERIFY2(md.contains(huge),   "MD truncated 10 KB content");
        QVERIFY2(json.contains(huge), "JSON truncated 10 KB content");

        // Soft perf budget -- should be sub-second on any modern dev box.
        QVERIFY2(ms < 5000, qPrintable(QStringLiteral("3-format export of 10 KB took %1 ms").arg(ms)));
    }

    // =====================================================================
    //  Slot 4 -- Markdown special characters survive verbatim across paths
    //
    //  exportToMarkdown does NOT escape Markdown sigils (`*`, `_`, `[`, `]`,
    //  `(`, `)`, backticks, `#`, `>`, etc.) -- task content is rendered
    //  raw inside a `- [ ] ` checkbox line, and description after `  > `.
    //
    //  This matches the original test's csv_unicode/md_special slots; here
    //  we drive a stress-test of MANY sigils in one task and verify they
    //  all survive in CSV, MD, and JSON forms.
    // =====================================================================

    void md_csv_json_preserve_markdown_specials_verbatim()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Specials"));
        const QString stress = QStringLiteral("`code` *bold* _it_ ~~strike~~ "
                                              "[link](url) > quote # H1 1. li "
                                              "- li > > nested | table |");
        ds.addTask(stress, p->id());

        TaskExporter exp(&ds);
        const QString csvPath = exp.exportToCsv(tmp.path());
        const QString mdPath  = exp.exportToMarkdown(tmp.path());
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!csvPath.isEmpty()); QVERIFY(!mdPath.isEmpty()); QVERIFY(!jsonPath.isEmpty());

        QVERIFY(readUtf8(csvPath).contains(stress));
        QVERIFY(readUtf8(mdPath).contains(stress));
        // In JSON the chars are quoted as JSON-string (no Markdown escaping).
        QVERIFY(readUtf8(jsonPath).contains(stress));

        // Pipe-as-content does NOT break CSV (no comma/quote/LF in this
        // particular string, so the cell ends up unquoted). Confirm.
        const QString csv = normaliseEol(readUtf8(csvPath));
        QVERIFY(csv.contains(stress));
    }

    // =====================================================================
    //  Slot 5 -- All-empty task (only id) renders without crash
    //
    //  A task with empty content / description / dueDate / no labels / no
    //  project assignment must not deref a nullptr or emit malformed CSV.
    // =====================================================================

    void csv_md_json_handle_empty_only_id_task()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        // Project deliberately empty-named.
        Project *p = ds.addProject(QString());
        Task *t = ds.addTask(QString(), p->id());
        // Section explicitly empty -- sectionId is "".
        t->setSectionId(QString());

        TaskExporter exp(&ds);
        const QString csvPath = exp.exportToCsv(tmp.path());
        QVERIFY(!csvPath.isEmpty());
        const QString csv = normaliseEol(readUtf8(csvPath));
        // CSV must have header + 1 data row. The data row has the id then
        // 9 empty cells (all-empty content/etc), separated by 9 commas.
        const QStringList lines = csv.split(QChar('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        const QStringList cols = lines.at(1).split(QChar(','));
        QCOMPARE(cols.size(), 10);
        QVERIFY2(!cols.at(0).isEmpty(), "id should be non-empty (UUID)");
        // Project name is "" -- but cell 1 is still emitted (empty between commas).
        QCOMPARE(cols.at(1), QString());
        QCOMPARE(cols.at(3), QString()); // content

        // Markdown: project section gets "## " heading with no name.
        const QString mdPath = exp.exportToMarkdown(tmp.path());
        QVERIFY(!mdPath.isEmpty());
        const QString md = readUtf8(mdPath);
        QVERIFY(md.contains(QStringLiteral("## ")));
        // Empty-content task: exporter renders "- [ ] " then the content
        // (empty) -- but priority defaults to 1, so a trailing " <flag>P1"
        // is appended. The checkbox prefix must still be present.
        QVERIFY2(md.contains(QStringLiteral("- [ ] ")),
                 qPrintable(QStringLiteral("MD missing '- [ ] ' checkbox prefix. Got:\n%1").arg(md.left(200))));

        // JSON should still produce a valid document.
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!jsonPath.isEmpty());
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(readBytes(jsonPath), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("tasks")).toArray().size(), 1);
    }

    // =====================================================================
    //  Slot 6 -- Deep parent chain (4 levels: parent -> child -> grandchild
    //            -> great-grandchild)
    //
    //  CSV iterates ALL tasks (regardless of parentId), JSON's secondary
    //  `allTasks` sweep picks them up, and MD now recurses through subtasks
    //  with 2-space indent per depth. Previously a bug (MD dropped every
    //  subtask); now flipped to assert the correct indented hierarchy.
    // =====================================================================

    void deep_parent_chain_md_drops_subtasks_pin_bug()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Chain"));
        Task *t1 = ds.addTask(QStringLiteral("L1-parent"), p->id());
        Task *t2 = ds.addTask(QStringLiteral("L2-child"), p->id());
        t2->setParentId(t1->id());
        Task *t3 = ds.addTask(QStringLiteral("L3-grandchild"), p->id());
        t3->setParentId(t2->id());
        Task *t4 = ds.addTask(QStringLiteral("L4-greatgrandchild"), p->id());
        t4->setParentId(t3->id());

        TaskExporter exp(&ds);

        // CSV -- ALL 4 must appear (csv loop is over allTasks).
        const QString csvPath = exp.exportToCsv(tmp.path());
        QVERIFY(!csvPath.isEmpty());
        const QString csv = readUtf8(csvPath);
        for (const QString &c : {QStringLiteral("L1-parent"),
                                 QStringLiteral("L2-child"),
                                 QStringLiteral("L3-grandchild"),
                                 QStringLiteral("L4-greatgrandchild")}) {
            QVERIFY2(csv.contains(c), qPrintable(QStringLiteral("CSV missing %1").arg(c)));
        }

        // JSON -- secondary sweep picks subtasks up.
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!jsonPath.isEmpty());
        const QString json = readUtf8(jsonPath);
        for (const QString &c : {QStringLiteral("L1-parent"),
                                 QStringLiteral("L2-child"),
                                 QStringLiteral("L3-grandchild"),
                                 QStringLiteral("L4-greatgrandchild")}) {
            QVERIFY2(json.contains(c), qPrintable(QStringLiteral("JSON missing %1").arg(c)));
        }

        // MD -- ALL 4 levels must appear, each indented by depth * 2 spaces
        // before its `- [ ] ` checkbox prefix. The exporter recurses into
        // DataStore::subtasks() for every rendered task.
        const QString mdPath = exp.exportToMarkdown(tmp.path());
        QVERIFY(!mdPath.isEmpty());
        const QString md = readUtf8(mdPath);
        // Top-level: no indent.
        QVERIFY2(md.contains(QStringLiteral("- [ ] L1-parent")),
                 "MD missing L1 root checkbox");
        // Depth 1: two-space indent.
        QVERIFY2(md.contains(QStringLiteral("  - [ ] L2-child")),
                 "MD missing L2 child at 2-space indent");
        // Depth 2: four-space indent.
        QVERIFY2(md.contains(QStringLiteral("    - [ ] L3-grandchild")),
                 "MD missing L3 grandchild at 4-space indent");
        // Depth 3: six-space indent.
        QVERIFY2(md.contains(QStringLiteral("      - [ ] L4-greatgrandchild")),
                 "MD missing L4 great-grandchild at 6-space indent");
    }

    // =====================================================================
    //  Slot 7 -- Concurrent export while DataStore mutates (same thread)
    //
    //  Qt model classes are NOT thread-safe; Qt Object Model documentation
    //  is explicit. The interesting interleaving is the only one that's
    //  actually possible from QML/main-thread code: the export reads the
    //  task list, and signal-slot reentrancy mid-iteration mutates it.
    //
    //  We test the export-while-emit case: the export holds an iterator
    //  via for(const Task* t : m_store->allTasks()), but allTasks() returns
    //  by value (QList<Task*>), which is a deep copy of the internal list.
    //  A subsequent addTask() during iteration must NOT corrupt the export.
    // =====================================================================

    void csv_export_resists_mid_export_addTask_mutation()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Mutate"));
        ds.addTask(QStringLiteral("pre-export-1"), p->id());
        ds.addTask(QStringLiteral("pre-export-2"), p->id());

        TaskExporter exp(&ds);
        // Connect a slot to the success signal that mutates DataStore mid-flight.
        bool mutated = false;
        QObject::connect(&exp, &TaskExporter::exportSucceeded,
                         &ds, [&]() {
            // Add a third task AFTER export wrote its file. This task should
            // NOT appear in the file we just wrote -- confirms snapshot
            // semantics by virtue of the signal firing post-write.
            if (!mutated) {
                ds.addTask(QStringLiteral("post-export-mutation"), p->id());
                mutated = true;
            }
        });

        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(mutated);

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("pre-export-1")));
        QVERIFY(text.contains(QStringLiteral("pre-export-2")));
        QVERIFY2(!text.contains(QStringLiteral("post-export-mutation")),
                 "Post-write mutation must not appear in the already-written file");

        // And the DataStore is now consistent -- re-export and the new task IS there.
        const QString path2 = exp.exportToCsv(tmp.path());
        QVERIFY(!path2.isEmpty());
        QVERIFY(readUtf8(path2).contains(QStringLiteral("post-export-mutation")));

        qInfo("Note: cross-thread mutation is undefined behaviour for Qt "
              "model classes -- TaskExporter does not (and cannot) defend "
              "against it. UI code MUST keep DataStore mutations on the "
              "GUI thread, which is the harness Qt enforces by default.");
    }

    // =====================================================================
    //  Slot 8 -- Disk-full / write error: exportFailed is emitted, empty
    //            string returned, no partial file left behind
    //
    //  We can't actually fill the disk in a unit test, but we CAN make the
    //  destination unwritable: place a regular file at exactly the path
    //  the exporter would write to (mkpath fails because the parent is a
    //  file -> QFile::open fails -> exportFailed fires). The original suite
    //  exercises one variant; we add the JSON path + verify no partial
    //  file is created.
    // =====================================================================

    void json_unwritable_destination_emits_failed_no_partial_file()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Plant a regular file where the JSON exporter would create the
        // *directory* part. mkpath cannot create a directory under a file.
        const QString blocker = tmp.path() + QStringLiteral("/blocker");
        {
            QFile f(blocker);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
        }
        const QString badDir = blocker + QStringLiteral("/sub");

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Test"));
        ds.addTask(QStringLiteral("doomed task"), p->id());

        TaskExporter exp(&ds);
        QSignalSpy failed(&exp, &TaskExporter::exportFailed);
        QSignalSpy ok(&exp, &TaskExporter::exportSucceeded);

        const QString path = exp.exportProjectToJson(p->id(), badDir);
        QCOMPARE(path, QString());
        QCOMPARE(ok.size(), 0);
        QCOMPARE(failed.size(), 1);
        // The failed signal carries a localized message -- verify it's non-empty.
        QVERIFY(!failed.takeFirst().value(0).toString().isEmpty());

        // Confirm no partial file was created in the bogus path.
        QDir d(badDir);
        QVERIFY(!d.exists());
    }

    // =====================================================================
    //  Slot 9 -- Format consistency: a single task's content survives
    //            through ALL three formats (CSV / MD / JSON)
    //
    //  This is a meta-test: the SAME task definition must round-trip its
    //  content, priority, due date, labels, and project name through every
    //  export format. Catches regressions where one path drops a field
    //  the other paths keep.
    //
    //  Findings (already known and documented elsewhere):
    //   - MD does NOT include section name (CSV + JSON do).
    //   - MD does NOT include labels (CSV + JSON do).
    //   - MD does NOT include description as a top-level field (it appears
    //     after a "  > " continuation line, so technically present).
    //   - MD does NOT include the task id (CSV + JSON do).
    //
    //  All four are intentional -- MD is a human-readable digest, not a
    //  data interchange format. We pin them as confirmed-by-test asymmetries.
    // =====================================================================

    void format_consistency_csv_md_json_for_same_input()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("CommonProj"));
        Section *s = ds.addSection(QStringLiteral("CommonSect"), p->id());
        Task *t = ds.addTask(QStringLiteral("CommonTask"), p->id());
        t->setSectionId(s->id());
        t->setPriority(2);
        t->setDescription(QStringLiteral("CommonDesc"));
        t->setDueDate(QStringLiteral("2026-12-31"));
        t->setLabels({QStringLiteral("CommonLabel")});

        TaskExporter exp(&ds);
        const QString csv  = readUtf8(exp.exportToCsv(tmp.path()));
        const QString md   = readUtf8(exp.exportToMarkdown(tmp.path()));
        const QString json = readUtf8(exp.exportProjectToJson(p->id(), tmp.path()));

        // Survives in ALL three
        for (const QString &fmtText : {csv, md, json}) {
            QVERIFY(fmtText.contains(QStringLiteral("CommonTask")));
            QVERIFY(fmtText.contains(QStringLiteral("CommonProj")));
            QVERIFY(fmtText.contains(QStringLiteral("2026-12-31")));
        }

        // CSV + JSON include section name and label; MD does NOT
        QVERIFY(csv.contains(QStringLiteral("CommonSect")));
        QVERIFY(json.contains(QStringLiteral("CommonSect")));
        QVERIFY2(!md.contains(QStringLiteral("CommonSect")),
                 "MD has no section grouping -- pinned as intentional.");

        QVERIFY(csv.contains(QStringLiteral("CommonLabel")));
        QVERIFY(json.contains(QStringLiteral("CommonLabel")));
        QVERIFY2(!md.contains(QStringLiteral("CommonLabel")),
                 "MD has no label rendering -- pinned as intentional.");

        // CSV + JSON include description; MD includes it after the "  > " prefix
        QVERIFY(csv.contains(QStringLiteral("CommonDesc")));
        QVERIFY(json.contains(QStringLiteral("CommonDesc")));
        QVERIFY(md.contains(QStringLiteral("  > CommonDesc")));

        // CSV + JSON include the UUID; MD does NOT
        const QString taskId = t->id();
        QVERIFY(csv.contains(taskId));
        QVERIFY(json.contains(taskId));
        QVERIFY2(!md.contains(taskId),
                 "MD has no task id rendering -- pinned as intentional.");
    }

    // =====================================================================
    //  Slot 10 -- Unicode normalisation NFC vs NFD round-trips byte-equal
    //
    //  "e-acute" can be encoded as a single precomposed codepoint U+00E9
    //  (NFC, 2 UTF-8 bytes) or as the base "e" + combining acute U+0301
    //  (NFD, 3 UTF-8 bytes). Many naive pipelines normalise to NFC, which
    //  would silently change the bytes a user wrote. We assert that BOTH
    //  forms pass through unchanged (the exporter never normalises), so an
    //  export+import preserves the user's exact codepoint sequence.
    // =====================================================================

    void unicode_nfc_and_nfd_survive_roundtrip_byte_for_byte()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // NFC: e-acute = U+00E9 (single precomposed codepoint). UTF-8: 0xC3 0xA9.
        const QString nfc = QString::fromUtf8("Caf\xC3\xA9 NFC");
        // NFD: e + combining acute U+0301. UTF-8: 0x65 0xCC 0x81.
        const QString nfd = QString::fromUtf8("Cafe\xCC\x81 NFD");
        // Sanity-check: byte-level different even though they'd display the
        // same glyph in a normaliser.
        QVERIFY(nfc.toUtf8() != nfd.toUtf8());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Norm"));
        ds.addTask(nfc, p->id());
        ds.addTask(nfd, p->id());

        TaskExporter exp(&ds);

        // CSV
        const QByteArray csvBytes = readBytes(exp.exportToCsv(tmp.path()));
        QVERIFY2(csvBytes.contains(nfc.toUtf8()), "NFC bytes lost in CSV");
        QVERIFY2(csvBytes.contains(nfd.toUtf8()), "NFD bytes lost in CSV");

        // MD
        const QByteArray mdBytes = readBytes(exp.exportToMarkdown(tmp.path()));
        QVERIFY2(mdBytes.contains(nfc.toUtf8()), "NFC bytes lost in MD");
        QVERIFY2(mdBytes.contains(nfd.toUtf8()), "NFD bytes lost in MD");

        // JSON -- codepoints round-trip via QJsonDocument's serialiser. A
        // U+00E9 may be emitted as the literal UTF-8 bytes OR as the JSON
        // backslash-u escape. We accept either form, but the *parsed* string
        // must equal the original.
        const QString jsonPath = exp.exportProjectToJson(p->id(), tmp.path());
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(readBytes(jsonPath), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        const QJsonArray tasksArr = doc.object().value(QStringLiteral("tasks")).toArray();
        QCOMPARE(tasksArr.size(), 2);

        QSet<QString> contents;
        for (const auto &v : std::as_const(tasksArr))
            contents.insert(v.toObject().value(QStringLiteral("content")).toString());
        QVERIFY2(contents.contains(nfc), "NFC string did not round-trip via JSON");
        QVERIFY2(contents.contains(nfd), "NFD string did not round-trip via JSON");

        qInfo("Verified: TaskExporter never normalises Unicode. NFC and NFD "
              "forms are preserved byte-equal -- required for any user that "
              "round-trips data through a normalising layer (e.g. macOS HFS+ "
              "filenames vs APFS, or an iOS keyboard that emits NFD).");
    }

    // =====================================================================
    //  Slot 11 -- Filename collision in same second (timestamp-only naming)
    //
    //  Both CSV and MD paths use yyyyMMdd-HHmmss as the timestamp. Previously,
    //  two exports inside the same second yielded the SAME path and the
    //  second silently overwrote the first. The exporter now passes the
    //  candidate path through a uniquifier that appends "-1", "-2", ...
    //  before the suffix when the file already exists -- both files are
    //  preserved and each captures the DataStore state at its own moment.
    // =====================================================================

    void filename_collision_within_one_second_overwrites()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Collide"));
        ds.addTask(QStringLiteral("first"), p->id());

        TaskExporter exp(&ds);
        const QString path1 = exp.exportToCsv(tmp.path());
        QVERIFY(!path1.isEmpty());

        // Mutate, immediately re-export -- both paths likely share the same
        // second, but the uniquifier must keep them distinct on disk.
        Task *t2 = ds.addTask(QStringLiteral("second"), p->id());
        Q_UNUSED(t2)

        const QString path2 = exp.exportToCsv(tmp.path());
        QVERIFY(!path2.isEmpty());

        // Two distinct files MUST exist now -- no silent overwrite.
        QVERIFY2(path1 != path2,
                 qPrintable(QStringLiteral("Same path returned twice: %1").arg(path1)));
        QVERIFY(QFile::exists(path1));
        QVERIFY(QFile::exists(path2));

        // First file: snapshot before the mutation -- contains "first" but not "second".
        const QString csv1 = readUtf8(path1);
        QVERIFY2(csv1.contains(QStringLiteral("first")),
                 "First export must contain the pre-mutation task");
        QVERIFY2(!csv1.contains(QStringLiteral("second")),
                 "First export must NOT contain the task added after it was written");

        // Second file: post-mutation snapshot -- contains both.
        const QString csv2 = readUtf8(path2);
        QVERIFY(csv2.contains(QStringLiteral("first")));
        QVERIFY(csv2.contains(QStringLiteral("second")));

        // The uniquifier should have appended a "-N" suffix before ".csv" on
        // whichever of the two collided. Confirm the second path matches the
        // <stem>-<N>.csv shape (either path2 carries the suffix, or path1 does
        // if the wall-clock second rolled between the two calls -- in which
        // case the timestamps themselves differ and no suffix was needed).
        const bool path2HasSuffix = QFileInfo(path2).fileName().contains(QRegularExpression(
                QStringLiteral("-\\d+\\.csv$")));
        const bool timestampsDiffer = QFileInfo(path1).completeBaseName()
                                      != QFileInfo(path2).completeBaseName().section(QChar('-'), 0, -2);
        QVERIFY2(path2HasSuffix || timestampsDiffer,
                 "Second path must either carry a -N suffix or fall in a different second");
    }

    // =====================================================================
    //  Slot 12 -- Description with embedded CRLF collapses to a single space
    //
    //  exportToMarkdown now uses a `[\r\n]+` regex replace, so any run of
    //  CR / LF characters in a description collapses to one space. Previously
    //  only LF was stripped, leaving stray CRs that strict Markdown renderers
    //  could treat as hard line breaks. Pinned bug, now flipped to assert
    //  the correct collapse.
    // =====================================================================

    void md_description_crlf_leaves_stray_cr_pin_bug()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("MDdesc"));
        Task *t = ds.addTask(QStringLiteral("hdr"), p->id());
        t->setDescription(QStringLiteral("Line1\r\nLine2"));

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        // Read raw bytes (NOT through normaliseEol) to preserve any embedded CR.
        const QByteArray raw = readBytes(path);
        // The previous bug emitted "Line1\r Line2" (CR survives, LF -> space).
        // The fix collapses CRLF (and any [\r\n]+ run) to a single space.
        QVERIFY2(!raw.contains("Line1\r Line2"),
                 "MD description must not leave stray CR after the LF->space replace");
        QVERIFY2(!raw.contains("Line1\rLine2"),
                 "MD description must not leave the raw CR untouched");
        QVERIFY2(raw.contains("Line1 Line2"),
                 "CRLF in description should collapse to a single space");

        // Also confirm CRLF (the typical Windows EOL) is the only run that
        // appears in the body of the task line, by sweeping for stray CRs
        // anywhere in the description payload area. The line-ending CR/LF
        // that QIODevice::Text adds is irrelevant because we look at the
        // specific 12-byte window around "Line1 Line2".
        const int pos = raw.indexOf("Line1 Line2");
        QVERIFY(pos >= 0);
        // No CR allowed in the 13-char "Line1 Line2" run itself.
        for (int i = 0; i < 11; ++i)
            QVERIFY(raw.at(pos + i) != '\r');
    }

    // =====================================================================
    //  Slot 13 -- Project name length 1000+ chars produces a sane filename
    //
    //  exportProjectToJson uses safeName.left(80) -- confirms that an
    //  attacker-controlled (or just very long) project name cannot drive
    //  the filename to MAX_PATH. Also verifies the file actually opens
    //  on Windows (where path-length limits are stricter).
    // =====================================================================

    void json_long_project_name_truncated_to_80_chars()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString huge = QString(1000, QChar('A'));
        DataStore ds;
        Project *p = ds.addProject(huge);

        TaskExporter exp(&ds);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());

        const QString fileName = QFileInfo(path).fileName();
        // Filename is "opentodo-project-<safeName(<=80)>-<stamp>.json".
        // Count the run of 'A' in the filename -- must be <= 80.
        int runOfA = 0;
        for (QChar c : fileName) {
            if (c == QChar('A')) ++runOfA;
        }
        QVERIFY2(runOfA <= 80, qPrintable(QStringLiteral("Run of A in filename = %1 (>80, truncation broke)").arg(runOfA)));
        // And not 0 -- some prefix made it through.
        QVERIFY(runOfA > 0);

        QVERIFY(QFile::exists(path));
    }

    // =====================================================================
    //  Slot 14 -- Project name with EVERY forbidden filename char replaced
    //
    //  safeName regex strips \ / : * ? " < > | (the Windows-illegal set).
    //  Verify that a project name made entirely of these still produces a
    //  valid filename (all underscores) and writes successfully.
    // =====================================================================

    void json_project_name_all_forbidden_chars_sanitised()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Every Windows-illegal filename char.
        const QString forbidden = QStringLiteral("\\/:*?\"<>|\\/:*?\"<>|");
        DataStore ds;
        Project *p = ds.addProject(forbidden);

        TaskExporter exp(&ds);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        const QString fileName = QFileInfo(path).fileName();
        // None of the illegal chars survives in the filename.
        for (QChar c : QStringLiteral("\\/:*?\"<>|")) {
            // Skip the implicit '/' in the path separator -- we work on the
            // bare filename, which has no path separator.
            if (c == QChar('/')) continue;
            QVERIFY2(!fileName.contains(c),
                     qPrintable(QStringLiteral("Forbidden char %1 leaked into filename %2")
                                .arg(c).arg(fileName)));
        }
        // The sanitised section should be a run of underscores.
        QVERIFY(fileName.contains(QStringLiteral("________")));
    }

    // =====================================================================
    //  Slot 15 -- Soft-deleted PROJECT: JSON export still serialises it
    //
    //  exportProjectToJson does NOT check `p->isDeleted()` -- it serialises
    //  whatever projectById returns. Verify that calling it on a tombstoned
    //  project still yields a valid file (the project field carries
    //  is_deleted: true). This may or may not be desired -- document.
    // =====================================================================

    void json_export_of_soft_deleted_project_still_writes_file()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Doomed"));
        const QString pid = p->id();
        ds.addTask(QStringLiteral("payload"), pid);
        // Soft-delete via the datastore API (cascade reminders/comments).
        QVERIFY(ds.removeProject(pid));

        TaskExporter exp(&ds);
        QSignalSpy ok(&exp, &TaskExporter::exportSucceeded);
        QSignalSpy failed(&exp, &TaskExporter::exportFailed);

        const QString path = exp.exportProjectToJson(pid, tmp.path());
        QVERIFY(!path.isEmpty());
        QCOMPARE(ok.size(), 1);
        QCOMPARE(failed.size(), 0);

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(readBytes(path), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        const QJsonObject project = doc.object().value(QStringLiteral("project")).toObject();
        QVERIFY(project.value(QStringLiteral("is_deleted")).toBool());

        qInfo("Note: exportProjectToJson serialises soft-deleted projects. "
              "Decide later if this is desired (currently useful for forensic "
              "export of trash; could be confusing if the user calls export "
              "from a UI that doesn't surface trashed projects).");
    }

    // =====================================================================
    //  Slot 16 -- RTL (Right-to-Left) script content survives all three formats
    //
    //  Hebrew + Arabic content with bidirectional override codepoints is a
    //  common attack vector for filename-spoofing CVEs (Trojan Source).
    //  TaskExporter must not interpret these -- they must pass through bytes-
    //  for-bytes.
    // =====================================================================

    void rtl_and_bidi_override_content_preserved()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("RTL"));
        // Hebrew "shalom" + RLO (U+202E, right-to-left override) + Arabic.
        const QString rtl = QString::fromUtf8("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"  // shalom
                                              "\xE2\x80\xAE"                       // U+202E RLO
                                              "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"); // salaam
        ds.addTask(rtl, p->id());

        TaskExporter exp(&ds);
        for (const QString &path : {exp.exportToCsv(tmp.path()),
                                    exp.exportToMarkdown(tmp.path()),
                                    exp.exportProjectToJson(p->id(), tmp.path())}) {
            QVERIFY(!path.isEmpty());
            const QByteArray bytes = readBytes(path);
            // Hebrew + Arabic + RLO bytes should all be present somewhere.
            QVERIFY(bytes.contains(QByteArray::fromHex("D7A9D79CD795D79D"))); // shalom
            QVERIFY(bytes.contains(QByteArray::fromHex("D8B3D984D8A7D985"))); // salaam
            // The RLO (U+202E) in JSON form is either a literal byte sequence
            // (E2 80 AE) or the JSON backslash-u escape -- accept either.
            const QString text = QString::fromUtf8(bytes);
            QVERIFY(text.contains(QChar(0x202E)) || text.contains(QStringLiteral("\\u202e")));
        }
    }
};

QTEST_GUILESS_MAIN(TestTaskExporterRobustness)
#include "test_taskexporter_robustness.moc"
