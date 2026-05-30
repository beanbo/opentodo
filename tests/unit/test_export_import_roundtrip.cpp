// Property / round-trip tests for the TaskExporter <-> TodoistImporter pair.
//
// IMPORTANT — the two halves don't share a common wire format:
//
//   * TaskExporter writes either:
//       - a flat OpenTodo-schema CSV (id,project,section,content,...), OR
//       - a Markdown render, OR
//       - a self-contained per-project JSON snapshot (Project::toJson() +
//         Section::toJson() + Task::toJson() + Comment::toJson()).
//
//   * TodoistImporter reads ZIP archives whose entries are Todoist-format
//     CSV files (TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,...) — one CSV per
//     project, named "<ProjectName> [<id>].csv".
//
// So a true byte-level round-trip via the same wire format does not exist
// in production — these are intentionally two different output formats.
//
// This suite therefore tests round-trip equivalence via *each* path:
//
//   (a) JSON path — exportProjectToJson() → parse JSON → Project::fromJson /
//       Section::fromJson / Task::fromJson into a fresh DataStore. This is
//       the lossless path and is the basis for most slots here.
//
//   (b) Todoist CSV-via-ZIP path — synthesize a Todoist-format CSV that
//       round-trips an OpenTodo task graph through the import side. This is
//       lossy by design (_meta is dropped, tombstones are not represented in
//       the format, etc.) but exercises the actual exporter intent of
//       "produce something importable elsewhere".
//
// We document any field that does not survive each path via qInfo() and only
// QCOMPARE the fields that the format actually carries.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QStringList>

#include "services/taskexporter.h"
#include "import/todoistimporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/comment.h"

// ===========================================================================
// File-scope helpers (kept out of the test class so moc never has to grok
// nested template machinery).  No Q_OBJECT / Q_GADGET here.
// ===========================================================================

