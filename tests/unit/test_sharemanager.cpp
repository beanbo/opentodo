// ============================================================================
// test_sharemanager.cpp
//
// Unit tests for the ShareManager + uploadPublicFile path on DriveApi.
//
// Reuses the MockNAM pattern from test_gdrive_resilience.cpp: a file-scope
// QNetworkAccessManager subclass intercepts every outbound HTTP call, returns
// a per-URL canned response, and records every call so the test can assert
// that the expected three-call dance happened (multipart upload → permission
// → webViewLink fallback).
//
// Slot coverage:
//   1. shareProject — happy path. Three Drive calls, link emitted, no error.
//   2. shareProject — upload step fails (5xx) → shareError, no link emitted.
//   3. shareProject — permission step fails → rolled back via DELETE,
//      shareError emitted.
//   4. shareTasks  — happy path with two task ids.
//   5. shareProject — unauthenticated client → shareError without any HTTP.
//   6. shareProject — unknown projectId → shareError without any HTTP.
//   7. revokeShare — happy path → shareRevoked emitted, DELETE was called.
//   8. shareProject — webViewLink absent from upload reply → fallback URL is
//      built from the file id.
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QBuffer>
#include <QTimer>
#include <QStandardPaths>

#include "sync/gdrive.h"
#include "sync/drive_api.h"
#include "sync/oauth_client.h"
#include "sync/sharemanager.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

// ────────────────────────────────────────────────────────────────────────────
// FakeReply + MockNAM — same shape as test_gdrive_resilience.cpp
// ────────────────────────────────────────────────────────────────────────────

struct CannedResponse {
    int statusCode = 200;
    QByteArray body = QByteArrayLiteral("{}");
    bool forceNetworkError = false;
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

