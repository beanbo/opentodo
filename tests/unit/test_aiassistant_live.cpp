// Live end-to-end check of the AI path against the real provider API:
// SecretStore (OS keyring) → AIAssistant → LlmProvider → QNetworkAccessManager
// with the AI proxy factory → network.
//
// Skipped unless OPENTODO_LIVE_AI=1, so plain ctest runs stay offline. A run
// spends a fraction of a cent of real API credit. It uses the API key the app
// stored in the OS keyring for the selected provider (Anthropic by default)
// and never prints it. Settings live in a separate test QSettings scope; the
// keyring is only read.
//
//   OPENTODO_LIVE_AI=1                     required
//   OPENTODO_LIVE_AI_PROXY=<mode or url>   "auto" (default), "none", or a URL
//                                          such as http://127.0.0.1:10809
//   OPENTODO_LIVE_AI_MODEL=<model id>      optional, else the provider default
//   OPENTODO_LIVE_AI_WORKSPACE=<id|auto>   anthropic-workspace-id for keys not
//                                          scoped to a workspace; "auto" looks
//                                          it up and takes the Default Workspace
//   OPENTODO_LIVE_AI_EXPECT_FAIL=1         assert the request FAILS (e.g. to
//                                          show a direct connection is blocked)

#include <QtTest>
#include <QSettings>

#include "services/aiassistant.h"
#include "services/secretstore.h"
#include "storage/datastore.h"

class TestAIAssistantLive : public QObject
{
    Q_OBJECT

    DataStore m_store;
    SecretStore m_secrets;
    AIAssistant *m_ai = nullptr;
    bool m_expectFail = false;

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("OPENTODO_LIVE_AI") != QLatin1String("1"))
            QSKIP("Live API test: set OPENTODO_LIVE_AI=1 to run it.");

        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_AILive"));
        QCoreApplication::setApplicationName(QStringLiteral("test_aiassistant_live"));
        QSettings().clear();
        m_expectFail = qEnvironmentVariable("OPENTODO_LIVE_AI_EXPECT_FAIL") == QLatin1String("1");

        m_ai = new AIAssistant(&m_store, this);

        // Wait for the keyring warm-up read of the current provider's key.
        QSignalSpy keySpy(&m_secrets, &SecretStore::secretRetrieved);
        QSignalSpy keyErr(&m_secrets, &SecretStore::secretError);
        m_ai->setSecretStore(&m_secrets);
        QTRY_VERIFY_WITH_TIMEOUT(m_ai->isConfigured() || keyErr.count() > 0, 10000);
        if (!m_ai->isConfigured())
            QSKIP("No API key for the current provider in the OS keyring.");

        const QString proxy = qEnvironmentVariable("OPENTODO_LIVE_AI_PROXY").trimmed();
        if (proxy.isEmpty() || proxy == QLatin1String("auto")) {
            m_ai->setProxyMode(QStringLiteral("auto"));
        } else if (proxy == QLatin1String("none")) {
            m_ai->setProxyMode(QStringLiteral("none"));
        } else {
            m_ai->setProxyMode(QStringLiteral("manual"));
            m_ai->setProxyUrl(proxy);
        }
        const QString model = qEnvironmentVariable("OPENTODO_LIVE_AI_MODEL").trimmed();
        if (!model.isEmpty())
            m_ai->setCurrentModel(model);

        // Anthropic keys not scoped to one workspace need anthropic-workspace-id:
        // a wrkspc_… id, or "auto" to look it up and take the Default Workspace.
        const QString ws = qEnvironmentVariable("OPENTODO_LIVE_AI_WORKSPACE").trimmed();
        if (ws == QLatin1String("auto")) {
            QSignalSpy fetched(m_ai, &AIAssistant::anthropicWorkspacesFetched);
            m_ai->fetchAnthropicWorkspaces();
            QVERIFY(fetched.wait(30000));
            const QVariantList list = fetched.first().at(0).toList();
            const QString error = fetched.first().at(1).toString();
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QString chosen;
            for (const QVariant &v : list) {
                const QVariantMap w = v.toMap();
                qInfo().noquote() << "workspace:" << w.value("name").toString()
                                  << w.value("id").toString();
                if (chosen.isEmpty() || w.value("name").toString() == QLatin1String("Default"))
                    chosen = w.value("id").toString();
            }
            m_ai->setAnthropicWorkspaceId(chosen);
        } else if (!ws.isEmpty()) {
            m_ai->setAnthropicWorkspaceId(ws);
        }

        qInfo().noquote() << "provider:" << m_ai->currentProviderId()
                          << "| model:" << m_ai->currentModel()
                          << "| workspace:" << m_ai->anthropicWorkspaceId()
                          << "| route:" << m_ai->proxyStatus();
    }

    void cleanupTestCase()
    {
        QSettings().clear();
    }

    void testConnection_roundTrip()
    {
        QSignalSpy done(m_ai, &AIAssistant::connectionTestFinished);
        m_ai->testConnection();
        QVERIFY(done.wait(60000));
        const bool ok = done.first().at(0).toBool();
        qInfo().noquote() << "testConnection:" << done.first().at(1).toString();
        QCOMPARE(ok, !m_expectFail);
    }

    void chat_roundTrip()
    {
        if (m_expectFail)
            QSKIP("Failure already shown by testConnection.");
        QSignalSpy reply(m_ai, &AIAssistant::responseReceived);
        QSignalSpy error(m_ai, &AIAssistant::errorReceived);
        QElapsedTimer t;
        t.start();
        m_ai->sendMessage(QStringLiteral("Ответь одним коротким предложением: что ты умеешь?"));
        QTRY_VERIFY_WITH_TIMEOUT(reply.count() + error.count() > 0, 60000);
        if (error.count() > 0)
            QFAIL(qPrintable(error.first().at(0).toString()));
        const QString text = reply.first().at(0).toString();
        qInfo().noquote() << QStringLiteral("chat (%1 ms):").arg(t.elapsed()) << text.left(200);
        QVERIFY(!text.trimmed().isEmpty());
    }

    void priority_roundTrip()
    {
        if (m_expectFail)
            QSKIP("Failure already shown by testConnection.");
        // The tightest cap in the app (8 output tokens) — the case thinking
        // models used to truncate into an empty reply.
        QSignalSpy prio(m_ai, &AIAssistant::prioritySuggested);
        QSignalSpy error(m_ai, &AIAssistant::errorReceived);
        m_ai->suggestPriority(QStringLiteral("Срочно оплатить штраф сегодня до 18:00"));
        QTRY_VERIFY_WITH_TIMEOUT(prio.count() + error.count() > 0, 60000);
        if (error.count() > 0)
            QFAIL(qPrintable(error.first().at(0).toString()));
        const int p = prio.first().at(0).toInt();
        qInfo() << "priority:" << p;
        QVERIFY(p >= 1 && p <= 4);
    }
};

QTEST_MAIN(TestAIAssistantLive)
#include "test_aiassistant_live.moc"
