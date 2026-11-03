// Tests for the AI proxy selection (services/aiproxy.h), the proxy settings
// on AIAssistant, and the Anthropic request/response shape for models that
// think by default. No network: queryProxy() is a pure decision and the
// provider calls only build or parse JSON.

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyQuery>
#include <QRegularExpression>
#include <QSettings>

#include "services/aiassistant.h"
#include "services/aiproxy.h"
#include "services/llmprovider.h"
#include "storage/datastore.h"

namespace {

const char *const kProxyVars[] = {
    "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY",
    "all_proxy", "ALL_PROXY", "no_proxy", "NO_PROXY",
};

QNetworkProxyQuery queryFor(const char *url)
{
    return QNetworkProxyQuery(QUrl(QString::fromLatin1(url)));
}

QJsonObject anthropicBody(const QString &model, int maxTokens,
                          QNetworkRequest *reqOut = nullptr)
{
    AnthropicProvider p;
    const auto rd = p.buildRequest(QStringLiteral("sk-test"), model, QStringLiteral("sys"),
                                   { { QStringLiteral("user"), QStringLiteral("hi") } },
                                   maxTokens);
    if (reqOut) *reqOut = rd.request;
    return QJsonDocument::fromJson(rd.body).object();
}

}  // namespace

class TestAiProxy : public QObject
{
    Q_OBJECT

