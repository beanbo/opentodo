#include "template.h"
#include "utils/uuid.h"

#include <QJsonDocument>

Template::Template(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Template::id() const { return m_id; }
void Template::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Template::name() const { return m_name; }
void Template::setName(const QString &name) { if (m_name != name) { m_name = name; emit nameChanged(); } }

QJsonArray Template::items() const { return m_items; }
void Template::setItems(const QJsonArray &items)
{
    if (m_items != items) { m_items = items; emit itemsChanged(); }
}
int Template::itemCount() const { return m_items.size(); }

QDateTime Template::createdAt() const { return m_createdAt; }
void Template::setCreatedAt(const QDateTime &dt) { if (m_createdAt != dt) { m_createdAt = dt; emit createdAtChanged(); } }

QDateTime Template::updatedAt() const { return m_updatedAt; }
void Template::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

bool Template::isDeleted() const { return m_isDeleted; }
void Template::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QJsonObject Template::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("items")] = m_items;
    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

Template *Template::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *t = new Template(parent);
    t->m_id = json[QStringLiteral("id")].toString();
    t->m_name = json[QStringLiteral("name")].toString();
    t->m_items = json[QStringLiteral("items")].toArray();
    if (json.contains(QStringLiteral("created_at")))
        t->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        t->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    t->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return t;
}

void Template::applyJson(const QJsonObject &json)
{
    if (json.contains(QStringLiteral("name")))
        setName(json[QStringLiteral("name")].toString());
    if (json.contains(QStringLiteral("items")))
        setItems(json[QStringLiteral("items")].toArray());
    if (json.contains(QStringLiteral("is_deleted")))
        setIsDeleted(json[QStringLiteral("is_deleted")].toBool());
}
