// Unit tests for TaskExporter — CSV, Markdown and per-project JSON serialisation.
//
// Focuses on edge cases that naive serialisation would break:
//   - quotes / commas / newlines in CSV cells
//   - unicode (Cyrillic + emoji) preserved as UTF-8
//   - soft-deleted tasks excluded
//   - completed-vs-open checkbox rendering in Markdown
//   - JSON round-trip (parse + structure inspection)
//   - failure paths for invalid output dirs

#include <QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>

#include "services/taskexporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/comment.h"

class TestTaskExporter : public QObject
{
    Q_OBJECT

private:
    // Read whole file as UTF-8 string. Returns empty string on failure.
    static QString readUtf8(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }

    // CSV writer uses QIODevice::Text on Windows which converts '\n' → '\r\n'.
    // For row-counting tests, normalise CRLF to LF first.
    static QString normaliseEol(QString s)
    {
        return s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }

private slots:
    void initTestCase()
    {
        // Belt-and-braces: even though every test passes an explicit `dir`,
        // any code path that falls back to QStandardPaths should land in a
        // test-isolated location, never the real user Documents folder.
        // Per-test org/app keeps the test-mode AppDataLocation isolated from
        // other tests that toggle setTestModeEnabled (test_filemanager*, etc.)
        // so they don't race on shared paths under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_TaskExporter"));
        QCoreApplication::setApplicationName(QStringLiteral("test_taskexporter"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // ── CSV: empty store ──────────────────────────────────────────────────────

    void csv_empty_store_has_header_only()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        TaskExporter exp(&ds);

        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        const QString text = normaliseEol(readUtf8(path));
        const QStringList lines = text.split(QChar('\n'), Qt::SkipEmptyParts);
        // Exactly one line: the header.
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines.first().startsWith(QStringLiteral("id,")));
    }

    // ── CSV: header columns ───────────────────────────────────────────────────

    void csv_header_has_expected_columns()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = normaliseEol(readUtf8(path));
        const QString header = text.split(QChar('\n')).value(0);

        const QStringList expected = {
            QStringLiteral("id"),
            QStringLiteral("project"),
            QStringLiteral("section"),
            QStringLiteral("content"),
            QStringLiteral("description"),
            QStringLiteral("priority"),
            QStringLiteral("due_date"),
            QStringLiteral("labels"),
            QStringLiteral("is_completed"),
            QStringLiteral("created_at"),
        };
        QCOMPARE(header, expected.join(QChar(',')));
    }

    // ── CSV: simple task content survives ─────────────────────────────────────

