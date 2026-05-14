#include "project.h"
#include "utils/uuid.h"

Project::Project(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Project::id() const { return m_id; }
void Project::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Project::name() const { return m_name; }
void Project::setName(const QString &name) { if (m_name != name) { m_name = name; emit nameChanged(); } }

QString Project::color() const { return m_color; }
void Project::setColor(const QString &color) { if (m_color != color) { m_color = color; emit colorChanged(); } }

QString Project::description() const { return m_description; }
void Project::setDescription(const QString &description) { if (m_description != description) { m_description = description; emit descriptionChanged(); } }

QString Project::parentId() const { return m_parentId; }
void Project::setParentId(const QString &parentId) { if (m_parentId != parentId) { m_parentId = parentId; emit parentIdChanged(); } }

int Project::order() const { return m_order; }
void Project::setOrder(int order) { if (m_order != order) { m_order = order; emit orderChanged(); } }

bool Project::isFavorite() const { return m_isFavorite; }
void Project::setIsFavorite(bool fav) { if (m_isFavorite != fav) { m_isFavorite = fav; emit isFavoriteChanged(); } }

bool Project::isInbox() const { return m_isInbox; }
void Project::setIsInbox(bool inbox) { if (m_isInbox != inbox) { m_isInbox = inbox; emit isInboxChanged(); } }

bool Project::isArchived() const { return m_isArchived; }
void Project::setIsArchived(bool archived) { if (m_isArchived != archived) { m_isArchived = archived; emit isArchivedChanged(); } }

QString Project::viewStyle() const { return m_viewStyle; }
void Project::setViewStyle(const QString &style) { if (m_viewStyle != style) { m_viewStyle = style; emit viewStyleChanged(); } }

QDateTime Project::createdAt() const { return m_createdAt; }
void Project::setCreatedAt(const QDateTime &dt) { if (m_createdAt != dt) { m_createdAt = dt; emit createdAtChanged(); } }

QDateTime Project::updatedAt() const { return m_updatedAt; }
void Project::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

bool Project::isDeleted() const { return m_isDeleted; }
void Project::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QString Project::icon() const { return m_icon; }
void Project::setIcon(const QString &icon) { if (m_icon != icon) { m_icon = icon; emit iconChanged(); } }

int Project::weeklyGoal() const { return m_weeklyGoal; }
void Project::setWeeklyGoal(int goal) { if (m_weeklyGoal != goal) { m_weeklyGoal = goal; emit weeklyGoalChanged(); } }

QString Project::notes() const { return m_notes; }
void Project::setNotes(const QString &notes) { if (m_notes != notes) { m_notes = notes; emit notesChanged(); } }

QJsonObject Project::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("color")] = m_color;
    obj[QStringLiteral("description")] = m_description;
    if (!m_parentId.isEmpty()) obj[QStringLiteral("parent_id")] = m_parentId;
    obj[QStringLiteral("order")] = m_order;
    obj[QStringLiteral("is_favorite")] = m_isFavorite;
    obj[QStringLiteral("is_inbox")] = m_isInbox;
    obj[QStringLiteral("is_archived")] = m_isArchived;
    obj[QStringLiteral("view_style")] = m_viewStyle;
    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    if (!m_icon.isEmpty()) obj[QStringLiteral("icon")] = m_icon;
    if (m_weeklyGoal > 0) obj[QStringLiteral("weekly_goal")] = m_weeklyGoal;
    if (!m_notes.isEmpty()) obj[QStringLiteral("notes")] = m_notes;
    return obj;
}

Project *Project::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *p = new Project(parent);
    p->m_id = json[QStringLiteral("id")].toString();
    p->m_name = json[QStringLiteral("name")].toString();
    p->m_color = json[QStringLiteral("color")].toString(QStringLiteral("grey"));
    p->m_description = json[QStringLiteral("description")].toString();
    p->m_parentId = json[QStringLiteral("parent_id")].toString();
    p->m_order = json[QStringLiteral("order")].toInt();
    p->m_isFavorite = json[QStringLiteral("is_favorite")].toBool();
    p->m_isInbox = json[QStringLiteral("is_inbox")].toBool();
    p->m_isArchived = json[QStringLiteral("is_archived")].toBool();
    p->m_viewStyle = json[QStringLiteral("view_style")].toString(QStringLiteral("list"));
    if (json.contains(QStringLiteral("created_at")))
        p->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        p->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    p->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    p->m_icon = json[QStringLiteral("icon")].toString();
    p->m_weeklyGoal = json[QStringLiteral("weekly_goal")].toInt(0);
    p->m_notes = json[QStringLiteral("notes")].toString();
    return p;
}
