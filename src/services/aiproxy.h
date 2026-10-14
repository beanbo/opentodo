#pragma once

// AiProxyFactory — picks the outbound proxy for AIAssistant's HTTP traffic.
//
// Why this exists: Qt resolves proxies on Windows (and macOS) from the OS
// settings only — WinINET / WinHTTP through
// QNetworkProxyFactory::systemProxyForQuery() — and ignores the HTTPS_PROXY /
// ALL_PROXY environment variables that curl, Python, Node and Claude Code
// honour. A per-app split tunnel (a local Xray / v2ray HTTP inbound such as
// 127.0.0.1:10809) deliberately leaves the system proxy off, so every AI
// request went out directly and api.anthropic.com answered
// 403 "Request not allowed" from unsupported regions.
//
// Modes (persisted by AIAssistant under ai/proxy/mode and ai/proxy/url):
//   Auto   – the environment proxy (https_proxy / HTTPS_PROXY, then
//            ALL_PROXY; NO_PROXY honoured), else the OS proxy settings.
//            Default: without those variables it is the pre-existing
//            behaviour.
//   Manual – an explicit http:// or socks5:// URL, optionally user:pass@.
//   None   – always direct.
//
// Loopback / private-LAN hosts (a local Ollama or LM Studio) bypass the proxy
// in every mode. A Manual URL that does not parse fails closed (the request
// errors with "No suitable proxy found") rather than leaking out directly.
//
// Scope: installed on AIAssistant's own QNetworkAccessManager only. Sync,
// update checks and telemetry keep Qt's default (system) proxy behaviour.

#include <QList>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QString>
#include <QUrl>

class AiProxyFactory : public QNetworkProxyFactory
{
public:
    enum class Mode { Auto, Manual, None };

    AiProxyFactory(Mode mode, const QString &manualUrl);

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override;

    // One-line, user-facing description (Russian) of the route a request to
    // `url` takes under this configuration. Never contains a password.
    QString describeRoute(const QUrl &url) const;

    // ── Pure helpers, public for tests ──────────────────────────────────────

    // "manual" / "none" map to Manual / None; anything else is Auto.
    static Mode modeFromString(const QString &s);
    static QString modeToString(Mode mode);

    // Parses "http://[user:pass@]host:port", "socks5://…" ("socks5h://" is
    // accepted as a synonym) or a bare "host:port" (read as http). The port
    // is required. https:// is rejected: Qt cannot speak TLS to the proxy
    // itself. On failure returns false and fills *error with a Russian
    // message.
    static bool parseProxyUrl(const QString &url, QNetworkProxy *out,
                              QString *error = nullptr);

    // The environment proxy for a request scheme, curl's lookup order:
    // <scheme>_proxy, <SCHEME>_PROXY, all_proxy, ALL_PROXY. Empty when unset.
    // *sourceVar receives the variable name that supplied it.
    static QString envProxyUrl(const QString &scheme, QString *sourceVar = nullptr);

    // curl-style NO_PROXY match: "*" matches every host; otherwise each
    // comma-separated entry matches the host exactly or as a domain suffix
    // (a leading dot is optional, a ":port" in the entry is ignored).
    static bool hostMatchesNoProxy(const QString &host, const QString &noProxy);

    // The URL with its password replaced by "***" (safe for logs and the UI).
    static QString redactedUrl(const QString &url);

private:
    // Resolution shared by queryProxy() and describeRoute(). `route` gets a
    // Russian description; the return value is the proxy list for Qt (an
    // empty list means "fail closed").
    QList<QNetworkProxy> resolve(const QNetworkProxyQuery &query, QString *route) const;

    Mode m_mode;
    QString m_manualUrl;
};

// True for loopback, RFC1918, link-local, "localhost" and *.local — hosts on
// this machine or LAN. Cleartext http:// to them is acceptable (a local LLM
// server) and they never go through the AI proxy.
bool isLocalOrPrivateHost(const QString &host);
