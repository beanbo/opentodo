// =============================================================================
// Todoist import fixes (global fix pass F2):
//
//   D1  CSV section ids are unique across the CSVs of one backup ZIP — a bare
//       row number made a Home.csv task land in Work.csv's section.
//   D7  the hand-written ZIP reader validates every offset / size from the
//       archive (negative-int casts, huge uncompressed sizes, truncated or
//       short DEFLATE streams) instead of reading out of bounds or importing
//       half a file.
//   D9  fixed-timezone dues ("…Z" / "…+03:00") use the LOCAL calendar date.
//   D14 a due string the date parser cannot resolve survives the save.
// =============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdlib>
#include <ctime>

#include "import/todoist_csv_parser.h"
#include "import/todoist_types.h"
#include "import/todoistimporter.h"
#include "storage/datastore.h"
#include "model/project.h"
#include "model/section.h"
#include "model/task.h"

namespace {

const QString kHeader = QStringLiteral(
    "TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,"
    "DATE,DATE_LANG,TIMEZONE,DURATION,DURATION_UNIT\n");

QByteArray le16(quint16 v) { QByteArray o(2, 0); o[0] = char(v); o[1] = char(v >> 8); return o; }
QByteArray le32(quint32 v)
{
    QByteArray o(4, 0);
    for (int i = 0; i < 4; ++i) o[i] = char(v >> (8 * i));
    return o;
}

// Raw DEFLATE stream (what ZIP stores) from qCompress, which emits a 4-byte
// big-endian length, a 2-byte zlib header, the DEFLATE data and a 4-byte
// Adler-32 trailer.
QByteArray rawDeflate(const QByteArray &data)
{
    const QByteArray z = qCompress(data, 9);
    return z.mid(6, z.size() - 10);
}

struct Entry {
    QString    name;
    QByteArray payload;          // bytes stored in the archive
    quint16    method = 0;       // 0 stored, 8 deflate
    quint32    uncompSize = 0;   // as written into the headers
};

Entry stored(const QString &name, const QByteArray &data)
{
    return {name, data, 0, quint32(data.size())};
}
Entry deflated(const QString &name, const QByteArray &data)
{
    return {name, rawDeflate(data), 8, quint32(data.size())};
}

// Minimal ZIP writer. CRCs are left 0 — the reader does not check them.
// `cdOffsetOverride` lets a test point the central directory anywhere.
QByteArray buildZip(const QList<Entry> &entries, qint64 cdOffsetOverride = -1)
{
    QByteArray archive, cd;
    for (const Entry &e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 localOff = quint32(archive.size());
        archive += QByteArray::fromHex("504b0304") + le16(20) + le16(0x0800) + le16(e.method)
                 + le16(0) + le16(0x21) + le32(0) + le32(quint32(e.payload.size()))
                 + le32(e.uncompSize) + le16(quint16(name.size())) + le16(0) + name + e.payload;
        cd += QByteArray::fromHex("504b0102") + le16(20) + le16(20) + le16(0x0800) + le16(e.method)
            + le16(0) + le16(0x21) + le32(0) + le32(quint32(e.payload.size()))
            + le32(e.uncompSize) + le16(quint16(name.size())) + le16(0) + le16(0) + le16(0)
            + le16(0) + le32(0) + le32(localOff) + name;
    }
    const quint32 cdStart = cdOffsetOverride >= 0 ? quint32(cdOffsetOverride)
                                                  : quint32(archive.size());
    archive += cd;
    archive += QByteArray::fromHex("504b0506") + le16(0) + le16(0)
             + le16(quint16(entries.size())) + le16(quint16(entries.size()))
             + le32(quint32(cd.size())) + le32(cdStart) + le16(0);
    return archive;
}

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes);
    return path;
}

