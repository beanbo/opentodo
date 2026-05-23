#include "merger.h"
#include "crdt_meta.h"
#include "hlc.h"
#include "services/telemetry.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcMerger, "opentodo.merger")

// ---------------------------------------------------------------------------
// Defence-in-depth skew budget for the per-field comparison.
// Mirrors HLC::update's kMaxClockSkewMs (1 hour). Any field-meta HLC whose
// physMs exceeds local wall-clock by > this budget is demoted (replaced with
// a clamped placeholder for comparison only — the on-disk ts in entity["_meta"]
// is preserved unchanged so forward sync to peers still sees the true HLC).
// See HybridLogicalClock::isWithinAcceptableSkew for rationale.
// ---------------------------------------------------------------------------
static constexpr qint64 kMergerMaxSkewMs = 60LL * 60LL * 1000LL;  // 1 hour

namespace {

// Format an HLC string from raw components (mirrors HLC::format, kept local
// because format() is private to hlc.cpp).
QString formatClampedHlc(quint64 physMs, quint32 counter, const QString &deviceId)
{
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

// Return a copy of `fieldMeta` whose "ts" has been clamped to local-wall-now
// + kMergerMaxSkewMs IF the original ts is more than kMergerMaxSkewMs ahead
// of local-wall-now. Otherwise returns `fieldMeta` unchanged.
//
// `wasClamped` is set to true when the meta was demoted (caller uses this to
// emit a once-per-entity qCWarning).
QJsonObject clampedForComparison(const QJsonObject &fieldMeta, bool &wasClamped)
{
    wasClamped = false;
    if (fieldMeta.isEmpty()) return fieldMeta;

    const QString ts = fieldMeta.value(QStringLiteral("ts")).toString();
    if (HybridLogicalClock::isWithinAcceptableSkew(ts, kMergerMaxSkewMs)) {
        return fieldMeta;
    }

    // Demote: rebuild meta with ts clamped to wall-clock-NOW (zero offset).
    // Counter is zeroed so the clamped meta has minimal weight against any
    // real concurrent edit at the same physMs from a legitimate peer.
    //
    // Why clamp to wall-now and not (wall-now + budget)?
    //   The goal is "bad HLC writes LOSE to legitimate peers with normal
    //   clocks". If we kept the clamped ts an hour ahead of wall-now, the
    //   bad HLC would still beat every legitimate concurrent edit that
    //   happened "right now" — defeating the defence. Clamping to wall-now
    //   means a legit peer's edit (physMs == wall-now, counter > 0) or a
    //   slightly later edit will out-rank the demoted bad HLC. Equal-physMs
    //   ties fall through to counter/deviceId — fair behaviour for the
    //   rare case the poisoned peer happens to share wall-now exactly.
    //
    // Why preserve deviceId?
    //   The deviceId is used for the lexicographic tiebreak. Preserving it
    //   keeps the comparison deterministic for the bad peer's other fields
    //   on the same entity (so all of its fields lose or tie consistently
    //   against a single legitimate peer, rather than thrashing).
    const HybridLogicalClock::Components orig =
        HybridLogicalClock::parse(ts);
    const quint64 clampedPhys = static_cast<quint64>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());

    QJsonObject demoted = fieldMeta;
    demoted[QStringLiteral("ts")] = formatClampedHlc(
        clampedPhys, /*counter=*/0, orig.deviceId);
    wasClamped = true;
    return demoted;
}

} // anonymous namespace

Merger::Merger(QObject *parent)
    : QObject(parent)
{
}

void Merger::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
}

