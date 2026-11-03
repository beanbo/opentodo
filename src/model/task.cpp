#include "task.h"
#include "utils/uuid.h"

// --- DueObject ---

QJsonObject DueObject::toJson() const
{
    QJsonObject obj;
    if (!date.isEmpty()) obj[QStringLiteral("date")] = date;
    if (!datetime.isEmpty()) obj[QStringLiteral("datetime")] = datetime;
    if (!timezone.isEmpty()) obj[QStringLiteral("timezone")] = timezone;
    if (!string.isEmpty()) obj[QStringLiteral("string")] = string;
    obj[QStringLiteral("is_recurring")] = isRecurring;
    if (!lang.isEmpty()) obj[QStringLiteral("lang")] = lang;
    if (!recurrenceRule.isEmpty()) obj[QStringLiteral("recurrence_rule")] = recurrenceRule;
    return obj;
}

DueObject DueObject::fromJson(const QJsonObject &json)
{
    DueObject d;
    d.date = json[QStringLiteral("date")].toString();
    d.datetime = json[QStringLiteral("datetime")].toString();
    d.timezone = json[QStringLiteral("timezone")].toString();
    d.string = json[QStringLiteral("string")].toString();
    d.isRecurring = json[QStringLiteral("is_recurring")].toBool();
    d.lang = json[QStringLiteral("lang")].toString(QStringLiteral("en"));
    d.recurrenceRule = json[QStringLiteral("recurrence_rule")].toString();
    return d;
}

// --- DurationObject ---

QJsonObject DurationObject::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("amount")] = amount;
    if (!unit.isEmpty()) obj[QStringLiteral("unit")] = unit;
    return obj;
}

DurationObject DurationObject::fromJson(const QJsonObject &json)
{
    DurationObject d;
    d.amount = json[QStringLiteral("amount")].toInt();
    d.unit = json[QStringLiteral("unit")].toString();
    return d;
}

// --- Task ---

Task::Task(QObject *parent)
    : QObject(parent)
{
    m_id = UuidHelper::generate();
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_updatedAt = m_createdAt;
}

QString Task::id() const { return m_id; }
void Task::setId(const QString &id) { if (m_id != id) { m_id = id; emit idChanged(); } }

QString Task::content() const { return m_content; }
void Task::setContent(const QString &content) { if (m_content != content) { m_content = content; emit contentChanged(); } }

QString Task::description() const { return m_description; }
void Task::setDescription(const QString &description) { if (m_description != description) { m_description = description; emit descriptionChanged(); } }

QString Task::projectId() const { return m_projectId; }
void Task::setProjectId(const QString &projectId) { if (m_projectId != projectId) { m_projectId = projectId; emit projectIdChanged(); } }

QString Task::sectionId() const { return m_sectionId; }
void Task::setSectionId(const QString &sectionId) { if (m_sectionId != sectionId) { m_sectionId = sectionId; emit sectionIdChanged(); } }

QString Task::parentId() const { return m_parentId; }
void Task::setParentId(const QString &parentId) { if (m_parentId != parentId) { m_parentId = parentId; emit parentIdChanged(); } }

int Task::order() const { return m_order; }
void Task::setOrder(int order) { if (m_order != order) { m_order = order; emit orderChanged(); } }

int Task::priority() const { return m_priority; }
void Task::setPriority(int priority) { if (m_priority != priority) { m_priority = priority; emit priorityChanged(); } }

QStringList Task::labels() const { return m_labels; }
void Task::setLabels(const QStringList &labels) { if (m_labels != labels) { m_labels = labels; emit labelsChanged(); } }

bool Task::isCompleted() const { return m_isCompleted; }
void Task::setIsCompleted(bool completed) {
    if (m_isCompleted != completed) {
        m_isCompleted = completed;
        if (completed) {
            m_completedAt = QDateTime::currentDateTimeUtc();
        } else {
            m_completedAt = QDateTime();
        }
        emit isCompletedChanged();
        emit completedAtChanged();
    }
}