namespace rt {

// ---------------------------------------------------------------------------
// Build a fresh DataStore with a single Inbox project (the production app
// always has one; importing into a store without an Inbox would silently
// route inbox tasks nowhere).
// ---------------------------------------------------------------------------
static DataStore *makeStoreWithInbox(QObject *parent = nullptr)
{
    auto *ds = new DataStore(parent);
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

// ---------------------------------------------------------------------------
// JSON-path round-trip.
//
// Read every per-project JSON snapshot file in `dir`, parse it via the
// stable Project/Section/Task/Comment::fromJson API, and inject the
// reconstructed entities into `dst`.  Stable id preservation lets parent_id
// / section_id references survive intact.
//
// Returns the number of entities that were re-imported (sum of projects,
// sections, tasks, comments).
// ---------------------------------------------------------------------------
static int reimportJsonSnapshots(const QString &dir, DataStore *dst)
{
    int count = 0;
    QDir d(dir);
    const QStringList jsons = d.entryList(QStringList()
        << QStringLiteral("opentodo-project-*.json"), QDir::Files);
    for (const QString &name : jsons) {
        QFile f(d.absoluteFilePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject root = doc.object();

        // --- Project: NEVER re-create the Inbox; map onto existing one ---
        const QJsonObject projObj = root.value(QStringLiteral("project")).toObject();
        const bool isInboxProj = projObj.value(QStringLiteral("is_inbox")).toBool();
        QString remappedProjectId; // for inbox the original id is replaced
        if (isInboxProj) {
            Project *existingInbox = dst->inboxProject();
            if (existingInbox) {
                remappedProjectId = existingInbox->id();
            } else {
                Project *p = Project::fromJson(projObj);
                dst->addExistingProject(p);
                ++count;
            }
        } else {
            // Plain project — preserve id so referring entities still resolve.
            Project *p = Project::fromJson(projObj);
            dst->addExistingProject(p);
            ++count;
        }

        // --- Sections ---
        const QJsonArray secArr = root.value(QStringLiteral("sections")).toArray();
        for (const QJsonValue &v : secArr) {
            Section *s = Section::fromJson(v.toObject());
            if (!remappedProjectId.isEmpty()) s->setProjectId(remappedProjectId);
            dst->addExistingSection(s);
            ++count;
        }

        // --- Tasks ---
        const QJsonArray taskArr = root.value(QStringLiteral("tasks")).toArray();
        for (const QJsonValue &v : taskArr) {
            Task *t = Task::fromJson(v.toObject());
            if (!remappedProjectId.isEmpty()) t->setProjectId(remappedProjectId);
            dst->addExistingTask(t);
            ++count;
        }

        // --- Comments ---
        const QJsonArray cmtArr = root.value(QStringLiteral("comments")).toArray();
        for (const QJsonValue &v : cmtArr) {
            Comment *c = Comment::fromJson(v.toObject());
            if (!remappedProjectId.isEmpty() && !c->projectId().isEmpty())
                c->setProjectId(remappedProjectId);
            dst->addExistingComment(c);
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Synthesize a tiny Todoist-format ZIP from a few CSV-text snippets.
// We use Stored (method=0) so we don't need DEFLATE on the writer side; the
// importer's ZIP reader supports both 0 and 8.
//
// Each CsvFile becomes one entry "<projectName>.csv" in the archive.
// ---------------------------------------------------------------------------
struct CsvFile {
    QString projectName;
    QByteArray contents;
};

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
static QByteArray buildTodoistZip(const QList<CsvFile> &files)
{
    QByteArray archive;
    QList<QByteArray> centralEntries;
    QList<quint32>    localOffsets;

    for (const CsvFile &f : files) {
        const QByteArray nameUtf8 =
            (f.projectName + QStringLiteral(".csv")).toUtf8();
        const quint32 crc    = crc32of(f.contents);
        const quint32 size   = quint32(f.contents.size());
        const quint16 gpFlag = 0x0800; // EFS bit -> UTF-8 filename

        localOffsets.append(quint32(archive.size()));

        archive += QByteArray::fromHex("504b0304");
        archive += le16(20);            // version needed
        archive += le16(gpFlag);
        archive += le16(0);             // method = Stored
        archive += le16(0);             // mod time
        archive += le16(0x21);          // mod date 1980-01-01
        archive += le32(crc);
        archive += le32(size);          // compressed
        archive += le32(size);          // uncompressed
        archive += le16(quint16(nameUtf8.size()));
        archive += le16(0);             // extra
        archive += nameUtf8;
        archive += f.contents;

        QByteArray cd;
        cd += QByteArray::fromHex("504b0102");
        cd += le16(20);
        cd += le16(20);
        cd += le16(gpFlag);
        cd += le16(0);
        cd += le16(0);
        cd += le16(0x21);
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
    for (const QByteArray &e : centralEntries) cdAll += e;
    archive += cdAll;
    const quint32 cdSize = quint32(cdAll.size());

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

// CSV-escape a single field for a Todoist-style row.  Quote if needed; double
// any internal quotes per RFC 4180.
static QString csvField(const QString &s)
{
    if (s.contains(QChar(',')) || s.contains(QChar('"')) || s.contains(QChar('\n'))) {
        QString esc = s;
        esc.replace(QChar('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(esc);
    }
    return s;
}

// Build a minimal Todoist-format CSV for one project's tasks.  The header
// matches the 12-column schema the importer expects.  A Todoist CSV and the
// OpenTodo internal priority use the SAME scale (1=urgent..4=normal), so the
// priority round-trips as the identity — see test_todoist_import.cpp
// "parser_priorities_mapped_correctly" for the contract.
static QByteArray buildTodoistCsv(const QList<Task *> &tasks)
{
    QString out;
    out += QStringLiteral("TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,"
                          "RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,"
                          "DURATION_UNIT\n");
    for (const Task *t : tasks) {
        if (t->isDeleted()) continue;
        // Internal 1..4 <-> CSV 1..4 (identity; both use 1=urgent)
        const int csvPrio = t->priority();
        out += QStringLiteral("task,") + csvField(t->content())
            + QChar(',') + csvField(t->description())
            + QChar(',') + QString::number(csvPrio)
            + QStringLiteral(",1,,,") + csvField(t->dueDate())
            + QStringLiteral(",en,,,\n");
    }
    return out.toUtf8();
}

// ---------------------------------------------------------------------------
// Field-by-field comparator for a Task across the lossless JSON path.
//
// `which` is a label for qInfo() output so a failing slot tells us which
// roundtrip stage broke ("first roundtrip" vs "second roundtrip" etc.).
//
// Returns true iff every checked field matches; logs each mismatch via
// qInfo() before returning so the test trace is debuggable.
// ---------------------------------------------------------------------------
static bool tasksJsonPathEquivalent(const Task *a, const Task *b, const char *which)
{
    auto cmp = [which](const char *field, const QString &av, const QString &bv) {
        if (av != bv) {
            qInfo("[%s] field %s mismatch: '%s' vs '%s'", which, field,
                  qUtf8Printable(av), qUtf8Printable(bv));
            return false;
        }
        return true;
    };
    auto cmpInt = [which](const char *field, int av, int bv) {
        if (av != bv) {
            qInfo("[%s] field %s mismatch: %d vs %d", which, field, av, bv);
            return false;
        }
        return true;
    };
    auto cmpBool = [which](const char *field, bool av, bool bv) {
        if (av != bv) {
            qInfo("[%s] field %s mismatch: %d vs %d", which, field,
                  int(av), int(bv));
            return false;
        }
        return true;
    };

    bool ok = true;
    ok &= cmp   ("id",            a->id(),               b->id());
    ok &= cmp   ("content",       a->content(),          b->content());
    ok &= cmp   ("description",   a->description(),      b->description());
    ok &= cmp   ("project_id",    a->projectId(),        b->projectId());
    ok &= cmp   ("section_id",    a->sectionId(),        b->sectionId());
    ok &= cmp   ("parent_id",     a->parentId(),         b->parentId());
    ok &= cmpInt("priority",      a->priority(),         b->priority());
    ok &= cmpBool("is_completed", a->isCompleted(),      b->isCompleted());
    ok &= cmp   ("due_date",      a->dueDate(),          b->dueDate());
    ok &= cmp   ("due_string",    a->dueString(),        b->dueString());
    ok &= cmpBool("due_recur",    a->dueIsRecurring(),   b->dueIsRecurring());

    // Labels: order matters for QStringList equality, so compare directly.
    if (a->labels() != b->labels()) {
        qInfo("[%s] labels mismatch: [%s] vs [%s]", which,
              qUtf8Printable(a->labels().join(QChar('|'))),
              qUtf8Printable(b->labels().join(QChar('|'))));
        ok = false;
    }
    return ok;
}

} // namespace rt

// ===========================================================================
// Test class
// ===========================================================================

class TestExportImportRoundtrip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Per-test-suite QSettings org/app — the importer's batch-id tracking
        // and DataStore::localDeviceId() both hit QSettings, and we don't
        // want them clobbering parallel runs of test_taskexporter or
        // test_todoist_importer_extended.
        // Set BEFORE setTestModeEnabled(true) so QStandardPaths sandbox uses
        // this org/app from the start.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ExportImport"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_export_import_roundtrip"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // ── 1. empty ─────────────────────────────────────────────────────────────
    //
    // An empty store (just an Inbox) round-trips into another empty store.
    // No project JSON snapshot will be produced for a non-existent non-Inbox
    // project; the inbox alone has no tasks so no JSON at all is written.

    void empty_datastore_roundtrips_cleanly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore src;
        Project *inboxA = new Project(&src);
        inboxA->setName(QStringLiteral("Inbox"));
        inboxA->setIsInbox(true);
        src.addExistingProject(inboxA);

        TaskExporter exp(&src);
        // Try to export the inbox snapshot — should succeed and produce a
        // file with zero tasks/sections/comments.
        const QString inboxPath = exp.exportProjectToJson(inboxA->id(), tmp.path());
        QVERIFY(!inboxPath.isEmpty());
        QVERIFY(QFile::exists(inboxPath));

        // Reimport into a fresh store that already has its own inbox.
        DataStore dst;
        Project *inboxB = new Project(&dst);
        inboxB->setName(QStringLiteral("Inbox"));
        inboxB->setIsInbox(true);
        dst.addExistingProject(inboxB);

        const int reimported = rt::reimportJsonSnapshots(tmp.path(), &dst);
        // The exporter wrote exactly one file (the inbox snapshot) but the
        // re-importer correctly mapped it to the existing inbox -> 0 new
        // entities reimported.
        qInfo("Empty-store round-trip reimported %d entities", reimported);
        QCOMPARE(reimported, 0);

        // Task count remains zero, inbox is still present and still flagged.
        QCOMPARE(dst.taskCount(), 0);
        Project *foundInbox = dst.inboxProject();
        QVERIFY(foundInbox != nullptr);
        QCOMPARE(foundInbox->name(), QStringLiteral("Inbox"));
        QVERIFY(foundInbox->isInbox());
    }

    // ── 2. single task with all fields ───────────────────────────────────────

    void single_task_with_all_fields_roundtrips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore src;
        Project *p = src.addProject(QStringLiteral("Roadmap"));
        Task *parent = src.addTask(QStringLiteral("Parent epic"), p->id());
        Task *t = src.addTask(QStringLiteral("Ship feature"), p->id());

        // Populate every interesting field on the child task.
        t->setDescription(QStringLiteral("Multi-line\ndescription with detail."));
        t->setPriority(2);
        t->setLabels({QStringLiteral("dev"), QStringLiteral("urgent")});
        t->setParentId(parent->id());
        t->setDueDate(QStringLiteral("2026-05-09"));
        t->setDueString(QStringLiteral("today"));

        TaskExporter exp(&src);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY2(!path.isEmpty(), "JSON export returned empty path");

        DataStore dst;
        const int reimported = rt::reimportJsonSnapshots(tmp.path(), &dst);
        QVERIFY2(reimported >= 3, // project + 2 tasks (no section, no comment)
                 qPrintable(QStringLiteral("Reimported only %1 entities")
                                .arg(reimported)));

        // Look up the same task by id — JSON path preserves ids verbatim.
        Task *reloaded = dst.taskById(t->id());
        QVERIFY2(reloaded != nullptr,
                 qPrintable(QStringLiteral("Task %1 not found after reimport")
                                .arg(t->id())));

        QVERIFY(rt::tasksJsonPathEquivalent(t, reloaded, "single-task"));

        // Spot-check the parent task survived too.
        Task *reloadedParent = dst.taskById(parent->id());
        QVERIFY(reloadedParent != nullptr);
        QCOMPARE(reloadedParent->content(), QStringLiteral("Parent epic"));

        // And the project itself.
        Project *reloadedProj = dst.projectById(p->id());
        QVERIFY(reloadedProj != nullptr);
        QCOMPARE(reloadedProj->name(), QStringLiteral("Roadmap"));
    }

    // ── 3. multi-project + sections ──────────────────────────────────────────
    //
    // 3 projects x 2 sections x 2 tasks = 12 tasks total.  Verify the project
    // / section / task hierarchy is preserved across the round-trip.

    void multi_project_with_sections_roundtrips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore src;

        struct Row { Project *p; QList<Section *> sections; };
        QList<Row> srcRows;

        for (int i = 0; i < 3; ++i) {
            Project *p = src.addProject(
                QStringLiteral("Project %1").arg(i + 1));
            QList<Section *> secs;
            for (int s = 0; s < 2; ++s) {
                Section *sec = src.addSection(
                    QStringLiteral("Section %1.%2").arg(i + 1).arg(s + 1),
                    p->id());
                secs.append(sec);
                for (int t = 0; t < 2; ++t) {
                    Task *task = src.addTask(
                        QStringLiteral("Task %1.%2.%3")
                            .arg(i + 1).arg(s + 1).arg(t + 1),
                        p->id());
                    task->setSectionId(sec->id());
                }
            }
            srcRows.append({p, secs});
        }

        TaskExporter exp(&src);
        for (const Row &r : srcRows) {
            const QString path = exp.exportProjectToJson(r.p->id(), tmp.path());
            QVERIFY2(!path.isEmpty(),
                     qPrintable(QStringLiteral("Failed to export project %1")
                                    .arg(r.p->name())));
        }

        DataStore dst;
        const int reimported = rt::reimportJsonSnapshots(tmp.path(), &dst);
        // 3 projects + 6 sections + 12 tasks = 21
        qInfo("Multi-project round-trip reimported %d entities", reimported);
        QCOMPARE(reimported, 21);

        QCOMPARE(dst.allProjects().size(), 3);
        QCOMPARE(dst.taskCount(),          12);

        // Verify each section keeps its tasks attached to it via section_id.
        for (const Row &r : srcRows) {
            Project *reloadedP = dst.projectById(r.p->id());
            QVERIFY2(reloadedP != nullptr,
                     qPrintable(QStringLiteral("Project %1 (%2) lost during roundtrip")
                                    .arg(r.p->name(), r.p->id())));

            const auto reloadedSecs = dst.sectionsForProject(r.p->id());
            QCOMPARE(reloadedSecs.size(), 2);

            for (Section *origSec : r.sections) {
                const auto sectionTasks = dst.tasksForSection(origSec->id());
                if (sectionTasks.size() != 2) {
                    qInfo("Section %s expected 2 tasks, got %d",
                          qUtf8Printable(origSec->name()),
                          int(sectionTasks.size()));
                }
                QCOMPARE(sectionTasks.size(), 2);
                for (Task *task : sectionTasks) {
                    QCOMPARE(task->projectId(), r.p->id());
                    QCOMPARE(task->sectionId(), origSec->id());
                }
            }
        }
    }

    // ── 4. tombstone handling ────────────────────────────────────────────────
    //
    // Documents the actual contract: TaskExporter (JSON path) excludes
    // tombstoned tasks/comments; tombstoned projects can still be exported
    // but their tasksForProject() will return an empty list.  We log the
    // observed behaviour rather than mandate one-or-the-other so the test
    // is robust across future implementation tightening.
    //
    // We also sanity-check the Todoist CSV-via-ZIP path because the Todoist
    // CSV format has no concept of an is_deleted flag; tombstoned tasks
    // simply do not survive that path (they wouldn't even reach the CSV).

    void tombstoned_entities_handling()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore src;
        Project *liveProj = src.addProject(QStringLiteral("LiveProject"));
        Project *deadProj = src.addProject(QStringLiteral("DeadProject"));
        deadProj->setIsDeleted(true);

        Task *aliveTask = src.addTask(QStringLiteral("Alive in live proj"),
                                      liveProj->id());
        Task *deadTask  = src.addTask(QStringLiteral("Dead in live proj"),
                                      liveProj->id());
        deadTask->setIsDeleted(true);
        Q_UNUSED(aliveTask);

        TaskExporter exp(&src);

        // (a) Live project — alive task should appear, dead task should NOT.
        const QString livePath = exp.exportProjectToJson(liveProj->id(), tmp.path());
        QVERIFY(!livePath.isEmpty());

        QFile f(livePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        QCOMPARE(pe.error, QJsonParseError::NoError);
        const QJsonArray exportedTasks =
            doc.object().value(QStringLiteral("tasks")).toArray();

        bool sawAlive = false;
        bool sawDead  = false;
        for (const QJsonValue &v : exportedTasks) {
            const QString c = v.toObject().value(QStringLiteral("content")).toString();
            if (c == QStringLiteral("Alive in live proj")) sawAlive = true;
            if (c == QStringLiteral("Dead in live proj"))  sawDead  = true;
        }
        qInfo("Tombstone JSON-export contract: alive task exported = %d; "
              "dead (tombstoned) task exported = %d", int(sawAlive), int(sawDead));
        // Production contract per taskexporter.cpp: tombstones are filtered
        // out before serialisation. If that ever changes, the qInfo() above
        // becomes an audit trail.
        QVERIFY(sawAlive);
        QVERIFY(!sawDead);

        // (b) Dead project — exporter still writes a file because the
        // project itself exists in the store; tasks list comes back empty.
        const QString deadPath = exp.exportProjectToJson(deadProj->id(), tmp.path());
        qInfo("Tombstone JSON-export of dead project: returned path = '%s'",
              qUtf8Printable(deadPath));
        // The exporter resolves projectById() without filtering on isDeleted,
        // so a path is returned. Verify it's well-formed.
        QVERIFY(!deadPath.isEmpty());
        QFile df(deadPath);
        QVERIFY(df.open(QIODevice::ReadOnly));
        const QJsonDocument deadDoc = QJsonDocument::fromJson(df.readAll());
        QVERIFY(deadDoc.isObject());

        // (c) Round-trip via the Todoist CSV path: tombstoned tasks are
        // *not* representable in the wire format so they never reach a
        // re-imported store.
        QList<rt::CsvFile> csvFiles;
        rt::CsvFile cf;
        cf.projectName = QStringLiteral("LiveProject [aaaaaaaaaaaaaaaa]");
        cf.contents    = rt::buildTodoistCsv(src.tasksForProject(liveProj->id()));
        csvFiles.append(cf);

        const QString zipPath = tmp.path() + QStringLiteral("/tombstone.zip");
        QFile zf(zipPath);
        QVERIFY(zf.open(QIODevice::WriteOnly));
        zf.write(rt::buildTodoistZip(csvFiles));
        zf.close();

        QScopedPointer<DataStore> dst(rt::makeStoreWithInbox());
        QScopedPointer<TodoistImporter> imp(
            new TodoistImporter(dst.data()));
        QSignalSpy finished(imp.data(), &TodoistImporter::importFinished);
        imp->importFromBackupZip(zipPath, false);
        QCOMPARE(finished.size(), 1);

        bool reimportedAlive = false;
        bool reimportedDead  = false;
        for (Task *t : dst->allTasks()) {
            if (t->content() == QStringLiteral("Alive in live proj"))
                reimportedAlive = true;
            if (t->content() == QStringLiteral("Dead in live proj"))
                reimportedDead = true;
        }
        qInfo("Tombstone Todoist-CSV round-trip: alive=%d dead=%d "
              "(Todoist format has no is_deleted flag, so dead never makes it)",
              int(reimportedAlive), int(reimportedDead));
        QVERIFY(reimportedAlive);
        QVERIFY(!reimportedDead);
    }

    // ── 5. large dataset perf ────────────────────────────────────────────────
    //
    // 100 tasks across 10 projects, plus 20 labels.  Each direction
    // (export, re-import) must complete in under 2 s on the test rig.

    void large_dataset_roundtrips_in_reasonable_time()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore src;
        // Labels: stamped via DataStore (so per-label _meta is generated).
        for (int i = 0; i < 20; ++i) {
            src.addLabel(QStringLiteral("label-%1").arg(i + 1));
        }
        QList<Project *> projects;
        for (int i = 0; i < 10; ++i) {
            projects.append(src.addProject(QStringLiteral("Proj-%1").arg(i + 1)));
        }
        for (int i = 0; i < 100; ++i) {
            Project *p = projects[i % projects.size()];
            Task *t = src.addTask(
                QStringLiteral("Task-%1").arg(i + 1), p->id());
            // Sprinkle some interesting fields to inflate the JSON payload.
            t->setPriority(1 + (i % 4));
            if (i % 5 == 0) {
                t->setLabels({
                    QStringLiteral("label-%1").arg(1 + (i % 20)),
                    QStringLiteral("label-%1").arg(1 + ((i + 7) % 20)),
                });
            }
            if (i % 3 == 0) {
                t->setDueDate(QStringLiteral("2026-05-09"));
            }
        }

        TaskExporter exp(&src);

        QElapsedTimer exportTimer;
        exportTimer.start();
        for (Project *p : projects) {
            const QString path = exp.exportProjectToJson(p->id(), tmp.path());
            QVERIFY(!path.isEmpty());
        }
        const qint64 exportMs = exportTimer.elapsed();
        qInfo("Large-dataset export of 10 projects / 100 tasks: %lld ms", exportMs);
        QVERIFY2(exportMs < 2000,
                 qPrintable(QStringLiteral("Export took %1 ms (>2s)").arg(exportMs)));

        DataStore dst;
        QElapsedTimer importTimer;
        importTimer.start();
        const int reimported = rt::reimportJsonSnapshots(tmp.path(), &dst);
        const qint64 importMs = importTimer.elapsed();
        qInfo("Large-dataset re-import of %d entities: %lld ms",
              reimported, importMs);
        QVERIFY2(importMs < 2000,
                 qPrintable(QStringLiteral("Import took %1 ms (>2s)").arg(importMs)));

        // 10 projects + 100 tasks (no sections, no comments) = 110 entities.
        QCOMPARE(reimported,            110);
        QCOMPARE(dst.allProjects().size(), 10);
        QCOMPARE(dst.taskCount(),         100);
    }

    // ── 6. unicode round-trip ────────────────────────────────────────────────
    //
    // Mixed Russian + emoji + Hebrew + zalgo combining diacritics. The JSON
    // path must preserve the bytes exactly (Qt's JSON serializer is UTF-8).

    void unicode_content_roundtrips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Build the test string from explicit codepoints to avoid any
        // source-file-encoding ambiguity. Components:
        //  - Russian: "Купить молоко"  (U+041A U+0443 U+043F U+0438 U+0442 U+044C
        //                              SP U+043C U+043E U+043B U+043E U+043A U+043E)
        //  - emoji: 🥛 (U+1F95B)
        //  - Hebrew: "שלום" (U+05E9 U+05DC U+05D5 U+05DD)
        //  - zalgo: "a" + 5 combining diacritics (U+0301 acute, U+0327 cedilla,
        //           U+030C caron, U+033E vertical tilde, U+0359 asterisk below)
        QString unicode;
        unicode += QChar(0x041A); unicode += QChar(0x0443); unicode += QChar(0x043F);
        unicode += QChar(0x0438); unicode += QChar(0x0442); unicode += QChar(0x044C);
        unicode += QChar(0x0020);
        unicode += QChar(0x043C); unicode += QChar(0x043E); unicode += QChar(0x043B);
        unicode += QChar(0x043E); unicode += QChar(0x043A); unicode += QChar(0x043E);
        unicode += QChar(0x0020);
        // 🥛 — surrogate pair for U+1F95B
        unicode += QChar(0xD83E); unicode += QChar(0xDD5B);
        unicode += QChar(0x0020);
        // Hebrew shalom (RTL)
        unicode += QChar(0x05E9); unicode += QChar(0x05DC);
        unicode += QChar(0x05D5); unicode += QChar(0x05DD);
        unicode += QChar(0x0020);
        // zalgo
        unicode += QChar('a');
        unicode += QChar(0x0301); unicode += QChar(0x0327);
        unicode += QChar(0x030C); unicode += QChar(0x033E);
        unicode += QChar(0x0359);

        DataStore src;
        Project *p = src.addProject(QStringLiteral("Юникод"));
        Task    *t = src.addTask(unicode, p->id());
        t->setDescription(unicode + QStringLiteral(" — описание"));

        TaskExporter exp(&src);
        const QString path = exp.exportProjectToJson(p->id(), tmp.path());
        QVERIFY(!path.isEmpty());

        DataStore dst;
        const int reimported = rt::reimportJsonSnapshots(tmp.path(), &dst);
        QVERIFY(reimported >= 2);

        Task *reloaded = dst.taskById(t->id());
        QVERIFY2(reloaded != nullptr, "Task missing after unicode round-trip");

        if (reloaded->content() != unicode) {
            qInfo("Unicode content mismatch.");
            qInfo("  src bytes (utf8 hex): %s",
                  unicode.toUtf8().toHex().constData());
            qInfo("  dst bytes (utf8 hex): %s",
                  reloaded->content().toUtf8().toHex().constData());
        }
        // Byte-for-byte preservation: convert both to UTF-8 and compare.
        QCOMPARE(reloaded->content().toUtf8(), unicode.toUtf8());
        QCOMPARE(reloaded->description().toUtf8(),
                 t->description().toUtf8());

        // Project name (also unicode) survived.
        Project *reloadedProj = dst.projectById(p->id());
        QVERIFY(reloadedProj != nullptr);
        QCOMPARE(reloadedProj->name(), QStringLiteral("Юникод"));
    }

    // ── 7. idempotency ───────────────────────────────────────────────────────
    //
    // DataStore A → export to dirA → DataStore B (lossless re-import) →
    // export to dirB → DataStore C.  B and C must be byte-equivalent on
    // every Task field that the JSON path round-trips.
    //
    // The key invariant: the *second* round-trip must not introduce drift
    // (e.g. a field defaulting back to "" on missing-key, or a meta blob
    // being added/removed) that the first one didn't.

    void roundtrip_idempotency()
    {
        QTemporaryDir tmpA, tmpB;
        QVERIFY(tmpA.isValid());
        QVERIFY(tmpB.isValid());

        // ── Build store A with a small but varied dataset ──────────────────
        DataStore A;
        Project *projA = A.addProject(QStringLiteral("Idem"));
        Section *secA  = A.addSection(QStringLiteral("Stage 1"), projA->id());

        Task *t1 = A.addTask(QStringLiteral("Plain"),                  projA->id());
        Task *t2 = A.addTask(QStringLiteral("Has labels and prio"),   projA->id());
        Task *t3 = A.addTask(QStringLiteral("Has section + due"),     projA->id());
        Task *t4 = A.addTask(QStringLiteral("Subtask of t2"),         projA->id());

        t2->setPriority(2);
        t2->setLabels({QStringLiteral("urgent"), QStringLiteral("ops")});
        t3->setSectionId(secA->id());
        t3->setDueDate(QStringLiteral("2026-06-15"));
        t3->setDueString(QStringLiteral("June 15"));
        t4->setParentId(t2->id());

        // ── Round-trip 1: A → B ────────────────────────────────────────────
        TaskExporter expA(&A);
        const QString pathA = expA.exportProjectToJson(projA->id(), tmpA.path());
        QVERIFY(!pathA.isEmpty());

        DataStore B;
        const int reimportedB = rt::reimportJsonSnapshots(tmpA.path(), &B);
        // 1 project + 1 section + 4 tasks = 6
        QCOMPARE(reimportedB, 6);

        // ── Round-trip 2: B → C ────────────────────────────────────────────
        TaskExporter expB(&B);
        // Project id was preserved on the JSON path so the same projectId
        // resolves in B.
        const QString pathB = expB.exportProjectToJson(projA->id(), tmpB.path());
        QVERIFY2(!pathB.isEmpty(),
                 "Second-round export failed — project id was not preserved");

        DataStore C;
        const int reimportedC = rt::reimportJsonSnapshots(tmpB.path(), &C);
        QCOMPARE(reimportedC, 6);

        // ── B and C must be equivalent on every task field ─────────────────
        QCOMPARE(B.taskCount(),          C.taskCount());
        QCOMPARE(B.allProjects().size(), C.allProjects().size());
        QCOMPARE(B.sectionsForProject(projA->id()).size(),
                 C.sectionsForProject(projA->id()).size());

        const QList<QString> ids = {t1->id(), t2->id(), t3->id(), t4->id()};
        for (const QString &id : ids) {
            Task *bt = B.taskById(id);
            Task *ct = C.taskById(id);
            QVERIFY2(bt != nullptr,
                     qPrintable(QStringLiteral("Task %1 missing in B").arg(id)));
            QVERIFY2(ct != nullptr,
                     qPrintable(QStringLiteral("Task %1 missing in C").arg(id)));
            QVERIFY2(rt::tasksJsonPathEquivalent(bt, ct, "B-vs-C"),
                     qPrintable(QStringLiteral(
                         "Task %1 drifted between first and second round-trip")
                         .arg(id)));
        }

        // Also compare the canonical raw exporter output: pathA's payload,
        // when normalised against pathB's payload, should differ only in
        // the "exported_at" timestamp. We don't enforce equality on that
        // field — only that no other top-level key drifted.
        QFile fa(pathA);
        QFile fb(pathB);
        QVERIFY(fa.open(QIODevice::ReadOnly));
        QVERIFY(fb.open(QIODevice::ReadOnly));
        QJsonObject ra = QJsonDocument::fromJson(fa.readAll()).object();
        QJsonObject rb = QJsonDocument::fromJson(fb.readAll()).object();
        ra.remove(QStringLiteral("exported_at"));
        rb.remove(QStringLiteral("exported_at"));
        if (ra != rb) {
            qInfo("Top-level idempotency drift between A and B:");
            qInfo("  A keys: %s",
                  qUtf8Printable(QStringList(ra.keys()).join(QChar(','))));
            qInfo("  B keys: %s",
                  qUtf8Printable(QStringList(rb.keys()).join(QChar(','))));
        }
        QCOMPARE(ra, rb);
    }
};

QTEST_GUILESS_MAIN(TestExportImportRoundtrip)
#include "test_export_import_roundtrip.moc"