    void registerResponse(const QString &urlSubstring, int status,
                          const QByteArray &body)
    {
        CannedResponse r; r.statusCode = status; r.body = body;
        responses.append({urlSubstring, r});
    }
    void registerJsonResponse(const QString &urlSubstring, int status,
                              const QJsonObject &obj)
    {
        registerResponse(urlSubstring, status,
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
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

protected:
    QNetworkReply *createRequest(Operation op,
                                 const QNetworkRequest &req,
                                 QIODevice *outgoingData) override
    {
        OutboundCall call;
        call.op   = op;
        call.url  = req.url();
        call.body = outgoingData ? outgoingData->readAll() : QByteArray();
        if (op == CustomOperation)       call.method = "CUSTOM";
        else if (op == PostOperation)    call.method = "POST";
        else if (op == GetOperation)     call.method = "GET";
        else if (op == DeleteOperation)  call.method = "DELETE";
        else if (op == PutOperation)     call.method = "PUT";
        calls.append(call);

        const CannedResponse r = responseFor(req.url());

        QNetworkReply::NetworkError err = QNetworkReply::NoError;
        if (r.forceNetworkError) {
            err = QNetworkReply::UnknownNetworkError;
        } else if (r.statusCode == 401) {
            err = QNetworkReply::AuthenticationRequiredError;
        } else if (r.statusCode == 403) {
            err = QNetworkReply::ContentAccessDenied;
        } else if (r.statusCode == 404) {
            err = QNetworkReply::ContentNotFoundError;
        } else if (r.statusCode >= 500 && r.statusCode < 600) {
            err = QNetworkReply::InternalServerError;
        } else if (r.statusCode >= 400) {
            err = QNetworkReply::UnknownContentError;
        }
        return new FakeReply(this, req, op, r.statusCode, r.body, err);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

namespace {

QJsonObject driveFileCreatedReply(const QString &id, const QString &webViewLink)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    if (!webViewLink.isEmpty())
        obj.insert(QStringLiteral("webViewLink"), webViewLink);
    return obj;
}

QJsonObject drivePermissionCreatedReply(const QString &permId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"),   permId);
    obj.insert(QStringLiteral("role"), QStringLiteral("reader"));
    obj.insert(QStringLiteral("type"), QStringLiteral("anyone"));
    return obj;
}

void useUniqueSettings(const QString &slot)
{
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_ShareManager"));
    QCoreApplication::setApplicationName(QStringLiteral("test_sharemanager_") + slot);
    QSettings s;
    s.clear();
    s.sync();
    s.setValue(QStringLiteral("sync/device_id"),
               QStringLiteral("test-device-share"));
    s.sync();
}

int countUrlContaining(const QList<MockNAM::OutboundCall> &calls,
                       const QString &needle,
                       QNetworkAccessManager::Operation op)
{
    int n = 0;
    for (const auto &c : calls) {
        if (c.op != op) continue;
        if (c.url.toString().contains(needle))
            ++n;
    }
    return n;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// Test class
// ────────────────────────────────────────────────────────────────────────────

class TestShareManager : public QObject
{
    Q_OBJECT

private:
    bool m_prevTestMode = false;

private slots:
    void initTestCase()
    {
        m_prevTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(m_prevTestMode);
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 1 — shareProject happy path
    //
    // Verifies the three-call dance:
    //   POST /upload/drive/v3/files?uploadType=multipart  (file create)
    //   POST /drive/v3/files/<id>/permissions             (anonymous reader)
    //   (webViewLink came back in step 1 — no GET needed)
    //
    // and that shareLinkReady fires with the URL + file id.
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_happyPath()
    {
        useUniqueSettings(QStringLiteral("happyPath"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Home"));
        QVERIFY(p);
        Task *t1 = store.addTask(QStringLiteral("Buy milk"), p->id());
        Task *t2 = store.addTask(QStringLiteral("Pay rent"), p->id());
        QVERIFY(t1 && t2);

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token-share"));
        QVERIFY(client.isAuthenticated());

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        const QString fakeFileId = QStringLiteral("drive-file-abc123");
        const QString fakeUrl    = QStringLiteral("https://drive.google.com/file/d/")
                                  + fakeFileId
                                  + QStringLiteral("/view?usp=drivesdk");

        mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                                  200, driveFileCreatedReply(fakeFileId, fakeUrl));
        mock.registerJsonResponse(QStringLiteral("/files/") + fakeFileId + QStringLiteral("/permissions"),
                                  200, drivePermissionCreatedReply(QStringLiteral("perm-1")));

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareProject(p->id());

        QCOMPARE(errSpy.count(), 0);
        QCOMPARE(linkSpy.count(), 1);
        const auto args = linkSpy.first();
        QCOMPARE(args.at(0).toString(), fakeUrl);
        QCOMPARE(args.at(1).toString(), fakeFileId);

        // HTTP call accounting
        const int uploads = countUrlContaining(mock.calls,
                                               QStringLiteral("/upload/drive/v3/files"),
                                               QNetworkAccessManager::PostOperation);
        const int perms = countUrlContaining(mock.calls,
                                             QStringLiteral("/files/") + fakeFileId
                                                 + QStringLiteral("/permissions"),
                                             QNetworkAccessManager::PostOperation);
        QCOMPARE(uploads, 1);
        QCOMPARE(perms, 1);

        // Body of the upload should contain the snapshot payload — the task
        // content strings are the easiest evidence.
        bool foundContent = false;
        for (const auto &c : mock.calls) {
            if (!c.url.toString().contains(QLatin1String("/upload/drive/v3/files"))) continue;
            if (c.body.contains("Buy milk") && c.body.contains("Pay rent")) {
                foundContent = true; break;
            }
        }
        QVERIFY2(foundContent,
                 "uploadPublicFile body must include the project task contents");
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 2 — upload step fails (5xx) → shareError, no link
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_uploadFails()
    {
        useUniqueSettings(QStringLiteral("uploadFails"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Work"));
        store.addTask(QStringLiteral("Write doc"), p->id());

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);
        // 503 on the upload — no fileId comes back, so the permission call
        // should never be issued.
        mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                                  503, QJsonObject{});

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareProject(p->id());

        QCOMPARE(linkSpy.count(), 0);
        QCOMPARE(errSpy.count(), 1);

        // Verify the second call (permissions) was NEVER attempted.
        const int perms = countUrlContaining(mock.calls,
                                             QStringLiteral("/permissions"),
                                             QNetworkAccessManager::PostOperation);
        QCOMPARE(perms, 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 3 — permission step fails → upload is rolled back via DELETE
    //
    // Background: production code in uploadPublicFile catches a failed
    // permission POST and issues deletePublicFileById to avoid leaving the
    // file in the user's Drive root half-shared. The test asserts that the
    // rollback DELETE was emitted and that shareError fired.
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_permissionFails_rollback()
    {
        useUniqueSettings(QStringLiteral("permFails"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Garden"));
        store.addTask(QStringLiteral("Water tomatoes"), p->id());

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        const QString fakeFileId = QStringLiteral("drive-file-perm-fail");
        mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                                  200, driveFileCreatedReply(fakeFileId,
                                                             QStringLiteral("https://x/view")));
        // Permission step fails — server returns 403.
        mock.registerJsonResponse(QStringLiteral("/permissions"), 403, QJsonObject{});

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareProject(p->id());

        QCOMPARE(linkSpy.count(), 0);
        QCOMPARE(errSpy.count(), 1);

        // The rollback DELETE on the file id must have been issued.
        const int deletes = countUrlContaining(mock.calls,
                                               QStringLiteral("/files/") + fakeFileId,
                                               QNetworkAccessManager::DeleteOperation);
        QVERIFY2(deletes >= 1, "Permission failure must roll back via DELETE");
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 4 — shareTasks happy path with two task ids
    // ════════════════════════════════════════════════════════════════════════
    void shareTasks_happyPath()
    {
        useUniqueSettings(QStringLiteral("shareTasks"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Errands"));
        Task *a = store.addTask(QStringLiteral("Mail letter"), p->id());
        Task *b = store.addTask(QStringLiteral("Pick up dry cleaning"), p->id());
        Task *c = store.addTask(QStringLiteral("Should not be included"), p->id());
        Q_UNUSED(c)

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        const QString fakeFileId = QStringLiteral("drive-tasks-id");
        const QString fakeUrl    = QStringLiteral("https://drive.google.com/file/d/")
                                  + fakeFileId + QStringLiteral("/view");
        mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                                  200, driveFileCreatedReply(fakeFileId, fakeUrl));
        mock.registerJsonResponse(QStringLiteral("/permissions"),
                                  200, drivePermissionCreatedReply(QStringLiteral("perm-2")));

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareTasks({a->id(), b->id()});

        QCOMPARE(errSpy.count(), 0);
        QCOMPARE(linkSpy.count(), 1);
        QCOMPARE(linkSpy.first().at(0).toString(), fakeUrl);

        bool aFound = false, bFound = false, cFound = false;
        for (const auto &call : mock.calls) {
            if (!call.url.toString().contains(QLatin1String("/upload/drive/v3/files"))) continue;
            if (call.body.contains("Mail letter"))               aFound = true;
            if (call.body.contains("Pick up dry cleaning"))      bFound = true;
            if (call.body.contains("Should not be included"))    cFound = true;
        }
        QVERIFY(aFound);
        QVERIFY(bFound);
        QVERIFY2(!cFound, "Non-selected task must NOT be included in the snapshot");
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 5 — unauthenticated client → shareError, no HTTP attempts
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_unauthenticated_skipsHttp()
    {
        useUniqueSettings(QStringLiteral("noauth"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Anything"));

        GDriveClient client;       // no setAccessToken
        QVERIFY(!client.isAuthenticated());

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareProject(p->id());

        QCOMPARE(linkSpy.count(), 0);
        QCOMPARE(errSpy.count(), 1);
        QCOMPARE(mock.calls.size(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 6 — unknown projectId → shareError, no HTTP attempts
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_unknownProject_skipsHttp()
    {
        useUniqueSettings(QStringLiteral("unknownProject"));

        DataStore store;

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.shareProject(QStringLiteral("nope-does-not-exist"));

        QCOMPARE(linkSpy.count(), 0);
        QCOMPARE(errSpy.count(), 1);
        QCOMPARE(mock.calls.size(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 7 — revokeShare happy path
    // ════════════════════════════════════════════════════════════════════════
    void revokeShare_happyPath()
    {
        useUniqueSettings(QStringLiteral("revoke"));

        DataStore store;
        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        const QString fakeFileId = QStringLiteral("file-to-revoke");
        // DELETE returns 204 No Content per Drive API spec; treat as success.
        mock.registerJsonResponse(QStringLiteral("/files/") + fakeFileId,
                                  204, QJsonObject{});

        ShareManager mgr(&store, &client);
        QSignalSpy revokedSpy(&mgr, &ShareManager::shareRevoked);
        QSignalSpy errSpy(&mgr, &ShareManager::shareError);

        mgr.revokeShare(fakeFileId);

        QCOMPARE(errSpy.count(), 0);
        QCOMPARE(revokedSpy.count(), 1);
        QCOMPARE(revokedSpy.first().at(0).toString(), fakeFileId);

        const int deletes = countUrlContaining(mock.calls,
                                               QStringLiteral("/files/") + fakeFileId,
                                               QNetworkAccessManager::DeleteOperation);
        QCOMPARE(deletes, 1);
    }

    // ════════════════════════════════════════════════════════════════════════
    // SLOT 8 — webViewLink absent from upload reply → fallback URL is built
    //
    // Background: in some edge cases (custom MIME types, certain account
    // configurations) Drive does not return webViewLink in the upload reply
    // and the follow-up GET also omits it. uploadPublicFile in that case
    // falls back to a deterministic /file/d/<id>/view URL. The test verifies
    // the fallback path produces a working URL — not an empty string that
    // would silently break the UI.
    // ════════════════════════════════════════════════════════════════════════
    void shareProject_fallbackUrl()
    {
        useUniqueSettings(QStringLiteral("fallbackUrl"));

        DataStore store;
        Project *p = store.addProject(QStringLiteral("Fallback project"));
        store.addTask(QStringLiteral("Some task"), p->id());

        GDriveClient client;
        client.setAccessToken(QStringLiteral("valid-token"));

        MockNAM mock;
        client.driveApiForTesting()->setNetworkManagerForTesting(&mock);

        const QString fakeFileId = QStringLiteral("file-without-link");
        // Upload returns id but NO webViewLink.
        mock.registerJsonResponse(QStringLiteral("/upload/drive/v3/files"),
                                  200, driveFileCreatedReply(fakeFileId, QString()));
        mock.registerJsonResponse(QStringLiteral("/permissions"),
                                  200, drivePermissionCreatedReply(QStringLiteral("perm")));
        // Follow-up GET also returns nothing useful — forces the deterministic
        // /file/d/<id>/view fallback.
        QJsonObject emptyReply;
        emptyReply.insert(QStringLiteral("id"), fakeFileId);
        mock.registerJsonResponse(QStringLiteral("?fields=webViewLink"),
                                  200, emptyReply);

        ShareManager mgr(&store, &client);
        QSignalSpy linkSpy(&mgr, &ShareManager::shareLinkReady);

        mgr.shareProject(p->id());

        QCOMPARE(linkSpy.count(), 1);
        const QString url = linkSpy.first().at(0).toString();
        QVERIFY2(!url.isEmpty(), "Fallback URL must not be empty");
        QVERIFY2(url.contains(fakeFileId),
                 qPrintable(QStringLiteral("Fallback URL must contain file id: %1").arg(url)));
        QVERIFY2(url.contains(QLatin1String("drive.google.com")),
                 qPrintable(QStringLiteral("Fallback URL must point at drive.google.com: %1").arg(url)));
    }
};

QTEST_MAIN(TestShareManager)
#include "test_sharemanager.moc"
