#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>

class Telemetry;

/**
 * @brief Conflict resolution for sync states.
 *
 * Merge strategy (new CRDT scheme):
 *  - Each scalar field carries per-field metadata { ts (HLC), dev (deviceId) }
 *    in entity["_meta"][fieldName].
 *  - Per-field LWW register: the field with the greater HLC wins (deviceId
 *    is the tiebreak when HLC timestamps are equal).
 *  - OR-Set semantics for "labels" (add-wins with per-element addedAt/removedAt).
 *  - Tombstones win: is_deleted=true on either side → deleted entity, unless
 *    the live side carries a strictly newer `_meta.is_deleted` stamp (an
 *    explicit restore from Trash / undo-delete).
 *  - "labels" without OR-Set elements: scalar per-field LWW on its stamp.
 *  - Settings: per-key LWW on the top-level `settings_meta` stamps; keys
 *    stamped on neither side keep the legacy rule (remote wins if present).
 *  - Legacy entities without "_meta": fall back to whole-object LWW on
 *    updated_at, same as before.
 */
class Merger : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Merger(QObject *parent = nullptr);

    // Inject optional telemetry service. The merger uses it to surface
    // tombstone-absorbs-edit events (CRDT-correct but otherwise invisible
    // data loss). Null is acceptable (tests).
    void setTelemetry(Telemetry *telemetry);

    // Called by mergeEntity when a remote edit is dropped because the local
    // side is tombstoned. Exposed for the entity-array merge to delegate
    // observability decisions to a single hook. Public so external test
    // doubles could intercept; production callers should not invoke directly.
    void recordTombstoneAbsorbed(const QString &entityType,
                                 const QString &entityId,
                                 const QString &winningSide);

    QJsonObject mergeStates(const QJsonObject &local, const QJsonObject &remote);
    QJsonArray  mergeEntityArrays(const QJsonArray &localArr, const QJsonArray &remoteArr,
                                  const QString &entityType = {});
    QJsonObject mergeEntity(const QJsonObject &local, const QJsonObject &remote,
                            const QString &entityType = {});

    // Highest acceptable HLC stamp in a state snapshot (entity `_meta` incl.
    // OR-Set elements, plus `settings_meta`); empty when none. SyncEngine
    // advances the local HLC past it after every merge (S4).
    static QString maxHlc(const QJsonObject &state);

private:
    // Per-key LWW merge of the `settings` blob using the top-level
    // `settings_meta` stamps written by DataStore (S1).
    void mergeSettings(const QJsonObject &local, const QJsonObject &remote,
                       QJsonObject &mergedSettings, QJsonObject &mergedMeta);

    // CRDT per-field merge (used when either entity has "_meta").
    QJsonObject resolveWithCrdt(const QJsonObject &local, const QJsonObject &remote);

    // Legacy whole-object LWW fallback (no "_meta" on either side).
    QJsonObject resolveWithLww(const QJsonObject &local, const QJsonObject &remote);

    bool isNewer(const QJsonObject &a, const QJsonObject &b) const;
    QDateTime parseTimestamp(const QJsonObject &obj) const;

    // Set of field names to skip when iterating "normal" fields
    // (handled separately: _meta, is_deleted).
    static bool isInternalField(const QString &key);

    Telemetry *m_telemetry = nullptr;
};
