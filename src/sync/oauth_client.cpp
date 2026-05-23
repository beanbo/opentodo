#include "oauth_client.h"

#include <QDebug>
#include <QDesktopServices>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcOAuth, "opentodo.oauth")

namespace GoogleEndpoints {
static const QString TOKEN_URL    = QStringLiteral("https://oauth2.googleapis.com/token");
static const QString AUTH_URL     = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
static const QString USERINFO_URL = QStringLiteral("https://openidconnect.googleapis.com/v1/userinfo");

static const QString DRIVE_SCOPE =
    QStringLiteral("https://www.googleapis.com/auth/drive.file email profile");

// Iter-task #36: previously the Desktop-OAuth client_id and client_secret were
// hard-coded into the source. Even though Google's own docs note that the
// "client_secret" for installed apps is not a real secret (PKCE-less Desktop
// flow makes it effectively a public value), embedding it in a public
// open-source repository has two real consequences:
//   1. Every fork of the project inherits ONE shared Google Cloud project's
//      quota — a single misbehaving fork can get the upstream client banned.
//   2. Abuse complaints route back to the original Google account owner.
//
// Resolution: ship empty defaults. The user / packager supplies their own
// client_id+secret via either:
//   - environment variables OPENTODO_GOOGLE_CLIENT_ID / _SECRET (preferred for
//     CI builds and per-machine installs), or
//   - the existing QSettings keys sync/client_id and sync/client_secret
//     (preferred for end users who want to point an existing instance at
//     a different Cloud project).
// When neither is provided, authenticate() short-circuits with a clear
// "not configured" error and Sync is disabled — the rest of the app works
// fine without Google Drive.
static QString resolveClientId(const QSettings &settings)
{
    const QString env = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("OPENTODO_GOOGLE_CLIENT_ID"));
    if (!env.isEmpty()) return env;
    return settings.value(QStringLiteral("sync/client_id")).toString();
}

static QString resolveClientSecret(const QSettings &settings)
{
    const QString env = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("OPENTODO_GOOGLE_CLIENT_SECRET"));
    if (!env.isEmpty()) return env;
    return settings.value(QStringLiteral("sync/client_secret")).toString();
}
} // namespace GoogleEndpoints

OAuthClient::OAuthClient(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_clientId     = GoogleEndpoints::resolveClientId(settings);
    m_clientSecret = GoogleEndpoints::resolveClientSecret(settings);
    loadTokens();
}

bool OAuthClient::isAuthenticated() const { return m_authenticated; }
QString OAuthClient::accountEmail() const { return m_accountEmail; }
QString OAuthClient::accessToken() const  { return m_accessToken; }

void OAuthClient::authenticate()
{
    qCInfo(lcOAuth) << "authenticate() called. hasToken=" << !m_accessToken.isEmpty()
                    << " hasClientId=" << !m_clientId.isEmpty();

    // Already signed in — try to use existing tokens, refreshing if needed.
    if (!m_accessToken.isEmpty()) {
        m_authenticated = true;
        emit authenticatedChanged();
        // Signal caller that auth is ready; Drive-level validation will happen
        // in DriveApi (a 401 there will trigger refreshAccessToken).
        emit authenticationSucceeded();
        return;
    }

    if (m_clientId.isEmpty() || m_clientSecret.isEmpty()) {
        emit authenticationFailed(
            QStringLiteral(
                "Google OAuth не настроен. Создайте OAuth-клиент типа «Desktop» "
                "в Google Cloud Console (включите Drive API + Identity Services), "
                "затем укажите его данные одним из способов:\n"
                "  1) Переменные окружения OPENTODO_GOOGLE_CLIENT_ID и "
                "OPENTODO_GOOGLE_CLIENT_SECRET, ИЛИ\n"
                "  2) Ключи QSettings sync/client_id и sync/client_secret."));
        return;
    }

    startAuthFlow();
}