QDateTime Task::completedAt() const { return m_completedAt; }
void Task::setCompletedAt(const QDateTime &completedAt) { if (m_completedAt != completedAt) { m_completedAt = completedAt; emit completedAtChanged(); } }

DueObject Task::due() const { return m_due; }
void Task::setDue(const DueObject &due) { m_due = due; emit dueChanged(); }

QString Task::dueDate() const { return m_due.date; }
void Task::setDueDate(const QString &date) { m_due.date = date; emit dueChanged(); }

QString Task::dueDatetime() const { return m_due.datetime; }
void Task::setDueDatetime(const QString &dt) { m_due.datetime = dt; emit dueChanged(); }

QString Task::dueTimezone() const { return m_due.timezone; }
void Task::setDueTimezone(const QString &tz) { m_due.timezone = tz; emit dueChanged(); }

QString Task::dueString() const { return m_due.string; }
void Task::setDueString(const QString &s) { m_due.string = s; emit dueChanged(); }

bool Task::dueIsRecurring() const { return m_due.isRecurring; }
void Task::setDueIsRecurring(bool r) { m_due.isRecurring = r; emit dueChanged(); }

QString Task::dueLang() const { return m_due.lang; }
void Task::setDueLang(const QString &l) { m_due.lang = l; emit dueChanged(); }

QString Task::dueRecurrenceRule() const { return m_due.recurrenceRule; }
void Task::setDueRecurrenceRule(const QString &rrule) {
    if (m_due.recurrenceRule != rrule) { m_due.recurrenceRule = rrule; emit dueChanged(); }
}

QString Task::deadline() const { return m_deadline; }
void Task::setDeadline(const QString &deadline) { if (m_deadline != deadline) { m_deadline = deadline; emit deadlineChanged(); } }

int Task::durationAmount() const { return m_duration.amount; }
void Task::setDurationAmount(int amount) { m_duration.amount = amount; emit durationChanged(); }

QString Task::durationUnit() const { return m_duration.unit; }
void Task::setDurationUnit(const QString &unit) { m_duration.unit = unit; emit durationChanged(); }

DurationObject Task::duration() const { return m_duration; }
void Task::setDuration(const DurationObject &dur) { m_duration = dur; emit durationChanged(); }

QDateTime Task::createdAt() const { return m_createdAt; }
void Task::setCreatedAt(const QDateTime &createdAt) { if (m_createdAt != createdAt) { m_createdAt = createdAt; emit createdAtChanged(); } }

QDateTime Task::updatedAt() const { return m_updatedAt; }
void Task::setUpdatedAt(const QDateTime &updatedAt) { if (m_updatedAt != updatedAt) { m_updatedAt = updatedAt; emit updatedAtChanged(); } }

bool Task::isDeleted() const { return m_isDeleted; }
void Task::setIsDeleted(bool deleted) { if (m_isDeleted != deleted) { m_isDeleted = deleted; emit isDeletedChanged(); } }

bool Task::isPurged() const { return m_isPurged; }
void Task::setIsPurged(bool purged) { if (m_isPurged != purged) { m_isPurged = purged; emit isPurgedChanged(); } }

int Task::dayOrder() const { return m_dayOrder; }
void Task::setDayOrder(int dayOrder) { if (m_dayOrder != dayOrder) { m_dayOrder = dayOrder; emit dayOrderChanged(); } }

QString Task::color() const { return m_color; }
void Task::setColor(const QString &c) { if (m_color != c) { m_color = c; emit colorChanged(); } }

bool Task::isPinned() const { return m_isPinned; }
void Task::setIsPinned(bool pinned) { if (m_isPinned != pinned) { m_isPinned = pinned; emit isPinnedChanged(); } }

