#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QQmlEngine>

class QNetworkReply;

class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the updateChecker context property.")

    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY checkCompleted)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY checkCompleted)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY checkCompleted)
    Q_PROPERTY(bool isChecking READ isChecking NOTIFY isCheckingChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseUrl() const { return m_releaseUrl; }
    bool updateAvailable() const;
    bool isChecking() const { return m_isChecking; }

    Q_INVOKABLE void checkForUpdates(bool force = false);
    Q_INVOKABLE void skipVersion(const QString &version);
    Q_INVOKABLE void openReleaseInBrowser();

    // Exposed as static for testability
    static bool isVersionGreater(const QString &a, const QString &b);

signals:
    void checkCompleted();
    void isCheckingChanged();
    void noUpdateAvailable();

private:
    // B-5.23: shared request dispatcher + reply handler (first attempt and retry).
    void postCheckRequest(const QNetworkRequest &req, bool isRetry);
    void onCheckReplyFinished(QNetworkReply *reply, bool isRetry);

    // versionGreater wraps the static helper for internal use
    bool versionGreater(const QString &a, const QString &b) const;

    QNetworkAccessManager m_nam;
    QString m_latestVersion;
    QString m_releaseUrl;
    bool m_isChecking = false;
};
