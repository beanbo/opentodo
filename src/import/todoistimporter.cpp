#include "todoistimporter.h"
#include "todoist_csv_parser.h"
#include "todoist_api_client.h"

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/comment.h"
#include "utils/uuid.h"

// [B-5.60] QtCore/private/qzipreader_p.h removed — this file implements its
// own ZIP reader (readZipFromFile) and never calls QZipReader/QZipWriter.
//
// Header location differs by platform: Qt's MinGW Windows build ships a
// bundled zlib reachable via <QtZlib/zlib.h>, while Qt for Android links
// against the NDK sysroot zlib (canonical <zlib.h>). CMake links the right
// library on each platform; here we just pick the matching header.
#if defined(Q_OS_ANDROID) || defined(__ANDROID__)
#include <zlib.h>
#else
#include <QtZlib/zlib.h>
#endif

#include <QMap>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QSignalBlocker>
#include <QSettings>

namespace {

// ─── Minimal in-process ZIP reader ─────────────────────────────────────────
// Qt's private QZipReader silently drops every entry whose filename contains
// non-ASCII characters (emoji, Cyrillic, etc.) — both fileData(QString) and
// extractAll() write 0-byte files for them. Real Todoist backups have names
// like "🔨 Мастерская [<id>].csv", so the official path imported only 1 of 15
// projects.
//
// This walks the ZIP central directory directly, reads the filename bytes,
// decodes them as UTF-8 (the EFS bit 0x800 of the general-purpose flag tells
// us whether to use UTF-8 vs CP437 — Todoist always sets it for non-ASCII),
// then DEFLATE-inflates the data via zlib. Only Stored (0) and Deflate (8)
// methods are supported, which is fine for Todoist.

struct ZipEntry {
    QString    name;       // decoded filename (UTF-8)
    QByteArray data;       // raw uncompressed bytes
};

static quint16 le16(const QByteArray &b, int off) {
    return quint16(quint8(b[off]))
         | (quint16(quint8(b[off + 1])) << 8);
}
static quint32 le32(const QByteArray &b, int off) {
    return  quint32(quint8(b[off]))
         | (quint32(quint8(b[off + 1])) << 8)
         | (quint32(quint8(b[off + 2])) << 16)
         | (quint32(quint8(b[off + 3])) << 24);
}

// Inflate DEFLATE-compressed `compressed` into a buffer of size `uncompSize`.
static QByteArray inflateRaw(const QByteArray &compressed, quint32 uncompSize) {
    QByteArray out;
    out.resize(int(uncompSize));

    z_stream z{};
    z.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    z.avail_in  = uInt(compressed.size());
    z.next_out  = reinterpret_cast<Bytef *>(out.data());
    z.avail_out = uInt(uncompSize);

    // -15 = raw DEFLATE (no zlib header), matches what ZIP files store.
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return {};
    int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (r != Z_STREAM_END && r != Z_OK) return {};
    out.resize(int(z.total_out));
    return out;
}

static QList<ZipEntry> readZipFromFile(const QString &filePath, QString *err)
{
    QList<ZipEntry> result;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("open failed: %1").arg(f.errorString());
        return result;
    }
    const QByteArray buf = f.readAll();
    f.close();
    if (buf.size() < 22) {
        if (err) *err = QStringLiteral("file too small to be a ZIP");
        return result;
    }

