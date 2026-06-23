#include "filter.h"
#include "utils/uuid.h"

Filter::Filter(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Filter::id() const { return m_id; }
void Filter::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Filter::name() const { return m_name; }
void Filter::setName(const QString &name) { if (m_name != name) { m_name = name; emit nameChanged(); } }

QString Filter::query() const { return m_query; }
void Filter::setQuery(const QString &query) { if (m_query != query) { m_query = query; emit queryChanged(); } }

QString Filter::color() const { return m_color; }
void Filter::setColor(const QString &color) { if (m_color != color) { m_color = color; emit colorChanged(); } }

int Filter::order() const { return m_order; }
void Filter::setOrder(int order) { if (m_order != order) { m_order = order; emit orderChanged(); } }

bool Filter::isFavorite() const { return m_isFavorite; }
void Filter::setIsFavorite(bool fav) { if (m_isFavorite != fav) { m_isFavorite = fav; emit isFavoriteChanged(); } }

QDateTime Filter::createdAt() const { return m_createdAt; }
void Filter::setCreatedAt(const QDateTime &dt) { if (m_createdAt != dt) { m_createdAt = dt; emit createdAtChanged(); } }

QDateTime Filter::updatedAt() const { return m_updatedAt; }
void Filter::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

bool Filter::isDeleted() const { return m_isDeleted; }
void Filter::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QJsonObject Filter::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("query")] = m_query;
    obj[QStringLiteral("color")] = m_color;
    obj[QStringLiteral("order")] = m_order;
    obj[QStringLiteral("is_favorite")] = m_isFavorite;
    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

Filter *Filter::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *f = new Filter(parent);
    f->m_id = json[QStringLiteral("id")].toString();
    f->m_name = json[QStringLiteral("name")].toString();
    f->m_query = json[QStringLiteral("query")].toString();
    f->m_color = json[QStringLiteral("color")].toString(QStringLiteral("charcoal"));
    f->m_order = json[QStringLiteral("order")].toInt();
    f->m_isFavorite = json[QStringLiteral("is_favorite")].toBool();
    if (json.contains(QStringLiteral("created_at")))
        f->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        f->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    f->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return f;
}

void Filter::applyJson(const QJsonObject &json)
{
    if (json.contains(QStringLiteral("name")))
        setName(json[QStringLiteral("name")].toString());
    if (json.contains(QStringLiteral("query")))
        setQuery(json[QStringLiteral("query")].toString());
    if (json.contains(QStringLiteral("color")))
        setColor(json[QStringLiteral("color")].toString());
    if (json.contains(QStringLiteral("order")))
        setOrder(json[QStringLiteral("order")].toInt());
    if (json.contains(QStringLiteral("is_deleted")))
        setIsDeleted(json[QStringLiteral("is_deleted")].toBool());
    if (json.contains(QStringLiteral("is_favorite")))
        setIsFavorite(json[QStringLiteral("is_favorite")].toBool());
}