void OAuthClient::startAuthFlow()
{
    abortAuthFlow(QString()); // Clean up any previous attempt.

    m_authServer = new QTcpServer(this);
    if (!m_authServer->listen(QHostAddress::LocalHost, 0)) {
        const QString err = m_authServer->errorString();
        m_authServer->deleteLater();
        m_authServer = nullptr;
        emit authenticationFailed(
            QStringLiteral("Не удалось открыть локальный порт: %1").arg(err));
        return;
    }
    m_authPort = m_authServer->serverPort();

    connect(m_authServer, &QTcpServer::newConnection, this, [this]() {
        while (auto *socket = m_authServer->nextPendingConnection())
            handleAuthCallback(socket);
    });

    // Auto-cancel after 5 minutes if user never completes the flow.
    QTimer::singleShot(5 * 60 * 1000, this, [this]() {
        if (m_authServer)
            abortAuthFlow(QStringLiteral("Истекло время ожидания авторизации (5 мин)."));
    });

    // Iter-15: per-flow CSRF nonce. Without it an attacker with a
    // pre-issued code could ferry victim's loopback callback into the
    // attacker's Drive account (RFC 6749 §10.12). 192 bits of randomness
    // from the OS CSPRNG is well beyond brute-force.
    QByteArray stateRaw(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(stateRaw.data()), stateRaw.size() / 4);
    m_authState = QString::fromLatin1(stateRaw.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    const QString redirectUri = QStringLiteral("http://127.0.0.1:%1").arg(m_authPort);
    QUrl url(GoogleEndpoints::AUTH_URL);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"),     m_clientId);
    q.addQueryItem(QStringLiteral("redirect_uri"),  redirectUri);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("scope"),         GoogleEndpoints::DRIVE_SCOPE);
    q.addQueryItem(QStringLiteral("access_type"),   QStringLiteral("offline"));
    q.addQueryItem(QStringLiteral("prompt"),        QStringLiteral("consent"));
    q.addQueryItem(QStringLiteral("state"),         m_authState);
    url.setQuery(q);

    qCInfo(lcOAuth) << "OAuth: listening on" << redirectUri
                    << "; opening browser to" << url.toString(QUrl::PrettyDecoded);
    const bool opened = QDesktopServices::openUrl(url);
    qCInfo(lcOAuth) << "QDesktopServices::openUrl returned" << opened;
    if (!opened) {
        abortAuthFlow(QStringLiteral("Не удалось открыть браузер для авторизации."));
    }
}

void OAuthClient::handleAuthCallback(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    if (!socket->waitForReadyRead(3000)) {
        socket->close();
        return;
    }

    const QByteArray request = socket->readAll();
    // First line: "GET /?code=... HTTP/1.1"
    const int eol = request.indexOf("\r\n");
    const QByteArray firstLine = (eol >= 0) ? request.left(eol) : request;
    const QList<QByteArray> parts = firstLine.split(' ');

    QString code, error, state;
    if (parts.size() >= 2) {
        const QUrl callbackUrl(QStringLiteral("http://127.0.0.1") +
                               QString::fromLatin1(parts.at(1)));
        const QUrlQuery q(callbackUrl);
        code  = q.queryItemValue(QStringLiteral("code"));
        error = q.queryItemValue(QStringLiteral("error"));
        state = q.queryItemValue(QStringLiteral("state"));
    }

    // Iter-15: validate state nonce. Mismatch ⇒ this callback isn't from
    // the flow we started; treat as a CSRF attempt and refuse to exchange
    // the code. Compares only when we actually have a non-empty
    // m_authState (legacy callbacks from earlier sessions where m_authState
    // was never populated should still error out cleanly).
    if (!m_authState.isEmpty() && state != m_authState) {
        qCWarning(lcOAuth) << "OAuth: state mismatch in callback — rejecting "
                           << "(expected nonce did not match)";
        code.clear();
        error = QStringLiteral("Несовпадение state — авторизация отклонена.");
    }

    const QString successHtml = QStringLiteral(
        "<!doctype html><html><head><meta charset='utf-8'><title>OpenTodo</title>"
        "<style>body{font-family:sans-serif;max-width:520px;margin:80px auto;text-align:center;color:#202020}"
        "h1{color:#db4c3f}</style></head><body>"
        "<h1>OpenTodo</h1><p>Авторизация прошла успешно.</p>"
        "<p>Можно закрыть это окно и вернуться в приложение.</p></body></html>");
    // Iter-15 sec fix: HTML-escape the error parameter before interpolating.
    // It comes from the URL query (queryItemValue is already URL-decoded),
    // so a crafted callback like /?error=<script>... would otherwise execute
    // in the loopback origin. Impact is limited (loopback has no app cookies
    // / no shared origin with anything sensitive), but it's an unnecessary
    // injection point.
    const QString errorText = error.isEmpty()
        ? QStringLiteral("Код авторизации не получен.")
        : error.toHtmlEscaped();
    const QString errorHtml = QStringLiteral(
        "<!doctype html><html><head><meta charset='utf-8'><title>OpenTodo</title></head>"
        "<body><h1>Ошибка</h1><p>%1</p></body></html>")
        .arg(errorText);

    const QString &body = (!code.isEmpty()) ? successHtml : errorHtml;
    const QByteArray bytes = body.toUtf8();
    QByteArray response;
    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(bytes.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += bytes;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();

    if (code.isEmpty()) {
        abortAuthFlow(error.isEmpty() ? QStringLiteral("Код авторизации не получен.") : error);
        return;
    }

    const QString redirectUri = QStringLiteral("http://127.0.0.1:%1").arg(m_authPort);

    // Stop listening before doing the network exchange.
    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }

    if (!exchangeCodeForTokens(code, redirectUri)) {
        emit authenticationFailed(QStringLiteral("Не удалось обменять код на токен."));
        return;
    }

    fetchUserInfo();
    saveTokens();

    // Iter-15: clear the CSRF nonce once we've finished exchanging the code.
    // A second callback re-using the same nonce should not silently accept
    // a fresh code.
    m_authState.clear();

    m_authenticated = true;
    emit authenticatedChanged();
    emit accountEmailChanged();
    emit authenticationSucceeded();
}

