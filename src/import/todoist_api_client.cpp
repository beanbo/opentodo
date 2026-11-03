#include "todoist_api_client.h"

#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUrl>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>

static constexpr const char *SYNC_API_URL      = "https://api.todoist.com/sync/v9/sync";
static constexpr const char *COMPLETED_API_URL = "https://api.todoist.com/sync/v9/items/completed/get_all";

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static QString jStr(const QJsonObject &obj, const QString &key)
{
    return obj.value(key).toString();
}

static bool jBool(const QJsonObject &obj, const QString &key, bool def = false)
{
    const QJsonValue v = obj.value(key);
    if (v.isBool()) return v.toBool();
    // Todoist sometimes encodes booleans as 0/1
    if (v.isDouble()) return v.toInt() != 0;
    return def;
}

static int jInt(const QJsonObject &obj, const QString &key, int def = 0)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) return v.toInt(def);
    if (v.isString()) {
        bool ok = false;
        int r = v.toString().toInt(&ok);
        return ok ? r : def;
    }
    return def;
}

static QDateTime jDateTime(const QJsonObject &obj, const QString &key)
{
    const QString s = jStr(obj, key);
    if (s.isEmpty()) return {};
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    return dt;
}

static TodoistDue parseDueObject(const QJsonValue &dueVal)
{
    if (!dueVal.isObject()) return {};
    const QJsonObject d = dueVal.toObject();

    TodoistDue due;
    due.date        = jStr(d, QStringLiteral("date"));
    due.string      = jStr(d, QStringLiteral("string"));
    due.lang        = jStr(d, QStringLiteral("lang"));
    due.isRecurring = jBool(d, QStringLiteral("is_recurring"));
    due.timezone    = jStr(d, QStringLiteral("timezone"));

    // "date" in the API can be a date-only (YYYY-MM-DD) or datetime (ISO 8601)
    if (due.date.length() > 10) {
        due.datetime = due.date;
        due.date     = due.date.left(10);
    }
    if (due.lang.isEmpty()) due.lang = QStringLiteral("en");
    return due;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TodoistApiClient::TodoistApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void TodoistApiClient::fetchAll(const QString &apiToken, bool importCompleted)
{
    m_apiToken        = apiToken.trimmed();
    m_importCompleted = importCompleted;
    m_accum           = {};
    m_syncToken       = QStringLiteral("*"); // full sync [B-5.50]
    m_syncRetryCount  = 0;
    m_completedOffset = 0;
    m_completedRetry  = 0;

    issueSyncRequest(m_syncToken);
}

// ---------------------------------------------------------------------------
// Sync endpoint: issue request [B-5.50]
// ---------------------------------------------------------------------------

void TodoistApiClient::issueSyncRequest(const QString &syncToken)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QNetworkRequest req{QUrl(QLatin1String(SYNC_API_URL))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArrayLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + m_apiToken.toUtf8());
    req.setRawHeader(QByteArrayLiteral("User-Agent"),
                     QByteArrayLiteral("OpenTodo/0.1 (https://github.com/beanbo/opentodo)"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("sync_token"), syncToken);
    body.addQueryItem(QStringLiteral("resource_types"),
                      QStringLiteral("[\"projects\",\"items\",\"sections\",\"labels\",\"notes\"]"));

    m_reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(m_reply, &QNetworkReply::finished, this, &TodoistApiClient::onSyncReplyFinished);
}

// ---------------------------------------------------------------------------
// Sync reply handler — pagination + 429 retry [B-5.49, B-5.50]
// ---------------------------------------------------------------------------

void TodoistApiClient::onSyncReplyFinished()
{
    const QByteArray   rawData    = m_reply->readAll();
    const QNetworkReply::NetworkError netErr = m_reply->error();
    const int httpStatus =
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // [B-5.49] Handle 429 Too Many Requests with Retry-After
    if (httpStatus == 429) {
        const QByteArray retryHeader = m_reply->rawHeader(QByteArrayLiteral("Retry-After"));
        m_reply->deleteLater();
        m_reply = nullptr;

        if (m_syncRetryCount < MAX_RETRIES) {
            ++m_syncRetryCount;
            int retryAfterSec = retryHeader.isEmpty() ? 5 : retryHeader.toInt();
            if (retryAfterSec <= 0) retryAfterSec = 5;
            QTimer::singleShot(retryAfterSec * 1000, this,
                               [this]() { issueSyncRequest(m_syncToken); });
            return;
        }
        emit finished({}, QStringLiteral("Превышен лимит запросов Todoist API (HTTP 429). Попробуйте позже."));
        return;
    }

    m_reply->deleteLater();
    m_reply = nullptr;

    if (netErr != QNetworkReply::NoError) {
        emit finished({}, QStringLiteral("Сетевая ошибка: %1 (HTTP %2)")
                              .arg(static_cast<int>(netErr))
                              .arg(httpStatus));
        return;
    }

    if (httpStatus == 401 || httpStatus == 403) {
        emit finished({}, QStringLiteral("Неверный API-токен (HTTP %1). "
                                          "Проверьте токен в Настройки → Интеграции → Developer.")
                              .arg(httpStatus));
        return;
    }

    if (httpStatus != 200) {
        emit finished({}, QStringLiteral("Ошибка сервера Todoist (HTTP %1)").arg(httpStatus));
        return;
    }

    QString parseError;
    QString nextSyncToken;
    bool fullSync = true;
    const TodoistImportData page = parseSyncResponse(rawData, parseError, nextSyncToken, fullSync);
    if (!parseError.isEmpty()) {
        emit finished({}, parseError);
        return;
    }

    // Merge page into accumulator [B-5.50]
    m_accum.projects  += page.projects;
    m_accum.labels    += page.labels;
    m_accum.sections  += page.sections;
    m_accum.tasks     += page.tasks;
    m_accum.notes     += page.notes;

    if (!fullSync && !nextSyncToken.isEmpty() && nextSyncToken != m_syncToken) {
        // More pages available — continue pagination [B-5.50]
        m_syncToken      = nextSyncToken;
        m_syncRetryCount = 0; // reset per-page retry counter
        issueSyncRequest(m_syncToken);
        return;
    }

    // All sync pages consumed.
    if (m_importCompleted) {
        // [B-5.46] Also fetch completed tasks from the dedicated endpoint.
        m_completedOffset = 0;
        m_completedRetry  = 0;
        issueCompletedRequest(0);
    } else {
        emit finished(m_accum, QString{});
    }
}

// ---------------------------------------------------------------------------
// Completed endpoint [B-5.46]
// ---------------------------------------------------------------------------

void TodoistApiClient::issueCompletedRequest(int offset)
{
    if (m_completedReply) {
        m_completedReply->abort();
        m_completedReply->deleteLater();
        m_completedReply = nullptr;
    }

    QNetworkRequest req{QUrl(QLatin1String(COMPLETED_API_URL))};
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + m_apiToken.toUtf8());
    req.setRawHeader(QByteArrayLiteral("User-Agent"),
                     QByteArrayLiteral("OpenTodo/0.1 (https://github.com/beanbo/opentodo)"));

    // GET request with query parameters
    QUrl url{QLatin1String(COMPLETED_API_URL)};  // braces avoid the vexing-parse interpretation as fn decl
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("limit"),  QStringLiteral("200"));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    url.setQuery(q);

    QNetworkRequest getReq{url};
    getReq.setRawHeader(QByteArrayLiteral("Authorization"),
                        QByteArrayLiteral("Bearer ") + m_apiToken.toUtf8());
    getReq.setRawHeader(QByteArrayLiteral("User-Agent"),
                        QByteArrayLiteral("OpenTodo/0.1 (https://github.com/beanbo/opentodo)"));

    m_completedReply = m_nam->get(getReq);
    connect(m_completedReply, &QNetworkReply::finished,
            this, &TodoistApiClient::onCompletedReplyFinished);
}

