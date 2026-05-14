#include "gdrive.h"

GDriveClient::GDriveClient(QObject *parent)
    : QObject(parent)
    , m_auth(new OAuthClient(this))
    , m_drive(new DriveApi(m_auth, this))
{
    connect(m_auth, &OAuthClient::authenticatedChanged,
            this, &GDriveClient::authenticatedChanged);
    connect(m_auth, &OAuthClient::accountEmailChanged,
            this, &GDriveClient::accountEmailChanged);
    connect(m_auth, &OAuthClient::authenticationSucceeded,
            this, &GDriveClient::authenticationSucceeded);
    connect(m_auth, &OAuthClient::authenticationFailed,
            this, &GDriveClient::authenticationFailed);
}

void GDriveClient::setAccessToken(const QString &token)
{
    m_auth->injectAccessToken(token);
}

void GDriveClient::setRefreshToken(const QString &token)
{
    m_auth->injectRefreshToken(token);
}
