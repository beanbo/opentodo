// ============================================================================
// test_synchttpserver.cpp
//
// Integration coverage for the self-hosted sync backend — SyncHttpServer +
// HttpRemote + LocalDirRemote — which previously had zero tests despite being
// security-sensitive networking code now exposed (behind a Tailscale Funnel) to
// the public internet.
//
// The server is started on an ephemeral loopback port and driven through the
// real HttpRemote client (same process, same event loop — HttpRemote::send
// spins a local QEventLoop that also pumps the server's QTcpServer). This
// exercises the wire protocol end to end: the bearer-token auth gate (now
// length-independent constant-time, checked at header-parse time), path
// traversal rejection via LocalDirRemote::resolve(), and the blob/list/lock
// round trips.
// ============================================================================

#include <QtTest>
#include <QDir>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpSocket>

#include "sync/synchttpserver.h"
#include "sync/httpremote.h"
#include "sync/localdirremote.h"

class TestSyncHttpServer : public QObject
{
    Q_OBJECT

    // A realistic 64-hex-char (256-bit) token, like the one the app generates.
    const QString TOKEN =
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    QString          m_dir;
    LocalDirRemote  *m_store  = nullptr;
    SyncHttpServer  *m_server = nullptr;
    quint16          m_port   = 0;

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_port); }

