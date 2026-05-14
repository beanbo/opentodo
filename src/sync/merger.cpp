#include "merger.h"
#include "crdt_meta.h"
#include "hlc.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>

Q_LOGGING_CATEGORY(lcMerger, "opentodo.merger")

Merger::Merger(QObject *parent)
    : QObject(parent)
{
}

// ── Top-level state merge ────────────────────────────────────────────────────

QJsonObject Merger::mergeStates(const QJsonObject &local, const QJsonObject &remote)
{
    QJsonObject merged;

    // Settings: remote wins for whole-object (unchanged, settings are rare).
    merged[QStringLiteral("settings")] = remote.value(QStringLiteral("settings")).toObject();

    static const QStringList collections = {
        QStringLiteral("tasks"),
        QStringLiteral("projects"),
        QStringLiteral("sections"),
        QStringLiteral("labels"),
        QStringLiteral("filters"),
        QStringLiteral("reminders"),
        QStringLiteral("comments"),
    };

    for (const auto &key : collections) {
        const QJsonArray localArr  = local.value(key).toArray();
        const QJsonArray remoteArr = remote.value(key).toArray();
        merged[key] = mergeEntityArrays(localArr, remoteArr);
    }

    merged[QStringLiteral("completed_tasks")] = QJsonArray();
    return merged;
}

// ── Schema validation helpers (S-2.43) ───────────────────────────────────────

namespace {

// Validate a single entity JSON before it enters the merge.
// Returns false and logs if the entity is structurally malformed.
bool validateEntity(const QJsonObject &obj)
{
    // Must have a non-empty string "id".
    const QJsonValue idVal = obj.value(QStringLiteral("id"));
    if (!idVal.isString() || idVal.toString().isEmpty()) {
        qCWarning(lcMerger) << "S-2.43: entity rejected — missing or invalid 'id'";
        return false;
    }

    // "_meta" deviceIds must be <= 64 chars; types must match.
    if (obj.contains(QStringLiteral("_meta"))) {
        const QJsonValue metaVal = obj.value(QStringLiteral("_meta"));
        if (!metaVal.isObject()) {
            qCWarning(lcMerger) << "S-2.43: entity rejected — '_meta' is not an object"
                                << "id=" << idVal.toString();
            return false;
        }
        const QJsonObject meta = metaVal.toObject();
        for (auto it = meta.begin(); it != meta.end(); ++it) {
            if (!it.value().isObject()) continue;
            const QJsonObject fm = it.value().toObject();
            const QString dev = fm.value(QStringLiteral("dev")).toString();
            if (dev.length() > 64) {
                qCWarning(lcMerger) << "S-2.43: entity rejected — _meta deviceId too long"
                                    << "id=" << idVal.toString() << "field=" << it.key();
                return false;
            }
        }
    }

    return true;
}

} // anonymous namespace

// ── Entity array merge ────────────────────────────────────────────────────────

QJsonArray Merger::mergeEntityArrays(const QJsonArray &localArr, const QJsonArray &remoteArr)
{
    QMap<QString, QJsonObject> localMap;
    for (const auto &v : localArr) {
        const QJsonObject obj = v.toObject();
        if (!validateEntity(obj)) continue; // S-2.43: skip malformed
        localMap.insert(obj[QStringLiteral("id")].toString(), obj);
    }

    QMap<QString, QJsonObject> remoteMap;
    for (const auto &v : remoteArr) {
        const QJsonObject obj = v.toObject();
        if (!validateEntity(obj)) continue; // S-2.43: skip malformed
        remoteMap.insert(obj[QStringLiteral("id")].toString(), obj);
    }

    QJsonArray result;
    QSet<QString> processed;

    for (auto it = localMap.begin(); it != localMap.end(); ++it) {
        const QString &id = it.key();
        processed.insert(id);

        if (remoteMap.contains(id)) {
            result.append(mergeEntity(it.value(), remoteMap.value(id)));
        } else {
            result.append(it.value());
        }
    }

    for (auto it = remoteMap.begin(); it != remoteMap.end(); ++it) {
        if (!processed.contains(it.key())) {
            result.append(it.value());
        }
    }

    return result;
}

// ── Single entity merge ───────────────────────────────────────────────────────

