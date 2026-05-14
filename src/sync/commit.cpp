#include "commit.h"
#include "utils/uuid.h"

// --- SyncOperation ---

QJsonObject SyncOperation::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("type")] = type;
    obj[QStringLiteral("entity_id")] = entityId;

    if (type.endsWith(QStringLiteral("_add"))) {
        obj[QStringLiteral("data")] = data;
    } else if (type.endsWith(QStringLiteral("_update"))) {
        obj[QStringLiteral("fields")] = data;
    }
    // delete operations have no data/fields

    return obj;
}

SyncOperation SyncOperation::fromJson(const QJsonObject &json)
{
    SyncOperation op;
    op.type = json[QStringLiteral("type")].toString();
    op.entityId = json[QStringLiteral("entity_id")].toString();

    if (json.contains(QStringLiteral("data")))
        op.data = json[QStringLiteral("data")].toObject();
    else if (json.contains(QStringLiteral("fields")))
        op.data = json[QStringLiteral("fields")].toObject();

    return op;
}

// --- Commit ---

Commit::Commit(QObject *parent)
    : QObject(parent)
{
    m_uuid = UuidHelper::generate();
    m_timestamp = QDateTime::currentDateTimeUtc();
}

QString Commit::uuid() const { return m_uuid; }
void Commit::setUuid(const QString &uuid) { if (m_uuid != uuid) { m_uuid = uuid; emit uuidChanged(); } }

QDateTime Commit::timestamp() const { return m_timestamp; }
void Commit::setTimestamp(const QDateTime &ts) { if (m_timestamp != ts) { m_timestamp = ts; emit timestampChanged(); } }

QString Commit::deviceId() const { return m_deviceId; }
void Commit::setDeviceId(const QString &deviceId) { if (m_deviceId != deviceId) { m_deviceId = deviceId; emit deviceIdChanged(); } }

QString Commit::parentCommit() const { return m_parentCommit; }
void Commit::setParentCommit(const QString &parent) { if (m_parentCommit != parent) { m_parentCommit = parent; emit parentCommitChanged(); } }

QString Commit::hlc() const { return m_hlc; }
void Commit::setHlc(const QString &hlc) { if (m_hlc != hlc) { m_hlc = hlc; emit hlcChanged(); } }

QList<SyncOperation> Commit::operations() const { return m_operations; }
void Commit::setOperations(const QList<SyncOperation> &ops) { m_operations = ops; }
void Commit::addOperation(const SyncOperation &op) { m_operations.append(op); }

QJsonObject Commit::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("uuid")] = m_uuid;
    obj[QStringLiteral("timestamp")] = m_timestamp.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("device_id")] = m_deviceId;
    if (!m_parentCommit.isEmpty())
        obj[QStringLiteral("parent_commit")] = m_parentCommit;
    if (!m_hlc.isEmpty())
        obj[QStringLiteral("hlc")] = m_hlc;

    QJsonArray opsArr;
    for (const auto &op : m_operations)
        opsArr.append(op.toJson());
    obj[QStringLiteral("operations")] = opsArr;

    return obj;
}

Commit *Commit::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *c = new Commit(parent);
    c->m_uuid = json[QStringLiteral("uuid")].toString();
    c->m_timestamp = QDateTime::fromString(json[QStringLiteral("timestamp")].toString(), Qt::ISODateWithMs);
    c->m_deviceId = json[QStringLiteral("device_id")].toString();
    c->m_parentCommit = json[QStringLiteral("parent_commit")].toString();
    // "hlc" is optional — older state files without it simply leave m_hlc empty.
    c->m_hlc = json[QStringLiteral("hlc")].toString();

    const auto opsArr = json[QStringLiteral("operations")].toArray();
    for (const auto &v : opsArr)
        c->m_operations.append(SyncOperation::fromJson(v.toObject()));

    return c;
}
