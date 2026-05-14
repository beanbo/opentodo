#pragma once

#include "todoist_types.h"

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief Asynchronous Todoist Sync API v9 client.
 *
 * Usage:
 *   auto *client = new TodoistApiClient(this);
 *   connect(client, &TodoistApiClient::finished, this, &MyClass::onApiFinished);
 *   client->fetchAll("YOUR_API_TOKEN");
 *
 * The `finished` signal is emitted once with either populated data or
 * a non-empty errorMessage.
 *
 * Supports:
 *  - [B-5.49] 429 Retry-After handling (up to 2 retries).
 *  - [B-5.50] Sync API pagination (full_sync=false loop).
 *  - [B-5.46] /completed/get_all fetch when importCompleted=true.
 *  - [B-5.53] completed_at field parsed into TodoistTask::completedAt.
 */
class TodoistApiClient : public QObject
{
    Q_OBJECT
public:
    explicit TodoistApiClient(QObject *parent = nullptr);

    /**
     * @brief Start fetching all data from the Todoist Sync API.
     * @param apiToken        Personal API token.
     * @param importCompleted If true, also fetch completed tasks via
     *                        /sync/v9/completed/get_all [B-5.46].
     */
    void fetchAll(const QString &apiToken, bool importCompleted = false);

signals:
    void finished(const TodoistImportData &data, const QString &errorMessage);

private slots:
    void onSyncReplyFinished();
    void onCompletedReplyFinished();

private:
    // Sync endpoint helpers
    void issueSyncRequest(const QString &syncToken);
    void issueCompletedRequest(int offset = 0);

    TodoistImportData parseSyncResponse(const QByteArray &json,
                                        QString &error,
                                        QString &nextSyncToken,
                                        bool    &fullSync);
    void parseCompletedResponse(const QByteArray &json,
                                QString &error,
                                bool    &hasMore);

    QNetworkAccessManager *m_nam;
    QNetworkReply         *m_reply          = nullptr;
    QNetworkReply         *m_completedReply = nullptr;

    QString           m_apiToken;
    bool              m_importCompleted = false;
    TodoistImportData m_accum;          // accumulates across pagination pages

    // Sync pagination state [B-5.50]
    QString m_syncToken;
    int     m_syncRetryCount  = 0;      // 429 retry counter [B-5.49]
    int     m_completedOffset = 0;      // completed pagination offset [B-5.46]
    int     m_completedRetry = 0;       // 429 retry for completed endpoint [B-5.49]

    static constexpr int MAX_RETRIES = 2;
};
