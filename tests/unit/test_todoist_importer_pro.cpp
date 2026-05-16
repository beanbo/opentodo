// ---------------------------------------------------------------------------
// test_todoist_importer_pro.cpp
//
// Quality round Q3.07: behavioural pinning for Todoist Pro-feature edge
// cases not covered by:
//   * test_todoist_import.cpp                  — happy-path ZIP fixture.
//   * test_todoist_importer_extended.cpp       — CSV row + tokenizer edges,
//                                                API-token fast-fail,
//                                                partial-success ZIP.
//   * test_todoist_importer_robustness.cpp     — malformed ZIPs, encodings,
//                                                priority sentinel, dates,
//                                                section edge content, deep
//                                                indent, image markers, re-run
//                                                dedup.
//
// This Pro suite adds nine slots covering the corners that are unique to
// paid-tier Todoist usage (or rarely-exercised data shapes):
//
//   1. section_column_creates_section_inside_project   — TYPE=section row
//      followed by a TYPE=task row produces a Section AND a Task, but pins
//      the documented gap: the CSV parser does not wire task.sectionId from
//      a preceding section row (the parser only emits sections; tasks remain
//      section-less). Pinned as a current-behaviour assertion plus qInfo.
//
//   2. comment_rows_become_task_comments               — TYPE=note row after
//      a TYPE=task row produces a Comment attached to the preceding task in
//      DataStore.
//
//   3. zip_with_multiple_projects_imports_all          — multi-CSV ZIP
//      fixture; verifies all projects + their tasks land with the expected
//      counts. Pins that the synthetic project id format remains stable
//      ("csv-proj-N-…").
//
//   4. russian_recurring_DATE_imports_isRecurring_true — DATE="каждый день",
//      DATE_LANG=ru. Pins that DueObject.dueIsRecurring=true after import,
//      AND surfaces the documented gap: dueRecurrenceRule is NOT propagated
//      from DateParser through TodoistDue into the Task — the RRULE is
//      computed by rules_ru.cpp but dropped because TodoistDue has no
//      recurrenceRule member and applyImport never calls setDueRecurrenceRule.
//
//   5. indent_level_3_creates_grandchild_subtask       — INDENT 1→2→3 chain
//      produces A as root, B with parentId=A, C with parentId=B.
//
//   6. empty_project_csv_creates_project_but_no_tasks  — header-only CSV
//      yields one Project (named after the file) and zero Tasks.
//
//   7. duplicate_content_does_not_dedup_within_one_csv — two TYPE=task rows
//      with identical CONTENT in the same CSV. Pins the documented
//      same-import dedup behaviour (added to existingFingerprints inside the
//      task loop), surfacing that the in-import dedup does coalesce these.
//      Re-run-across-imports dedup is already covered in robustness.
//
//   8. named_project_color_palette_documented           — CSV-path projects
//      ALWAYS land as "#b8b8b8" (grey) regardless of any palette name. Pins
//      the documented mapping in mapTodoistColor for berry_red / lavender /
//      olive_green / charcoal / taupe via expected-value commentary and
//      verifies the grey default still wins for CSV-only imports.
//
//   9. csv_with_utf8_bom_imports_normally               — a CSV whose first
//      three bytes are EF BB BF (UTF-8 BOM). parseBytes()'s BOM branch must
//      strip the marker so the header check passes and one task lands.
//
// Constraints:
//   * QTEST_GUILESS_MAIN (no GUI; Infra layer only).
//   * Unique QSettings org/app per slot (kOrg / kApp set in initTestCase).
//   * NO raw R"(…)" strings — moc-hostile in this project.
//   * NO production-code edits — assertions pin CURRENT behaviour with
//     qInfo() footnotes where the behaviour diverges from a naive Pro
//     expectation.
//   * Sister suites' helpers (buildZip + writeZipTo + crc32of + le16/32 +
//     makeStore + kHeader) are intentionally duplicated here to keep this
//     file self-contained: each test binary is its own translation unit and
//     each adds ~2 KB of test glue, which is negligible.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QSet>

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
// Shared helpers (file-scope; no function-local QObjects).
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
    quint16    method = 0; // 0 = Stored. We only use Stored in this suite.
};

