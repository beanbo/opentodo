#include "label.h"
#include "utils/uuid.h"

Label::Label(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
}

QString Label::id() const { return m_id; }
void Label::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Label::name() const { return m_name; }
void Label::setName(const QString &name) { if (m_name != name) { m_name = name; emit nameChanged(); } }

QString Label::color() const { return m_color; }
void Label::setColor(const QString &color) { if (m_color != color) { m_color = color; emit colorChanged(); } }

int Label::order() const { return m_order; }
void Label::setOrder(int order) { if (m_order != order) { m_order = order; emit orderChanged(); } }

bool Label::isFavorite() const { return m_isFavorite; }
void Label::setIsFavorite(bool fav) { if (m_isFavorite != fav) { m_isFavorite = fav; emit isFavoriteChanged(); } }

bool Label::isDeleted() const { return m_isDeleted; }
void Label::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QJsonObject Label::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("color")] = m_color;
    obj[QStringLiteral("order")] = m_order;
    obj[QStringLiteral("is_favorite")] = m_isFavorite;
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

Label *Label::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *l = new Label(parent);
    l->m_id = json[QStringLiteral("id")].toString();
    l->m_name = json[QStringLiteral("name")].toString();
    l->m_color = json[QStringLiteral("color")].toString(QStringLiteral("charcoal"));
    l->m_order = json[QStringLiteral("order")].toInt();
    l->m_isFavorite = json[QStringLiteral("is_favorite")].toBool();
    l->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return l;
}

void Label::applyJson(const QJsonObject &json)
{
    if (json.contains(QStringLiteral("color")))
        setColor(json[QStringLiteral("color")].toString());
    if (json.contains(QStringLiteral("order")))
        setOrder(json[QStringLiteral("order")].toInt());
    if (json.contains(QStringLiteral("is_deleted")))
        setIsDeleted(json[QStringLiteral("is_deleted")].toBool());
    if (json.contains(QStringLiteral("is_favorite")))
        setIsFavorite(json[QStringLiteral("is_favorite")].toBool());
    // name excluded — applyCommit uses DataStore::renameLabel.
}
