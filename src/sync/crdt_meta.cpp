#include "crdt_meta.h"
#include "hlc.h"

#include <QJsonArray>
#include <algorithm>

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

static const QString kMeta      = QStringLiteral("_meta");
static const QString kTs        = QStringLiteral("ts");
static const QString kDev       = QStringLiteral("dev");
static const QString kElements  = QStringLiteral("elements");
static const QString kId        = QStringLiteral("id");
static const QString kAddedAt   = QStringLiteral("added_at");
static const QString kRemovedAt = QStringLiteral("removed_at");

// ---------------------------------------------------------------------------
// OrsElement
// ---------------------------------------------------------------------------

QJsonObject CrdtMeta::OrsElement::toJson() const
{
    QJsonObject obj;
    obj[kId]      = id;
    obj[kAddedAt] = addedAt;
    if (!removedAt.isEmpty())
        obj[kRemovedAt] = removedAt;
    return obj;
}

CrdtMeta::OrsElement CrdtMeta::OrsElement::fromJson(const QJsonObject &j)
{
    OrsElement e;
    e.id        = j[kId].toString();
    e.addedAt   = j[kAddedAt].toString();
    e.removedAt = j[kRemovedAt].toString();
    return e;
}

// ---------------------------------------------------------------------------
// Scalar field metadata
// ---------------------------------------------------------------------------

QJsonObject CrdtMeta::getFieldMeta(const QJsonObject &entity, const QString &fieldName)
{
    return entity.value(kMeta).toObject().value(fieldName).toObject();
}

void CrdtMeta::setFieldMeta(QJsonObject &entity,
                             const QString &fieldName,
                             const QString &ts,
                             const QString &dev)
{
    QJsonObject meta = entity.value(kMeta).toObject();
    QJsonObject fm;
    fm[kTs]  = ts;
    fm[kDev] = dev;
    meta[fieldName] = fm;
    entity[kMeta] = meta;
}

int CrdtMeta::compareFieldMeta(const QJsonObject &a, const QJsonObject &b)
{
    const QString tsA  = a[kTs].toString();
    const QString tsB  = b[kTs].toString();
    const int hlcCmp   = HybridLogicalClock::compare(tsA, tsB);
    if (hlcCmp != 0) return hlcCmp;

    // Equal HLC timestamps — tiebreak by deviceId.
    return a[kDev].toString().compare(b[kDev].toString());
}

// ---------------------------------------------------------------------------
// OR-Set
// ---------------------------------------------------------------------------

QList<CrdtMeta::OrsElement> CrdtMeta::getOrSetElements(const QJsonObject &entity,
                                                         const QString &fieldName)
{
    const QJsonObject fm = entity.value(kMeta).toObject().value(fieldName).toObject();
    const QJsonArray arr = fm.value(kElements).toArray();
    QList<OrsElement> result;
    result.reserve(arr.size());
    for (const auto &v : arr)
        result.append(OrsElement::fromJson(v.toObject()));
    return result;
}

void CrdtMeta::setOrSetElements(QJsonObject &entity,
                                 const QString &fieldName,
                                 const QList<OrsElement> &elements,
                                 const QString &ts,
                                 const QString &dev)
{
    QJsonObject meta = entity.value(kMeta).toObject();
    QJsonObject fm   = meta.value(fieldName).toObject();

    fm[kTs]  = ts;
    fm[kDev] = dev;

    QJsonArray arr;
    for (const auto &e : elements)
        arr.append(e.toJson());
    fm[kElements] = arr;

    meta[fieldName] = fm;
    entity[kMeta]   = meta;
}

QList<CrdtMeta::OrsElement> CrdtMeta::mergeOrSets(const QList<OrsElement> &local,
                                                    const QList<OrsElement> &remote)
{
    // Build a map: id → merged element.
    QMap<QString, OrsElement> merged;

    auto insert = [&](const OrsElement &e) {
        if (!merged.contains(e.id)) {
            merged.insert(e.id, e);
            return;
        }
        OrsElement &existing = merged[e.id];

        // Pick the greater addedAt.
        if (HybridLogicalClock::compare(e.addedAt, existing.addedAt) > 0) {
            existing.addedAt = e.addedAt;
        }

        // Pick the greater removedAt (if present).
        if (!e.removedAt.isEmpty()) {
            if (existing.removedAt.isEmpty() ||
                HybridLogicalClock::compare(e.removedAt, existing.removedAt) > 0)
            {
                existing.removedAt = e.removedAt;
            }
        }
    };

    for (const auto &e : local)  insert(e);
    for (const auto &e : remote) insert(e);

    // Add-wins: if addedAt > removedAt, clear removedAt (re-add wins).
    for (auto &e : merged) {
        if (!e.removedAt.isEmpty()) {
            if (HybridLogicalClock::compare(e.addedAt, e.removedAt) > 0) {
                e.removedAt.clear();
            }
        }
    }

    QList<OrsElement> result;
    result.reserve(merged.size());
    for (const auto &e : merged) result.append(e);
    return result;
}

QStringList CrdtMeta::orSetToList(const QList<OrsElement> &elements)
{
    // B-2.25: use strict > consistently with mergeOrSets.
    // "Live" means removedAt is empty, OR addedAt strictly > removedAt
    // (a re-add after removal resurrects the element).
    // When addedAt == removedAt the element is considered removed (deterministic
    // tiebreak: removal wins on equal HLC, matching mergeOrSets behaviour which
    // only clears removedAt when addedAt STRICTLY > removedAt).
    QList<const OrsElement *> live;
    for (const auto &e : elements) {
        const bool removed = !e.removedAt.isEmpty() &&
                             HybridLogicalClock::compare(e.addedAt, e.removedAt) <= 0;
        if (!removed)
            live.append(&e);
    }

    // Sort by addedAt ascending for deterministic ordering.
    std::sort(live.begin(), live.end(), [](const OrsElement *a, const OrsElement *b) {
        return HybridLogicalClock::compare(a->addedAt, b->addedAt) < 0;
    });

    QStringList result;
    result.reserve(static_cast<int>(live.size()));
    for (const auto *e : live)
        result.append(e->id);
    return result;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

bool CrdtMeta::hasMeta(const QJsonObject &entity)
{
    return entity.contains(kMeta) && !entity.value(kMeta).toObject().isEmpty();
}

QJsonObject CrdtMeta::getMeta(const QJsonObject &entity)
{
    return entity.value(kMeta).toObject();
}

QStringList CrdtMeta::metaFieldNames(const QJsonObject &entity)
{
    return entity.value(kMeta).toObject().keys();
}

const QStringList &CrdtMeta::orSetFields()
{
    static const QStringList kOrSetFields = {
        QStringLiteral("labels"),
    };
    return kOrSetFields;
}
