#include "reminder.h"
#include "utils/uuid.h"

// --- LocationObject ---

QJsonObject LocationObject::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("latitude")] = latitude;
    obj[QStringLiteral("longitude")] = longitude;
    obj[QStringLiteral("radius")] = radius;
    obj[QStringLiteral("trigger")] = trigger;
    return obj;
}

LocationObject LocationObject::fromJson(const QJsonObject &json)
{
    LocationObject loc;
    loc.name = json[QStringLiteral("name")].toString();
    loc.latitude = json[QStringLiteral("latitude")].toDouble();
    loc.longitude = json[QStringLiteral("longitude")].toDouble();
    loc.radius = json[QStringLiteral("radius")].toInt(100);
    loc.trigger = json[QStringLiteral("trigger")].toString(QStringLiteral("on_enter"));
    return loc;
}

// --- ReminderDue ---

QJsonObject ReminderDue::toJson() const
{
    QJsonObject obj;
    if (!date.isEmpty()) obj[QStringLiteral("date")] = date;
    if (!datetime.isEmpty()) obj[QStringLiteral("datetime")] = datetime;
    if (!timezone.isEmpty()) obj[QStringLiteral("timezone")] = timezone;
    if (!lang.isEmpty()) obj[QStringLiteral("lang")] = lang;
    return obj;
}

ReminderDue ReminderDue::fromJson(const QJsonObject &json)
{
    ReminderDue d;
    d.date = json[QStringLiteral("date")].toString();
    d.datetime = json[QStringLiteral("datetime")].toString();
    d.timezone = json[QStringLiteral("timezone")].toString();
    d.lang = json[QStringLiteral("lang")].toString(QStringLiteral("en"));
    return d;
}

// --- Reminder ---

Reminder::Reminder(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Reminder::id() const { return m_id; }
void Reminder::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Reminder::taskId() const { return m_taskId; }
void Reminder::setTaskId(const QString &taskId) { if (m_taskId != taskId) { m_taskId = taskId; emit taskIdChanged(); } }

QString Reminder::type() const { return m_type; }
void Reminder::setType(const QString &type) { if (m_type != type) { m_type = type; emit typeChanged(); } }

ReminderDue Reminder::due() const { return m_due; }
void Reminder::setDue(const ReminderDue &due) { m_due = due; emit dueChanged(); }

int Reminder::minuteOffset() const { return m_minuteOffset; }
void Reminder::setMinuteOffset(int offset) { if (m_minuteOffset != offset) { m_minuteOffset = offset; emit minuteOffsetChanged(); } }

LocationObject Reminder::location() const { return m_location; }
void Reminder::setLocation(const LocationObject &loc) { m_location = loc; emit locationChanged(); }

QDateTime Reminder::createdAt() const { return m_createdAt; }
void Reminder::setCreatedAt(const QDateTime &dt) { if (m_createdAt != dt) { m_createdAt = dt; emit createdAtChanged(); } }

QDateTime Reminder::updatedAt() const { return m_updatedAt; }
void Reminder::setUpdatedAt(const QDateTime &dt) { if (m_updatedAt != dt) { m_updatedAt = dt; emit updatedAtChanged(); } }

bool Reminder::isDeleted() const { return m_isDeleted; }
void Reminder::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

QString Reminder::recurrenceRule() const { return m_recurrenceRule; }
void Reminder::setRecurrenceRule(const QString &rule)
{
    if (m_recurrenceRule != rule) {
        m_recurrenceRule = rule;
        emit recurrenceRuleChanged();
    }
}

QJsonObject Reminder::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("task_id")] = m_taskId;
    obj[QStringLiteral("type")] = m_type;

    if (m_type == QStringLiteral("absolute") && !m_due.isNull())
        obj[QStringLiteral("due")] = m_due.toJson();

    if (m_type == QStringLiteral("relative"))
        obj[QStringLiteral("minute_offset")] = m_minuteOffset;

    if (m_type == QStringLiteral("location") && !m_location.isNull())
        obj[QStringLiteral("location")] = m_location.toJson();

    if (!m_recurrenceRule.isEmpty())
        obj[QStringLiteral("recurrence_rule")] = m_recurrenceRule;

    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    return obj;
}

// Convenience setters for QML — see header for rationale.
void Reminder::setDueDatetime(const QString &isoDatetime,
                              const QString &timezone,
                              const QString &lang)
{
    ReminderDue d = m_due;
    d.datetime = isoDatetime;
    if (!timezone.isEmpty()) d.timezone = timezone;
    if (!lang.isEmpty())     d.lang     = lang;
    setDue(d);
    if (m_type != QStringLiteral("absolute")) {
        m_type = QStringLiteral("absolute");
        emit typeChanged();
    }
}

void Reminder::setRepeatRule(const QString &rule)
{
    setRecurrenceRule(rule);
}

void Reminder::setLocationCoords(double lat, double lon, int radiusMeters,
                                 const QString &name,
                                 const QString &trigger)
{
    LocationObject loc = m_location;
    loc.latitude  = lat;
    loc.longitude = lon;
    loc.radius    = radiusMeters > 0 ? radiusMeters : 200;
    if (!name.isEmpty())    loc.name    = name;
    if (!trigger.isEmpty()) loc.trigger = trigger;
    setLocation(loc);
    if (m_type != QStringLiteral("location")) {
        m_type = QStringLiteral("location");
        emit typeChanged();
    }
}

Reminder *Reminder::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *r = new Reminder(parent);
    r->m_id = json[QStringLiteral("id")].toString();
    r->m_taskId = json[QStringLiteral("task_id")].toString();
    r->m_type = json[QStringLiteral("type")].toString(QStringLiteral("absolute"));

    if (json.contains(QStringLiteral("due")))
        r->m_due = ReminderDue::fromJson(json[QStringLiteral("due")].toObject());

    r->m_minuteOffset = json[QStringLiteral("minute_offset")].toInt();

    if (json.contains(QStringLiteral("location")))
        r->m_location = LocationObject::fromJson(json[QStringLiteral("location")].toObject());

    r->m_recurrenceRule = json[QStringLiteral("recurrence_rule")].toString();
    if (json.contains(QStringLiteral("created_at")))
        r->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        r->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);
    r->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    return r;
}