void TodoistApiClient::onCompletedReplyFinished()
{
    const QByteArray   rawData   = m_completedReply->readAll();
    const QNetworkReply::NetworkError netErr = m_completedReply->error();
    const int httpStatus =
        m_completedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // [B-5.49] 429 retry for completed endpoint
    if (httpStatus == 429) {
        const QByteArray retryHeader = m_completedReply->rawHeader(QByteArrayLiteral("Retry-After"));
        m_completedReply->deleteLater();
        m_completedReply = nullptr;

        if (m_completedRetry < MAX_RETRIES) {
            ++m_completedRetry;
            int retryAfterSec = retryHeader.isEmpty() ? 5 : retryHeader.toInt();
            if (retryAfterSec <= 0) retryAfterSec = 5;
            const int offset = m_completedOffset;
            QTimer::singleShot(retryAfterSec * 1000, this,
                               [this, offset]() { issueCompletedRequest(offset); });
            return;
        }
        // 429 on completed endpoint is non-fatal — emit what we have so far.
        emit finished(m_accum, QString{});
        return;
    }

    m_completedReply->deleteLater();
    m_completedReply = nullptr;

    if (netErr != QNetworkReply::NoError || httpStatus != 200) {
        // Completed endpoint unavailable — emit what we have (non-fatal)
        emit finished(m_accum, QString{});
        return;
    }

    QString parseError;
    bool hasMore = false;
    parseCompletedResponse(rawData, parseError, hasMore);

    if (!parseError.isEmpty() || !hasMore) {
        emit finished(m_accum, QString{});
        return;
    }

    // More completed pages [B-5.46 pagination]
    m_completedOffset += 200;
    m_completedRetry   = 0;
    issueCompletedRequest(m_completedOffset);
}