    void csv_simple_content_appears_unquoted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Personal"));
        ds.addTask(QStringLiteral("Buy milk"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = normaliseEol(readUtf8(path));
        // No quoting needed — the content has no comma/quote/newline.
        QVERIFY(text.contains(QStringLiteral(",Buy milk,")));
        QVERIFY(!text.contains(QStringLiteral("\"Buy milk\"")));
    }

    // ── CSV: comma in content → quoted ────────────────────────────────────────

    void csv_content_with_commas_is_quoted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Errands"));
        ds.addTask(QStringLiteral("Buy, eat, sleep"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        // Cell is wrapped in double quotes so the embedded commas don't split fields.
        QVERIFY(text.contains(QStringLiteral("\"Buy, eat, sleep\"")));
    }

    // ── CSV: double-quote in content → escaped via doubling ───────────────────

    void csv_content_with_double_quotes_is_escaped()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Quotes"));
        ds.addTask(QStringLiteral("Said \"hi\""), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        // Per RFC 4180: the field is wrapped in quotes, and each internal
        // quote is doubled.  "Said \"hi\""  →  "Said ""hi"""
        QVERIFY(text.contains(QStringLiteral("\"Said \"\"hi\"\"\"")));
    }

    // ── CSV: newline in content → quoted, content preserved ───────────────────

    void csv_content_with_newline_is_quoted_and_preserved()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Multi"));
        ds.addTask(QStringLiteral("Line1\nLine2"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        // Embedded newline forces quoting; the literal LF (or CRLF after Qt's
        // text-mode conversion on Windows) sits inside the quoted cell.
        QVERIFY(text.contains(QStringLiteral("\"Line1")));
        QVERIFY(text.contains(QStringLiteral("Line2\"")));
    }

    // ── CSV: unicode / emoji preserved as UTF-8 ───────────────────────────────

    void csv_unicode_and_emoji_preserved_utf8()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Покупки"));
        ds.addTask(QString::fromUtf8("Купить молоко \xF0\x9F\xA5\x9B"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QString::fromUtf8("Купить молоко")));
        QVERIFY(text.contains(QString::fromUtf8("\xF0\x9F\xA5\x9B"))); // 🥛
        QVERIFY(text.contains(QString::fromUtf8("Покупки")));         // project col
    }

    // ── CSV: priority + project + completed flag in their columns ─────────────

    void csv_columns_populated_correctly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Work"));
        Task *t = ds.addTask(QStringLiteral("Ship feature"), p->id());
        t->setPriority(2);
        t->setIsCompleted(true);
        t->setLabels({QStringLiteral("dev"), QStringLiteral("urgent")});
        t->setDueDate(QStringLiteral("2026-05-09"));

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = normaliseEol(readUtf8(path));
        // Find the data row (any line that contains the task content).
        QString dataRow;
        for (const QString &ln : text.split(QChar('\n'), Qt::SkipEmptyParts)) {
            if (ln.contains(QStringLiteral("Ship feature"))) { dataRow = ln; break; }
        }
        QVERIFY2(!dataRow.isEmpty(), "Data row not found");

        // Order: id,project,section,content,description,priority,due_date,labels,is_completed,created_at
        const QStringList cols = dataRow.split(QChar(','));
        QVERIFY(cols.size() >= 10);
        QCOMPARE(cols.at(1), QStringLiteral("Work"));
        QCOMPARE(cols.at(3), QStringLiteral("Ship feature"));
        QCOMPARE(cols.at(5), QStringLiteral("2"));
        QCOMPARE(cols.at(6), QStringLiteral("2026-05-09"));
        QCOMPARE(cols.at(7), QStringLiteral("dev|urgent"));
        QCOMPARE(cols.at(8), QStringLiteral("true"));
    }

    // ── CSV: soft-deleted tasks excluded ──────────────────────────────────────

    void csv_soft_deleted_tasks_excluded()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Mixed"));
        ds.addTask(QStringLiteral("alive task"), p->id());
        Task *dead = ds.addTask(QStringLiteral("dead task"), p->id());
        dead->setIsDeleted(true);

        TaskExporter exp(&ds);
        const QString path = exp.exportToCsv(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("alive task")));
        QVERIFY(!text.contains(QStringLiteral("dead task")));
    }

    // ── Markdown: empty store → header only, no project sections ──────────────

    void md_empty_store_emits_header_only()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        TaskExporter exp(&ds);

        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        const QString text = readUtf8(path);
        QVERIFY(text.startsWith(QStringLiteral("# OpenTodo")));
        // No project means no "## " heading.
        QVERIFY(!text.contains(QStringLiteral("\n## ")));
    }

    // ── Markdown: completed vs open render with [x] / [ ] checkboxes ──────────

    void md_completed_uses_x_open_uses_space()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Status"));
        ds.addTask(QStringLiteral("open task"), p->id());
        Task *done = ds.addTask(QStringLiteral("done task"), p->id());
        done->setIsCompleted(true);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("- [ ] open task")));
        QVERIFY(text.contains(QStringLiteral("- [x] done task")));
    }

    // ── Markdown: tasks grouped under their project's heading ─────────────────

    void md_tasks_grouped_by_project_heading()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p1 = ds.addProject(QStringLiteral("Alpha"));
        Project *p2 = ds.addProject(QStringLiteral("Beta"));
        ds.addTask(QStringLiteral("alpha-task"), p1->id());
        ds.addTask(QStringLiteral("beta-task"), p2->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("## Alpha")));
        QVERIFY(text.contains(QStringLiteral("## Beta")));

        const int alphaPos = text.indexOf(QStringLiteral("## Alpha"));
        const int alphaTaskPos = text.indexOf(QStringLiteral("alpha-task"));
        const int betaPos  = text.indexOf(QStringLiteral("## Beta"));
        const int betaTaskPos  = text.indexOf(QStringLiteral("beta-task"));

        QVERIFY(alphaPos >= 0 && alphaTaskPos > alphaPos);
        QVERIFY(betaPos > alphaTaskPos);
        QVERIFY(betaTaskPos > betaPos);
    }

    // ── Markdown: markdown special chars are passed through verbatim ──────────

    void md_special_characters_preserved_verbatim()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Specials"));
        ds.addTask(QStringLiteral("*bold* _italic_ [link](url)"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        // Exporter does NOT escape; assert we round-tripped each character set.
        QVERIFY(text.contains(QStringLiteral("*bold*")));
        QVERIFY(text.contains(QStringLiteral("_italic_")));
        QVERIFY(text.contains(QStringLiteral("[link](url)")));
    }

    // ── Markdown: archived projects excluded ──────────────────────────────────

    void md_archived_projects_excluded()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *active = ds.addProject(QStringLiteral("Active"));
        Project *archived = ds.addProject(QStringLiteral("Archived"));
        archived->setIsArchived(true);
        ds.addTask(QStringLiteral("active-task"), active->id());
        ds.addTask(QStringLiteral("archived-task"), archived->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("## Active")));
        QVERIFY(!text.contains(QStringLiteral("## Archived")));
        QVERIFY(text.contains(QStringLiteral("active-task")));
        QVERIFY(!text.contains(QStringLiteral("archived-task")));
    }

    // ── JSON: round-trip — parse and inspect structure ────────────────────────

    void json_round_trip_structure()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Roadmap"));
        Section *s = ds.addSection(QStringLiteral("Q3"), p->id());
        Task *t = ds.addTask(QStringLiteral("Plan launch"), p->id());
        t->setSectionId(s->id());
        t->setPriority(2);
        ds.addComment(QStringLiteral("первый коммент"), t->id(), QString());

        TaskExporter exp(&ds);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());

        const QJsonObject root = doc.object();
        QCOMPARE(root.value(QStringLiteral("format")).toString(),
                 QStringLiteral("opentodo-project-snapshot"));
        QCOMPARE(root.value(QStringLiteral("format_version")).toInt(), 1);
        QVERIFY(root.contains(QStringLiteral("exported_at")));
        QVERIFY(root.contains(QStringLiteral("project")));
        QVERIFY(root.contains(QStringLiteral("sections")));
        QVERIFY(root.contains(QStringLiteral("tasks")));
        QVERIFY(root.contains(QStringLiteral("comments")));

        // Project name round-trips.
        QCOMPARE(root.value(QStringLiteral("project")).toObject()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Roadmap"));

        // Sections / tasks / comments arrays each contain one entry.
        QCOMPARE(root.value(QStringLiteral("sections")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("tasks")).toArray().size(), 1);
        QCOMPARE(root.value(QStringLiteral("comments")).toArray().size(), 1);

        // Task content survives.
        QCOMPARE(root.value(QStringLiteral("tasks")).toArray().at(0)
                     .toObject().value(QStringLiteral("content")).toString(),
                 QStringLiteral("Plan launch"));
    }

    // ── JSON: soft-deleted tasks/comments are excluded ────────────────────────

    void json_excludes_soft_deleted_entities()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Mixed JSON"));
        ds.addTask(QStringLiteral("alive"), p->id());
        Task *dead = ds.addTask(QStringLiteral("dead"), p->id());
        dead->setIsDeleted(true);

        Comment *c = ds.addComment(QStringLiteral("kept"), QString(), p->id());
        Comment *c2 = ds.addComment(QStringLiteral("trashed"), QString(), p->id());
        c2->setIsDeleted(true);
        Q_UNUSED(c)

        TaskExporter exp(&ds);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());

        const QString text = readUtf8(path);
        QVERIFY(text.contains(QStringLiteral("alive")));
        QVERIFY(!text.contains(QStringLiteral("\"content\": \"dead\"")));
        QVERIFY(text.contains(QStringLiteral("kept")));
        QVERIFY(!text.contains(QStringLiteral("\"content\": \"trashed\"")));
    }

    // ── JSON: encoding is UTF-8, no BOM ───────────────────────────────────────

    void json_is_utf8_without_bom()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QString::fromUtf8("Юникод"));
        ds.addTask(QString::fromUtf8("Купить молоко \xF0\x9F\xA5\x9B"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();

        // No UTF-8 BOM (EF BB BF) at the start.
        QVERIFY(bytes.size() >= 3);
        QVERIFY(!(static_cast<unsigned char>(bytes.at(0)) == 0xEF
                  && static_cast<unsigned char>(bytes.at(1)) == 0xBB
                  && static_cast<unsigned char>(bytes.at(2)) == 0xBF));

        // The Cyrillic project name and the milk-glass emoji round-trip.
        const QString text = QString::fromUtf8(bytes);
        QVERIFY(text.contains(QString::fromUtf8("Юникод")));
        QVERIFY(text.contains(QString::fromUtf8("Купить молоко")));
        QVERIFY(text.contains(QString::fromUtf8("\xF0\x9F\xA5\x9B")));
    }

    // ── JSON: unknown project id → empty string + failure signal ──────────────

    void json_unknown_project_returns_empty_and_emits_failed()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        TaskExporter exp(&ds);
        QSignalSpy failed(&exp, &TaskExporter::exportFailed);
        QSignalSpy ok(&exp, &TaskExporter::exportSucceeded);

        const QString path = exp.exportProjectToJson(QStringLiteral("nonexistent-id"),
                                                     tmp.path());
        QVERIFY(path.isEmpty());
        QCOMPARE(ok.size(), 0);
        QCOMPARE(failed.size(), 1);
    }

    // ── Generic: empty `dir` falls back to a writable location, returns path ──

    void csv_empty_dir_falls_back_to_documents_writable_path()
    {
        DataStore ds;
        ds.addProject(QStringLiteral("Fallback"));
        TaskExporter exp(&ds);

        // With QStandardPaths in test mode, DocumentsLocation points into a
        // sandbox under XDG_*/AppData test root — safe to write.
        const QString path = exp.exportToCsv(QString());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));

        // Cleanup so we don't pollute the test sandbox between runs.
        QFile::remove(path);
    }

    // ── Generic: unwritable destination → empty path + failure signal ─────────
    //
    // We sabotage `dir` by making it the path of a regular file. `QDir().mkpath`
    // can't create a directory where a file exists, so the subsequent QFile
    // open fails → exportFailed fires → empty string returned.

    void csv_invalid_dir_returns_empty_and_emits_failed()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Create a regular file at the path we'll then ask the exporter to
        // treat as a directory.
        const QString filePath = tmp.path() + QStringLiteral("/blocker");
        {
            QFile blocker(filePath);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            blocker.write("x");
            blocker.close();
        }

        // Now ask the exporter to write into a *subdirectory* of `blocker`.
        // mkpath cannot succeed because `blocker` is a file.
        const QString badDir = filePath + QStringLiteral("/sub");

        DataStore ds;
        TaskExporter exp(&ds);
        QSignalSpy failed(&exp, &TaskExporter::exportFailed);
        QSignalSpy ok(&exp, &TaskExporter::exportSucceeded);

        const QString path = exp.exportToCsv(badDir);
        QVERIFY(path.isEmpty());
        QCOMPARE(ok.size(), 0);
        QCOMPARE(failed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestTaskExporter)
#include "test_taskexporter.moc"