QJsonObject Merger::mergeEntity(const QJsonObject &local, const QJsonObject &remote)
{
    // Tombstone always wins (either side deleted → keep deleted).
    if (remote.value(QStringLiteral("is_deleted")).toBool())
        return remote;
    if (local.value(QStringLiteral("is_deleted")).toBool())
        return local;

    // Choose CRDT or legacy path based on presence of "_meta".
    if (CrdtMeta::hasMeta(local) || CrdtMeta::hasMeta(remote)) {
        return resolveWithCrdt(local, remote);
    }
    return resolveWithLww(local, remote);
}

// ── CRDT per-field merge ──────────────────────────────────────────────────────

QJsonObject Merger::resolveWithCrdt(const QJsonObject &local, const QJsonObject &remote)
{
    // Collect the union of all data field names (exclude _meta itself).
    QSet<QString> allFields;
    for (auto it = local.begin(); it != local.end(); ++it) {
        if (!isInternalField(it.key()))
            allFields.insert(it.key());
    }
    for (auto it = remote.begin(); it != remote.end(); ++it) {
        if (!isInternalField(it.key()))
            allFields.insert(it.key());
    }

    // Determine a "whole-entity" fallback timestamp for legacy fields
    // (fields present in either entity but without meta).
    const QDateTime localTime  = parseTimestamp(local);
    const QDateTime remoteTime = parseTimestamp(remote);
    const bool localIsNewer    = localTime.isValid() && localTime > remoteTime;

    // If one side has _meta and the other doesn't, treat the meta-less side as
    // "legacy": its updated_at covers ALL its fields.
    const bool localHasMeta  = CrdtMeta::hasMeta(local);
    const bool remoteHasMeta = CrdtMeta::hasMeta(remote);

    // Build the merged entity starting from the local copy (arbitrary base).
    QJsonObject merged = local;
    // B-2.22: mergedMeta is written back to merged["_meta"] at the end.
    // We accumulate field-level meta decisions here rather than letting
    // each branch independently call setFieldMeta on `merged` (which they
    // already do) — but we still need the initial local meta as the base.
    // The per-branch CrdtMeta::setFieldMeta calls below write directly to
    // `merged`, so mergedMeta is only needed as the initial-state reference.
    // Remove the dead variable and rely solely on the inline setFieldMeta calls.

    const QStringList &orSetFlds = CrdtMeta::orSetFields();

    for (const QString &field : std::as_const(allFields)) {
        // Skip fields only in one side — they stay as-is from the base.
        const bool inLocal  = local.contains(field);
        const bool inRemote = remote.contains(field);

        if (!inLocal && inRemote) {
            merged[field] = remote.value(field);
            // Carry over remote's meta for this field.
            const QJsonObject rm = CrdtMeta::getFieldMeta(remote, field);
            if (!rm.isEmpty()) {
                CrdtMeta::setFieldMeta(merged, field,
                                       rm[QStringLiteral("ts")].toString(),
                                       rm[QStringLiteral("dev")].toString());
            }
            continue;
        }
        if (inLocal && !inRemote) {
            // Already in merged from local; nothing to do.
            continue;
        }

        // Field exists in both — resolve.
        if (orSetFlds.contains(field)) {
            // OR-Set merge.
            const QList<CrdtMeta::OrsElement> lElems =
                CrdtMeta::getOrSetElements(local, field);
            const QList<CrdtMeta::OrsElement> rElems =
                CrdtMeta::getOrSetElements(remote, field);

            if (!lElems.isEmpty() || !rElems.isEmpty()) {
                // Both (or one) have OR-Set metadata.
                const QList<CrdtMeta::OrsElement> merged_elems =
                    CrdtMeta::mergeOrSets(lElems, rElems);

                // Determine the max ts/dev from either side's field meta.
                const QJsonObject lFm = CrdtMeta::getFieldMeta(local, field);
                const QJsonObject rFm = CrdtMeta::getFieldMeta(remote, field);

                QString winTs, winDev;
                if (CrdtMeta::compareFieldMeta(lFm, rFm) >= 0) {
                    winTs  = lFm[QStringLiteral("ts")].toString();
                    winDev = lFm[QStringLiteral("dev")].toString();
                } else {
                    winTs  = rFm[QStringLiteral("ts")].toString();
                    winDev = rFm[QStringLiteral("dev")].toString();
                }

                // Reconstruct the plain array from the merged OR-Set.
                const QStringList liveIds = CrdtMeta::orSetToList(merged_elems);
                QJsonArray arr;
                for (const auto &id : liveIds) arr.append(id);
                merged[field] = arr;

                CrdtMeta::setOrSetElements(merged, field, merged_elems, winTs, winDev);
            } else {
                // No OR-Set metadata on either side — fall back to LWW for this field.
                if (localIsNewer) {
                    // keep local value (already in merged)
                } else {
                    merged[field] = remote.value(field);
                }
            }
        } else {
            // Scalar field.
            const QJsonObject lFm = CrdtMeta::getFieldMeta(local, field);
            const QJsonObject rFm = CrdtMeta::getFieldMeta(remote, field);

            bool remoteWins = false;
            if (lFm.isEmpty() && rFm.isEmpty()) {
                // No meta on either side: fall back to whole-entity LWW.
                remoteWins = !localIsNewer;
            } else if (lFm.isEmpty() && !rFm.isEmpty()) {
                // Remote has CRDT meta; local is legacy.
                if (localHasMeta) {
                    // local entity has _meta but not for this field — remote wins
                    // if it has meta (newer data model knows about this field).
                    remoteWins = true;
                } else {
                    // Both sides: local is a legacy whole-entity, remote has CRDT.
                    // Compare remote HLC ts against local updated_at.
                    const QString remoteTs = rFm[QStringLiteral("ts")].toString();
                    const QString legacyTs = local.value(
                        QStringLiteral("updated_at")).toString();
                    // Convert legacyTs to an HLC-comparable string (use "legacy" device).
                    // Since HLC physical part is ms-since-epoch and ISO timestamp
                    // can be parsed, we do a pragmatic: if remote HLC physMs >
                    // local's updated_at ms → remote wins.
                    const qint64 localMs = localTime.isValid()
                        ? localTime.toMSecsSinceEpoch() : 0;
                    const HybridLogicalClock::Components rc =
                        HybridLogicalClock::parse(remoteTs);
                    remoteWins = rc.valid
                        ? (static_cast<qint64>(rc.physMs) >= localMs)
                        : !localIsNewer;
                }
            } else if (!lFm.isEmpty() && rFm.isEmpty()) {
                // Local has CRDT meta; remote is legacy.
                if (remoteHasMeta) {
                    // remote entity has _meta but not for this field → local wins.
                    remoteWins = false;
                } else {
                    const QString localTs = lFm[QStringLiteral("ts")].toString();
                    const qint64 remoteMs = remoteTime.isValid()
                        ? remoteTime.toMSecsSinceEpoch() : 0;
                    const HybridLogicalClock::Components lc =
                        HybridLogicalClock::parse(localTs);
                    remoteWins = lc.valid
                        ? (static_cast<qint64>(lc.physMs) < remoteMs)
                        : !localIsNewer;
                }
            } else {
                // Both have CRDT meta.
                remoteWins = CrdtMeta::compareFieldMeta(rFm, lFm) > 0;
            }

            if (remoteWins) {
                merged[field] = remote.value(field);
                if (!rFm.isEmpty()) {
                    CrdtMeta::setFieldMeta(merged, field,
                                           rFm[QStringLiteral("ts")].toString(),
                                           rFm[QStringLiteral("dev")].toString());
                }
                // else: no meta to copy, field value wins by LWW.
            }
            // else: local wins, value already in merged, local meta already in merged["_meta"].
        }
    }

    return merged;
}

