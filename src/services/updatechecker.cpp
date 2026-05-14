#include "updatechecker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

static constexpr qint64 CHECK_INTERVAL_SECS = 24 * 60 * 60; // 24 hours
static const QString DEFAULT_REPO_URL =
    QStringLiteral("https://api.github.com/repos/beanbo/opentodo/releases/latest");

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

QString UpdateChecker::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

bool UpdateChecker::updateAvailable() const
{
    if (m_latestVersion.isEmpty())
        return false;
    return versionGreater(m_latestVersion, currentVersion());
}

void UpdateChecker::checkForUpdates(bool force)
{
    if (m_isChecking)
        return;

    QSettings settings;

    // F-5.32: Respect the user's opt-out setting. If updates are disabled, skip
    // entirely — no HTTP request is made, no UI notification is produced.
    if (settings.value(QStringLiteral("update/disabled"), false).toBool())
        return;

    if (!force) {
        const qint64 lastCheck = settings.value(QStringLiteral("update/last_check"), 0).toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if ((now - lastCheck) < CHECK_INTERVAL_SECS)
            return;
    }

    m_isChecking = true;
    emit isCheckingChanged();

    // Allow overriding the repo URL via settings (e.g. for staging)
    const QString repoUrl = settings.value(QStringLiteral("update/repo_url"), DEFAULT_REPO_URL).toString();

    QNetworkRequest request{QUrl(repoUrl)};
    request.setRawHeader("User-Agent", "OpenTodo-UpdateChecker");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    postCheckRequest(request, /*isRetry=*/false);
}

void UpdateChecker::postCheckRequest(const QNetworkRequest &req, bool isRetry)
{
    QNetworkReply *reply = m_nam.get(req);
    reply->setProperty("_reqUrl", req.url().toString());

    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, req, isRetry]() {
        onCheckReplyFinished(reply, isRetry);
        Q_UNUSED(req);
    });
}

void UpdateChecker::onCheckReplyFinished(QNetworkReply *reply, bool isRetry)
{
    reply->deleteLater();

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // B-5.23: Retry once on HTTP 429 (rate-limit) or 5xx (server error).
    if (reply->error() != QNetworkReply::NoError && !isRetry) {
        bool shouldRetry = false;
        int delayMs = 2000;

        if (httpStatus == 429) {
            shouldRetry = true;
            const QByteArray ra = reply->rawHeader("Retry-After");
            if (!ra.isEmpty()) {
                bool ok = false;
                int secs = ra.toInt(&ok);
                if (ok && secs > 0) delayMs = secs * 1000;
            }
        } else if (httpStatus >= 500 && httpStatus < 600) {
            shouldRetry = true;
        }

        if (shouldRetry) {
            const QUrl savedUrl(reply->property("_reqUrl").toString());
            QNetworkRequest req2{savedUrl};
            req2.setRawHeader("User-Agent", "OpenTodo-UpdateChecker");
            req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                              QNetworkRequest::NoLessSafeRedirectPolicy);

            QTimer::singleShot(delayMs, this, [this, req2]() {
                if (!m_isChecking) return;
                postCheckRequest(req2, /*isRetry=*/true);
            });
            return;
        }
    }

    m_isChecking = false;
    emit isCheckingChanged();

    if (reply->error() != QNetworkReply::NoError)
        return;

    // Save last check timestamp regardless of parse outcome
    QSettings settings;
    settings.setValue(QStringLiteral("update/last_check"),
                      QDateTime::currentSecsSinceEpoch());

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    QString tagName = obj.value(QStringLiteral("tag_name")).toString();
    if (tagName.isEmpty())
        return;

    // Strip leading "v" prefix (e.g. "v0.2.0" → "0.2.0")
    if (tagName.startsWith(QLatin1Char('v')))
        tagName.remove(0, 1);

    m_releaseUrl = obj.value(QStringLiteral("html_url")).toString();

    // Check whether this version is in the skip list
    const QString skipped = settings.value(QStringLiteral("update/skipped_version")).toString();
    if (!skipped.isEmpty() && tagName == skipped) {
        emit noUpdateAvailable();
        return;
    }

    m_latestVersion = tagName;

    if (updateAvailable())
        emit checkCompleted();
    else
        emit noUpdateAvailable();
}

void UpdateChecker::skipVersion(const QString &version)
{
    QSettings settings;
    settings.setValue(QStringLiteral("update/skipped_version"), version);
}

void UpdateChecker::openReleaseInBrowser()
{
    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
}

// static
bool UpdateChecker::isVersionGreater(const QString &a, const QString &b)
{
    // Simple semver compare: split on '.', compare numeric components left-to-right.
    const QStringList aParts = a.split(QLatin1Char('.'));
    const QStringList bParts = b.split(QLatin1Char('.'));
    const int len = qMax(aParts.size(), bParts.size());
    for (int i = 0; i < len; ++i) {
        const int av = (i < aParts.size()) ? aParts.at(i).toInt() : 0;
        const int bv = (i < bParts.size()) ? bParts.at(i).toInt() : 0;
        if (av > bv)
            return true;
        if (av < bv)
            return false;
    }
    return false; // equal
}

bool UpdateChecker::versionGreater(const QString &a, const QString &b) const
{
    return isVersionGreater(a, b);
}