DataStore *makeStore()
{
    auto *ds = new DataStore;
    auto *inbox = new Project(ds);
    inbox->setName(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    ds->addExistingProject(inbox);
    return ds;
}

struct ImportResult { int tasks = -1; int projects = -1; QString error; QStringList warnings; };

ImportResult runImport(DataStore *ds, const QString &zipPath)
{
    TodoistImporter imp(ds);
    QSignalSpy finished(&imp, &TodoistImporter::importFinished);
    QSignalSpy warnings(&imp, &TodoistImporter::importWarnings);
    imp.importFromBackupZip(zipPath, false);
    ImportResult r;
    if (finished.count() == 1) {
        r.tasks = finished[0][0].toInt();
        r.projects = finished[0][1].toInt();
        r.error = finished[0][2].toString();
    }
    if (!warnings.isEmpty())
        r.warnings = warnings[0][0].toStringList();
    return r;
}

Task *taskByContent(DataStore *ds, const QString &content)
{
    for (Task *t : ds->allTasks())
        if (t->content() == content) return t;
    return nullptr;
}

} // namespace

class TestTodoistImportHardening : public QObject
{
    Q_OBJECT

    // The D9 assertions need local time = America/New_York. The TZ override
    // works with glibc; skip where the platform ignores it (e.g. Windows).
    static void requireNewYorkLocalTime()
    {
        const QDate local = QDateTime::fromString(QStringLiteral("2024-01-16T01:00:00Z"),
                                                  Qt::ISODate).toLocalTime().date();
        if (local != QDate(2024, 1, 15))
            QSKIP("TZ=America/New_York not honoured on this platform");
    }

private slots:
    void initTestCase()
    {
        // D9 needs a zone whose local date differs from UTC around midnight.
        qputenv("TZ", "America/New_York");
#ifndef Q_OS_WIN
        tzset();
#endif
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_TodoistHardening"));
        QCoreApplication::setApplicationName(QStringLiteral("test_todoist_import_hardening"));
    }

    // ── D1 ──────────────────────────────────────────────────────────────────

    void parser_sectionIdsIncludeProject()
    {
        const QString csv = kHeader + QStringLiteral("section,S,,,,,,,,,,\n");
        const auto a = TodoistCsvParser::parse(csv, QStringLiteral("projA"));
        const auto b = TodoistCsvParser::parse(csv, QStringLiteral("projB"));
        QCOMPARE(a.sections.size(), 1);
        QCOMPARE(b.sections.size(), 1);
        QVERIFY(a.sections[0].id != b.sections[0].id);
    }

    void zip_sameSectionRowInTwoCsvs_keepsTasksInTheirOwnProject()
    {
        const QByteArray home = (kHeader
            + QStringLiteral("section,Chores,,,,,,,,,,\n"
                             "task,Vacuum,,4,1,,,,,,,\n")).toUtf8();
        const QByteArray work = (kHeader
            + QStringLiteral("section,Meetings,,,,,,,,,,\n"
                             "task,Standup,,4,1,,,,,,,\n")).toUtf8();
        QTemporaryDir dir;
        const QString zip = writeFile(dir, QStringLiteral("backup.zip"),
            buildZip({stored(QStringLiteral("Home [aaaaaaaa].csv"), home),
                      stored(QStringLiteral("Work [bbbbbbbb].csv"), work)}));

        QScopedPointer<DataStore> ds(makeStore());
        const ImportResult r = runImport(ds.data(), zip);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 2);