// ---------------------------------------------------------------------------
// JSON → TodoistImportData (sync endpoint) [B-5.53]
// ---------------------------------------------------------------------------

TodoistImportData TodoistApiClient::parseSyncResponse(const QByteArray &json,
                                                       QString &error,
                                                       QString &nextSyncToken,
                                                       bool    &fullSync)
{
    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError) {
        error = QStringLiteral("Ошибка разбора JSON: %1").arg(jsonErr.errorString());
        return {};
    }

    const QJsonObject root = doc.object();

    // Pagination tokens [B-5.50]
    nextSyncToken = jStr(root, QStringLiteral("sync_token"));
    fullSync      = jBool(root, QStringLiteral("full_sync"), true);

    TodoistImportData data;

    // ── Projects ────────────────────────────────────────────────────────────
    for (const QJsonValue &v : root.value(QStringLiteral("projects")).toArray()) {
        const QJsonObject o = v.toObject();
        if (jBool(o, QStringLiteral("is_deleted"))) continue;

        TodoistProject p;
        p.id         = jStr(o, QStringLiteral("id"));
        p.name       = jStr(o, QStringLiteral("name"));
        p.color      = jStr(o, QStringLiteral("color"));
        p.parentId   = jStr(o, QStringLiteral("parent_id"));
        p.order      = jInt(o, QStringLiteral("child_order"));
        p.isFavorite = jBool(o, QStringLiteral("is_favorite"));
        p.isArchived = jBool(o, QStringLiteral("is_archived"));
        p.isInbox    = jBool(o, QStringLiteral("inbox_project"));
        data.projects.append(p);
    }

    // ── Labels ──────────────────────────────────────────────────────────────
    for (const QJsonValue &v : root.value(QStringLiteral("labels")).toArray()) {
        const QJsonObject o = v.toObject();
        if (jBool(o, QStringLiteral("is_deleted"))) continue;

        TodoistLabel l;
        l.id    = jStr(o, QStringLiteral("id"));
        l.name  = jStr(o, QStringLiteral("name"));
        l.color = jStr(o, QStringLiteral("color"));
        l.order = jInt(o, QStringLiteral("item_order"));
        data.labels.append(l);
    }

    // ── Sections ────────────────────────────────────────────────────────────
    for (const QJsonValue &v : root.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject o = v.toObject();
        if (jBool(o, QStringLiteral("is_deleted"))) continue;

        TodoistSection s;
        s.id        = jStr(o, QStringLiteral("id"));
        s.projectId = jStr(o, QStringLiteral("project_id"));
        s.name      = jStr(o, QStringLiteral("name"));
        s.order     = jInt(o, QStringLiteral("section_order"));
        data.sections.append(s);
    }

    // ── Items (tasks) ─────────────────────────────────────────────────────
    for (const QJsonValue &v : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        if (jBool(o, QStringLiteral("is_deleted"))) continue;

        TodoistTask t;
        t.id          = jStr(o, QStringLiteral("id"));
        t.content     = jStr(o, QStringLiteral("content"));
        t.description = jStr(o, QStringLiteral("description"));
        t.projectId   = jStr(o, QStringLiteral("project_id"));
        t.sectionId   = jStr(o, QStringLiteral("section_id"));
        t.parentId    = jStr(o, QStringLiteral("parent_id"));
        // Todoist Sync API uses 4=urgent … 1=normal; OpenTodo internal is the
        // inverse (1=urgent … 4=normal), so invert here. A missing value
        // defaults to API-normal (1) -> internal 4 (normal). [priority-inversion fix]
        t.priority    = 5 - jInt(o, QStringLiteral("priority"), 1);
        t.order       = jInt(o, QStringLiteral("child_order"));
        t.isCompleted = jBool(o, QStringLiteral("checked")) ||
                        jBool(o, QStringLiteral("is_completed"));
        t.createdAt   = jDateTime(o, QStringLiteral("added_at"));
        if (!t.createdAt.isValid())
            t.createdAt = jDateTime(o, QStringLiteral("date_added"));

        // [B-5.53] Read completed_at when present
        t.completedAt = jDateTime(o, QStringLiteral("completed_at"));

        // Labels: API v9 returns an array of label *names* for personal labels
        for (const QJsonValue &lv : o.value(QStringLiteral("labels")).toArray())
            t.labelNames.append(lv.toString());

        t.due = parseDueObject(o.value(QStringLiteral("due")));

        // Duration
        const QJsonValue durVal = o.value(QStringLiteral("duration"));
        if (durVal.isObject()) {
            const QJsonObject dur = durVal.toObject();
            t.durationAmount = jInt(dur, QStringLiteral("amount"));
            t.durationUnit   = jStr(dur, QStringLiteral("unit"));
        }

        data.tasks.append(t);
    }

    // ── Notes (comments) ────────────────────────────────────────────────────
    for (const QJsonValue &v : root.value(QStringLiteral("notes")).toArray()) {
        const QJsonObject o = v.toObject();
        if (jBool(o, QStringLiteral("is_deleted"))) continue;

        TodoistNote n;
        n.id        = jStr(o, QStringLiteral("id"));
        n.taskId    = jStr(o, QStringLiteral("item_id"));
        n.projectId = jStr(o, QStringLiteral("project_id"));
        n.content   = jStr(o, QStringLiteral("content"));
        n.postedAt  = jDateTime(o, QStringLiteral("posted_at"));
        if (!n.postedAt.isValid())
            n.postedAt = jDateTime(o, QStringLiteral("date_added"));
        data.notes.append(n);
    }

    return data;
}

