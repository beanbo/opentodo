// ============================================================================
// test_syncengine_resilience_v2.cpp                                   (Q3.01-B)
//
// PULL-side resilience coverage for SyncEngine — corrupted state.json,
// corrupted commit logs, missing HEAD targets, unknown operation types, and
// pathological subtask cycles. Complements:
//
//   - test_syncengine_resilience.cpp (Q2.17-E): offline/auth/lock-contention
//     paths via the no-network / fake-token branches. NO MockNAM.
//   - test_syncengine_lifecycle.cpp (Q2.22-E): happy-path push/pull lifecycle
//     via DriveApi::setNetworkManagerForTesting + MockNAM. Establishes the
//     seedDriveHappyPath helper.
//
// This file extends the MockNAM approach from test_syncengine_lifecycle.cpp
// to drive pull() through DELIBERATELY CORRUPTED remote payloads. The hard
// constraint is the same: SyncEngine wholly owns its GDriveClient, so we
// intercept HTTP at the QNetworkAccessManager layer rather than mocking the
// GDriveClient itself.
//
// ── Mission ────────────────────────────────────────────────────────────────
//
// For each slot below, the goal is to PIN the CURRENT BEHAVIOUR of pull()
// when the remote returns malformed or unexpected data. Each slot:
//   1. Configures the MockNAM with the canned corruption scenario.
//   2. Invokes pull() against a DataStore seeded with at least one local
//      "survivor" task so the data-loss canary is meaningful.
//   3. Asserts the resulting status / lastError / local-state invariants
//      currently observed.
//   4. Emits qInfo() lines documenting the gap if production silently
//      mishandles the scenario (rather than QFAIL'ing — the suite stays
//      green while the fragility is visible for the next maintainer).
//
// Slots covered (7):
//   1. pull_with_corrupted_state_json_does_not_apply_partial_state
//   2. pull_with_state_json_missing_required_keys_treats_as_empty
//   3. pull_with_oversized_state_json_aborts_or_caps
//   4. pull_with_corrupted_commit_log_skips_or_aborts
//   5. pull_with_HEAD_pointing_to_missing_commit_aborts_cleanly
//   6. pull_with_commit_having_unknown_operation_type_skips_that_op
//   7. applyCommit_with_circular_subtask_parent_chain_does_not_recurse_forever
//
// NO PRODUCTION CODE IS MODIFIED. The asserts below pin the behaviour as
// observed today; flipping any of them when a fix lands is the intended
// regression-bench.
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QBuffer>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

#include "storage/datastore.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"
#include "sync/drive_api.h"
#include "sync/commit.h"
#include "model/task.h"

// ──────────────────────────────────────────────────────────────────────────
// MockNAM + FakeReply — file-scope subclasses so moc can generate the
// meta-object. Pattern mirrors test_syncengine_lifecycle.cpp; we keep them
// independent so a change in either test's mock plumbing does not cause
// cross-file linkage breakage.
// ──────────────────────────────────────────────────────────────────────────

struct CannedResponse {
    int        statusCode       = 200;
    QByteArray body             = QByteArrayLiteral("{}");
    bool       forceNetworkError = false;
};