int Task::estimateMinutes() const { return m_estimateMinutes; }
void Task::setEstimateMinutes(int minutes) { if (m_estimateMinutes != minutes) { m_estimateMinutes = minutes; emit estimateMinutesChanged(); } }

QJsonObject Task::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_id;
    obj[QStringLiteral("content")] = m_content;
    obj[QStringLiteral("description")] = m_description;
    obj[QStringLiteral("project_id")] = m_projectId;
    if (!m_sectionId.isEmpty()) obj[QStringLiteral("section_id")] = m_sectionId;
    if (!m_parentId.isEmpty()) obj[QStringLiteral("parent_id")] = m_parentId;
    obj[QStringLiteral("order")] = m_order;
    obj[QStringLiteral("priority")] = m_priority;

    QJsonArray labelsArr;
    for (const auto &l : m_labels) labelsArr.append(l);
    obj[QStringLiteral("labels")] = labelsArr;

    obj[QStringLiteral("is_completed")] = m_isCompleted;
    if (m_completedAt.isValid())
        obj[QStringLiteral("completed_at")] = m_completedAt.toString(Qt::ISODateWithMs);

    if (!m_due.isNull())
        obj[QStringLiteral("due")] = m_due.toJson();

    if (!m_deadline.isEmpty())
        obj[QStringLiteral("deadline")] = QJsonObject{{QStringLiteral("date"), m_deadline}};

    if (!m_duration.isNull())
        obj[QStringLiteral("duration")] = m_duration.toJson();

    obj[QStringLiteral("created_at")] = m_createdAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("updated_at")] = m_updatedAt.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("is_deleted")] = m_isDeleted;
    // Written only when true (like is_pinned) — almost no task is purged.
    if (m_isPurged) obj[QStringLiteral("is_purged")] = true;
    obj[QStringLiteral("day_order")] = m_dayOrder;
    if (!m_color.isEmpty()) obj[QStringLiteral("color")] = m_color;
    // Записываем is_pinned только когда true — не засоряем JSON для большинства задач
    if (m_isPinned) obj[QStringLiteral("is_pinned")] = true;
    // Записываем estimate_minutes только когда задана оценка (> 0)
    if (m_estimateMinutes > 0) obj[QStringLiteral("estimate_minutes")] = m_estimateMinutes;

    return obj;
}