private slots:
    void initTestCase()
    {
        m_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + QStringLiteral("/opentodo_test_shs_%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(m_dir);
        m_store = new LocalDirRemote(m_dir);
        m_server = new SyncHttpServer(m_store, this);
        QVERIFY2(m_server->start(0, TOKEN), "server failed to bind an ephemeral loopback port");
        m_port = m_server->port();
        QVERIFY(m_port != 0);
    }

    void cleanupTestCase()
    {
        if (m_server) m_server->stop();
        delete m_store;
        QDir(m_dir).removeRecursively();
    }

    // ── blob upload → download round trip ──────────────────────────────────
    void uploadDownloadRoundTrip()
    {
        HttpRemote c;
        c.configure(baseUrl(), TOKEN);
        QVERIFY(c.isAuthenticated());

        const QByteArray payload = QByteArrayLiteral("{\"hello\":\"world\"}");
        QVERIFY(c.uploadFile(QStringLiteral("state.json"), payload));

        QByteArray got;
        QVERIFY(c.downloadFile(QStringLiteral("state.json"), got));
        QCOMPARE(got, payload);

        // 404 path: a missing file with notFoundIsOk=true is a clean "absent".
        QByteArray missing;
        QVERIFY(c.downloadFile(QStringLiteral("nope.json"), missing, /*notFoundIsOk=*/true));
        QVERIFY(missing.isEmpty());
    }

    // ── /list returns the folder's filenames ───────────────────────────────
    void listFiles()
    {
        HttpRemote c;
        c.configure(baseUrl(), TOKEN);
        QVERIFY(c.uploadFile(QStringLiteral("log/aaa.json"), QByteArrayLiteral("1")));
        QVERIFY(c.uploadFile(QStringLiteral("log/bbb.json"), QByteArrayLiteral("2")));

        const QStringList names = c.listFiles(QStringLiteral("log"));
        QVERIFY(names.contains(QStringLiteral("aaa.json")));
        QVERIFY(names.contains(QStringLiteral("bbb.json")));
    }

    // ── delete removes the blob ────────────────────────────────────────────
    void deleteFile()
    {
        HttpRemote c;
        c.configure(baseUrl(), TOKEN);
        QVERIFY(c.uploadFile(QStringLiteral("HEAD"), QByteArrayLiteral("abc")));
        QVERIFY(c.deleteFile(QStringLiteral("HEAD")));
        QByteArray got;
        QVERIFY(c.downloadFile(QStringLiteral("HEAD"), got, /*notFoundIsOk=*/true));
        QVERIFY(got.isEmpty());
    }

    // ── advisory lock acquire/release ──────────────────────────────────────
    void lockCycle()
    {
        HttpRemote c;
        c.configure(baseUrl(), TOKEN);
        QVERIFY(c.acquireLock());
        QVERIFY(c.releaseLock());
    }

    // ── auth gate: a wrong token is rejected (401 → operation fails) ────────
    void wrongTokenRejected()
    {
        HttpRemote c;
        c.configure(baseUrl(), QStringLiteral("totally-the-wrong-token"));
        QVERIFY(c.isAuthenticated());                 // it *has* a token, just wrong
        QVERIFY(!c.uploadFile(QStringLiteral("x.json"), QByteArrayLiteral("data")));
    }

    // ── auth gate: a same-length wrong token is rejected (constant-time
    //    compare folds the length check; a same-length mismatch must still 401)
    void sameLengthWrongTokenRejected()
    {
        QString wrong = TOKEN;
        wrong[0] = (wrong[0] == QLatin1Char('f')) ? QLatin1Char('e') : QLatin1Char('f');
        QCOMPARE(wrong.size(), TOKEN.size());
        QVERIFY(wrong != TOKEN);

        HttpRemote c;
        c.configure(baseUrl(), wrong);
        QVERIFY(!c.uploadFile(QStringLiteral("y.json"), QByteArrayLiteral("data")));
    }

    // ── an empty token never even sends a request ──────────────────────────
    void emptyTokenNotAuthenticated()
    {
        HttpRemote c;
        c.configure(baseUrl(), QString());
        QVERIFY(!c.isAuthenticated());
        QVERIFY(!c.uploadFile(QStringLiteral("z.json"), QByteArrayLiteral("data")));
    }

    // ── path traversal is refused by LocalDirRemote::resolve() (server 400) ─
    void pathTraversalRejected()
    {
        HttpRemote c;
        c.configure(baseUrl(), TOKEN);

        // Sanity: resolve() itself rejects the escape.
        QVERIFY(m_store->resolve(QStringLiteral("../escape.json")).isEmpty());

        // And over the wire the upload/download must fail (not silently escape).
        QVERIFY(!c.uploadFile(QStringLiteral("../escape.json"), QByteArrayLiteral("x")));
        QByteArray got;
        QVERIFY(!c.downloadFile(QStringLiteral("../escape.json"), got));

        // The file must NOT exist outside the store root.
        const QString parentEscape = QDir(m_dir).filePath(QStringLiteral("../escape.json"));
        QVERIFY(!QFileInfo::exists(parentEscape));
    }

    // ── S14: a client that never finishes its headers is dropped ────────────
    void slowHeadersConnectionIsClosed()
    {
        SyncHttpServer srv(m_store);
        srv.setConnectionLimits(/*header*/ 200, /*total*/ 2000, /*max*/ 8);
        QVERIFY(srv.start(0, TOKEN));

        QTcpSocket sock;
        sock.connectToHost(QHostAddress::LocalHost, srv.port());
        QVERIFY(sock.waitForConnected(2000));
        sock.write("GET /ping HTTP/1.1\r\nHost: x\r\n");   // header block never ends
        sock.flush();
        QTRY_COMPARE_WITH_TIMEOUT(sock.state(), QAbstractSocket::UnconnectedState, 3000);
        QTRY_COMPARE(srv.activeConnections(), 0);

        // A normal authenticated request is unaffected by the timers.
        HttpRemote c;
        c.configure(QStringLiteral("http://127.0.0.1:%1").arg(srv.port()), TOKEN);
        QVERIFY(c.uploadFile(QStringLiteral("s14.json"), QByteArrayLiteral("{}")));
    }

    // ── S14: concurrent connections beyond the cap are refused ──────────────
    void connectionCapDropsExtraConnections()
    {
        SyncHttpServer srv(m_store);
        srv.setConnectionLimits(/*header*/ 5000, /*total*/ 5000, /*max*/ 2);
        QVERIFY(srv.start(0, TOKEN));

        QTcpSocket a, b, extra;
        for (QTcpSocket *s : {&a, &b}) {
            s->connectToHost(QHostAddress::LocalHost, srv.port());
            QVERIFY(s->waitForConnected(2000));
        }
        QTRY_COMPARE(srv.activeConnections(), 2);

        extra.connectToHost(QHostAddress::LocalHost, srv.port());
        QVERIFY(extra.waitForConnected(2000));
        QTRY_COMPARE_WITH_TIMEOUT(extra.state(), QAbstractSocket::UnconnectedState, 3000);
        QCOMPARE(srv.activeConnections(), 2);
        QCOMPARE(a.state(), QAbstractSocket::ConnectedState);

        // Closing the held connections frees the slots again.
        a.abort();
        b.abort();
        QTRY_COMPARE(srv.activeConnections(), 0);
    }
};

QTEST_MAIN(TestSyncHttpServer)
#include "test_synchttpserver.moc"