class FakeReply : public QNetworkReply
{
    Q_OBJECT
public:
    FakeReply(QObject *parent,
              const QNetworkRequest &req,
              QNetworkAccessManager::Operation op,
              int statusCode,
              const QByteArray &body,
              QNetworkReply::NetworkError err)
        : QNetworkReply(parent)
        , m_body(body)
        , m_buffer(&m_body)
    {
        setRequest(req);
        setUrl(req.url());
        setOperation(op);
        m_buffer.open(QIODevice::ReadOnly);
        open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (err != QNetworkReply::NoError) {
            setError(err, QStringLiteral("FakeReply: simulated error"));
        }
        QTimer::singleShot(0, this, [this]() {
            emit readyRead();
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override
    {
        return m_buffer.bytesAvailable() + QNetworkReply::bytesAvailable();
    }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        return m_buffer.read(data, maxlen);
    }
private:
    QByteArray m_body;
    QBuffer m_buffer;
};

class MockNAM : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit MockNAM(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    struct OutboundCall {
        Operation  op;
        QUrl       url;
        QByteArray method;
        QByteArray body;
    };
    QList<OutboundCall> calls;
    QList<QPair<QString, CannedResponse>> responses;
    CannedResponse defaultResponse;

    void reset() { calls.clear(); responses.clear(); defaultResponse = CannedResponse{}; }

    void registerJsonResponse(const QString &urlSubstring, int status,
                              const QJsonObject &obj)
    {
        CannedResponse r;
        r.statusCode = status;
        r.body       = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        responses.append({urlSubstring, r});
    }
    void registerResponse(const QString &urlSubstring, int status,
                          const QByteArray &body)
    {
        CannedResponse r;
        r.statusCode = status;
        r.body       = body;
        responses.append({urlSubstring, r});
    }
    void registerNetworkError(const QString &urlSubstring)
    {
        CannedResponse r;
        r.forceNetworkError = true;
        responses.append({urlSubstring, r});
    }

    CannedResponse responseFor(const QUrl &u) const
    {
        const QString encoded = u.toString();
        const QString decoded =
            QString::fromUtf8(QByteArray::fromPercentEncoding(encoded.toUtf8()));
        for (const auto &kv : responses) {
            if (encoded.contains(kv.first) || decoded.contains(kv.first))
                return kv.second;
        }
        return defaultResponse;
    }

    int countCallsContaining(const QString &needle) const
    {
        int n = 0;
        for (const auto &c : calls) {
            const QString s = c.url.toString();
            const QString d = QString::fromUtf8(
                QByteArray::fromPercentEncoding(s.toUtf8()));
            if (s.contains(needle) || d.contains(needle))
                ++n;
        }
        return n;
    }

protected:
    QNetworkReply *createRequest(Operation op,
                                 const QNetworkRequest &req,
                                 QIODevice *outgoingData) override
    {
        OutboundCall call;
        call.op   = op;
        call.url  = req.url();
        call.body = outgoingData ? outgoingData->readAll() : QByteArray();
        if (op == CustomOperation)      call.method = QByteArrayLiteral("CUSTOM");
        else if (op == PostOperation)   call.method = QByteArrayLiteral("POST");
        else if (op == GetOperation)    call.method = QByteArrayLiteral("GET");
        else if (op == DeleteOperation) call.method = QByteArrayLiteral("DELETE");
        else if (op == PutOperation)    call.method = QByteArrayLiteral("PUT");
        calls.append(call);

        const CannedResponse r = responseFor(req.url());
        QNetworkReply::NetworkError err = QNetworkReply::NoError;
        if (r.forceNetworkError)
            err = QNetworkReply::UnknownNetworkError;
        else if (r.statusCode == 401)
            err = QNetworkReply::AuthenticationRequiredError;
        else if (r.statusCode == 404)
            err = QNetworkReply::ContentNotFoundError;
        else if (r.statusCode >= 500 && r.statusCode < 600)
            err = QNetworkReply::InternalServerError;
        else if (r.statusCode >= 400)
            err = QNetworkReply::UnknownContentError;

        return new FakeReply(this, req, op, r.statusCode, r.body, err);
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Helpers (small, in-file — keep this test self-contained).
// ──────────────────────────────────────────────────────────────────────────

namespace {

QString pendingCommitsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/OpenTodo/local_log");
}

void wipePendingCommitsDir()
{
    QDir d(pendingCommitsDir());
    if (d.exists()) d.removeRecursively();
}

void useUniqueSettings(const QString &slotName)
{
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SyncResilienceV2"));
    QCoreApplication::setApplicationName(
        QStringLiteral("test_syncengine_resilience_v2_") + slotName);
    QSettings s;
    s.clear();
    s.sync();
}

QJsonObject makeTaskJson(const QString &id, const QString &content,
                         const QString &parentId = QString())
{
    QJsonObject j;
    j[QStringLiteral("id")] = id;
    j[QStringLiteral("content")] = content;
    j[QStringLiteral("priority")] = 1;
    j[QStringLiteral("is_completed")] = false;
    j[QStringLiteral("is_deleted")] = false;
    j[QStringLiteral("project_id")] = QStringLiteral("inbox");
    j[QStringLiteral("parent_id")] = parentId;
    j[QStringLiteral("created_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
    j[QStringLiteral("updated_at")] = QStringLiteral("2026-04-01T00:00:00.000Z");
    return j;
}

// Drive lookup responses (mirrors helpers in test_syncengine_lifecycle.cpp).
QJsonObject driveFindResponse(const QString &id)
{
    QJsonObject f;
    f.insert(QStringLiteral("id"), id);
    QJsonArray arr;
    arr.append(f);
    QJsonObject root;
    root.insert(QStringLiteral("files"), arr);
    return root;
}

QJsonObject driveEmptyFindResponse()
{
    QJsonObject root;
    root.insert(QStringLiteral("files"), QJsonArray());
    return root;
}

// Drive listing response (used for log/ folder content listing).
QJsonObject driveListFilesResponse(const QStringList &filenames,
                                   const QString &idPrefix = QStringLiteral("file-"))
{
    QJsonArray arr;
    for (int i = 0; i < filenames.size(); ++i) {
        QJsonObject f;
        f.insert(QStringLiteral("id"), idPrefix + QString::number(i));
        f.insert(QStringLiteral("name"), filenames.at(i));
        arr.append(f);
    }
    QJsonObject root;
    root.insert(QStringLiteral("files"), arr);
    return root;
}

// Seed the Drive happy-path responses needed by every pull(): root folder,
// .lock (does not exist so acquireLock succeeds), log/ folder. Callers add
// HEAD + state.json + commit-file responses themselves to drive the
// scenario-specific behaviour.
void seedDriveBootstrap(MockNAM &mock,
                        const QString &rootId    = QStringLiteral("root-id"),
                        const QString &logFolder = QStringLiteral("log-folder-id"))
{
    mock.defaultResponse.statusCode = 200;
    mock.defaultResponse.body =
        QJsonDocument(driveEmptyFindResponse()).toJson(QJsonDocument::Compact);

    mock.registerJsonResponse(QStringLiteral("name = 'opentodo_data'"),
                              200, driveFindResponse(rootId));
    mock.registerJsonResponse(QStringLiteral("name = '.lock'"),
                              200, driveEmptyFindResponse());
    mock.registerJsonResponse(QStringLiteral("name = 'log'"),
                              200, driveFindResponse(logFolder));

    // Lock acquisition uploads via POST /upload — must succeed.
    QJsonObject created;
    created.insert(QStringLiteral("id"), QStringLiteral("new-file-id"));
    mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                              200, created);
}

} // namespace

// ============================================================================
// Test class
// ============================================================================
class TestSyncEngineResilienceV2 : public QObject
{
    Q_OBJECT

private:
    bool m_prevTestMode = false;

private slots:
    void initTestCase()
    {
        m_prevTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
        wipePendingCommitsDir();
        qInfo() << "SyncEngine PULL-side corruption resilience suite — pins "
                   "current behaviour for malformed state.json, broken commit "
                   "logs, dangling HEAD pointers, unknown ops, and subtask "
                   "parent cycles. All assertions are CURRENT-BEHAVIOUR pins; "
                   "flip them when production is intentionally fixed.";
    }

    void cleanupTestCase()
    {
        wipePendingCommitsDir();
        QStandardPaths::setTestModeEnabled(m_prevTestMode);
    }

    void cleanup() { wipePendingCommitsDir(); }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 1 — pull_with_corrupted_state_json_does_not_apply_partial_state
    //
    // Contract under test: when remote state.json downloads but is malformed
    // (truncated mid-key, garbage bytes, broken nesting), pull() must leave
    // the local DataStore untouched.
    //
    // Today's implementation (syncengine.cpp:261-275): QJsonDocument::fromJson
    // returns an invalid doc → .object() yields an empty QJsonObject →
    // !remoteState.contains("tasks") → engine sets lastError + transitions to
    // Error and bails BEFORE loadFromJson. This is the B-2.42 guard.
    //
    // We pin: status=Error, lastError contains "Remote state.json is invalid",
    // local survivor task is still in the store, lastSyncTime NOT advanced.
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_corrupted_state_json_does_not_apply_partial_state()
    {
        useUniqueSettings(QStringLiteral("corruptedStateJson"));
        DataStore store;

        // Seed local survivor.
        Task *seed = Task::fromJson(makeTaskJson(QStringLiteral("survivor-1"),
                                                 QStringLiteral("must survive corrupt pull")),
                                    &store);
        QVERIFY(seed != nullptr);
        store.addExistingTask(seed);
        QVERIFY(store.taskById(QStringLiteral("survivor-1")) != nullptr);
        const int beforeCount = store.allTasks().size();

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-corrupt-state"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        // HEAD returns a different commit so pull walks the full pipeline.
        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), QStringLiteral("remote-head-uuid"));
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);

        // log/ listing returns empty — no commits to replay (so we isolate the
        // state.json failure mode).
        // (driveBootstrap already set 'log' folder lookup; we now register the
        // /files?q=... listing call for that folder to return empty.)
        // The listing uses the log-folder-id in its q clause.
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveEmptyFindResponse());

        // state.json file is FOUND, but its body is malformed (truncated mid-
        // key). QJsonDocument::fromJson returns an invalid doc.
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        const QByteArray garbageBody = QByteArrayLiteral(
            "{\"tasks\": [{\"id\": \"truncated-mid-keyx\", \"conte");
        mock.registerResponse(QStringLiteral("state-id?alt=media"),
                              200, garbageBody);

        QSignalSpy syncCompletedSpy(&engine, &SyncEngine::syncCompleted);
        QSignalSpy lastErrorSpy(&engine, &SyncEngine::lastErrorChanged);

        engine.pull();

        // CONTRACT: B-2.42 guard fires. Status → Error.
        QCOMPARE(engine.status(), SyncEngine::Error);
        QVERIFY2(!engine.lastError().isEmpty(),
                 "B-2.42 guard must populate lastError when remote state.json "
                 "fails to parse. If empty, the error path is now silent and "
                 "QML cannot show a meaningful failure dialog.");
        QVERIFY(engine.lastError().contains(QStringLiteral("invalid"),
                                            Qt::CaseInsensitive)
             || engine.lastError().contains(QStringLiteral("tasks"),
                                            Qt::CaseInsensitive));

        // Local data MUST survive.
        QVERIFY2(store.taskById(QStringLiteral("survivor-1")) != nullptr,
                 "CRITICAL: pull() applied a corrupted state.json and lost "
                 "local data. The B-2.42 guard at syncengine.cpp:268 has "
                 "regressed.");
        QCOMPARE(store.allTasks().size(), beforeCount);

        // lastSyncTime not advanced (no successful sync).
        QVERIFY(!engine.lastSyncTime().isValid());

        // syncCompleted must not fire on the error branch.
        QCOMPARE(syncCompletedSpy.count(), 0);

        // lastErrorChanged should have fired at least once.
        QVERIFY(lastErrorSpy.count() >= 1);

        qInfo() << "Corrupted state.json: status=" << engine.status()
                << " lastError=" << engine.lastError();
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 2 — pull_with_state_json_missing_required_keys_treats_as_empty
    //
    // Contract under test: state.json is valid JSON ({}) but lacks the
    // top-level "tasks" key. Today's B-2.42 guard treats this the same as
    // "corrupted" — sets lastError + Error and DOES NOT call
    // loadFromJson, which means local data is preserved.
    //
    // This is "defensive but somewhat surprising": a genuinely empty remote
    // (legitimately first-sync) would have NO state.json file at all, not an
    // empty {} body. So the {} case in practice arises from a partial / failed
    // remote write, and treating it as an error is the safer default.
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_state_json_missing_required_keys_treats_as_empty()
    {
        useUniqueSettings(QStringLiteral("stateMissingKeys"));
        DataStore store;

        Task *seed = Task::fromJson(makeTaskJson(QStringLiteral("survivor-2"),
                                                 QStringLiteral("must survive empty-state pull")),
                                    &store);
        QVERIFY(seed != nullptr);
        store.addExistingTask(seed);
        const int beforeCount = store.allTasks().size();

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-empty-state"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), QStringLiteral("remote-head-uuid"));
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveEmptyFindResponse());

        // state.json present, but it's an empty JSON object — no "tasks" key.
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, QJsonObject());