bool OAuthClient::exchangeCodeForTokens(const QString &code, const QString &redirectUri)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("code"),          code);
    form.addQueryItem(QStringLiteral("client_id"),     m_clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("redirect_uri"),  redirectUri);
    form.addQueryItem(QStringLiteral("grant_type"),    QStringLiteral("authorization_code"));

    QNetworkRequest req(QUrl{GoogleEndpoints::TOKEN_URL});
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_nam.post(req, form.toString(QUrl::FullyEncoded).toUtf8());

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status != 200)
        return false;

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    m_accessToken  = obj.value(QStringLiteral("access_token")).toString();
    if (obj.contains(QStringLiteral("refresh_token")))
        m_refreshToken = obj.value(QStringLiteral("refresh_token")).toString();

    return !m_accessToken.isEmpty();
}

void OAuthClient::fetchUserInfo()
{
    if (m_accessToken.isEmpty()) return;

    QNetworkRequest req(QUrl{GoogleEndpoints::USERINFO_URL});
    req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    QNetworkReply *reply = m_nam.get(req);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    m_accountEmail = obj.value(QStringLiteral("email")).toString();
}

void OAuthClient::abortAuthFlow(const QString &reason)
{
    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }
    // Iter-15: clear the CSRF nonce so a stale callback that arrives after
    // abort can't sneak through.
    m_authState.clear();
    if (!reason.isEmpty())
        emit authenticationFailed(reason);
}

void OAuthClient::signOut()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accountEmail.clear();
    m_authenticated = false;

    QSettings settings;
    settings.remove(QStringLiteral("gdrive/access_token"));
    settings.remove(QStringLiteral("gdrive/refresh_token"));
    settings.remove(QStringLiteral("gdrive/email"));

    emit authenticatedChanged();
    emit accountEmailChanged();
}