        for (const auto &[content, sectionName] :
             {std::pair{QStringLiteral("Vacuum"), QStringLiteral("Chores")},
              std::pair{QStringLiteral("Standup"), QStringLiteral("Meetings")}}) {
            Task *t = taskByContent(ds.data(), content);
            QVERIFY(t);
            Section *s = ds->sectionById(t->sectionId());
            QVERIFY2(s, qPrintable(content));
            QCOMPARE(s->name(), sectionName);
            QCOMPARE(s->projectId(), t->projectId());
        }
    }

    // ── D7 ──────────────────────────────────────────────────────────────────

    void zip_validDeflateEntry_stillImports()
    {
        const QByteArray csv = (kHeader + QStringLiteral("task,Deflated,,4,1,,,,,,,\n")).toUtf8();
        QTemporaryDir dir;
        const QString zip = writeFile(dir, QStringLiteral("ok.zip"),
            buildZip({deflated(QStringLiteral("P [cccccccc].csv"), csv)}));
        QScopedPointer<DataStore> ds(makeStore());
        const ImportResult r = runImport(ds.data(), zip);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 1);
        QVERIFY(taskByContent(ds.data(), QStringLiteral("Deflated")));
    }

    void zip_hugeCentralDirectoryOffset_isRejected()
    {
        const QByteArray csv = (kHeader + QStringLiteral("task,T,,4,1,,,,,,,\n")).toUtf8();
        QTemporaryDir dir;
        for (qint64 off : {qint64(0x80000000LL), qint64(0xFFFFFFF0LL)}) {
            const QString zip = writeFile(dir, QStringLiteral("cd-%1.zip").arg(off),
                buildZip({stored(QStringLiteral("P [cccccccc].csv"), csv)}, off));
            QScopedPointer<DataStore> ds(makeStore());
            const ImportResult r = runImport(ds.data(), zip);
            QVERIFY(!r.error.isEmpty());
            QCOMPARE(r.tasks, 0);
        }
    }

    void zip_hugeUncompressedSize_isRejected()
    {
        const QByteArray csv = (kHeader + QStringLiteral("task,T,,4,1,,,,,,,\n")).toUtf8();
        Entry bomb = deflated(QStringLiteral("P [cccccccc].csv"), csv);
        bomb.uncompSize = 0xFFFFFF00u;   // int(…) used to go negative
        QTemporaryDir dir;
        const QString zip = writeFile(dir, QStringLiteral("bomb.zip"), buildZip({bomb}));
        QScopedPointer<DataStore> ds(makeStore());
        const ImportResult r = runImport(ds.data(), zip);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(r.tasks, 0);
    }

    void zip_truncatedOrShortDeflateStream_isRejected()
    {
        QByteArray body = kHeader.toUtf8();
        for (int i = 0; i < 200; ++i)
            body += QStringLiteral("task,Row %1 with some padding text,,4,1,,,,,,,\n").arg(i).toUtf8();

        Entry truncated = deflated(QStringLiteral("T [cccccccc].csv"), body);
        truncated.payload.chop(truncated.payload.size() / 2);

        Entry overstated = deflated(QStringLiteral("O [dddddddd].csv"), body);
        overstated.uncompSize += 100;   // stream ends early vs. promised size

        Entry storedMismatch = stored(QStringLiteral("S [eeeeeeee].csv"), body);
        storedMismatch.uncompSize += 1;

        QTemporaryDir dir;
        for (const Entry &e : {truncated, overstated, storedMismatch}) {
            const QString zip = writeFile(dir, e.name + QStringLiteral(".zip"), buildZip({e}));
            QScopedPointer<DataStore> ds(makeStore());
            const ImportResult r = runImport(ds.data(), zip);
            QVERIFY2(!r.error.isEmpty(), qPrintable(e.name));
            QCOMPARE(r.tasks, 0);
        }
    }

    void zip_corruptEntryNextToGoodOne_importsOnlyTheGoodOne()
    {
        const QByteArray good = (kHeader + QStringLiteral("task,Good,,4,1,,,,,,,\n")).toUtf8();
        Entry bad = deflated(QStringLiteral("Bad [dddddddd].csv"),
                             (kHeader + QStringLiteral("task,Bad,,4,1,,,,,,,\n")).toUtf8());
        bad.payload.chop(4);
        QTemporaryDir dir;
        const QString zip = writeFile(dir, QStringLiteral("mixed.zip"),
            buildZip({bad, stored(QStringLiteral("Good [cccccccc].csv"), good)}));
        QScopedPointer<DataStore> ds(makeStore());
        const ImportResult r = runImport(ds.data(), zip);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(taskByContent(ds.data(), QStringLiteral("Good")));
        QVERIFY(!taskByContent(ds.data(), QStringLiteral("Bad")));
    }

    // ── D9 ──────────────────────────────────────────────────────────────────

    void localDueDate_convertsInstantsOnly()
    {
        requireNewYorkLocalTime();
        // 2024-01-16 01:00 UTC is 2024-01-15 20:00 in New York.
        QCOMPARE(todoistLocalDueDate(QStringLiteral("2024-01-16T01:00:00Z")),
                 QStringLiteral("2024-01-15"));
        QCOMPARE(todoistLocalDueDate(QStringLiteral("2024-01-16T01:00:00.000Z")),
                 QStringLiteral("2024-01-15"));
        // 02:00+03:00 = 2024-01-15 23:00 UTC = 18:00 New York.
        QCOMPARE(todoistLocalDueDate(QStringLiteral("2024-01-16T02:00:00+03:00")),
                 QStringLiteral("2024-01-15"));
        // Floating time and date-only keep their literal date.
        QCOMPARE(todoistLocalDueDate(QStringLiteral("2024-01-16T01:00:00")),
                 QStringLiteral("2024-01-16"));
        QCOMPARE(todoistLocalDueDate(QStringLiteral("2024-01-16")),
                 QStringLiteral("2024-01-16"));
    }

    void csvParseDue_fixedTimezone_usesLocalDate()
    {
        requireNewYorkLocalTime();
        const auto r = TodoistCsvParser::parse(
            kHeader + QStringLiteral("task,T,,4,1,,,2024-01-16T01:00:00Z,en,,,\n"),
            QStringLiteral("p"));
        QCOMPARE(r.tasks.size(), 1);
        QCOMPARE(r.tasks[0].due.date, QStringLiteral("2024-01-15"));
        QVERIFY(!r.tasks[0].due.datetime.isEmpty());
    }

    // ── D14 ─────────────────────────────────────────────────────────────────

    void dueStringOnly_survivesSerialisation()
    {
        Task t;
        t.setContent(QStringLiteral("x"));
        t.setDueString(QStringLiteral("when the moon is full"));
        QVERIFY(!t.due().isNull());
        const QJsonObject json = t.toJson();
        QVERIFY(json.contains(QStringLiteral("due")));
        QScopedPointer<Task> back(Task::fromJson(json));
        QCOMPARE(back->dueString(), QStringLiteral("when the moon is full"));
        QVERIFY(back->dueDate().isEmpty());

        Task none;
        QVERIFY(none.due().isNull());
        QVERIFY(!none.toJson().contains(QStringLiteral("due")));
    }

    void import_unparseableDue_keepsStringAndWarns()
    {
        const QByteArray csv = (kHeader
            + QStringLiteral("task,Someday task,,4,1,,,qwerty zxcv,en,,,\n")).toUtf8();
        QTemporaryDir dir;
        const QString zip = writeFile(dir, QStringLiteral("due.zip"),
            buildZip({stored(QStringLiteral("P [cccccccc].csv"), csv)}));
        QScopedPointer<DataStore> ds(makeStore());
        const ImportResult r = runImport(ds.data(), zip);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        Task *t = taskByContent(ds.data(), QStringLiteral("Someday task"));
        QVERIFY(t);
        QVERIFY(t->dueDate().isEmpty());
        QCOMPARE(t->dueString(), QStringLiteral("qwerty zxcv"));

        // Survives a save/load cycle.
        DataStore reloaded;
        reloaded.loadFromJson(ds->toJson());
        Task *rt = reloaded.taskById(t->id());
        QVERIFY(rt);
        QCOMPARE(rt->dueString(), QStringLiteral("qwerty zxcv"));

        bool warned = false;
        for (const QString &w : r.warnings)
            if (w.contains(QStringLiteral("срок"))) warned = true;
        QVERIFY2(warned, qPrintable(r.warnings.join(QStringLiteral(" | "))));
    }
};

QTEST_GUILESS_MAIN(TestTodoistImportHardening)
#include "test_todoist_import_hardening.moc"