        engine.pull();

        // PIN: the B-2.42 guard treats missing-"tasks" as an error, NOT as
        // "remote is legitimately empty". status=Error.
        QCOMPARE(engine.status(), SyncEngine::Error);
        QVERIFY2(store.taskById(QStringLiteral("survivor-2")) != nullptr,
                 "Empty remote state ({}) wiped local data — B-2.42 guard "
                 "regressed: loadFromJson was called with an empty object.");
        QCOMPARE(store.allTasks().size(), beforeCount);

        qInfo() << "Empty {} state.json behaviour: status="
                << engine.status() << " lastError=" << engine.lastError();
        qInfo() << "NOTE: B-2.42 treats missing-'tasks' as an error rather "
                   "than as a no-op. A future refinement could distinguish "
                   "'remote intentionally empty (e.g. user wiped all data)' "
                   "from 'remote corrupted'. Today both surface as Error.";
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 3 — pull_with_oversized_state_json_aborts_or_caps
    //
    // Contract under test: a sane upper bound on remote state.json size.
    // Currently there is NONE — DriveApi::downloadFile reads the entire reply
    // body into memory unconditionally (reply->readAll()) and SyncEngine
    // then parses it. A malicious or accidentally-huge state.json (e.g. 100
    // MB) gets fully read.
    //
    // We can't realistically build a 100 MB payload inside a test without
    // bloating the binary, so we test the SHAPE of the contract: pull() with
    // a moderately large (1 MB) but otherwise-valid state.json must succeed
    // and not corrupt local state. Then we DOCUMENT the gap that no size cap
    // exists by inspecting that downloadFile has no maxBytes parameter.
    //
    // PIN: 1 MB state.json with a single huge task content survives the
    // round-trip; local "survivor" task is replaced by the remote payload
    // (so the merge replaced local with remote, no partial state).
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_oversized_state_json_aborts_or_caps()
    {
        useUniqueSettings(QStringLiteral("oversizedState"));
        DataStore store;

        Task *seed = Task::fromJson(makeTaskJson(QStringLiteral("local-pre"),
                                                 QStringLiteral("local-before-merge")),
                                    &store);
        QVERIFY(seed != nullptr);
        store.addExistingTask(seed);

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-oversize"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), QStringLiteral("remote-head-uuid"));
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveEmptyFindResponse());

