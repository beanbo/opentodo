#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class QNetworkReply;
class SecretStore;

/**
 * @brief Handles the Google OAuth 2.0 "installed app" authorization flow.
 *
 * Flow overview:
 *  1. Call authenticate().  If an access token is already stored in QSettings
 *     (key "gdrive/access_token") the flow completes immediately and
 *     authenticationSucceeded() is emitted.
 *  2. Otherwise a local QTcpServer is opened on a random port and the system
 *     browser is launched with the Google consent URL
 *     (accounts.google.com/o/oauth2/v2/auth).
 *  3. Google redirects to http://127.0.0.1:<port>/?code=… after the user
 *     grants consent.  The local server exchanges the code for tokens
 *     (TOKEN_URL) and fetches the account email (USERINFO_URL).
 *  4. Tokens are persisted to QSettings under "gdrive/" keys.
 *     refreshAccessToken() can exchange the refresh token for a new access
 *     token without user interaction.
 *
 * Thread safety: must be used on the main (Qt event loop) thread.
 */
class OAuthClient : public QObject
{
    Q_OBJECT

public:
    explicit OAuthClient(QObject *parent = nullptr);

    bool isAuthenticated() const;
    QString accountEmail() const;
    QString accessToken() const;

    void authenticate();
    void signOut();

    // Android custom-scheme OAuth: the redirect (com.googleusercontent.apps.
    // <id>:/oauth2redirect?code=…) is delivered to the app via an Intent, not a
    // loopback socket. MainActivity.onNewIntent forwards the full redirect URI
    // here through JNI (Java_org_opentodo_MainActivity_nativeOnOAuthRedirect).
    // No-op on desktop, which keeps the QTcpServer loopback handler.
    void handleAndroidRedirect(const QString &redirectUri);

    // Returns true if a new access token was obtained.
    bool refreshAccessToken();

    // Direct token injection (used by GDriveClient backward-compat setters).
    void injectAccessToken(const QString &token);
    void injectRefreshToken(const QString &token);

    // Sec-1: route the gdrive access/refresh tokens through the OS keyring
    // (SecretStore) instead of plaintext QSettings. Runs a one-time migration of
    // any existing plaintext tokens and (on later runs) loads them back from the
    // keyring. The account email stays in QSettings (not a secret). No-op if null.
    void setSecretStore(SecretStore *store);

signals:
    void authenticatedChanged();
    void accountEmailChanged();
    void authenticationSucceeded();
    void authenticationFailed(const QString &error);

private:
    void startAuthFlow();
    void handleAuthCallback(QTcpSocket *socket);
    bool exchangeCodeForTokens(const QString &code, const QString &redirectUri);
    void fetchUserInfo();
    void abortAuthFlow(const QString &reason);

    void saveTokens();
    void loadTokens();
    // Synchronously warm the SecretStore cache for `key` (pumps a local event
    // loop until the async keyring read resolves, with a guard timeout).
    void warmSecretSync(const QString &key);

    QPointer<SecretStore> m_secretStore;   // Sec-1: nullable; QSettings path when unset
    QNetworkAccessManager m_nam;
    QString m_accessToken;
    QString m_refreshToken;
    QString m_clientId;
    QString m_clientSecret;
    QString m_accountEmail;
    bool    m_authenticated = false;

    // X-2.34: single-flight refresh guard (prevents duplicate refresh on
    // nested event loops from concurrent push/pull on the main thread).
    bool m_refreshInProgress = false;

    QTcpServer *m_authServer = nullptr;
    quint16     m_authPort   = 0;

    // Iter-15: per-flow CSRF nonce. RFC 6749 §10.12 requires `state` so an
    // attacker can't ferry their own authorization code into our loopback
    // callback (it would silently bind the victim's app to the attacker's
    // Drive account). Generated cryptographically on startAuthFlow,
    // compared against `state` in handleAuthCallback, cleared on either
    // success, abort, or timeout.
    QString m_authState;

    // Android custom-scheme flow uses PKCE (RFC 7636) instead of a client
    // secret: iOS-type Google OAuth clients are public clients with no secret.
    // m_codeVerifier is the high-entropy random string generated in
    // startAuthFlow(); its S256 challenge goes in the auth URL and the verifier
    // itself is sent in the token exchange. Empty on the desktop loopback path.
    QString m_codeVerifier;

    // The redirect_uri used for the current Android flow (custom scheme). Needed
    // again verbatim in the token exchange. Empty on desktop.
    QString m_androidRedirectUri;
};
