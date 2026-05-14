#pragma once

#include <QString>

/**
 * @brief Hybrid Logical Clock for CRDT per-field metadata.
 *
 * HLC timestamps encode (physical-ms | counter | deviceId) and are
 * serialised as  "<physical_ms_zero_padded16>:<counter_zero_padded8>:<deviceId>"
 * so that simple lexicographic comparison on the first two components is
 * correct when physical clocks agree. DeviceId is the tiebreaker when both
 * physical and counter are equal.
 *
 * Thread safety: now() / update() are thread-safe via QAtomicInt counter and
 * a per-process QMutex (static).
 *
 * Persistence: the last-seen HLC is stored in QSettings under
 * "sync/last_hlc" so the logical counter survives process restarts.
 */
class HybridLogicalClock
{
public:
    /**
     * @brief Generate a new HLC timestamp for the local device.
     * @param deviceId  The device's UUID string.
     * @return Timestamp string: "<phys_ms_16hex>:<ctr_8hex>:<deviceId>"
     */
    static QString now(const QString &deviceId);

    /**
     * @brief Three-way compare two HLC strings.
     * @return -1 if a < b, 0 if a == b, +1 if a > b.
     *
     * Comparison order:
     *   1. Physical ms component (lexicographic after zero-padding → numeric)
     *   2. Counter component
     *   3. DeviceId (deterministic tiebreak)
     */
    static int compare(const QString &a, const QString &b);

    /**
     * @brief Advance the local clock after receiving a remote HLC.
     *
     * Implements the HLC receive rule:
     *   l' = max(l_local, l_remote, phys_now)
     *   if l' == l_local == l_remote: c' = max(c_local, c_remote) + 1
     *   elif l' == l_local:           c' = c_local + 1
     *   elif l' == l_remote:          c' = c_remote + 1
     *   else:                         c' = 0
     *
     * @param localHlc  The current local HLC string (may be empty for first call).
     * @param remoteHlc The HLC received from a remote device.
     * @param deviceId  The local device's ID.
     * @return The new local HLC to store.
     */
    static QString update(const QString &localHlc, const QString &remoteHlc, const QString &deviceId);

    // Exposed for use by CrdtMeta/Merger when comparing HLC vs ISO timestamps.
    struct Components {
        quint64 physMs = 0;
        quint32 counter = 0;
        QString deviceId;
        bool valid = false;
    };

    /**
     * @brief Parse an HLC string into its components.
     *
     * Returns an invalid (valid==false) Components on malformed input.
     */
    static Components parse(const QString &hlc);

    // Internal helpers (public so tests can reach them if needed)
    static QString loadLast();

private:
    static QString format(quint64 physMs, quint32 counter, const QString &deviceId);
    static void persistLast(const QString &hlc);
};