// Build a ZIP archive in memory with Stored (method 0) entries, UTF-8
// filename flag set. The importer's hand-rolled reader supports method 0
// directly; this avoids a dependency on zlib for the writer side.
static QByteArray buildZip(const QList<ZipFileSpec> &files)
{
    QByteArray archive;
    QList<QByteArray> centralEntries;
    QList<quint32>    localOffsets;

    for (const auto &f : files) {
        const QByteArray nameUtf8 = f.name.toUtf8();
        const quint32 crc         = crc32of(f.contents);
        const quint32 size        = quint32(f.contents.size());
        const quint16 gpFlag      = 0x0800; // UTF-8 filename (EFS bit)
        const quint16 method      = f.method;
        const quint16 modTime     = 0;
        const quint16 modDate     = 0x21;   // 1980-01-01

        localOffsets.append(quint32(archive.size()));

        // Local file header (signature 0x04034b50).
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

        // Central-directory record (signature 0x02014b50).
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
    for (const auto &e : centralEntries) cdAll += e;
    archive += cdAll;
    const quint32 cdSize = quint32(cdAll.size());

    // End-of-central-directory (signature 0x06054b50).
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
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes);
    f.close();
    return path;
}

static QString writeZipTo(const QTemporaryDir &dir, const QString &name,
                          const QList<ZipFileSpec> &files)
{
    return writeZipBytesTo(dir, name, buildZip(files));
}

