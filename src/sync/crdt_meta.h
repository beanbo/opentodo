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
    //
    // OR-Set (Observed-Remove Set) implementation. Used for ordered
    // collections such as Task::labels where two devices may concurrently
    // add or remove the same element id.
    //
    // ┌─────────────────────────────────────────────────────────────────────┐
    // │ Storage shape (per element id):                                     │
    // │   { id: "label-uuid", addedAt: "<hlc>", removedAt: "<hlc>"|"" }     │
    // │                                                                     │
    // │ Note: there is exactly ONE OrsElement per id — not a log of separate│
    // │ add/remove events.  Both addedAt and removedAt are the HLC          │
    // │ high-water-marks observed locally for that id.                      │
    // └─────────────────────────────────────────────────────────────────────┘
    //
    // Semantics (see mergeOrSets() + orSetToList() for the implementation):
    //
    //   add(id, hlc):
    //     Sets addedAt = max(addedAt, hlc) on the element with this id.
    //     If no entry exists, creates one with addedAt = hlc.
    //
    //   remove(id, hlc):
    //     Sets removedAt = max(removedAt, hlc) on the element with this id.
    //
    //   merge(local, remote):
    //     For each id present on either side:
    //       addedAt   = HLC-max of both sides' addedAt
    //       removedAt = HLC-max of both sides' removedAt
    //     Then: if addedAt STRICTLY > removedAt, clear removedAt
    //           (the element is alive — "add-wins" / re-add resurrects).
    //
    //   orSetToList(merged):
    //     An element is "live" iff
    //         removedAt is empty
    //       OR addedAt STRICTLY > removedAt        (`compare > 0`).
    //     Otherwise it is treated as removed.
    //
    // ┌─────────────────────────────────────────────────────────────────────┐
    // │ ASYMMETRIC RE-ADD — IMPORTANT INVARIANT                             │
    // │                                                                     │
    // │ A re-add after a remove resurrects the element ONLY IF the re-add's │
    // │ HLC is STRICTLY GREATER than the remove's HLC.  Equal HLC ⇒ remove  │
    // │ wins.  Older (concurrent) add ⇒ remove wins.                        │
    // │                                                                     │
    // │ Examples (let HLC(t) abbreviate the HLC at logical-time t):         │
    // │                                                                     │
    // │   remove @ HLC(5)  ▸ add    @ HLC(6)   ⇒ live   (strict >)          │
    // │   remove @ HLC(5)  ▸ add    @ HLC(5)   ⇒ REMOVED (equal — remove    │
    // │                                          wins as the tiebreaker).   │
    // │   remove @ HLC(5)  ▸ add    @ HLC(4)   ⇒ REMOVED (older add).       │
    // │   add    @ HLC(3)  ▸ remove @ HLC(7)   ⇒ REMOVED (linear remove).   │
    // │                                                                     │
    // │ HLC equality across devices is rare but possible: two devices with  │
    // │ skewed clocks may produce the same `physMs:counter` prefix.  In     │
    // │ those rare ties, the OR-Set's internal use of                       │
    // │ HybridLogicalClock::compare() further tiebreaks by deviceId — but   │
    // │ ONLY when comparing addedAt-vs-addedAt or removedAt-vs-removedAt.   │
    // │ When deciding live-vs-dead the comparison is addedAt vs removedAt,  │
    // │ which sit on *the same* element record carrying *one* HLC each —    │
    // │ so the deviceId-tiebreak only matters if those HLCs were produced   │
    // │ on different devices.                                               │
    // │                                                                     │
    // │ Why strictly-greater rather than >=                                 │
    // │   1. Convergence: every replica must reach the same answer for any  │
    // │      merge order.  A strict total order on (addedAt, removedAt)     │
    // │      gives a unique winner per element, which is essential for the  │
    // │      OR-Set to be a true CRDT (commutative, associative, idempotent).│
    // │   2. Safety bias on ties: when an add and a remove have identical   │
    // │      HLCs the user's intent is ambiguous; biasing toward remove is  │
    // │      the safer default — the user can always re-add (which will     │
    // │      advance the HLC and unambiguously win).                        │
    // │                                                                     │
    // │ Practical implication: on a SINGLE device, remove-then-re-add       │
    // │ ALWAYS resurrects, because the local HLC advances monotonically     │
    // │ between calls to HybridLogicalClock::now() — the re-add's HLC is    │
    // │ strictly greater than the remove's.  The "remove wins on tie" rule  │
    // │ only ever surfaces in true cross-device concurrent edits, and even  │
    // │ then only when the two devices' physMs:counter prefixes happen to   │
    // │ collide exactly.                                                    │
    // │                                                                     │
    // │ Pinned by:                                                          │
    // │   tests/unit/test_crdt.cpp                                          │
    // │     · orset_concurrentRemoveThenLaterAdd_resurrects                 │
    // │     · orset_concurrentRemoveThenEqualHlcAdd_doesNotResurrect        │
    // │     · orset_concurrentRemoveThenEarlierAdd_doesNotResurrect         │
    // │     · orset_localRemoveThenLocalReAdd_resurrects                    │
    // │     · orset_sameHlcTie_deterministicAcrossDeviceIds                 │
    // │   tests/unit/test_crdt_multidevice.cpp                              │
    // │     · add_then_remove_then_re_add_orset_semantics                   │
    // └─────────────────────────────────────────────────────────────────────┘

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