// ---------------------------------------------------------------------------
// JSON → TodoistImportData (completed endpoint) [B-5.46, B-5.53]
// ---------------------------------------------------------------------------

void TodoistApiClient::parseCompletedResponse(const QByteArray &json,
                                               QString &error,
                                               bool    &hasMore)
{
    hasMore = false;
    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError) {
        error = QStringLiteral("Ошибка разбора JSON (completed): %1").arg(jsonErr.errorString());
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    hasMore = !items.isEmpty(); // if we got a full page, assume there may be more

    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();

        TodoistTask t;
        t.id          = jStr(o, QStringLiteral("id"));
        t.content     = jStr(o, QStringLiteral("content"));
        t.projectId   = jStr(o, QStringLiteral("project_id"));
        t.sectionId   = jStr(o, QStringLiteral("section_id"));
        // Invert Todoist API priority (4=urgent) to OpenTodo internal
        // (1=urgent). Missing -> 1 -> internal 4 (normal). [priority-inversion fix]
        t.priority    = 5 - jInt(o, QStringLiteral("priority"), 1);
        t.isCompleted = true;
        t.createdAt   = jDateTime(o, QStringLiteral("added_at"));
        if (!t.createdAt.isValid())
            t.createdAt = jDateTime(o, QStringLiteral("date_added"));
        // [B-5.53] completed_at from the completed endpoint
        t.completedAt = jDateTime(o, QStringLiteral("completed_at"));
        if (!t.completedAt.isValid())
            t.completedAt = jDateTime(o, QStringLiteral("completed_date"));

        for (const QJsonValue &lv : o.value(QStringLiteral("labels")).toArray())
            t.labelNames.append(lv.toString());

        m_accum.tasks.append(t);
    }
}