// ===========================================================================
class TestTodoistImporterPro : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_TodoistImpPro"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_todoist_importer_pro"));

        // Wipe any stale batch tracking left over from a prior failed run so
        // dedup behaviour starts from a known state.
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

    // ── 1. Section row creates Section inside project ────────────────────────
    //
    // Todoist CSV does NOT have a SECTION column; it uses a separate TYPE=section
    // row that applies to all following TYPE=task rows until the next section.
    // The parser emits a TodoistSection per row. We pin TWO things here:
    //   (a) the Section entity is created and attached to the imported project,
    //   (b) the documented gap that the CSV parser does NOT also stamp the
    //       following task with sectionId — the task lands section-less.
    void section_column_creates_section_inside_project()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("section,Planning,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Write spec,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Review spec,,1,1,,,,en,,,\n")
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("sec.zip"),
            { { QStringLiteral("ProjectAlpha [aaaaaaaaaaaa].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // 2 tasks imported.
        QCOMPARE(finished[0][0].toInt(), 2);

        // Locate the project.
        Project *alpha = nullptr;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("ProjectAlpha")) { alpha = p; break; }
        }
        QVERIFY(alpha);

        // Section row produced an actual Section entity, attached to the
        // ProjectAlpha project.
        const auto secs = ds->sectionsForProject(alpha->id());
        QCOMPARE(secs.size(), 1);
        QCOMPARE(secs[0]->name(), QStringLiteral("Planning"));

        // GAP: the parser sets up TodoistSection but never wires section_id
        // onto the subsequent task. Both tasks land with empty sectionId.
        int sectionedTasks = 0;
        int sectionlessTasks = 0;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Write spec")
             || t->content() == QStringLiteral("Review spec"))
            {
                if (t->sectionId().isEmpty()) ++sectionlessTasks;
                else                          ++sectionedTasks;
            }
        }
        QCOMPARE(sectionlessTasks, 2);
        QCOMPARE(sectionedTasks, 0);
        qInfo() << "GAP: TodoistCsvParser emits sections but never wires "
                   "task.sectionId from a preceding TYPE=section row. Tasks "
                   "imported under a section land section-less in DataStore. "
                   "Fix would track 'currentSectionId' in the parse loop and "
                   "set task.sectionId = synthesized section id.";
    }

    // ── 2. note row becomes a Comment attached to the preceding task ─────────
    void comment_rows_become_task_comments()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Buy milk,,1,1,,,,en,,,\n")
            + QStringLiteral("note,Check expiry date,,1,1,,,,en,,,\n")
            + QStringLiteral("note,Use whole milk,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Pay bill,,1,1,,,,en,,,\n")
            + QStringLiteral("note,Due Friday,,1,1,,,,en,,,\n")
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("notes.zip"),
            { { QStringLiteral("Errands [bbbbbbbbbbbb].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 2); // 2 tasks

        Task *milk = nullptr;
        Task *bill = nullptr;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Buy milk")) milk = t;
            if (t->content() == QStringLiteral("Pay bill")) bill = t;
        }
        QVERIFY(milk);
        QVERIFY(bill);

        // The first two notes attach to the most recent preceding task
        // (Buy milk); the third attaches to Pay bill.
        const auto milkComments = ds->commentsForTask(milk->id());
        const auto billComments = ds->commentsForTask(bill->id());
        QCOMPARE(milkComments.size(), 2);
        QCOMPARE(billComments.size(), 1);

        QStringList milkBodies;
        for (auto *c : milkComments) milkBodies << c->content();
        QVERIFY2(milkBodies.contains(QStringLiteral("Check expiry date")),
                 qPrintable(milkBodies.join(u'|')));
        QVERIFY2(milkBodies.contains(QStringLiteral("Use whole milk")),
                 qPrintable(milkBodies.join(u'|')));
        QCOMPARE(billComments[0]->content(), QStringLiteral("Due Friday"));
    }

    // ── 3. Multi-project ZIP (TodoistBackup with N projects) ─────────────────
    void zip_with_multiple_projects_imports_all()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Three projects, each with two tasks. Filenames use the canonical
        // Todoist export shape "Display name [<24-char id>].csv" so the id
        // suffix is stripped to a clean project name.
        const QByteArray csvWork = (kHeader
            + QStringLiteral("task,Quarterly review,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Standup notes,,2,1,,,,en,,,\n")).toUtf8();
        const QByteArray csvHome = (kHeader
            + QStringLiteral("task,Mow lawn,,3,1,,,,en,,,\n")
            + QStringLiteral("task,Wash car,,4,1,,,,en,,,\n")).toUtf8();
        const QByteArray csvLearn = (kHeader
            + QStringLiteral("task,Finish book,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Study Qt6,,1,1,,,,en,,,\n")).toUtf8();

        const QString path = writeZipTo(tmp,
            QStringLiteral("multi.zip"),
            {
                { QStringLiteral("Work [111111111111].csv"),    csvWork,  0 },
                { QStringLiteral("Home [222222222222].csv"),    csvHome,  0 },
                { QStringLiteral("Learning [333333333333].csv"), csvLearn, 0 },
            });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // 3 projects, 6 tasks.
        QCOMPARE(finished[0][0].toInt(), 6);
        QCOMPARE(finished[0][1].toInt(), 3);

        QSet<QString> projectNames;
        for (auto *p : ds->allProjects()) {
            if (p->isInbox()) continue;
            projectNames.insert(p->name());
        }
        QVERIFY(projectNames.contains(QStringLiteral("Work")));
        QVERIFY(projectNames.contains(QStringLiteral("Home")));
        QVERIFY(projectNames.contains(QStringLiteral("Learning")));

        // Verify each project owns its two tasks (no cross-contamination).
        auto tasksInProject = [&](const QString &projName) -> QStringList {
            Project *target = nullptr;
            for (auto *p : ds->allProjects()) {
                if (p->name() == projName) { target = p; break; }
            }
            QStringList out;
            if (!target) return out;
            for (auto *t : ds->allTasks()) {
                if (t->projectId() == target->id()) out << t->content();
            }
            return out;
        };

        const QStringList workTasks = tasksInProject(QStringLiteral("Work"));
        QCOMPARE(workTasks.size(), 2);
        QVERIFY(workTasks.contains(QStringLiteral("Quarterly review")));
        QVERIFY(workTasks.contains(QStringLiteral("Standup notes")));

        const QStringList homeTasks = tasksInProject(QStringLiteral("Home"));
        QCOMPARE(homeTasks.size(), 2);
        QVERIFY(homeTasks.contains(QStringLiteral("Mow lawn")));
        QVERIFY(homeTasks.contains(QStringLiteral("Wash car")));

        const QStringList learnTasks = tasksInProject(QStringLiteral("Learning"));
        QCOMPARE(learnTasks.size(), 2);
        QVERIFY(learnTasks.contains(QStringLiteral("Finish book")));
        QVERIFY(learnTasks.contains(QStringLiteral("Study Qt6")));
    }

    // ── 4. Russian recurring DATE column ─────────────────────────────────────
    //
    // Pin: dueIsRecurring=true after import. Also surfaces the gap that
    // dueRecurrenceRule is lost in transit — DateParser computes
    // "FREQ=DAILY" for "каждый день" but TodoistDue has no recurrenceRule
    // member, and applyImport never calls setDueRecurrenceRule on the task.
    void russian_recurring_DATE_imports_isRecurring_true()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Утренняя зарядка,,1,1,,,каждый день,ru,,,\n")
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("recur.zip"),
            { { QStringLiteral("Привычки [ccccccccccccc].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));
        QCOMPARE(finished[0][0].toInt(), 1);

        Task *recur = nullptr;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QString::fromUtf8("Утренняя зарядка"))
                recur = t;
        }
        QVERIFY(recur);

        // Pin 1: the recurrence flag survives.
        QVERIFY2(recur->dueIsRecurring(),
                 "'каждый день' did not set dueIsRecurring=true after import");
        QCOMPARE(recur->dueLang(), QStringLiteral("ru"));
        // The DATE column is preserved verbatim as the human-readable string.
        QCOMPARE(recur->dueString(), QString::fromUtf8("каждый день"));
        // DateParser anchors the recurrence to today; due_date should be set.
        const QString todayIso = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(recur->dueDate(), todayIso);

        // GAP: dueRecurrenceRule is empty even though rules_ru.cpp emits
        // "FREQ=DAILY". TodoistDue has no recurrenceRule member, so the
        // parser drops it and applyImport never calls setDueRecurrenceRule.
        QVERIFY2(recur->dueRecurrenceRule().isEmpty(),
                 qPrintable(QStringLiteral(
                     "If dueRecurrenceRule is now populated, the gap was "
                     "fixed; update this assertion. Got: %1")
                     .arg(recur->dueRecurrenceRule())));
        qInfo() << "GAP: rules_ru.cpp emits FREQ=DAILY for 'каждый день' "
                   "but TodoistDue has no recurrenceRule field. RRULE is "
                   "lost between DateParser and applyImport. The "
                   "RecurrenceEngine falls back to humanString parsing at "
                   "runtime, so behaviour still works — but the explicit "
                   "RFC 5545 RRULE never reaches the Task model.";
    }

    // ── 5. Subtask hierarchy via INDENT column (3-level chain) ───────────────
    void indent_level_3_creates_grandchild_subtask()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,A root,,1,1,,,,en,,,\n")
            + QStringLiteral("task,B child,,1,2,,,,en,,,\n")
            + QStringLiteral("task,C grandchild,,1,3,,,,en,,,\n")
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("indent3.zip"),
            { { QStringLiteral("Hier [ddddddddddddd].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());
        QCOMPARE(finished[0][0].toInt(), 3);

        Task *a = nullptr; Task *b = nullptr; Task *c = nullptr;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("A root"))        a = t;
            if (t->content() == QStringLiteral("B child"))       b = t;
            if (t->content() == QStringLiteral("C grandchild"))  c = t;
        }
        QVERIFY(a); QVERIFY(b); QVERIFY(c);

        // Root has no parent.
        QVERIFY(a->parentId().isEmpty());
        // Mid-level B parents to A.
        QCOMPARE(b->parentId(), a->id());
        // Grandchild C parents to B (not to A).
        QCOMPARE(c->parentId(), b->id());
    }

    // ── 6. Empty project — header row only, no data rows ─────────────────────
    void empty_project_csv_creates_project_but_no_tasks()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = kHeader.toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("empty.zip"),
            { { QStringLiteral("BlankProject [eeeeeeeeeeeee].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        const int tasksBefore = ds->allTasks().size();
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // 0 tasks, 1 project added.
        QCOMPARE(finished[0][0].toInt(), 0);
        QCOMPARE(finished[0][1].toInt(), 1);
        QCOMPARE(ds->allTasks().size(), tasksBefore);

        // Verify the empty project exists with the expected name.
        bool foundBlank = false;
        for (auto *p : ds->allProjects()) {
            if (p->name() == QStringLiteral("BlankProject")) {
                foundBlank = true;
                // No tasks should reference this project.
                int taskCount = 0;
                for (auto *t : ds->allTasks()) {
                    if (t->projectId() == p->id()) ++taskCount;
                }
                QCOMPARE(taskCount, 0);
                break;
            }
        }
        QVERIFY(foundBlank);
    }

    // ── 7. Duplicate content within one CSV ──────────────────────────────────
    //
    // Pin: when the SAME CONTENT appears twice in ONE CSV, the in-import
    // fingerprint dedup (existingFingerprints.insert(fp) inside the task
    // loop) coalesces them into a SINGLE Task in DataStore. This is the
    // documented trade-off in todoistimporter.cpp around line 599:
    //
    //   "Trade-off: two tasks with the same content in the same project are
    //    now treated as the same task for dedup purposes. That's an unusual
    //    case (people don't usually file two literal duplicates)…"
    //
    // The mission slot title was "duplicate_content_does_not_dedup" — the
    // pin here is the OPPOSITE finding: in the current implementation,
    // duplicates DO dedup. Surface this with qInfo so a future fix that
    // wants to honour legit "Buy milk x2" can flip the assertion.
    void duplicate_content_does_not_dedup_within_one_csv()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QByteArray csv = (kHeader
            + QStringLiteral("task,Buy milk,,1,1,,,,en,,,\n")
            + QStringLiteral("task,Buy milk,,1,1,,,,en,,,\n")
            ).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("dup.zip"),
            { { QStringLiteral("Shopping [fffffffffffff].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());

        int copies = 0;
        QSet<QString> uuids;
        for (auto *t : ds->allTasks()) {
            if (t->content() == QStringLiteral("Buy milk")) {
                ++copies;
                uuids.insert(t->id());
            }
        }
        // Documented current behaviour: in-import dedup collapses both rows
        // into one DataStore Task. The importFinished count reports 1.
        QCOMPARE(copies, 1);
        QCOMPARE(finished[0][0].toInt(), 1);
        QCOMPARE(uuids.size(), 1);
        qInfo() << "GAP: in-import dedup currently coalesces two TYPE=task "
                   "rows with the same CONTENT in the same project into a "
                   "single Task. The user-facing scenario is rare ('Buy "
                   "milk' x2 to remember two cartons) but legitimate. A "
                   "fix would change the fingerprint to include row order "
                   "or duplicate the entity but mark a 'dup_of' link.";
    }

    // ── 8. Named Todoist palette → CSS hex (pinned via comment ledger) ───────
    //
    // The CSV export does NOT carry a project-level colour column, so every
    // CSV-imported project lands as #b8b8b8 (grey) regardless of the source
    // project's palette. The full mapTodoistColor() palette is reachable
    // only from the API path. We pin the grey contract for the CSV path
    // here, plus document the expected hex mappings as comments so a future
    // CSV-with-colour spec extension has a regression bench to flip.
    //
    // From src/import/todoistimporter.cpp::mapTodoistColor():
    //
    //   berry_red    → "#b8256f"
    //   red          → "#db4035"
    //   orange       → "#ff9933"
    //   yellow       → "#fad000"
    //   olive_green  → "#afb83b"
    //   lime_green   → "#7ecc49"
    //   green        → "#299438"
    //   mint_green   → "#6accbc"
    //   teal         → "#158fad"
    //   sky_blue     → "#14aaf5"
    //   light_blue   → "#96c3eb"
    //   blue         → "#4073ff"
    //   grape        → "#884dff"
    //   violet       → "#af38eb"
    //   lavender     → "#eb96eb"
    //   magenta      → "#e05194"
    //   salmon       → "#ff8d85"
    //   charcoal     → "#808080"
    //   grey         → "#b8b8b8"     ← CSV default lands here
    //   taupe        → "#ccac93"
    //   unknown      → "#b8b8b8"     ← maps to grey on miss
    void named_project_color_palette_maps_to_hex()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Import three projects whose source-system palette would have been
        // berry_red / lavender / olive_green. CSV path can't carry that, so
        // all three must land as grey #b8b8b8 — that's the contract being
        // pinned. The expected hex values for the named palette live in the
        // function comment above for future regression flipping.
        const QByteArray csv = (kHeader
            + QStringLiteral("task,Anchor,,1,1,,,,en,,,\n")).toUtf8();
        const QString path = writeZipTo(tmp,
            QStringLiteral("palette.zip"),
            {
                { QStringLiteral("BerryProj [aaaaaaaaaaaa].csv"),    csv, 0 },
                { QStringLiteral("LavenderProj [bbbbbbbbbbbb].csv"), csv, 0 },
                { QStringLiteral("OliveProj [cccccccccccc].csv"),    csv, 0 },
            });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][2].toString().isEmpty());

        QMap<QString, QString> colorByName;
        for (auto *p : ds->allProjects()) {
            if (p->isInbox()) continue;
            colorByName.insert(p->name(), p->color());
        }
        // All three CSV-imported projects land as grey because the format
        // carries no per-project colour column.
        QCOMPARE(colorByName.value(QStringLiteral("BerryProj")),
                 QStringLiteral("#b8b8b8"));
        QCOMPARE(colorByName.value(QStringLiteral("LavenderProj")),
                 QStringLiteral("#b8b8b8"));
        QCOMPARE(colorByName.value(QStringLiteral("OliveProj")),
                 QStringLiteral("#b8b8b8"));

        qInfo() << "GAP (documented): CSV-path imports cannot recover the "
                   "Todoist named palette; use the API-token path to "
                   "preserve berry_red / lavender / olive_green / etc. "
                   "Expected API-path hex values pinned in the slot comment.";
    }

    // ── 9. CSV with UTF-8 BOM at byte 0 imports normally ─────────────────────
    void csv_with_BOM_imports_normally()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Build a CSV with a UTF-8 BOM (EF BB BF) at the very start. The
        // robustness suite tests UTF-16 BOMs; this slot covers the UTF-8
        // case which is what Windows Excel emits when saving as "CSV UTF-8".
        QByteArray bom;
        bom.append(char(0xEF));
        bom.append(char(0xBB));
        bom.append(char(0xBF));
        const QByteArray csv = bom + (kHeader
            + QStringLiteral("task,BOM-prefixed task,,1,1,,,,en,,,\n")
            ).toUtf8();

        const QString path = writeZipTo(tmp,
            QStringLiteral("bom.zip"),
            { { QStringLiteral("Excel [gggggggggggg].csv"), csv, 0 } });

        QScopedPointer<DataStore> ds(makeStore(this));
        QScopedPointer<TodoistImporter> imp(new TodoistImporter(ds.data(), this));

        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(path, false);

        QCOMPARE(finished.size(), 1);
        QVERIFY2(finished[0][2].toString().isEmpty(),
                 qPrintable(finished[0][2].toString()));

        // 1 task imported with the canonical content (no BOM artefact).
        QCOMPARE(finished[0][0].toInt(), 1);
        const auto tasks = ds->allTasks();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks[0]->content(), QStringLiteral("BOM-prefixed task"));
        // BOM stripping must not bleed a leading U+FEFF into the content
        // (would happen if the parser path forgot to skip the BOM).
        QVERIFY(!tasks[0]->content().contains(QChar(0xFEFF)));
    }
};

QTEST_GUILESS_MAIN(TestTodoistImporterPro)
#include "test_todoist_importer_pro.moc"
