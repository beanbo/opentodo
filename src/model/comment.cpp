#include "comment.h"
#include "utils/uuid.h"

// --- AttachmentObject ---

QJsonObject AttachmentObject::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("file_name")] = fileName;
    obj[QStringLiteral("file_type")] = fileType;
    obj[QStringLiteral("file_url")] = fileUrl;
    obj[QStringLiteral("file_size")] = fileSize;
    return obj;
}

AttachmentObject AttachmentObject::fromJson(const QJsonObject &json)
{
    AttachmentObject a;
    a.fileName = json[QStringLiteral("file_name")].toString();
    a.fileType = json[QStringLiteral("file_type")].toString();
    a.fileUrl = json[QStringLiteral("file_url")].toString();
    a.fileSize = json[QStringLiteral("file_size")].toInt();
    return a;
}

// --- Comment ---

Comment::Comment(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_postedAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_postedAt;
}

QString Comment::id() const { return m_id; }
void Comment::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Comment::taskId() const { return m_taskId; }
void Comment::setTaskId(const QString &taskId) { if (m_taskId != taskId) { m_taskId = taskId; emit taskIdChanged(); } }

QString Comment::projectId() const { return m_projectId; }
void Comment::setProjectId(const QString &projectId) { if (m_projectId != projectId) { m_projectId = projectId; emit projectIdChanged(); } }

QString Comment::content() const { return m_content; }
void Comment::setContent(const QString &content) { if (m_content != content) { m_content = content; emit contentChanged(); } }

QDateTime Comment::postedAt() const { return m_postedAt; }
void Comment::setPostedAt(const QDateTime &dt) { if (m_postedAt != dt) { m_postedAt = dt; emit postedAtChanged(); } }

QDateTime Comment::updatedAt() const { return m_updatedAt; }
void Comment::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

AttachmentObject Comment::attachment() const { return m_attachment; }
void Comment::setAttachment(const AttachmentObject &att) { m_attachment = att; emit attachmentChanged(); }

bool Comment::isDeleted() const { return m_isDeleted; }
void Comment::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QJsonObject Comment::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    if (!m_taskId.isEmpty()) obj[QStringLiteral("task_id")] = m_taskId;
    if (!m_projectId.isEmpty()) obj[QStringLiteral("project_id")] = m_projectId;
    obj[QStringLiteral("content")] = m_content;
    obj[QStringLiteral("posted_at")] = m_postedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    if (!m_attachment.isNull())
        obj[QStringLiteral("attachment")] = m_attachment.toJson();
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

Comment *Comment::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *c = new Comment(parent);
    c->m_id = json[QStringLiteral("id")].toString();
    c->m_taskId = json[QStringLiteral("task_id")].toString();
    c->m_projectId = json[QStringLiteral("project_id")].toString();
    c->m_content = json[QStringLiteral("content")].toString();
    if (json.contains(QStringLiteral("posted_at")))
        c->m_postedAt = QDateTime::fromString(json[QStringLiteral("posted_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        c->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("attachment")))
        c->m_attachment = AttachmentObject::fromJson(json[QStringLiteral("attachment")].toObject());
    c->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return c;
}