bool OAuthClient::refreshAccessToken()
{
    if (m_refreshToken.isEmpty()) return false;

    // X-2.34: single-flight guard — if a refresh is already in progress
    // (possible with nested event loops from concurrent push/pull on the same
    // thread), skip issuing a second request and return the current auth state.
    // Since OAuthClient lives on the main Qt thread, we use a simple bool
    // rather than a blocking mutex/condvar (which would deadlock on one thread).
    if (m_refreshInProgress) {
        qCInfo(lcOAuth) << "refreshAccessToken: already in progress, skipping duplicate";
        return m_authenticated;
    }
    m_refreshInProgress = true;

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("client_id"),     m_clientId);
    params.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    params.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    params.addQueryItem(QStringLiteral("grant_type"),    QStringLiteral("refresh_token"));

    QNetworkRequest req(QUrl{GoogleEndpoints::TOKEN_URL});
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    // Iter-night-1 (zombie sign-out race): the nested QEventLoop below pumps
    // every Qt event including UI clicks. If the user hits "Sign out" while
    // we're waiting on Google's response, signOut() reentrantly clears
    // m_refreshToken / m_accessToken / m_authenticated. When the response
    // arrives and we resume execution, the old code path would overwrite
    // the cleared state with the freshly-received tokens, silently undoing
    // the user's sign-out. Snapshot m_refreshToken now; after the loop, if
    // it no longer matches we know signOut() ran during the wait and we
    // abort instead of restoring the session.
    const QString refreshTokenSnapshot = m_refreshToken;

    QNetworkReply *reply = m_nam.post(req, params.toString(QUrl::FullyEncoded).toUtf8());

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    // Detect a reentrant signOut(): refresh_token was cleared (or replaced
    // by re-auth) while we were waiting. Drop the response on the floor so
    // we don't resurrect tokens the user just asked us to forget.
    if (m_refreshToken != refreshTokenSnapshot) {
        qCInfo(lcOAuth) << "refreshAccessToken: refresh_token changed during "
                           "the nested event loop (sign-out or re-auth) — "
                           "discarding response to honour the new state.";
        m_refreshInProgress = false;
        return m_authenticated;
    }

    bool success = false;

    // B-2.32: differentiate network error vs invalid_grant.
    if (netErr != QNetworkReply::NoError && httpStatus == 0) {
        // Pure network / transport error (no response from server).
        // Keep m_authenticated as-is — do NOT sign out; caller can retry later.
        qCInfo(lcOAuth) << "refreshAccessToken: network error" << netErr
                        << "— keeping authenticated state, will retry later";
        success = false;
    } else {
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        if (json.contains(QStringLiteral("access_token"))) {
            m_accessToken   = json[QStringLiteral("access_token")].toString();
            m_authenticated = true;
            saveTokens();
            emit authenticatedChanged();
            success = true;
        } else {
            // HTTP error response. Check for invalid_grant (revoked / expired token).
            const QString errorCode = json[QStringLiteral("error")].toString();
            if (errorCode == QStringLiteral("invalid_grant")) {
                // Refresh token has been revoked — must sign the user out.
                qCInfo(lcOAuth) << "refreshAccessToken: invalid_grant — signing out";
                m_accessToken.clear();
                m_refreshToken.clear();
                m_authenticated = false;
                QSettings settings;
                settings.remove(QStringLiteral("gdrive/access_token"));
                settings.remove(QStringLiteral("gdrive/refresh_token"));
                emit authenticatedChanged();
                // Emit signOut equivalent via authenticationFailed so UI can react.
                emit authenticationFailed(QStringLiteral("Сессия истекла. Войдите в аккаунт Google снова."));
            } else {
                // Other HTTP error (500, rate limit, etc.) — do not clear tokens.
                qCInfo(lcOAuth) << "refreshAccessToken failed (HTTP" << httpStatus
                                << "):" << json;
            }
            success = false;
        }
    }

    // X-2.34: release the single-flight guard.
    m_refreshInProgress = false;

    return success;
}

void OAuthClient::injectAccessToken(const QString &token)
{
    m_accessToken   = token;
    m_authenticated = !token.isEmpty();
    saveTokens();
    emit authenticatedChanged();
}

void OAuthClient::injectRefreshToken(const QString &token)
{
    m_refreshToken = token;
    saveTokens();
}

void OAuthClient::saveTokens()
{
    QSettings settings;
    settings.setValue(QStringLiteral("gdrive/access_token"),  m_accessToken);
    settings.setValue(QStringLiteral("gdrive/refresh_token"), m_refreshToken);
    settings.setValue(QStringLiteral("gdrive/email"),         m_accountEmail);
}

void OAuthClient::loadTokens()
{
    QSettings settings;
    m_accessToken  = settings.value(QStringLiteral("gdrive/access_token")).toString();
    m_refreshToken = settings.value(QStringLiteral("gdrive/refresh_token")).toString();
    m_accountEmail = settings.value(QStringLiteral("gdrive/email")).toString();
    m_authenticated = !m_accessToken.isEmpty();
}