void Merger::recordTombstoneAbsorbed(const QString &entityType,
                                     const QString &entityId,
                                     const QString &winningSide)
{
    // CRDT-correct, but historically silent: a peer's later edit lands on an
    // already-tombstoned entity and is dropped. Surface this so operators can
    // distinguish "lost edits because peer was offline + we tombstoned" from
    // "lost edits because of a sync bug".
    qInfo() << "Merger: dropping later edit on tombstoned" << entityType
            << entityId << "(winning side:" << winningSide << ")";
    if (m_telemetry) {
        QVariantMap props;
        props.insert(QStringLiteral("entity_type"),   entityType);
        props.insert(QStringLiteral("entity_id"),     entityId);
        props.insert(QStringLiteral("winning_side"),  winningSide);
        m_telemetry->trackEvent(QStringLiteral("merger_tombstone_absorbed"), props);
    }
}

// ── Top-level state merge ────────────────────────────────────────────────────

QJsonObject Merger::mergeStates(const QJsonObject &local, const QJsonObject &remote)
{
    QJsonObject merged;

    // Settings: remote wins for whole-object (per CLAUDE.md design — settings
    // are rare and don't carry per-field _meta). iter-night-30: only adopt
    // remote if it actually carries a settings payload. A missing or empty
    // `settings` key on remote means the peer hasn't pushed any user settings
    // yet (e.g. brand-new device whose state.json was bootstrapped) — taking
    // its empty object would silently nuke the local user's entire
    // configuration (theme, language, karma score/level/streak, AI-provider
    // wiring, hotkeys, default project). The follow-up loadFromJson(merged)
    // overwrites `m_settings` directly, so the loss is total — autosave then
    // commits the cleared state to disk on the 1-second debounce.
    const QJsonObject remoteSettings = remote.value(QStringLiteral("settings")).toObject();
    if (!remoteSettings.isEmpty()) {
        merged[QStringLiteral("settings")] = remoteSettings;
    } else {
        merged[QStringLiteral("settings")] = local.value(QStringLiteral("settings")).toObject();
    }

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
        // Pass entityType through so tombstone-absorb events name the right
        // collection in logs / telemetry.
        merged[key] = mergeEntityArrays(localArr, remoteArr, key);
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

QJsonArray Merger::mergeEntityArrays(const QJsonArray &localArr, const QJsonArray &remoteArr,
                                     const QString &entityType)
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
            result.append(mergeEntity(it.value(), remoteMap.value(id), entityType));
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

QJsonObject Merger::mergeEntity(const QJsonObject &local, const QJsonObject &remote,
                                const QString &entityType)
{
    // Tombstone always wins (either side deleted → keep deleted).
    //
    // The bug being fixed here is observability, not semantics: when the OTHER
    // side has a non-deleted entity (possibly with edits at a later HLC than
    // the tombstone), those edits are absorbed by the tombstone and lost.
    // CRDT-correct, but historically silent. Surface it via qInfo + telemetry
    // so operators triaging "where did my edit go" have a trail.
    const bool remoteDeleted = remote.value(QStringLiteral("is_deleted")).toBool();
    const bool localDeleted  = local.value(QStringLiteral("is_deleted")).toBool();

    if (remoteDeleted) {
        if (!localDeleted) {
            const QString id = remote.value(QStringLiteral("id")).toString();
            recordTombstoneAbsorbed(entityType, id, QStringLiteral("remote"));
        }
        return remote;
    }
    if (localDeleted) {
        const QString id = local.value(QStringLiteral("id")).toString();
        recordTombstoneAbsorbed(entityType, id, QStringLiteral("local"));
        return local;
    }

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

    // Q3.05-C defence-in-depth: track whether any field on this entity was
    // demoted due to far-future HLC, so we can log exactly once per entity
    // (not per field — a misconfigured peer typically poisons all its
    // fields, and we want one informative line per entity, not 20).
    bool entityHadClampedField = false;
    QString clampedDeviceId;

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

            // Q3.05-C defence-in-depth: clamp the per-field HLC used for
            // COMPARISON purposes only — the on-disk ts (in lFm / rFm) is
            // preserved when we copy meta back. This means a peer with a
            // year-2099 BIOS clock still has its writes captured, but it
            // no longer wins every field over a legitimate peer.
            bool lClamped = false, rClamped = false;
            const QJsonObject lFmCmp = clampedForComparison(lFm, lClamped);
            const QJsonObject rFmCmp = clampedForComparison(rFm, rClamped);
            if (rClamped) {
                entityHadClampedField = true;
                if (clampedDeviceId.isEmpty()) {
                    clampedDeviceId = rFm.value(QStringLiteral("dev")).toString();
                }
            }
            if (lClamped) {
                entityHadClampedField = true;
                if (clampedDeviceId.isEmpty()) {
                    clampedDeviceId = lFm.value(QStringLiteral("dev")).toString();
                }
            }

            bool remoteWins = false;
            if (lFm.isEmpty() && rFm.isEmpty()) {
                // No meta on either side: fall back to whole-entity LWW.
                remoteWins = !localIsNewer;
            } else if (lFm.isEmpty() && !rFm.isEmpty()) {
                // Remote has CRDT meta; local is legacy.
                if (localHasMeta) {
                    // local entity has _meta but not for this field — remote wins
                    // if it has meta (newer data model knows about this field).
                    // Defence-in-depth: if remote ts is far-future, demote it —
                    // a legacy local with sane updated_at should not lose to a
                    // peer with a broken BIOS clock.
                    remoteWins = !rClamped;
                } else {
                    // Both sides: local is a legacy whole-entity, remote has CRDT.
                    // Compare remote HLC ts against local updated_at.
                    // Use the clamped-for-comparison meta so a far-future
                    // remote can't auto-win over a legacy local with a sane ts.
                    const QString remoteTs = rFmCmp[QStringLiteral("ts")].toString();
                    const QString legacyTs = local.value(
                        QStringLiteral("updated_at")).toString();
                    Q_UNUSED(legacyTs);
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
                    // Unless the local meta is itself far-future — in that
                    // case the legacy remote (with a sane updated_at) should win.
                    remoteWins = lClamped;
                } else {
                    const QString localTs = lFmCmp[QStringLiteral("ts")].toString();
                    const qint64 remoteMs = remoteTime.isValid()
                        ? remoteTime.toMSecsSinceEpoch() : 0;
                    const HybridLogicalClock::Components lc =
                        HybridLogicalClock::parse(localTs);
                    remoteWins = lc.valid
                        ? (static_cast<qint64>(lc.physMs) < remoteMs)
                        : !localIsNewer;
                }
            } else {
                // Both have CRDT meta. Use clamped-for-comparison versions
                // so a far-future HLC on either side doesn't auto-win.
                remoteWins = CrdtMeta::compareFieldMeta(rFmCmp, lFmCmp) > 0;
            }

            if (remoteWins) {
                merged[field] = remote.value(field);
                if (!rFm.isEmpty()) {
                    // IMPORTANT: write back the ORIGINAL rFm (not rFmCmp) so
                    // the on-disk ts is preserved. The clamp only applied to
                    // the comparison decision; if a legitimate peer later
                    // catches up to that physical time, the original ts is
                    // still authoritative.
                    CrdtMeta::setFieldMeta(merged, field,
                                           rFm[QStringLiteral("ts")].toString(),
                                           rFm[QStringLiteral("dev")].toString());
                }
                // else: no meta to copy, field value wins by LWW.
            }
            // else: local wins, value already in merged, local meta already in merged["_meta"].
        }
    }

    // Q3.05-C: log once per entity if any field was demoted. Logged under the
    // merger category — admins triaging "why does device X always win
    // conflicts" can enable opentodo.merger=true to see this line and grep
    // for "clamped far-future HLC".
    if (entityHadClampedField) {
        const QString entityId = local.value(QStringLiteral("id")).toString();
        qCWarning(lcMerger,
                  "Merger: clamped far-future HLC on entity %s (peer device=%s); "
                  "possible BIOS-clock misconfiguration or legacy state bypass of "
                  "HLC::update — demoted for per-field comparison only, ts preserved",
                  qPrintable(entityId), qPrintable(clampedDeviceId));
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