    QHash<QByteArray, QByteArray> m_savedEnv;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_AiProxy"));
        QCoreApplication::setApplicationName(QStringLiteral("test_aiproxy"));
        QSettings().clear();
        // The developer shell may export a proxy; every slot starts clean.
        for (const char *v : kProxyVars)
            if (qEnvironmentVariableIsSet(v))
                m_savedEnv.insert(v, qgetenv(v));
    }

    void init()
    {
        for (const char *v : kProxyVars)
            qunsetenv(v);
    }

    void cleanupTestCase()
    {
        QSettings().clear();
        for (const char *v : kProxyVars)
            qunsetenv(v);
        for (auto it = m_savedEnv.cbegin(); it != m_savedEnv.cend(); ++it)
            qputenv(it.key().constData(), it.value());
    }

    // ── parseProxyUrl ─────────────────────────────────────────────────────

    void parse_httpUrl()
    {
        QNetworkProxy p;
        QVERIFY(AiProxyFactory::parseProxyUrl(QStringLiteral("http://127.0.0.1:10809"), &p));
        QCOMPARE(p.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(p.hostName(), QStringLiteral("127.0.0.1"));
        QCOMPARE(p.port(), quint16(10809));
        QVERIFY(p.user().isEmpty());
    }

    void parse_bareHostPortIsHttp()
    {
        QNetworkProxy p;
        QVERIFY(AiProxyFactory::parseProxyUrl(QStringLiteral(" 10.0.0.5:3128 "), &p));
        QCOMPARE(p.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(p.hostName(), QStringLiteral("10.0.0.5"));
        QCOMPARE(p.port(), quint16(3128));
    }

    void parse_socks5WithCredentials()
    {
        QNetworkProxy p;
        QVERIFY(AiProxyFactory::parseProxyUrl(
            QStringLiteral("socks5h://bob:p%40ss@proxy.example.com:1080"), &p));
        QCOMPARE(p.type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(p.user(), QStringLiteral("bob"));
        QCOMPARE(p.password(), QStringLiteral("p@ss"));
    }

    void parse_rejectsHttpsMissingPortAndGarbage()
    {
        QString err;
        QVERIFY(!AiProxyFactory::parseProxyUrl(QStringLiteral("https://1.2.3.4:8443"), nullptr, &err));
        QVERIFY(err.contains(QStringLiteral("https://")));
        QVERIFY(!AiProxyFactory::parseProxyUrl(QStringLiteral("http://127.0.0.1"), nullptr, &err));
        QVERIFY(err.contains(QStringLiteral("порт")));
        QVERIFY(!AiProxyFactory::parseProxyUrl(QStringLiteral("ftp://h:21"), nullptr, &err));
        QVERIFY(!AiProxyFactory::parseProxyUrl(QString(), nullptr, &err));
        QVERIFY(!err.isEmpty());
    }

    // ── helpers ───────────────────────────────────────────────────────────

    void noProxy_matching()
    {
        const QString list = QStringLiteral("localhost, .local,example.com:443,::1");
        QVERIFY(AiProxyFactory::hostMatchesNoProxy(QStringLiteral("example.com"), list));
        QVERIFY(AiProxyFactory::hostMatchesNoProxy(QStringLiteral("api.EXAMPLE.com"), list));
        QVERIFY(AiProxyFactory::hostMatchesNoProxy(QStringLiteral("nas.local"), list));
        QVERIFY(AiProxyFactory::hostMatchesNoProxy(QStringLiteral("::1"), list));
        QVERIFY(!AiProxyFactory::hostMatchesNoProxy(QStringLiteral("notexample.com"), list));
        QVERIFY(!AiProxyFactory::hostMatchesNoProxy(QStringLiteral("api.anthropic.com"), list));
        QVERIFY(AiProxyFactory::hostMatchesNoProxy(QStringLiteral("anything"), QStringLiteral("*")));
        QVERIFY(!AiProxyFactory::hostMatchesNoProxy(QStringLiteral("anything"), QString()));
    }

    void localOrPrivateHosts()
    {
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("localhost")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("127.0.0.1")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("::1")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("192.168.31.10")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("172.20.1.1")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("10.99.0.2")));
        QVERIFY(isLocalOrPrivateHost(QStringLiteral("printer.local")));
        QVERIFY(!isLocalOrPrivateHost(QStringLiteral("172.32.0.1")));
        QVERIFY(!isLocalOrPrivateHost(QStringLiteral("api.anthropic.com")));
        QVERIFY(!isLocalOrPrivateHost(QString()));
    }

    void redactedUrl_hidesPassword()
    {
        const QString r = AiProxyFactory::redactedUrl(QStringLiteral("http://bob:secret@h:8080"));
        QVERIFY(!r.contains(QStringLiteral("secret")));
        QVERIFY(r.contains(QStringLiteral("bob")));
        QCOMPARE(AiProxyFactory::redactedUrl(QStringLiteral("http://127.0.0.1:10809")),
                 QStringLiteral("http://127.0.0.1:10809"));
    }

    // ── queryProxy ────────────────────────────────────────────────────────

    void manual_routesRemoteHostsThroughProxy()
    {
        AiProxyFactory f(AiProxyFactory::Mode::Manual, QStringLiteral("http://127.0.0.1:10809"));
        const auto list = f.queryProxy(queryFor("https://api.anthropic.com/v1/messages"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().type(), QNetworkProxy::HttpProxy);
        QCOMPARE(list.first().port(), quint16(10809));
        QVERIFY(f.describeRoute(QUrl(QStringLiteral("https://api.anthropic.com")))
                    .contains(QStringLiteral("127.0.0.1:10809")));
    }

    void localHosts_bypassInEveryMode()
    {
        for (auto mode : { AiProxyFactory::Mode::Auto, AiProxyFactory::Mode::Manual }) {
            AiProxyFactory f(mode, QStringLiteral("http://127.0.0.1:10809"));
            const auto list = f.queryProxy(queryFor("http://localhost:11434/v1/chat/completions"));
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().type(), QNetworkProxy::NoProxy);
        }
    }

    void none_isDirect()
    {
        qputenv("HTTPS_PROXY", "http://127.0.0.1:10809");
        AiProxyFactory f(AiProxyFactory::Mode::None, QString());
        const auto list = f.queryProxy(queryFor("https://api.anthropic.com/v1/messages"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().type(), QNetworkProxy::NoProxy);
    }

    void manual_invalidUrlFailsClosed()
    {
        AiProxyFactory f(AiProxyFactory::Mode::Manual, QStringLiteral("https://1.2.3.4:8443"));
        QVERIFY(f.queryProxy(queryFor("https://api.anthropic.com/v1/messages")).isEmpty());
        QVERIFY(f.describeRoute(QUrl(QStringLiteral("https://api.anthropic.com")))
                    .startsWith(QStringLiteral("ошибка")));
    }

    void auto_usesEnvironmentProxy()
    {
        qputenv("HTTPS_PROXY", "http://127.0.0.1:10809");
        AiProxyFactory f(AiProxyFactory::Mode::Auto, QString());
        const auto list = f.queryProxy(queryFor("https://api.anthropic.com/v1/messages"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().type(), QNetworkProxy::HttpProxy);
        QCOMPARE(list.first().hostName(), QStringLiteral("127.0.0.1"));
        // Windows env names are case-insensitive, so the lookup may report
        // the lowercase spelling for a variable set as HTTPS_PROXY.
        QVERIFY(f.describeRoute(QUrl(QStringLiteral("https://api.anthropic.com")))
                    .contains(QStringLiteral("https_proxy"), Qt::CaseInsensitive));
    }

    void auto_allProxyFallbackAndNoProxy()
    {
        qputenv("ALL_PROXY", "socks5://127.0.0.1:10808");
        AiProxyFactory f(AiProxyFactory::Mode::Auto, QString());
        auto list = f.queryProxy(queryFor("https://generativelanguage.googleapis.com/v1beta/x"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().type(), QNetworkProxy::Socks5Proxy);

        qputenv("NO_PROXY", ".googleapis.com");
        list = f.queryProxy(queryFor("https://generativelanguage.googleapis.com/v1beta/x"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().type(), QNetworkProxy::NoProxy);
    }

    // ── AIAssistant settings ──────────────────────────────────────────────

    void assistant_persistsProxySettings()
    {
        QSettings().clear();
        DataStore store;
        {
            AIAssistant ai(&store);
            QCOMPARE(ai.proxyMode(), QStringLiteral("auto"));
            QSignalSpy spy(&ai, &AIAssistant::proxyChanged);
            ai.setProxyMode(QStringLiteral("manual"));
            ai.setProxyUrl(QStringLiteral("  http://127.0.0.1:10809 "));
            QCOMPARE(spy.count(), 2);
            QVERIFY(ai.proxyStatus().contains(QStringLiteral("127.0.0.1:10809")));
            ai.setProxyMode(QStringLiteral("bogus"));  // normalised to auto
            QCOMPARE(ai.proxyMode(), QStringLiteral("auto"));
            ai.setProxyMode(QStringLiteral("manual"));
        }
        AIAssistant reloaded(&store);
        QCOMPARE(reloaded.proxyMode(), QStringLiteral("manual"));
        QCOMPARE(reloaded.proxyUrl(), QStringLiteral("http://127.0.0.1:10809"));
        QVERIFY(reloaded.validateProxyUrl(QStringLiteral("http://127.0.0.1:10809")).isEmpty());
        QVERIFY(!reloaded.validateProxyUrl(QStringLiteral("https://x:1")).isEmpty());
        QSettings().clear();
    }

    // ── Anthropic request shape ───────────────────────────────────────────

    void anthropic_thinkingModelsGetEffortHeadroomAndFallback()
    {
        QNetworkRequest req;
        const QJsonObject body = anthropicBody(QStringLiteral("claude-opus-5-5"), 8, &req);
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        QVERIFY(body.value(QStringLiteral("max_tokens")).toInt() > 1000);
        QCOMPARE(body.value(QStringLiteral("fallbacks")).toString(), QStringLiteral("default"));
        QCOMPARE(req.rawHeader("anthropic-beta"), QByteArray("server-side-fallback-2026-07-01"));
        QVERIFY(!body.contains(QStringLiteral("thinking")));  // 400 on Opus 5.5 if disabled
    }

    void anthropic_haiku55HasNoFallback()
    {
        QNetworkRequest req;
        const QJsonObject body = anthropicBody(QStringLiteral("claude-haiku-5-5"), 8, &req);
        QVERIFY(body.contains(QStringLiteral("output_config")));
        QVERIFY(!body.contains(QStringLiteral("fallbacks")));
        QVERIFY(req.rawHeader("anthropic-beta").isEmpty());
    }

    void anthropic_olderModelsUnchanged()
    {
        for (const char *m : { "claude-opus-4-7", "claude-haiku-4-5", "claude-sonnet-4-6",
                               "claude-3-5-haiku-20241022" }) {
            QNetworkRequest req;
            const QJsonObject body = anthropicBody(QString::fromLatin1(m), 8, &req);
            QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 8);
            QVERIFY2(!body.contains(QStringLiteral("output_config")), m);
            QVERIFY2(!body.contains(QStringLiteral("fallbacks")), m);
            QVERIFY(req.rawHeader("anthropic-beta").isEmpty());
        }
    }

    void anthropic_workspaceHeader()
    {
        AnthropicProvider p;
        auto rd = p.buildRequest(QStringLiteral("k"), QStringLiteral("claude-opus-4-7"), {}, {}, 8);
        QVERIFY(!rd.request.hasRawHeader("anthropic-workspace-id"));
        p.setWorkspaceId(QStringLiteral(" wrkspc_01ABC "));
        rd = p.buildRequest(QStringLiteral("k"), QStringLiteral("claude-opus-4-7"), {}, {}, 8);
        QCOMPARE(rd.request.rawHeader("anthropic-workspace-id"), QByteArray("wrkspc_01ABC"));

        QString err;
        p.parseResponse(R"({"type":"error","error":{"type":"invalid_request_error","message":"anthropic-workspace-id is required when authenticating with an identity-linked API key; send the id of the workspace this request acts in."}})",
                        400, &err);
        QVERIFY(err.contains(QStringLiteral("Workspace ID")));
    }

    void assistant_persistsWorkspaceId()
    {
        QSettings().clear();
        DataStore store;
        {
            AIAssistant ai(&store);
            QSignalSpy spy(&ai, &AIAssistant::anthropicWorkspaceIdChanged);
            ai.setAnthropicWorkspaceId(QStringLiteral("wrkspc_01XYZ"));
            QCOMPARE(spy.count(), 1);
        }
        AIAssistant reloaded(&store);
        QCOMPARE(reloaded.anthropicWorkspaceId(), QStringLiteral("wrkspc_01XYZ"));
        reloaded.setAnthropicWorkspaceId(QString());
        QVERIFY(reloaded.anthropicWorkspaceId().isEmpty());
        QSettings().clear();
    }

    void anthropic_defaultModelIsCurrent()
    {
        AnthropicProvider p;
        QCOMPARE(p.defaultModel(), QStringLiteral("claude-opus-5-5"));
        QVERIFY(p.suggestedModels().contains(p.defaultModel()));
    }

    // ── Anthropic response parsing ────────────────────────────────────────

    void anthropic_parse_skipsThinkingAndFallbackPartial()
    {
        AnthropicProvider p;
        const QByteArray raw = R"({"content":[
            {"type":"thinking","thinking":""},
            {"type":"text","text":"partial from declined model"},
            {"type":"fallback","from":{"model":"claude-opus-5-5"},"to":{"model":"claude-opus-4-8"}},
            {"type":"text","text":"Готово"}],"stop_reason":"end_turn"})";
        QString err;
        QCOMPARE(p.parseResponse(raw, 200, &err), QStringLiteral("Готово"));
        QVERIFY(err.isEmpty());
    }

    void anthropic_parse_refusalAndTruncation()
    {
        AnthropicProvider p;
        QString err;
        QVERIFY(p.parseResponse(R"({"content":[],"stop_reason":"refusal"})", 200, &err).isEmpty());
        QVERIFY(err.contains(QStringLiteral("отказался")));

        err.clear();
        QVERIFY(p.parseResponse(R"({"content":[{"type":"thinking","thinking":""}],"stop_reason":"max_tokens"})",
                                200, &err).isEmpty());
        QVERIFY(err.contains(QStringLiteral("лимиту")));
    }

    void anthropic_parse_geoBlockHint()
    {
        AnthropicProvider p;
        QString err;
        p.parseResponse(R"({"error":{"type":"forbidden","message":"Request not allowed"}})", 403, &err);
        QVERIFY(err.startsWith(QStringLiteral("Request not allowed")));
        QVERIFY(err.contains(QStringLiteral("прокси")));
    }

    // ── OpenAI-compatible base URL: fails closed ──────────────────────────
    //
    // A malformed / scheme-less / public-cleartext base URL must never be
    // "corrected" to api.openai.com (that would ship an OpenRouter / Groq /
    // self-hosted key to a third party) nor send the key in the clear.

    void openaiCompat_emptyBaseUrlUsesOpenAi()
    {
        OpenAICompatibleProvider p;
        QCOMPARE(p.effectiveBaseUrl(), QStringLiteral("https://api.openai.com"));
        const auto rd = p.buildRequest(QStringLiteral("sk-x"), QStringLiteral("gpt-4o-mini"),
                                       QStringLiteral("sys"),
                                       { { QStringLiteral("user"), QStringLiteral("hi") } }, 16);
        QCOMPARE(rd.request.url(),
                 QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions")));
        QCOMPARE(rd.request.rawHeader("Authorization"), QByteArray("Bearer sk-x"));
    }

    void openaiCompat_validHttpsBaseUrlIsUsed()
    {
        OpenAICompatibleProvider p;
        p.setBaseUrl(QStringLiteral(" https://openrouter.ai/api/ "));
        QCOMPARE(p.effectiveBaseUrl(), QStringLiteral("https://openrouter.ai/api"));
        const auto rd = p.buildRequest(QStringLiteral("sk-or"), QStringLiteral("m"), QString(),
                                       { { QStringLiteral("user"), QStringLiteral("hi") } }, 16);
        QCOMPARE(rd.request.url(),
                 QUrl(QStringLiteral("https://openrouter.ai/api/v1/chat/completions")));
        QCOMPARE(rd.request.rawHeader("Authorization"), QByteArray("Bearer sk-or"));
    }

    void openaiCompat_rejectedBaseUrlFailsClosed_data()
    {
        QTest::addColumn<QString>("baseUrl");
        QTest::newRow("scheme-less")  << QStringLiteral("api.example.com");
        QTest::newRow("host:port")    << QStringLiteral("localhost:11434");
        QTest::newRow("ftp")          << QStringLiteral("ftp://api.example.com");
        QTest::newRow("file")         << QStringLiteral("file:///etc/passwd");
        QTest::newRow("public http")  << QStringLiteral("http://api.example.com");
        QTest::newRow("public http ip") << QStringLiteral("http://8.8.8.8:8080");
        QTest::newRow("no host")      << QStringLiteral("https://");
    }

    void openaiCompat_rejectedBaseUrlFailsClosed()
    {
        QFETCH(QString, baseUrl);
        OpenAICompatibleProvider p;
        p.setBaseUrl(baseUrl);

        // effectiveBaseUrl() warns once per call (here, buildRequest and
        // parseResponse) — expected, keep the log clean.
        const QRegularExpression rejectMsg(QStringLiteral("OpenAICompatibleProvider: rejecting"));
        for (int i = 0; i < 3; ++i)
            QTest::ignoreMessage(QtWarningMsg, rejectMsg);

        QCOMPARE(p.effectiveBaseUrl(), QString());

        const auto rd = p.buildRequest(QStringLiteral("sk-secret"), QStringLiteral("m"),
                                       QStringLiteral("sys"),
                                       { { QStringLiteral("user"), QStringLiteral("hi") } }, 16);
        QVERIFY(rd.request.url().isEmpty());
        QVERIFY(!rd.request.hasRawHeader("Authorization"));
        for (const QByteArray &h : rd.request.rawHeaderList())
            QVERIFY(!rd.request.rawHeader(h).contains("sk-secret"));

        // Even a well-formed body is reported as the base-URL error, so the
        // user sees why nothing was sent.
        QString err;
        QVERIFY(p.parseResponse(R"({"choices":[{"message":{"content":"hi"}}]})", 200, &err)
                    .isEmpty());
        QVERIFY2(err.contains(QStringLiteral("Base URL")), qPrintable(err));
    }

    void openaiCompat_localCleartextAllowed_data()
    {
        QTest::addColumn<QString>("baseUrl");
        QTest::newRow("localhost")  << QStringLiteral("http://localhost:11434");
        QTest::newRow("loopback")   << QStringLiteral("http://127.0.0.1:1234");
        QTest::newRow("lan 192")    << QStringLiteral("http://192.168.1.10:8080");
        QTest::newRow("lan 10")     << QStringLiteral("http://10.0.0.2");
        QTest::newRow("mdns")       << QStringLiteral("http://studio.local:1234");
    }

    void openaiCompat_localCleartextAllowed()
    {
        QFETCH(QString, baseUrl);
        OpenAICompatibleProvider p;
        p.setBaseUrl(baseUrl + QLatin1Char('/'));
        QCOMPARE(p.effectiveBaseUrl(), baseUrl);
        const auto rd = p.buildRequest(QStringLiteral("sk-local"), QStringLiteral("m"), QString(),
                                       { { QStringLiteral("user"), QStringLiteral("hi") } }, 16);
        QCOMPARE(rd.request.url(), QUrl(baseUrl + QStringLiteral("/v1/chat/completions")));
        QCOMPARE(rd.request.rawHeader("Authorization"), QByteArray("Bearer sk-local"));

        QString err;
        QCOMPARE(p.parseResponse(R"({"choices":[{"message":{"content":"hi"}}]})", 200, &err),
                 QStringLiteral("hi"));
        QVERIFY(err.isEmpty());
    }
};

QTEST_MAIN(TestAiProxy)
#include "test_aiproxy.moc"