// ── Legacy whole-object LWW ──────────────────────────────────────────────────

QJsonObject Merger::resolveWithLww(const QJsonObject &local, const QJsonObject &remote)
{
    const QDateTime localTime  = parseTimestamp(local);
    const QDateTime remoteTime = parseTimestamp(remote);

    if (!localTime.isValid() || !remoteTime.isValid()) {
        return isNewer(local, remote) ? local : remote;
    }

    if (localTime > remoteTime)
        return local;

    return remote;  // remote wins on equal timestamps (original behaviour)
}

// ── Helpers ──────────────────────────────────────────────────────────────────

bool Merger::isNewer(const QJsonObject &a, const QJsonObject &b) const
{
    return parseTimestamp(a) > parseTimestamp(b);
}

QDateTime Merger::parseTimestamp(const QJsonObject &obj) const
{
    QString ts = obj[QStringLiteral("updated_at")].toString();
    if (ts.isEmpty()) ts = obj[QStringLiteral("created_at")].toString();
    if (ts.isEmpty()) ts = obj[QStringLiteral("posted_at")].toString();
    return QDateTime::fromString(ts, Qt::ISODateWithMs);
}

bool Merger::isInternalField(const QString &key)
{
    return key == QStringLiteral("_meta") ||
           key == QStringLiteral("is_deleted");
}