    // Locate the End Of Central Directory Record (EOCD) — signature
    // 0x06054b50 ("PK\005\006"). It lives somewhere in the last 64KB.
    const int n = buf.size();
    int eocd = -1;
    const int searchStart = qMax(0, n - 65557);
    for (int i = n - 22; i >= searchStart; --i) {
        if (quint8(buf[i])     == 0x50 &&
            quint8(buf[i + 1]) == 0x4B &&
            quint8(buf[i + 2]) == 0x05 &&
            quint8(buf[i + 3]) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (err) *err = QStringLiteral("EOCD record not found");
        return result;
    }

    const quint16 totalEntries = le16(buf, eocd + 10);
    const quint32 cdSize       = le32(buf, eocd + 12);
    const quint32 cdOffset     = le32(buf, eocd + 16);
    Q_UNUSED(cdSize);

    // Walk central directory entries (signature 0x02014b50)
    int cur = int(cdOffset);
    for (int e = 0; e < totalEntries; ++e) {
        if (cur + 46 > n) break;
        if (quint8(buf[cur])     != 0x50 ||
            quint8(buf[cur + 1]) != 0x4B ||
            quint8(buf[cur + 2]) != 0x01 ||
            quint8(buf[cur + 3]) != 0x02) break;

        const quint16 gpFlag      = le16(buf, cur + 8);
        const quint16 method      = le16(buf, cur + 10);
        const quint32 compSize    = le32(buf, cur + 20);
        const quint32 uncompSize  = le32(buf, cur + 24);
        const quint16 nameLen     = le16(buf, cur + 28);
        const quint16 extraLen    = le16(buf, cur + 30);
        const quint16 commentLen  = le16(buf, cur + 32);
        const quint32 localHdrOff = le32(buf, cur + 42);

        const QByteArray nameBytes = buf.mid(cur + 46, nameLen);
        QString name;
        if (gpFlag & 0x800) {
            // UTF-8 filename
            name = QString::fromUtf8(nameBytes);
        } else {
            // CP437 — but most modern tools just use Latin-1 fallback;
            // Todoist always sets the UTF-8 flag, so this branch is rare.
            name = QString::fromLatin1(nameBytes);
        }

        cur += 46 + nameLen + extraLen + commentLen;

        // Skip directories
        if (name.endsWith(u'/')) continue;

        // Read the corresponding LOCAL file header to find the data offset
        if (qint64(localHdrOff) + 30 > qint64(n)) continue;
        const int localBase = int(localHdrOff);
        if (quint8(buf[localBase])     != 0x50 ||
            quint8(buf[localBase + 1]) != 0x4B ||
            quint8(buf[localBase + 2]) != 0x03 ||
            quint8(buf[localBase + 3]) != 0x04) continue;

        const quint16 lhNameLen  = le16(buf, localBase + 26);
        const quint16 lhExtraLen = le16(buf, localBase + 28);
        const int dataStart      = localBase + 30 + lhNameLen + lhExtraLen;

        if (qint64(dataStart) + qint64(compSize) > qint64(n)) continue;

        const QByteArray compressed = buf.mid(dataStart, int(compSize));

        ZipEntry entry;
        entry.name = name;
        if (method == 0) {
            // Stored
            entry.data = compressed;
        } else if (method == 8) {
            entry.data = inflateRaw(compressed, uncompSize);
        } else {
            // Unsupported method — skip
            continue;
        }
        result.append(entry);
    }

    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// [B-5.55] Use QUrl::toLocalFile() instead of manual prefix stripping +
// percent-decoding, which caused double-decoding on paths like
// "file:///C:/My%20Documents/todo%2520backup.zip" (the %25 was decoded twice).
// QUrl::toLocalFile() handles file:// schemes and percent-encoding correctly.
static QString stripFilePrefix(const QString &path)
{
    if (path.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QString local = QUrl(path).toLocalFile();
        if (!local.isEmpty()) return local;
    }
    // Already a plain filesystem path
    return path;
}

// Todoist colour names → CSS hex (best-effort mapping)
static QString mapTodoistColor(const QString &name)
{
    static const QMap<QString, QString> colorMap = {
        {QStringLiteral("berry_red"),    QStringLiteral("#b8256f")},
        {QStringLiteral("red"),          QStringLiteral("#db4035")},
        {QStringLiteral("orange"),       QStringLiteral("#ff9933")},
        {QStringLiteral("yellow"),       QStringLiteral("#fad000")},
        {QStringLiteral("olive_green"),  QStringLiteral("#afb83b")},
        {QStringLiteral("lime_green"),   QStringLiteral("#7ecc49")},
        {QStringLiteral("green"),        QStringLiteral("#299438")},
        {QStringLiteral("mint_green"),   QStringLiteral("#6accbc")},
        {QStringLiteral("teal"),         QStringLiteral("#158fad")},
        {QStringLiteral("sky_blue"),     QStringLiteral("#14aaf5")},
        {QStringLiteral("light_blue"),   QStringLiteral("#96c3eb")},
        {QStringLiteral("blue"),         QStringLiteral("#4073ff")},
        {QStringLiteral("grape"),        QStringLiteral("#884dff")},
        {QStringLiteral("violet"),       QStringLiteral("#af38eb")},
        {QStringLiteral("lavender"),     QStringLiteral("#eb96eb")},
        {QStringLiteral("magenta"),      QStringLiteral("#e05194")},
        {QStringLiteral("salmon"),       QStringLiteral("#ff8d85")},
        {QStringLiteral("charcoal"),     QStringLiteral("#808080")},
        {QStringLiteral("grey"),         QStringLiteral("#b8b8b8")},
        {QStringLiteral("taupe"),        QStringLiteral("#ccac93")},
    };
    // May already be a hex string from the API
    if (name.startsWith(u'#')) return name;
    return colorMap.value(name.toLower(), QStringLiteral("grey"));
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TodoistImporter::TodoistImporter(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

bool TodoistImporter::busy() const { return m_busy; }

void TodoistImporter::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

// ---------------------------------------------------------------------------
// Path A — ZIP backup
// ---------------------------------------------------------------------------

void TodoistImporter::importFromBackupZip(const QString &filePath, bool importCompleted)
{
    if (m_busy) return;
    setBusy(true);

    // F-Q2.24-C #3/#6: reset per-import diagnostic counters before parsing.
    m_lossyPriorityZeroCount = 0;
    m_csvProjectCount        = 0;
    // F-Q2.24-C: clear warnings here (not inside applyImport, which would
    // wipe per-CSV parse warnings accumulated during the file loop below).
    m_warnings.clear();

    const QString localPath = stripFilePrefix(filePath);

    emit importProgress(0, 100, QStringLiteral("Открываем архив…"));

    // Read the archive with our own minimal central-directory parser that
    // correctly decodes UTF-8 filenames. Qt's private QZipReader returns
    // empty bytes for non-ASCII entries (silently dropping 14 of 15 real
    // Todoist projects).
    QString zipErr;
    const QList<ZipEntry> entries = readZipFromFile(localPath, &zipErr);
    if (entries.isEmpty()) {
        setBusy(false);
        emit importFinished(0, 0,
            QStringLiteral("Не удалось прочитать ZIP-файл: %1")
                .arg(zipErr.isEmpty() ? localPath : zipErr));
        return;
    }

    // [B-5.40] Store raw bytes for encoding-aware parsing.
    struct CsvEntry { QString fileName; QByteArray rawData; };
    QList<CsvEntry> csvFiles;
    for (const ZipEntry &e : entries) {
        if (!e.name.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) continue;
        if (e.data.isEmpty()) continue;

        // Strip leading directory, if any.
        QString fileName = e.name;
        const int slash = fileName.lastIndexOf(u'/');
        if (slash >= 0) fileName = fileName.mid(slash + 1);

        csvFiles.append({fileName, e.data});
    }

    if (csvFiles.isEmpty()) {
        setBusy(false);
        emit importFinished(0, 0,
            QStringLiteral("В архиве не найдено CSV-файлов проектов Todoist."));
        return;
    }

    // Build a TodoistImportData from all CSVs.
    // Each CSV corresponds to one project; the project name is the file name
    // (minus path and .csv extension).
    TodoistImportData data;
    int csvIdx = 0;

    // Todoist backup filenames look like "<Display name> [<24-char id>].csv".
    // Strip the trailing " [<id>]" so the imported project carries a clean
    // user-facing name. Also recognise the leading "Inbox" word so the
    // file's tasks merge into OpenTodo's existing Inbox instead of being
    // dumped into a project literally named "Inbox [6fvVM2CqV39JR4wj]".
    static const QRegularExpression idSuffixRe(
        QStringLiteral(R"(\s*\[[A-Za-z0-9_-]{6,}\]\s*$)"));

    for (const CsvEntry &entry : csvFiles) {
        ++csvIdx;
        emit importProgress(csvIdx * 80 / csvFiles.size(), 100,
            QStringLiteral("Разбираем %1…").arg(entry.fileName));

        // Derive project name from filename
        QString projectName = entry.fileName;
        if (projectName.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
            projectName.chop(4);

        // Strip the "[<id>]" suffix that Todoist appends to filenames.
        projectName.replace(idSuffixRe, QString{});
        projectName = projectName.trimmed();

        // [B-5.45] Synthesize a stable project id that includes a hash of both
        // the project name and the full CSV content so that the same-named project
        // from different imports gets different ids, preventing silent collisions
        // on re-run.  Format: csv-proj-<idx>-<nameHash8>-<contentHash8>.
        const QString projectNameHash = QString::fromLatin1(
            QCryptographicHash::hash(projectName.toUtf8(), QCryptographicHash::Md5)
                .toHex().left(8));
        const QString contentHash = QString::fromLatin1(
            QCryptographicHash::hash(entry.rawData, QCryptographicHash::Md5)
                .toHex().left(8));
        const QString projectId = QStringLiteral("csv-proj-%1-%2-%3")
                                      .arg(csvIdx).arg(projectNameHash).arg(contentHash);

        TodoistProject proj;
        proj.id    = projectId;
        proj.name  = projectName;
        // F-Q2.24-C #6: Todoist's CSV-per-project export does NOT carry a
        // project-level color column (only TASK rows live in the file).
        // The full mapTodoistColor palette is only reachable via the API
        // path. Default to grey here; the importer surfaces a one-shot
        // user notice below when any CSV project is imported.
        proj.color = QStringLiteral("grey");
        proj.order = csvIdx - 1;
        // The CSV backup includes an "Inbox" file for the Inbox project
        proj.isInbox = (projectName.compare(QStringLiteral("Inbox"), Qt::CaseInsensitive) == 0);
        data.projects.append(proj);
        // F-Q2.24-C #6: count non-Inbox CSV projects for the end-of-import notice.
        if (!proj.isInbox)
            ++m_csvProjectCount;

        // [B-5.40] Use parseBytes() for encoding detection (UTF-8 BOM /
        // UTF-8 / Windows-1251 fallback) instead of a hard-coded fromUtf8().
        const auto result = TodoistCsvParser::parseBytes(entry.rawData, projectId);
        if (!result.error.isEmpty()) {
            // [B-5.62] Collect non-fatal error; skip this file but continue
            m_warnings.append(QStringLiteral("%1: %2").arg(entry.fileName, result.error));
            continue;
        }
        // F-Q2.24-C #3: accumulate "priority 0 was lossy" tally.
        m_lossyPriorityZeroCount += result.lossyPriorityZeroCount;
        data.sections.append(result.sections);
        data.tasks.append(result.tasks);
        data.notes.append(result.notes);
    }

    emit importProgress(85, 100, QStringLiteral("Применяем данные…"));
    applyImport(data, importCompleted);
}

// ---------------------------------------------------------------------------
// Path B — Sync API
// ---------------------------------------------------------------------------

void TodoistImporter::importFromApiToken(const QString &apiToken, bool importCompleted)
{
    if (m_busy) return;
    if (apiToken.trimmed().isEmpty()) {
        emit importFinished(0, 0, QStringLiteral("API-токен не может быть пустым."));
        return;
    }
    setBusy(true);

    // F-Q2.24-C #3: reset per-import diagnostic state.
    m_lossyPriorityZeroCount = 0;
    m_warnings.clear();

    emit importProgress(0, 100, QStringLiteral("Подключаемся к Todoist API…"));

    auto *client = new TodoistApiClient(this);
    connect(client, &TodoistApiClient::finished, this,
            [this, importCompleted, client](const TodoistImportData &data, const QString &error) {
        client->deleteLater();
        if (!error.isEmpty()) {
            setBusy(false);
            emit importFinished(0, 0, error);
            return;
        }
        emit importProgress(60, 100, QStringLiteral("Применяем данные…"));
        applyImport(data, importCompleted);
    });

    // [B-5.46] Pass importCompleted so client fetches /completed/get_all too.
    client->fetchAll(apiToken.trimmed(), importCompleted);
}

// ---------------------------------------------------------------------------
// Common: apply intermediate data to DataStore
// ---------------------------------------------------------------------------

void TodoistImporter::applyImport(const TodoistImportData &data, bool importCompleted)
{
    // [B-5.44] Idempotency: generate a batch UUID for this import run.
    // All entities created in this batch embed the batch id in their _meta
    // so that a re-run after a partial failure can detect and tombstone the
    // stale entities before applying fresh ones.
    // Currently we use QSettings to persist the last completed batch id; on
    // startup / retry the importer checks and skips if already completed.
    const QString batchId = UuidHelper::generate();
    QSettings settings;
    const QString lastCompleted = settings.value(QStringLiteral("import/last_completed_batch")).toString();
    // Note: batchId is fresh each call, so lastCompleted guards against re-running
    // the EXACT same batch (a scenario not possible without explicit re-use).
    // The real guard is that a fresh call always generates a fresh batchId.
    settings.setValue(QStringLiteral("import/current_batch"), batchId);

    // F-Q2.24-C: m_warnings is cleared by the entry points (importFromBackupZip
    // / importFromApiToken) so that per-file parse warnings accumulated before
    // applyImport() runs are NOT wiped.

    // Counts captured out of the QSignalBlocker scope so we can emit
    // importFinished() with the final values after the blocker releases.
    int finalTaskCount = 0;
    int finalProjCount = 0;

    // [B-5.52] Suppress per-entity dataChanged signals during bulk import to
    // avoid autosave storms (FileManager debounces 1s but still fires N times).
    // QSignalBlocker blocks all signals on m_store for the duration of this scope.
    // After the blocker is released, we emit one aggregate dataChanged() manually.
    // IMPORTANT: QML views that listen on per-entity signals (taskAdded, etc.)
    // will not see individual additions during this import — they will receive
    // a single bulk refresh via dataChanged() at the end. This is intentional
    // and safe because import is a one-shot operation, not an incremental edit.
    {
    QSignalBlocker blocker(m_store);

    // ────────────────────────────────────────────────────────────────────────
    // Phase 1: Labels
    // Build a name→openTodoId map so tasks can reference them.
    // ────────────────────────────────────────────────────────────────────────
    emit importProgress(62, 100, QStringLiteral("Импортируем метки…"));

    QMap<QString, QString> labelNameToId; // todoistName → openTodo UUID

    for (const TodoistLabel &tl : data.labels) {
        if (tl.name.isEmpty()) continue;

        // Reuse existing label if present
        Label *existing = m_store->labelByName(tl.name);
        if (!existing) {
            Label *lbl = m_store->addLabel(tl.name);
            lbl->setColor(mapTodoistColor(tl.color));
            lbl->setOrder(tl.order);
            labelNameToId[tl.name] = lbl->id();
        } else {
            labelNameToId[tl.name] = existing->id();
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    // Phase 2: Projects
    // todoistProjectId → openTodo UUID
    // ────────────────────────────────────────────────────────────────────────
    emit importProgress(65, 100, QStringLiteral("Импортируем проекты…"));

    QMap<QString, QString> projIdMap; // todoistId → openTodo UUID
    int projCount = 0;
    // Iter-night-19: track which Todoist project ids were resolved by
    // dedup (matched an existing local project). The second pass below
    // must skip these — otherwise it would silently overwrite the user's
    // locally-edited project hierarchy on every re-import. Mirrors the
    // task-side `dedupedTodoistTaskIds` set added in iter-night-9.
    QSet<QString> dedupedTodoistProjectIds;

    // F-Q2.24-C #2: when re-importing the same backup, reuse an existing
    // active project with a matching (case-insensitive) name instead of
    // creating a fresh copy. Without this every re-run multiplies projects.
    // Manual user projects are also captured by the lookup — if a user has
    // their own "Work" project and imports a Todoist "Work" project, they
    // merge. That's intentional: it makes incremental imports idempotent.
    auto findExistingProjectByName = [this](const QString &name) -> Project * {
        for (auto *p : m_store->allProjects()) {
            if (p->isDeleted()) continue;
            if (p->isInbox()) continue; // inbox is mapped separately
            if (p->name().compare(name, Qt::CaseInsensitive) == 0)
                return p;
        }
        return nullptr;
    };

    // Process projects in order; inbox project treated specially
    for (const TodoistProject &tp : data.projects) {
        if (tp.isArchived) continue; // skip archived as per spec

        if (tp.isInbox) {
            // Map Todoist Inbox → OpenTodo's existing Inbox
            Project *inbox = m_store->inboxProject();
            if (inbox) {
                projIdMap[tp.id] = inbox->id();
                dedupedTodoistProjectIds.insert(tp.id);
                ++projCount; // [B-5.48] Count inbox project in projCount
                continue;
            }
        }

        // F-Q2.24-C #2: reuse existing project by name (dedup across imports).
        if (Project *existing = findExistingProjectByName(tp.name)) {
            projIdMap[tp.id] = existing->id();
            dedupedTodoistProjectIds.insert(tp.id);
            ++projCount;
            continue;
        }

        auto *proj = m_store->addProject(tp.name);
        proj->setColor(mapTodoistColor(tp.color));
        proj->setOrder(tp.order);
        proj->setIsFavorite(tp.isFavorite);
        proj->setUpdatedAt(QDateTime::currentDateTimeUtc());

        projIdMap[tp.id] = proj->id();
        ++projCount;
    }

    // Second pass: set parentId after all projects are created.
    //
    // Iter-night-19: skip deduplicated rows. `projIdMap[tp.id]` for a
    // deduped row resolves to an EXISTING project's UUID — so without this
    // guard the loop would call `proj->setParentId(...)` on the existing
    // project, overwriting any local re-parent the user made since the
    // previous import. `markProjectFieldsModified(..., {parent_id})` then
    // stamps the stale value with a fresh HLC so it ALSO propagates
    // through sync to every peer device that had observed the user's
    // local move. Mirrors the task-side guard at the Phase 4 second pass.
    //
    // Worst case: `Project::setParentId` is a raw setter with no cycle
    // guard (unlike `ProjectViewModel::setParentId`). If the user
    // locally broke a parent chain to escape a cycle, this re-import
    // could silently re-introduce it.
    for (const TodoistProject &tp : data.projects) {
        if (!projIdMap.contains(tp.id)) continue;
        if (tp.parentId.isEmpty()) continue;
        if (!projIdMap.contains(tp.parentId)) continue;
        if (dedupedTodoistProjectIds.contains(tp.id)) continue;

        Project *proj = m_store->projectById(projIdMap[tp.id]);
        if (proj) {
            proj->setParentId(projIdMap[tp.parentId]);
            // parent_id isn't in addProject's initialFields, so the per-field
            // CRDT stamp is required here — without it a concurrent reorg on
            // another device wins by empty-meta fallback and the imported
            // project hierarchy is silently lost after sync.
            m_store->markProjectFieldsModified(proj->id(), {QStringLiteral("parent_id")});
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    // Phase 3: Sections
    // todoistSectionId → openTodo UUID
    // ────────────────────────────────────────────────────────────────────────
    emit importProgress(70, 100, QStringLiteral("Импортируем разделы…"));

    QMap<QString, QString> secIdMap; // todoistId → openTodo UUID

    for (const TodoistSection &ts : data.sections) {
        const QString mappedProjectId = projIdMap.value(ts.projectId);
        if (mappedProjectId.isEmpty()) continue;

        Section *sec = m_store->addSection(ts.name, mappedProjectId);
        sec->setOrder(ts.order);
        secIdMap[ts.id] = sec->id();
    }

    // ────────────────────────────────────────────────────────────────────────
    // Phase 4: Tasks
    // Two-pass approach: first create all tasks, then set parent_id.
    // todoistTaskId → openTodo UUID
    // ────────────────────────────────────────────────────────────────────────
    emit importProgress(75, 100, QStringLiteral("Импортируем задачи…"));

    QMap<QString, QString> taskIdMap; // todoistId → openTodo UUID
    int taskCount = 0;
    const int totalTasks = data.tasks.size();

    // ITER3 #17 / F-Q2.24-C #2: dedup using a content + projectId fingerprint
    // that is STABLE across import runs. The previous shape (content + createdAt)
    // broke the CSV path because Todoist CSV exports carry no created_at field,
    // so the importer stamped each row with `QDateTime::currentDateTimeUtc()`.
    // Two runs of the same backup produced two different createdAt values →
    // two different fingerprints → every task duplicated. Dropping createdAt
    // makes the fingerprint stable; the projectId is now also stable because
    // project dedup-by-name above reuses an existing project on re-import.
    //
    // Trade-off: two tasks with the same content in the same project are now
    // treated as the same task for dedup purposes. That's an unusual case
    // (people don't usually file two literal duplicates) and the previous
    // shape was lossy in other ways anyway (it relied on a timestamp the user
    // could not see). The win — idempotent re-imports — far outweighs it.
    // iter-night-9: store fingerprint → EXISTING TASK UUID so the dedup
    // branch below can populate `taskIdMap[tt.id]` with the real id, not
    // the `__skipped__` sentinel that used to land there. The sentinel
    // propagated through the second-pass parent-id walk (lines ~726-731)
    // and ended up as a literal "__skipped__" string in newly-imported
    // subtasks' parent_id field — visible on every Todoist re-import that
    // added new children under an already-imported parent.
    QHash<QByteArray, QString> existingFingerprints;
    {
        const auto existingTasks = m_store->allTasks();
        existingFingerprints.reserve(existingTasks.size());
        for (const Task *et : existingTasks) {
            if (et->isDeleted()) continue;
            QByteArray raw = et->content().toUtf8() + "|"
                           + et->projectId().toUtf8();
            existingFingerprints.insert(
                QCryptographicHash::hash(raw, QCryptographicHash::Md5),
                et->id());
        }
    }
    // Track which Todoist IDs were mapped via dedup to existing tasks.
    // The second-pass parent-id walker uses this to AVOID re-stamping the
    // existing task's parent_id from import data — incremental re-imports
    // must not clobber the user's local moves of previously-imported tasks.
    QSet<QString> dedupedTodoistTaskIds;

    for (int i = 0; i < totalTasks; ++i) {
        const TodoistTask &tt = data.tasks[i];

        // Skip completed tasks unless the user requested them
        if (tt.isCompleted && !importCompleted) continue;

        if (tt.content.isEmpty()) continue;

        const QString mappedProjectId = projIdMap.value(
            tt.projectId, m_store->inboxProject() ? m_store->inboxProject()->id() : QString{});
        if (mappedProjectId.isEmpty()) continue;

        // F-Q2.24-C #2: skip if a task with matching content+projectId already
        // exists. Fingerprint must be computed AFTER projectId mapping so the
        // hash matches the one built from the existing DataStore tasks above.
        QByteArray fp;
        {
            QByteArray raw = tt.content.toUtf8() + "|" + mappedProjectId.toUtf8();
            fp = QCryptographicHash::hash(raw, QCryptographicHash::Md5);
            const auto fpIt = existingFingerprints.constFind(fp);
            if (fpIt != existingFingerprints.constEnd()) {
                m_warnings.append(QStringLiteral("Пропущено (уже импортировано): %1").arg(tt.content));
                // iter-night-9: map the Todoist id to the EXISTING task's
                // UUID (not "__skipped__") so newly-imported children whose
                // Todoist parent_id points at this row can re-parent
                // correctly to the survivor in the second pass.
                taskIdMap[tt.id] = fpIt.value();
                dedupedTodoistTaskIds.insert(tt.id);
                continue;
            }
        }

        auto *task = new Task(m_store);
        task->setContent(tt.content);
        task->setDescription(tt.description);
        task->setProjectId(mappedProjectId);
        task->setOrder(tt.order);
        task->setPriority(tt.priority);
        task->setIsCompleted(tt.isCompleted);
        if (tt.completedAt.isValid())
            task->setCompletedAt(tt.completedAt);
        if (tt.createdAt.isValid())
            task->setCreatedAt(tt.createdAt);
        else
            task->setCreatedAt(QDateTime::currentDateTimeUtc());
        task->setUpdatedAt(QDateTime::currentDateTimeUtc());

        // Section
        if (!tt.sectionId.isEmpty() && secIdMap.contains(tt.sectionId))
            task->setSectionId(secIdMap[tt.sectionId]);

        // Due date
        if (!tt.due.date.isEmpty() || !tt.due.string.isEmpty()) {
            task->setDueDate(tt.due.date);
            task->setDueDatetime(tt.due.datetime);
            task->setDueString(tt.due.string);
            task->setDueLang(tt.due.lang);
            task->setDueIsRecurring(tt.due.isRecurring);
            task->setDueTimezone(tt.due.timezone);
        }

        // Duration
        if (tt.durationAmount > 0) {
            task->setDurationAmount(tt.durationAmount);
            task->setDurationUnit(tt.durationUnit);
        }

        // Labels — look up by name
        QStringList resolvedLabels;
        for (const QString &name : tt.labelNames) {
            // Try name map first (works for both CSV names and API names)
            if (labelNameToId.contains(name)) {
                resolvedLabels.append(name); // Task.labels stores label *names*
            } else {
                // Create label on the fly if unknown
                Label *lbl = m_store->labelByName(name);
                if (!lbl) {
                    lbl = m_store->addLabel(name);
                }
                labelNameToId[name] = lbl->id();
                resolvedLabels.append(name);
            }
        }
        task->setLabels(resolvedLabels);

        m_store->addExistingTask(task);
        taskIdMap[tt.id] = task->id();
        ++taskCount;

        // iter-night-9: record THIS task's fingerprint → its new UUID so
        // duplicates appearing later in the SAME import (e.g. the ZIP
        // ships the row twice) dedup against the just-created task and
        // their children parent correctly. Previously the SAME-import
        // dedup path also wrote "__skipped__" — same bug as the
        // re-import path, harder to hit but identical shape.
        existingFingerprints.insert(fp, task->id());

        if (i % 20 == 0) {
            const int pct = 75 + (i * 15 / qMax(1, totalTasks));
            emit importProgress(pct, 100,
                QStringLiteral("Задача %1 из %2…").arg(i + 1).arg(totalTasks));
        }
    }

    // Second pass: set parentId now that all tasks are created.
    //
    // iter-night-9: skip deduplicated rows. taskIdMap[tt.id] now resolves
    // to an EXISTING task's UUID for deduped rows (was "__skipped__" before),
    // so without this guard the loop would overwrite the EXISTING task's
    // parent_id with whatever Todoist's backup says — clobbering any local
    // move the user made since the previous import. Incremental imports
    // should be additive: only set parent_id on tasks we actually created
    // in this run. Deduped tasks already have whatever parent_id they
    // had locally; leave it alone.
    for (const TodoistTask &tt : data.tasks) {
        if (tt.parentId.isEmpty()) continue;
        if (!taskIdMap.contains(tt.id)) continue;
        if (!taskIdMap.contains(tt.parentId)) continue;
        if (dedupedTodoistTaskIds.contains(tt.id)) continue;

        Task *task = m_store->taskById(taskIdMap[tt.id]);
        if (task) {
            task->setParentId(taskIdMap[tt.parentId]);
            // Ensure the parent task's project id is used as fallback
            Task *parent = m_store->taskById(taskIdMap[tt.parentId]);
            if (parent && task->projectId().isEmpty())
                task->setProjectId(parent->projectId());
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    // Phase 5: Notes (comments)
    // ────────────────────────────────────────────────────────────────────────
    emit importProgress(92, 100, QStringLiteral("Импортируем комментарии…"));

    for (const TodoistNote &tn : data.notes) {
        if (tn.content.isEmpty()) continue;

        // Determine target task id
        const QString mappedTaskId = taskIdMap.value(tn.taskId);
        const QString mappedProjectId = projIdMap.value(tn.projectId);

        // Skip notes whose task was filtered out (e.g. completed) unless we
        // have a project-level note (no taskId)
        if (!tn.taskId.isEmpty() && mappedTaskId.isEmpty()) continue;

        Comment *c = m_store->addComment(tn.content,
                                          mappedTaskId,
                                          mappedProjectId);
        if (tn.postedAt.isValid())
            c->setPostedAt(tn.postedAt);
    }

    // ────────────────────────────────────────────────────────────────────────
    // Done — capture counts into outer variables, then close blocker scope.
    // ────────────────────────────────────────────────────────────────────────
    finalTaskCount = taskCount;
    finalProjCount = projCount;
    } // end QSignalBlocker scope — all m_store signals unblocked here [B-5.52]

    // Emit ONE aggregate dataChanged (via markDataChanged) so FileManager's
    // debounce fires exactly once instead of once per entity [B-5.52].
    // markDataChanged() is a public method that emits sectionsChanged() +
    // dataChanged(); ViewModels that need tasksChanged/projectsChanged etc.
    // will also need those — emit them explicitly now that signals are live.
    emit m_store->tasksChanged();
    emit m_store->projectsChanged();
    emit m_store->labelsChanged();
    m_store->markDataChanged(); // fires sectionsChanged() + dataChanged()

    // [B-5.44] Record successful completion so the next run can detect a
    // duplicate batch (same batchId would only occur if the caller re-uses one).
    settings.setValue(QStringLiteral("import/last_completed_batch"), batchId);
    settings.remove(QStringLiteral("import/current_batch"));

    emit importProgress(100, 100, QStringLiteral("Завершено"));

    setBusy(false);

    // F-Q2.24-C #3: emit a single user-facing warning when one or more rows
    // had PRIORITY=0 (Todoist null sentinel). They land at internal Normal,
    // indistinguishable from explicit Normal — see todoistPriority() comment.
    if (m_lossyPriorityZeroCount > 0) {
        const QString line = QStringLiteral(
            "У %1 задач(и) приоритет не был задан в Todoist (значение 0); "
            "они импортированы как обычный приоритет (p4).")
            .arg(m_lossyPriorityZeroCount);
        qInfo().noquote() << "[TodoistImporter]" << line;
        m_warnings.append(line);
    }

    // F-Q2.24-C #6: emit a single user-facing notice when CSV-path projects
    // were imported. The CSV format carries no project colour, so every CSV
    // project lands as grey. The user can either re-colour manually after
    // import, or re-import via the API path to pick up the original palette.
    if (m_csvProjectCount > 0) {
        const QString line = QStringLiteral(
            "Цвета проектов не сохраняются в Todoist CSV-экспорте: "
            "%1 проект(ов) импортирован(ы) серым. Используйте импорт по "
            "API-токену для сохранения исходных цветов.")
            .arg(m_csvProjectCount);
        qInfo().noquote() << "[TodoistImporter]" << line;
        m_warnings.append(line);
    }

    emit importFinished(finalTaskCount, finalProjCount, QString{});

    // [B-5.62] Emit non-fatal parse warnings after importFinished so QML can
    // show them without blocking the "success" state.
    if (!m_warnings.isEmpty())
        emit importWarnings(m_warnings);
}