Task *Task::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *task = new Task(parent);
    task->m_id = json[QStringLiteral("id")].toString();
    task->m_content = json[QStringLiteral("content")].toString();
    task->m_description = json[QStringLiteral("description")].toString();
    task->m_projectId = json[QStringLiteral("project_id")].toString();
    task->m_sectionId = json[QStringLiteral("section_id")].toString();
    task->m_parentId = json[QStringLiteral("parent_id")].toString();
    task->m_order = json[QStringLiteral("order")].toInt();
    task->m_priority = json[QStringLiteral("priority")].toInt(4); // default: normal (p4)

    const auto labelsArr = json[QStringLiteral("labels")].toArray();
    for (const auto &v : labelsArr)
        task->m_labels.append(v.toString());

    task->m_isCompleted = json[QStringLiteral("is_completed")].toBool();
    if (json.contains(QStringLiteral("completed_at")))
        task->m_completedAt = QDateTime::fromString(json[QStringLiteral("completed_at")].toString(), Qt::ISODateWithMs);

    if (json.contains(QStringLiteral("due")))
        task->m_due = DueObject::fromJson(json[QStringLiteral("due")].toObject());

    if (json.contains(QStringLiteral("deadline")))
        task->m_deadline = json[QStringLiteral("deadline")].toObject()[QStringLiteral("date")].toString();

    if (json.contains(QStringLiteral("duration")))
        task->m_duration = DurationObject::fromJson(json[QStringLiteral("duration")].toObject());

    if (json.contains(QStringLiteral("created_at")))
        task->m_createdAt = QDateTime::fromString(json[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (json.contains(QStringLiteral("updated_at")))
        task->m_updatedAt = QDateTime::fromString(json[QStringLiteral("updated_at")].toString(), Qt::ISODateWithMs);

    task->m_isDeleted = json[QStringLiteral("is_deleted")].toBool();
    task->m_isPurged = json[QStringLiteral("is_purged")].toBool();
    task->m_dayOrder = json[QStringLiteral("day_order")].toInt();
    task->m_color = json[QStringLiteral("color")].toString();
    // Отсутствующий ключ трактуется как false — незакреплённая задача по умолчанию
    task->m_isPinned = json[QStringLiteral("is_pinned")].toBool();
    // Отсутствующий ключ трактуется как 0 — без оценки по умолчанию
    task->m_estimateMinutes = json[QStringLiteral("estimate_minutes")].toInt(0);

    return task;
}

void Task::applyJson(const QJsonObject &json)
{
    // Setter-based (signals fire) partial application — mirrors exactly the
    // field set SyncEngine::applyCommit's task_update used to hand-write. Only
    // keys present are touched; parent_id cycle-guarding stays in applyCommit.
    if (json.contains(QStringLiteral("content")))
        setContent(json[QStringLiteral("content")].toString());
    if (json.contains(QStringLiteral("description")))
        setDescription(json[QStringLiteral("description")].toString());
    if (json.contains(QStringLiteral("priority")))
        setPriority(json[QStringLiteral("priority")].toInt());
    if (json.contains(QStringLiteral("is_completed")))
        setIsCompleted(json[QStringLiteral("is_completed")].toBool());
    if (json.contains(QStringLiteral("is_deleted")))
        setIsDeleted(json[QStringLiteral("is_deleted")].toBool());
    if (json.contains(QStringLiteral("is_purged")))
        setIsPurged(json[QStringLiteral("is_purged")].toBool());
    if (json.contains(QStringLiteral("project_id")))
        setProjectId(json[QStringLiteral("project_id")].toString());
    if (json.contains(QStringLiteral("section_id")))
        setSectionId(json[QStringLiteral("section_id")].toString());
    if (json.contains(QStringLiteral("parent_id")))
        setParentId(json[QStringLiteral("parent_id")].toString());
    if (json.contains(QStringLiteral("order")))
        setOrder(json[QStringLiteral("order")].toInt());
    if (json.contains(QStringLiteral("day_order")))
        setDayOrder(json[QStringLiteral("day_order")].toInt());
    if (json.contains(QStringLiteral("is_pinned")))
        setIsPinned(json[QStringLiteral("is_pinned")].toBool());
    if (json.contains(QStringLiteral("due")))
        setDue(DueObject::fromJson(json[QStringLiteral("due")].toObject()));
    if (json.contains(QStringLiteral("deadline"))) {
        const QJsonValue dv = json[QStringLiteral("deadline")];
        setDeadline(dv.isObject()
            ? dv.toObject().value(QStringLiteral("date")).toString()
            : dv.toString());
    }
    if (json.contains(QStringLiteral("labels"))) {
        QStringList labels;
        for (const auto &v : json[QStringLiteral("labels")].toArray())
            labels.append(v.toString());
        setLabels(labels);
    }
    if (json.contains(QStringLiteral("completed_at"))) {
        const QString iso = json[QStringLiteral("completed_at")].toString();
        setCompletedAt(iso.isEmpty()
            ? QDateTime()
            : QDateTime::fromString(iso, Qt::ISODateWithMs));
    }
    if (json.contains(QStringLiteral("color")))
        setColor(json[QStringLiteral("color")].toString());
    if (json.contains(QStringLiteral("duration")))
        setDuration(DurationObject::fromJson(json[QStringLiteral("duration")].toObject()));
    if (json.contains(QStringLiteral("estimate_minutes")))
        setEstimateMinutes(json[QStringLiteral("estimate_minutes")].toInt());
}