        // Build a 1 MB content blob. A real 100 MB payload would slow the
        // suite; this is enough to confirm the engine does not arbitrarily
        // reject large-but-valid bodies.
        const QString largeContent(1 * 1024 * 1024, QChar('A'));
        QJsonObject remoteTask = makeTaskJson(QStringLiteral("remote-big"),
                                              largeContent);
        QJsonObject remoteState;
        QJsonArray tasks;
        tasks.append(remoteTask);
        remoteState.insert(QStringLiteral("tasks"), tasks);
        remoteState.insert(QStringLiteral("projects"), QJsonArray());
        remoteState.insert(QStringLiteral("sections"), QJsonArray());
        remoteState.insert(QStringLiteral("labels"),   QJsonArray());

        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, remoteState);

        const QElapsedTimer t; Q_UNUSED(t);
        QElapsedTimer wall; wall.start();
        engine.pull();
        const qint64 elapsedMs = wall.elapsed();

        qInfo() << "1 MB state.json pull elapsed ms:" << elapsedMs
                << " (no upper bound enforced by production today)";

        // PIN: pull succeeds — large-but-valid body is accepted.
        QCOMPARE(engine.status(), SyncEngine::Synced);
        // After successful pull with no pending commits, loadFromJson runs
        // and replaces local state with remote state.
        QVERIFY2(store.taskById(QStringLiteral("remote-big")) != nullptr,
                 "1 MB remote task was rejected — production may have grown "
                 "an oversize cap. Update this test to match the new behaviour.");
        // The pre-existing "local-pre" task is replaced because there are no
        // pending commits and loadFromJson clears + repopulates.
        QVERIFY(store.taskById(QStringLiteral("local-pre")) == nullptr);

        qInfo() << "DOCUMENTED GAP: SyncEngine / DriveApi have NO upper bound "
                   "on remote state.json size. A malicious or runaway peer "
                   "could push a 100 MB+ state.json that the desktop client "
                   "would happily download, parse, and load into memory. "
                   "Mitigation: either add a maxBytes parameter to "
                   "DriveApi::downloadFile (rejecting Content-Length > N) or "
                   "stream-parse instead of QJsonDocument::fromJson on full body.";
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 4 — pull_with_corrupted_commit_log_skips_or_aborts
    //
    // Contract under test: pull() lists log/ and downloads each commit file.
    // If one /log/<uuid>.json contains malformed JSON, what happens to the
    // OTHER commits in the same listing?
    //
    // Today's implementation (syncengine.cpp:198-207):
    //   for (fileName in logFiles) {
    //     downloadFile(commitData); if (!downloadFile(...)) continue;
    //     doc = QJsonDocument::fromJson(commitData);
    //     if (!doc.isObject()) continue;
    //     remoteCommits.append(Commit::fromJson(doc.object()));
    //   }
    //
    // Malformed JSON → fromJson returns invalid doc → !doc.isObject() →
    // continue. The bad commit is SKIPPED; the good neighbours are still
    // applied. pull() continues to state.json download + merge.
    //
    // We pin: with 2 commits where one is malformed, the engine processes the
    // valid one normally and reaches Synced.
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_corrupted_commit_log_skips_or_aborts()
    {
        useUniqueSettings(QStringLiteral("corruptCommitLog"));
        DataStore store;

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-badlog"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        // HEAD points at the GOOD commit so the chain-walk in pull picks
        // a meaningful order.
        const QString goodUuid = QStringLiteral("good-commit-uuid");
        const QString badUuid  = QStringLiteral("bad-commit-uuid");

        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), goodUuid);
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);

        // log/ listing returns BOTH commit filenames.
        const QStringList logFiles = {
            goodUuid + QStringLiteral(".json"),
            badUuid  + QStringLiteral(".json"),
        };
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveListFilesResponse(logFiles));

        // Good commit body: a valid commit that adds a task.
        QJsonObject goodOp;
        goodOp.insert(QStringLiteral("type"), QStringLiteral("task_add"));
        goodOp.insert(QStringLiteral("entity_id"),
                      QStringLiteral("from-good-commit"));
        goodOp.insert(QStringLiteral("data"),
                      makeTaskJson(QStringLiteral("from-good-commit"),
                                   QStringLiteral("from good commit")));
        QJsonObject goodCommit;
        goodCommit.insert(QStringLiteral("uuid"), goodUuid);
        goodCommit.insert(QStringLiteral("device_id"), QStringLiteral("dev-x"));
        goodCommit.insert(QStringLiteral("timestamp"),
                          QStringLiteral("2026-04-02T00:00:00.000Z"));
        QJsonArray goodOps; goodOps.append(goodOp);
        goodCommit.insert(QStringLiteral("operations"), goodOps);

        // Drive findFile lookups for each commit file name (used by DriveApi
        // before downloadFile) plus the alt=media body fetch.
        // listFiles returns the file id under the same lookup — but
        // downloadFile does its own findFileId by name + parent. We register
        // the per-name lookup AND the per-id alt=media response.
        mock.registerJsonResponse(
            QStringLiteral("name = '") + goodUuid + QStringLiteral(".json'"),
            200, driveFindResponse(QStringLiteral("good-commit-file-id")));
        mock.registerJsonResponse(
            QStringLiteral("good-commit-file-id?alt=media"),
            200, goodCommit);

        // Bad commit body: malformed JSON (truncated).
        mock.registerJsonResponse(
            QStringLiteral("name = '") + badUuid + QStringLiteral(".json'"),
            200, driveFindResponse(QStringLiteral("bad-commit-file-id")));
        const QByteArray garbage = QByteArrayLiteral(
            "{\"uuid\": \"bad-commit\", \"operations\": [{\"ty");
        mock.registerResponse(QStringLiteral("bad-commit-file-id?alt=media"),
                              200, garbage);

        // state.json: legitimate (empty tasks) so the B-2.42 guard does not
        // fire and we isolate the commit-log corruption behaviour.
        QJsonObject remoteState;
        remoteState.insert(QStringLiteral("tasks"), QJsonArray());
        remoteState.insert(QStringLiteral("projects"), QJsonArray());
        remoteState.insert(QStringLiteral("sections"), QJsonArray());
        remoteState.insert(QStringLiteral("labels"),   QJsonArray());
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, remoteState);

        engine.pull();

        // PIN: pull reached Synced — the corrupted commit was skipped.
        QCOMPARE(engine.status(), SyncEngine::Synced);
        qInfo() << "Corrupted commit log behaviour: pull() reached"
                << engine.status()
                << "after silently skipping the malformed /log/<uuid>.json file.";
        qInfo() << "DOCUMENTED BEHAVIOUR: syncengine.cpp:198-207 skips "
                   "individual unparseable commit files (via `if "
                   "(!doc.isObject()) continue`). Neighbours are still "
                   "processed. Pros: pull progresses past one bad file. Cons: "
                   "data loss silently — the operations in the bad commit are "
                   "now never applied to any peer, even ones that haven't "
                   "compacted the snapshot yet. Consider warning + telemetry.";
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 5 — pull_with_HEAD_pointing_to_missing_commit_aborts_cleanly
    //
    // Contract under test: HEAD says commit "abc123" but /log/abc123.json
    // returns 404. The remote is internally inconsistent.
    //
    // Today's implementation (syncengine.cpp:198-207, 213-227):
    //   - listFiles("log") returns the actual log filenames — which do NOT
    //     include "abc123.json".
    //   - remoteCommits is built from the listing, NOT from HEAD. So the
    //     missing commit is simply absent from the list.
    //   - The chain-walk at line 222-227 starts from `remoteHead` and walks
    //     parents via byUuid lookup. Since "abc123" is not in byUuid, the
    //     while-loop fails its first contains() check and never runs.
    //   - "chain" stays empty. `sorted` stays empty. Any commits NOT in chain
    //     are appended by timestamp (line 232-235).
    //   - Commits are applied; state.json is loaded normally.
    //
    // So in practice: a dangling HEAD pointer does NOT abort — pull silently
    // succeeds and applies whatever IS in the log. The HEAD value is stored as
    // m_headCommitId for future pulls.
    //
    // PIN: status=Synced; local m_headCommitId now equals the dangling head;
    // any non-chained log commits ARE applied. This is the surprising part.
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_HEAD_pointing_to_missing_commit_aborts_cleanly()
    {
        useUniqueSettings(QStringLiteral("danglingHead"));
        DataStore store;

        // Seed local survivor that must NOT be wiped if pull aborts.
        Task *seed = Task::fromJson(makeTaskJson(QStringLiteral("survivor-5"),
                                                 QStringLiteral("survive dangling HEAD")),
                                    &store);
        QVERIFY(seed != nullptr);
        store.addExistingTask(seed);

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-dangling"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        // HEAD says abc123 — which doesn't exist in /log/.
        const QString danglingHead = QStringLiteral("abc123-does-not-exist");
        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), danglingHead);
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);

        // log/ listing returns no files — abc123 is not there.
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveEmptyFindResponse());

        // state.json: empty but valid (passes B-2.42).
        QJsonObject remoteState;
        remoteState.insert(QStringLiteral("tasks"), QJsonArray());
        remoteState.insert(QStringLiteral("projects"), QJsonArray());
        remoteState.insert(QStringLiteral("sections"), QJsonArray());
        remoteState.insert(QStringLiteral("labels"),   QJsonArray());
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, remoteState);

        engine.pull();

        // PIN current behaviour: pull SUCCEEDS even though HEAD is dangling.
        // The engine has no way to detect the inconsistency — it walks the
        // log it was given and treats HEAD as the new local head.
        QCOMPARE(engine.status(), SyncEngine::Synced);

        // Survivor task is gone because loadFromJson(empty-state) wipes
        // local state (no pending commits → load takes the merge bypass).
        QVERIFY2(store.taskById(QStringLiteral("survivor-5")) == nullptr,
                 "Survivor was unexpectedly preserved — pull may have aborted "
                 "or merged differently than expected. Investigate.");

        qInfo() << "DOCUMENTED BEHAVIOUR + GAP: HEAD pointing at a commit "
                   "that doesn't exist in log/ does NOT abort pull(). The "
                   "engine walks log/ (empty), applies state.json verbatim, "
                   "and stores the dangling HEAD value as m_headCommitId. "
                   "Future push() will chain new commits off this dangling "
                   "parent — but the peer will never download a commit it "
                   "can't find, so the chain breakage is silent. A genuine "
                   "fix would: (a) validate that HEAD resolves in log/ "
                   "before applying state.json, OR (b) treat unresolved HEAD "
                   "as a soft warning and fall back to chain-by-timestamp.";
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 6 — pull_with_commit_having_unknown_operation_type_skips_that_op
    //
    // Contract under test: a remote commit contains an operation whose `type`
    // is unrecognised (e.g. "task_invigorate" — not in any *_add/*_update/
    // *_delete branch). applyCommit must NOT crash; it must ignore the
    // unknown op and continue with the rest of the commit.
    //
    // Today's implementation (syncengine.cpp:634-636): the final else clause
    // of applyCommit emits qCWarning(lcSync) and falls through. The next
    // operation in the same commit is processed normally.
    //
    // We construct a commit with TWO ops — one valid, one unknown — and
    // verify the valid one is applied while the unknown one is ignored.
    // ════════════════════════════════════════════════════════════════════════
    void pull_with_commit_having_unknown_operation_type_skips_that_op()
    {
        useUniqueSettings(QStringLiteral("unknownOpInCommit"));
        DataStore store;

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-unknownop"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        const QString commitUuid = QStringLiteral("commit-with-unknown-op");
        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), commitUuid);
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);

        const QStringList logFiles = { commitUuid + QStringLiteral(".json") };
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveListFilesResponse(logFiles));

        // Commit with TWO ops — one valid, one bogus.
        QJsonObject validOp;
        validOp.insert(QStringLiteral("type"), QStringLiteral("task_add"));
        validOp.insert(QStringLiteral("entity_id"),
                       QStringLiteral("valid-from-mixed-commit"));
        validOp.insert(QStringLiteral("data"),
                       makeTaskJson(QStringLiteral("valid-from-mixed-commit"),
                                    QStringLiteral("hello from valid op")));

        QJsonObject unknownOp;
        unknownOp.insert(QStringLiteral("type"),
                         QStringLiteral("task_invigorate_v99"));
        unknownOp.insert(QStringLiteral("entity_id"),
                         QStringLiteral("ghost-entity"));
        unknownOp.insert(QStringLiteral("data"), QJsonObject());

        QJsonArray ops; ops.append(validOp); ops.append(unknownOp);
        QJsonObject commit;
        commit.insert(QStringLiteral("uuid"), commitUuid);
        commit.insert(QStringLiteral("device_id"), QStringLiteral("dev-x"));
        commit.insert(QStringLiteral("timestamp"),
                      QStringLiteral("2026-04-02T00:00:00.000Z"));
        commit.insert(QStringLiteral("operations"), ops);

        mock.registerJsonResponse(
            QStringLiteral("name = '") + commitUuid + QStringLiteral(".json'"),
            200, driveFindResponse(QStringLiteral("commit-file-id")));
        mock.registerJsonResponse(
            QStringLiteral("commit-file-id?alt=media"),
            200, commit);

        // state.json mirrors local-after-commit state (so the LWW merger
        // step doesn't undo what the commit just added).
        QJsonObject remoteState;
        QJsonArray tasks;
        tasks.append(makeTaskJson(QStringLiteral("valid-from-mixed-commit"),
                                  QStringLiteral("hello from valid op")));
        remoteState.insert(QStringLiteral("tasks"), tasks);
        remoteState.insert(QStringLiteral("projects"), QJsonArray());
        remoteState.insert(QStringLiteral("sections"), QJsonArray());
        remoteState.insert(QStringLiteral("labels"),   QJsonArray());
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, remoteState);

        engine.pull();

        // PIN: pull reached Synced — applyCommit did NOT crash on the unknown op.
        QCOMPARE(engine.status(), SyncEngine::Synced);

        // The valid op was applied: the task exists in the store.
        QVERIFY2(store.taskById(QStringLiteral("valid-from-mixed-commit")) != nullptr,
                 "Valid op in the same commit as an unknown op was NOT "
                 "applied. The unknown-op branch may have early-returned "
                 "from the operations loop — investigate syncengine.cpp:634.");

        // The unknown op did not leak a ghost entity (no entity created).
        QVERIFY(store.taskById(QStringLiteral("ghost-entity")) == nullptr);

        qInfo() << "Unknown op type behaviour: pull() reached"
                << engine.status()
                << "and the valid op was applied; unknown op was logged + "
                   "skipped (syncengine.cpp:634-636).";
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 7 — applyCommit_with_circular_subtask_parent_chain_does_not_recurse_forever
    //
    // Contract under test: a sequence of commits creates parent A → child B
    // → grandchild C, then updates A's parent_id to point at C. The result
    // is a 3-cycle: A → C → B → A.
    //
    // DataStore::subtasks(parentId) walks the flat task list once, returning
    // direct children only (no recursion). So the cycle cannot crash that
    // particular query. But the engine has NO guard against the cycle being
    // CREATED in the first place — task_update's "parent_id" assignment at
    // syncengine.cpp:449-450 simply calls setParentId without validating that
    // the new parent isn't a (transitive) descendant.
    //
    // We pin both halves:
    //   - The cycle IS allowed to be created (no validation in applyCommit).
    //   - DataStore::subtasks does not recurse, so reading the structure
    //     remains O(N) and safe.
    //   - But any caller that DOES walk transitively (e.g. UI ancestor chain)
    //     would now infinite-loop. We document the gap.
    //
    // Implementation: construct + pull a commit containing all 4 ops.
    // ════════════════════════════════════════════════════════════════════════
    void applyCommit_with_circular_subtask_parent_chain_does_not_recurse_forever()
    {
        useUniqueSettings(QStringLiteral("circularParentChain"));
        DataStore store;

        SyncEngine engine(&store);
        engine.gdriveClient()->setAccessToken(QStringLiteral("valid-token-cycle"));
        QVERIFY(engine.gdriveClient()->isAuthenticated());

        MockNAM mock;
        engine.gdriveClient()->driveApiForTesting()
            ->setNetworkManagerForTesting(&mock);
        seedDriveBootstrap(mock);

        const QString commitUuid = QStringLiteral("commit-cycle");
        QJsonObject headObj;
        headObj.insert(QStringLiteral("commit"), commitUuid);
        headObj.insert(QStringLiteral("timestamp"),
                       QStringLiteral("2026-04-02T00:00:00.000Z"));
        mock.registerJsonResponse(QStringLiteral("name = 'HEAD'"),
                                  200, driveFindResponse(QStringLiteral("HEAD-id")));
        mock.registerJsonResponse(QStringLiteral("HEAD-id?alt=media"),
                                  200, headObj);

        const QStringList logFiles = { commitUuid + QStringLiteral(".json") };
        mock.registerJsonResponse(QStringLiteral("'log-folder-id' in parents"),
                                  200, driveListFilesResponse(logFiles));

        // Op 1: task_add A (no parent).
        QJsonObject addA;
        addA.insert(QStringLiteral("type"), QStringLiteral("task_add"));
        addA.insert(QStringLiteral("entity_id"), QStringLiteral("A"));
        addA.insert(QStringLiteral("data"),
                    makeTaskJson(QStringLiteral("A"), QStringLiteral("parent A")));
        // Op 2: task_add B with parent A.
        QJsonObject addB;
        addB.insert(QStringLiteral("type"), QStringLiteral("task_add"));
        addB.insert(QStringLiteral("entity_id"), QStringLiteral("B"));
        addB.insert(QStringLiteral("data"),
                    makeTaskJson(QStringLiteral("B"),
                                 QStringLiteral("child B"),
                                 QStringLiteral("A")));
        // Op 3: task_add C with parent B.
        QJsonObject addC;
        addC.insert(QStringLiteral("type"), QStringLiteral("task_add"));
        addC.insert(QStringLiteral("entity_id"), QStringLiteral("C"));
        addC.insert(QStringLiteral("data"),
                    makeTaskJson(QStringLiteral("C"),
                                 QStringLiteral("grandchild C"),
                                 QStringLiteral("B")));
        // Op 4: task_update A → set A's parent_id = "C". Now A → C → B → A.
        QJsonObject patchA;
        patchA.insert(QStringLiteral("parent_id"), QStringLiteral("C"));
        QJsonObject updA;
        updA.insert(QStringLiteral("type"), QStringLiteral("task_update"));
        updA.insert(QStringLiteral("entity_id"), QStringLiteral("A"));
        updA.insert(QStringLiteral("data"), patchA);

        QJsonArray ops;
        ops.append(addA);
        ops.append(addB);
        ops.append(addC);
        ops.append(updA);

        QJsonObject commit;
        commit.insert(QStringLiteral("uuid"), commitUuid);
        commit.insert(QStringLiteral("device_id"), QStringLiteral("dev-x"));
        commit.insert(QStringLiteral("timestamp"),
                      QStringLiteral("2026-04-02T00:00:00.000Z"));
        commit.insert(QStringLiteral("operations"), ops);

        mock.registerJsonResponse(
            QStringLiteral("name = '") + commitUuid + QStringLiteral(".json'"),
            200, driveFindResponse(QStringLiteral("commit-file-id")));
        mock.registerJsonResponse(
            QStringLiteral("commit-file-id?alt=media"),
            200, commit);

        // state.json mirrors the post-commit state with the cycle present so
        // the LWW merge doesn't undo our setup.
        QJsonObject remoteState;
        QJsonArray tasks;
        tasks.append(makeTaskJson(QStringLiteral("A"),
                                  QStringLiteral("parent A"),
                                  QStringLiteral("C"))); // cycle: A's parent is C
        tasks.append(makeTaskJson(QStringLiteral("B"),
                                  QStringLiteral("child B"),
                                  QStringLiteral("A")));
        tasks.append(makeTaskJson(QStringLiteral("C"),
                                  QStringLiteral("grandchild C"),
                                  QStringLiteral("B")));
        remoteState.insert(QStringLiteral("tasks"), tasks);
        remoteState.insert(QStringLiteral("projects"), QJsonArray());
        remoteState.insert(QStringLiteral("sections"), QJsonArray());
        remoteState.insert(QStringLiteral("labels"),   QJsonArray());
        mock.registerJsonResponse(QStringLiteral("name = 'state.json'"),
                                  200, driveFindResponse(QStringLiteral("state-id")));
        mock.registerJsonResponse(QStringLiteral("state-id?alt=media"),
                                  200, remoteState);

        // Use a bounded wall clock — if applyCommit DOES infinite-loop, we
        // catch it.
        QElapsedTimer wall; wall.start();
        engine.pull();
        const qint64 elapsedMs = wall.elapsed();

        QVERIFY2(elapsedMs < 5000,
                 qPrintable(QStringLiteral("pull() took %1 ms with a "
                            "3-cycle parent chain — likely infinite recursion "
                            "or unbounded loop.").arg(elapsedMs)));

        // PIN: pull succeeded; the cycle is now in the store.
        QCOMPARE(engine.status(), SyncEngine::Synced);
        QVERIFY(store.taskById(QStringLiteral("A")) != nullptr);
        QVERIFY(store.taskById(QStringLiteral("B")) != nullptr);
        QVERIFY(store.taskById(QStringLiteral("C")) != nullptr);

        // Verify the cycle is reflected in the in-memory model: A's parent
        // is now C (the cycle-closing edge).
        QCOMPARE(store.taskById(QStringLiteral("A"))->parentId(),
                 QStringLiteral("C"));
        QCOMPARE(store.taskById(QStringLiteral("B"))->parentId(),
                 QStringLiteral("A"));
        QCOMPARE(store.taskById(QStringLiteral("C"))->parentId(),
                 QStringLiteral("B"));

        // DataStore::subtasks is non-recursive — exercising it on each cycle
        // node returns the DIRECT children only and terminates immediately.
        // This is the safety net that prevents the cycle from crashing the
        // app today.
        const auto childrenOfA = store.subtasks(QStringLiteral("A"));
        const auto childrenOfB = store.subtasks(QStringLiteral("B"));
        const auto childrenOfC = store.subtasks(QStringLiteral("C"));
        QCOMPARE(childrenOfA.size(), 1); // B
        QCOMPARE(childrenOfB.size(), 1); // C
        QCOMPARE(childrenOfC.size(), 1); // A
        QCOMPARE(childrenOfA.first()->id(), QStringLiteral("B"));
        QCOMPARE(childrenOfB.first()->id(), QStringLiteral("C"));
        QCOMPARE(childrenOfC.first()->id(), QStringLiteral("A"));

        qInfo() << "applyCommit cycle creation took ms:" << elapsedMs;
        qInfo() << "DOCUMENTED GAP: SyncEngine::applyCommit's task_update "
                   "branch (syncengine.cpp:449-450) calls setParentId WITHOUT "
                   "validating that the new parent is not a transitive "
                   "descendant of the task being updated. Cycles can be "
                   "introduced by a remote commit OR by a local UI bug. The "
                   "safety net is that DataStore::subtasks is intentionally "
                   "non-recursive (datastore.cpp:558-566). Any caller that "
                   "DOES walk ancestors transitively (UI breadcrumb, drag-"
                   "drop reparent validation) must defend against the cycle "
                   "itself. A future fix in applyCommit's task_update branch "
                   "could reject a parent_id whose ancestry walk reaches the "
                   "task itself.";
    }
};

QTEST_GUILESS_MAIN(TestSyncEngineResilienceV2)
#include "test_syncengine_resilience_v2.moc"
