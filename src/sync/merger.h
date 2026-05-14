#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>

/**
 * @brief Conflict resolution for sync states.
 *
 * Merge strategy (new CRDT scheme):
 *  - Each scalar field carries per-field metadata { ts (HLC), dev (deviceId) }
 *    in entity["_meta"][fieldName].
 *  - Per-field LWW register: the field with the greater HLC wins (deviceId
 *    is the tiebreak when HLC timestamps are equal).
 *  - OR-Set semantics for "labels" (add-wins with per-element addedAt/removedAt).
 *  - Tombstones always win: is_deleted=true on either side → deleted entity.
 *  - Settings: whole-object LWW (remote wins), unchanged.
 *  - Legacy entities without "_meta": fall back to whole-object LWW on
 *    updated_at, same as before.
 */
class Merger : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Merger(QObject *parent = nullptr);

    QJsonObject mergeStates(const QJsonObject &local, const QJsonObject &remote);
    QJsonArray  mergeEntityArrays(const QJsonArray &localArr, const QJsonArray &remoteArr);
    QJsonObject mergeEntity(const QJsonObject &local, const QJsonObject &remote);

private:
    // CRDT per-field merge (used when either entity has "_meta").
    QJsonObject resolveWithCrdt(const QJsonObject &local, const QJsonObject &remote);

    // Legacy whole-object LWW fallback (no "_meta" on either side).
    QJsonObject resolveWithLww(const QJsonObject &local, const QJsonObject &remote);

    bool isNewer(const QJsonObject &a, const QJsonObject &b) const;
    QDateTime parseTimestamp(const QJsonObject &obj) const;

    // Set of field names to skip when iterating "normal" fields
    // (handled separately: _meta, is_deleted).
    static bool isInternalField(const QString &key);
};
