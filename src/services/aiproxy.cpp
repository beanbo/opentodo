#include "aiproxy.h"

#include <QHostAddress>
#include <QNetworkProxyQuery>
#include <QStringList>

bool isLocalOrPrivateHost(const QString &host)
{
    if (host.isEmpty())
        return false;
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
        || host.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
        return true;
    const QHostAddress a(host);
    if (a.isNull())
        return false;
    return a.isLoopback()
        || a.isLinkLocal()
        || a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16);
}

AiProxyFactory::AiProxyFactory(Mode mode, const QString &manualUrl)
    : m_mode(mode)
    , m_manualUrl(manualUrl.trimmed())
{
}

AiProxyFactory::Mode AiProxyFactory::modeFromString(const QString &s)
{
    if (s == QLatin1String("manual"))
        return Mode::Manual;
    if (s == QLatin1String("none"))
        return Mode::None;
    return Mode::Auto;
}

QString AiProxyFactory::modeToString(Mode mode)
{
    switch (mode) {
    case Mode::Manual: return QStringLiteral("manual");
    case Mode::None:   return QStringLiteral("none");
    case Mode::Auto:   break;
    }
    return QStringLiteral("auto");
}

bool AiProxyFactory::parseProxyUrl(const QString &url, QNetworkProxy *out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    QString s = url.trimmed();
    if (s.isEmpty())
        return fail(QStringLiteral("Адрес прокси не указан."));
    // A bare "host:port" (what most proxy panels show) is read as http.
    if (!s.contains(QLatin1String("://")))
        s.prepend(QLatin1String("http://"));

    const QUrl u(s, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || u.host().isEmpty())
        return fail(QStringLiteral("Не удалось разобрать адрес прокси. Пример: http://127.0.0.1:10809"));

    QNetworkProxy::ProxyType type;
    if (scheme == QLatin1String("http"))
        type = QNetworkProxy::HttpProxy;
    else if (scheme == QLatin1String("socks5") || scheme == QLatin1String("socks5h"))
        type = QNetworkProxy::Socks5Proxy;
    else if (scheme == QLatin1String("https"))
        return fail(QStringLiteral("https:// прокси (TLS до самого прокси) Qt не поддерживает. "
                                   "Используй http:// или socks5://."));
    else
        return fail(QStringLiteral("Неизвестная схема прокси «%1». Поддерживаются http:// и socks5://.")
                        .arg(scheme));

    if (u.port() <= 0)
        return fail(QStringLiteral("Укажи порт прокси, например http://127.0.0.1:10809"));

    if (out) {
        *out = QNetworkProxy(type, u.host(), quint16(u.port()),
                             u.userName(QUrl::FullyDecoded),
                             u.password(QUrl::FullyDecoded));
    }
    if (error) error->clear();
    return true;
}

QString AiProxyFactory::envProxyUrl(const QString &scheme, QString *sourceVar)
{
    const QString lower = scheme.toLower();
    const QStringList names = {
        lower + QStringLiteral("_proxy"),
        lower.toUpper() + QStringLiteral("_PROXY"),
        QStringLiteral("all_proxy"),
        QStringLiteral("ALL_PROXY"),
    };
    for (const QString &name : names) {
        const QString v = qEnvironmentVariable(name.toLatin1().constData()).trimmed();
        if (!v.isEmpty()) {
            if (sourceVar) *sourceVar = name;
            return v;
        }
    }
    return {};
}

bool AiProxyFactory::hostMatchesNoProxy(const QString &host, const QString &noProxy)
{
    if (host.isEmpty())
        return false;
    const QString h = host.toLower();
    const QStringList entries = noProxy.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString entry : entries) {
        entry = entry.trimmed().toLower();
        if (entry == QLatin1String("*"))
            return true;
        // Drop a ":port" suffix, but leave bare IPv6 literals ("::1") alone.
        const int colon = entry.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && entry.count(QLatin1Char(':')) == 1)
            entry.truncate(colon);
        while (entry.startsWith(QLatin1Char('.')))
            entry.remove(0, 1);
        if (entry.isEmpty())
            continue;
        if (h == entry || h.endsWith(QLatin1Char('.') + entry))
            return true;
    }
    return false;
}

QString AiProxyFactory::redactedUrl(const QString &url)
{
    QString s = url.trimmed();
    if (!s.contains(QLatin1String("://")))
        s.prepend(QLatin1String("http://"));
    QUrl u(s, QUrl::StrictMode);
    if (!u.isValid())
        return url.trimmed().contains(QLatin1Char('@')) ? QStringLiteral("(адрес скрыт)")
                                                        : url.trimmed();
    if (!u.password().isEmpty())
        u.setPassword(QStringLiteral("***"));
    return u.toString(QUrl::RemovePath | QUrl::StripTrailingSlash);
}

QList<QNetworkProxy> AiProxyFactory::resolve(const QNetworkProxyQuery &query, QString *route) const
{
    const QList<QNetworkProxy> direct{ QNetworkProxy(QNetworkProxy::NoProxy) };
    const QString host = query.peerHostName();

    if (isLocalOrPrivateHost(host)) {
        if (route) *route = QStringLiteral("напрямую (локальный адрес)");
        return direct;
    }

    if (m_mode == Mode::None) {
        if (route) *route = QStringLiteral("напрямую (прокси отключён)");
        return direct;
    }

    if (m_mode == Mode::Manual) {
        QNetworkProxy p;
        QString err;
        if (!parseProxyUrl(m_manualUrl, &p, &err)) {
            if (route) *route = QStringLiteral("ошибка: %1").arg(err);
            return {};  // fail closed: no silent direct fallback
        }
        if (route) *route = QStringLiteral("через %1").arg(redactedUrl(m_manualUrl));
        return { p };
    }

    // Auto: environment first (what Qt itself ignores on Windows), then OS.
    QString var;
    const QString scheme = query.url().scheme().isEmpty() ? QStringLiteral("https")
                                                          : query.url().scheme();
    const QString env = envProxyUrl(scheme, &var);
    if (!env.isEmpty()) {
        QString noProxy = qEnvironmentVariable("no_proxy");
        if (noProxy.isEmpty())
            noProxy = qEnvironmentVariable("NO_PROXY");
        if (hostMatchesNoProxy(host, noProxy)) {
            if (route) *route = QStringLiteral("напрямую (хост в NO_PROXY)");
            return direct;
        }
        QNetworkProxy p;
        if (parseProxyUrl(env, &p)) {
            if (route) *route = QStringLiteral("через %1 (из %2)").arg(redactedUrl(env), var);
            return { p };
        }
        // A malformed variable is ignored, as if unset.
    }

    const QList<QNetworkProxy> sys = QNetworkProxyFactory::systemProxyForQuery(query);
    if (route) {
        if (!sys.isEmpty() && sys.first().type() != QNetworkProxy::NoProxy
            && sys.first().type() != QNetworkProxy::DefaultProxy) {
            *route = QStringLiteral("через системный прокси %1:%2")
                         .arg(sys.first().hostName()).arg(sys.first().port());
        } else {
            *route = QStringLiteral("напрямую (системный прокси не задан, HTTPS_PROXY не задана)");
        }
    }
    return sys;
}

QList<QNetworkProxy> AiProxyFactory::queryProxy(const QNetworkProxyQuery &query)
{
    return resolve(query, nullptr);
}

QString AiProxyFactory::describeRoute(const QUrl &url) const
{
    QString route;
    resolve(QNetworkProxyQuery(url), &route);
    return route;
}
