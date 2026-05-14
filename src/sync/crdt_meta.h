#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>

/**
 * @brief Helpers for CRDT per-field metadata stored inline in entity JSON.
 *
 * Metadata is stored as:
 *
 *   {
 *     "content": "hello",
 *     "_meta": {
 *       "content": { "ts": "<hlc>", "dev": "<deviceId>" },
 *       "labels":  {
 *         "ts": "<hlc>", "dev": "<deviceId>",
 *         "elements": [
 *           { "id": "label-uuid", "addedAt": "<hlc>", "removedAt": "<hlc>" },
 *           ...
 *         ]
 *       }
 *     }
 *   }
 *
 * For OR-Set (ordered collections), the "elements" array under a field's
 * meta entry carries per-element (addedAt, removedAt?) pairs.
 *
 * Backwards compatibility: entities without "_meta" are treated as "legacy"
 * and fall back to whole-object LWW using the entity's updated_at.
 */
class CrdtMeta
{
public:
    // ── Scalar field metadata ────────────────────────────────────────────────

    /**
     * @brief Return the {ts, dev} object for a specific field, or empty if absent.
     */
    static QJsonObject getFieldMeta(const QJsonObject &entity, const QString &fieldName);

    /**
     * @brief Write {ts, dev} into entity["_meta"][fieldName].
     *
     * The entity object is modified in-place.
     */
    static void setFieldMeta(QJsonObject &entity,
                             const QString &fieldName,
                             const QString &ts,
                             const QString &dev);

    /**
     * @brief Compare two field-meta objects {ts, dev} using HLC then deviceId.
     * @return -1 / 0 / +1.
     */
    static int compareFieldMeta(const QJsonObject &a, const QJsonObject &b);

    // ── OR-Set (label_ids / ordered collections) ─────────────────────────────

    struct OrsElement {
        QString id;
        QString addedAt;     ///< HLC when this element was added
        QString removedAt;   ///< HLC when removed, or empty if still present

        QJsonObject toJson() const;
        static OrsElement fromJson(const QJsonObject &j);
    };

    /**
     * @brief Retrieve the OR-Set element list for a field, or empty if absent.
     */
    static QList<OrsElement> getOrSetElements(const QJsonObject &entity,
                                               const QString &fieldName);

    /**
     * @brief Write an OR-Set element list back to entity["_meta"][fieldName]["elements"].
     */
    static void setOrSetElements(QJsonObject &entity,
                                 const QString &fieldName,
                                 const QList<OrsElement> &elements,
                                 const QString &ts,
                                 const QString &dev);

    /**
     * @brief Merge two OR-Set element lists (add-wins semantics).
     *
     * For each unique element id:
     *   - The entry with the greatest addedAt wins on addedAt.
     *   - removedAt: take the max(removedAt) from both sides, but only if
     *     the winning side's addedAt is NOT strictly greater than the
     *     remove timestamp (i.e. a re-add after removal resurrects the element).
     */
    static QList<OrsElement> mergeOrSets(const QList<OrsElement> &local,
                                          const QList<OrsElement> &remote);

    /**
     * @brief Build a QStringList from a merged OR-Set (only live elements).
     *
     * "Live" means removedAt is empty, or addedAt > removedAt.
     * Result is sorted by addedAt ascending (simple deterministic order).
     */
    static QStringList orSetToList(const QList<OrsElement> &elements);

    // ── Utility ──────────────────────────────────────────────────────────────

    /**
     * @brief Return true if the entity has any "_meta" block at all.
     */
    static bool hasMeta(const QJsonObject &entity);

    /**
     * @brief Return the raw "_meta" object (may be empty).
     */
    static QJsonObject getMeta(const QJsonObject &entity);

    /**
     * @brief Return all field names present in "_meta" (excludes "_meta" itself).
     */
    static QStringList metaFieldNames(const QJsonObject &entity);

    /**
     * @brief Names of fields treated as OR-Sets (arrays with per-element tracking).
     */
    static const QStringList &orSetFields();
};
