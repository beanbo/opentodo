#include "section.h"
#include "utils/uuid.h"

Section::Section(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Section::id() const { return m_id; }
void Section::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Section::projectId() const { return m_projectId; }
void Section::setProjectId(const QString &projectId) { if (m_projectId != projectId) { m_projectId = projectId; emit projectIdChanged(); } }

QString Section::name() const { return m_name; }
void Section::setName(const QString &name) { if (m_name != name) { m_name = name; emit nameChanged(); } }

int Section::order() const { return m_order; }
void Section::setOrder(int order) { if (m_order != order) { m_order = order; emit orderChanged(); } }

bool Section::isCollapsed() const { return m_isCollapsed; }
void Section::setIsCollapsed(bool collapsed) { if (m_isCollapsed != collapsed) { m_isCollapsed = collapsed; emit isCollapsedChanged(); } }

bool Section::isArchived() const { return m_isArchived; }
void Section::setIsArchived(bool archived) { if (m_isArchived != archived) { m_isArchived = archived; emit isArchivedChanged(); } }

QDateTime Section::createdAt() const { return m_createdAt; }
void Section::setCreatedAt(const QDateTime &dt) { if (m_createdAt != dt) { m_createdAt = dt; emit createdAtChanged(); } }

QDateTime Section::updatedAt() const { return m_updatedAt; }
void Section::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

bool Section::isDeleted() const { return m_isDeleted; }
void Section::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QJsonObject Section::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("project_id")] = m_projectId;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("order")] = m_order;
    obj[QStringLiteral("is_collapsed")] = m_isCollapsed;
    obj[QStringLiteral("is_archived")] = m_isArchived;
    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

Section *Section::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *s = new Section(parent);
    s->m_id = json[QStringLiteral("id")].toString();
    s->m_projectId = json[QStringLiteral("project_id")].toString();
    s->m_name = json[QStringLiteral("name")].toString();
    s->m_order = json[QStringLiteral("order")].toInt();
    s->m_isCollapsed = json[QStringLiteral("is_collapsed")].toBool();
    s->m_isArchived = json[QStringLiteral("is_archived")].toBool();
    if (json.contains(QStringLiteral("created_at")))
        s->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        s->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    s->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return s;
}
