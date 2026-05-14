#include "hlc.h"

#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStringList>

// ---------------------------------------------------------------------------
// Maximum tolerated forward clock skew when accepting remote / persisted HLC.
// Inbound HLCs whose physical-ms timestamp exceeds the local wall clock by
// more than this budget are rejected (see update()), and persisted state
// exceeding it on first read is treated as poisoned and reset to physNow.
// 60 minutes — generous enough for typical NTP drift / brief sleep-wake
// skew, tight enough that a far-future BIOS clock won't poison the CRDT
// merge order for years.
// ---------------------------------------------------------------------------
static constexpr quint64 kMaxClockSkewMs = 60ULL * 60ULL * 1000ULL;  // 1 hour

// ---------------------------------------------------------------------------
// Internal state: last physical ms and counter, protected by mutex.
// ---------------------------------------------------------------------------
namespace {
    static QMutex s_mutex;
    static quint64 s_lastPhysMs = 0;
    static quint32 s_lastCounter = 0;
    static bool s_initialised = false;

    void ensureInitialised()
    {
        if (s_initialised) return;
        s_initialised = true;

        // Restore from QSettings so counter is monotone across restarts.
        const QString stored = HybridLogicalClock::loadLast();
        if (!stored.isEmpty()) {
            const HybridLogicalClock::Components c = HybridLogicalClock::parse(stored);
            if (c.valid) {
                s_lastPhysMs  = c.physMs;
                s_lastCounter = c.counter;
            }
        }
    }
} // namespace

// ---------------------------------------------------------------------------
// Public static API
// ---------------------------------------------------------------------------

QString HybridLogicalClock::now(const QString &deviceId)
{
    QMutexLocker lk(&s_mutex);
    ensureInitialised();

    const quint64 physNow = static_cast<quint64>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());

    // Defensive read: if the last-seen physical ms exceeds the local wall
    // clock by more than the skew budget, treat the persisted state as
    // poisoned (BIOS-battery reset, malicious tamper, prior version that
    // accepted a far-future remote HLC) and reset it to physNow.  This is a
    // one-time correction per process, so the qWarning isn't going to spam.
    if (s_lastPhysMs > physNow + kMaxClockSkewMs) {
        qWarning("HLC::now: persisted s_lastPhysMs %llu exceeds local clock by "
                 "> %llu ms; resetting to %llu (poisoned persistence recovery)",
                 static_cast<unsigned long long>(s_lastPhysMs),
                 static_cast<unsigned long long>(kMaxClockSkewMs),
                 static_cast<unsigned long long>(physNow));
        s_lastPhysMs  = physNow;
        s_lastCounter = 0;
    }

    quint64 newPhys;
    quint32 newCounter;

    if (physNow > s_lastPhysMs) {
        newPhys    = physNow;
        newCounter = 0;
    } else {
        newPhys    = s_lastPhysMs;
        newCounter = s_lastCounter + 1;
    }

    s_lastPhysMs  = newPhys;
    s_lastCounter = newCounter;

    const QString result = format(newPhys, newCounter, deviceId);
    persistLast(result);
    return result;
}

int HybridLogicalClock::compare(const QString &a, const QString &b)
{
    if (a == b) return 0;

    const Components ca = parse(a);
    const Components cb = parse(b);

    // Invalid entries sort last (treat as very old).
    if (!ca.valid && !cb.valid) return 0;
    if (!ca.valid) return -1;
    if (!cb.valid) return +1;

    if (ca.physMs != cb.physMs)
        return (ca.physMs < cb.physMs) ? -1 : +1;

    if (ca.counter != cb.counter)
        return (ca.counter < cb.counter) ? -1 : +1;

    // Deterministic tiebreak on deviceId (lexicographic).
    return ca.deviceId.compare(cb.deviceId);
}

QString HybridLogicalClock::update(const QString &localHlc,
                                   const QString &remoteHlc,
                                   const QString &deviceId)
{
    QMutexLocker lk(&s_mutex);
    ensureInitialised();

    const quint64 physNow = static_cast<quint64>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());

    const Components cl = parse(localHlc);
    const Components cr = parse(remoteHlc);

    // Clamp inbound remote HLC: a peer device whose physical clock is set
    // far in the future (BIOS battery dead, malicious tamper, etc.) would
    // otherwise poison s_lastPhysMs and force every subsequent local edit
    // to climb past the poisoned value via counter-only increments.
    // The poisoned device would "win" every CRDT field conflict until real
    // time catches up — potentially years.  Reject the merge entirely and
    // return the local HLC unchanged (advancing nothing).
    if (cr.valid && cr.physMs > physNow + kMaxClockSkewMs) {
        qWarning("HLC::update: rejecting remote HLC %llu (dev=%s) — exceeds "
                 "local clock by > %llu ms; possible clock skew",
                 static_cast<unsigned long long>(cr.physMs),
                 qPrintable(cr.deviceId),
                 static_cast<unsigned long long>(kMaxClockSkewMs));
        // Return the local HLC string as-is (or empty if it was empty);
        // local s_lastPhysMs / s_lastCounter are NOT advanced.
        return localHlc;
    }

    // Defensive: also detect and recover from poisoned local persisted state
    // — mirrors the check in now() so update() called before the first now()
    // doesn't silently propagate a poisoned high-water mark.
    if (s_lastPhysMs > physNow + kMaxClockSkewMs) {
        qWarning("HLC::update: persisted s_lastPhysMs %llu exceeds local "
                 "clock by > %llu ms; resetting to %llu (poisoned persistence "
                 "recovery)",
                 static_cast<unsigned long long>(s_lastPhysMs),
                 static_cast<unsigned long long>(kMaxClockSkewMs),
                 static_cast<unsigned long long>(physNow));
        s_lastPhysMs  = physNow;
        s_lastCounter = 0;
    }

    const quint64 lPhys = cl.valid ? cl.physMs  : 0;
    const quint32 lCtr  = cl.valid ? cl.counter : 0;
    const quint64 rPhys = cr.valid ? cr.physMs  : 0;
    const quint32 rCtr  = cr.valid ? cr.counter : 0;

    quint64 newPhys;
    quint32 newCounter;

    const quint64 maxPhys = qMax(physNow, qMax(lPhys, rPhys));

    if (maxPhys == lPhys && maxPhys == rPhys) {
        newPhys    = maxPhys;
        newCounter = qMax(lCtr, rCtr) + 1;
    } else if (maxPhys == lPhys) {
        newPhys    = lPhys;
        newCounter = lCtr + 1;
    } else if (maxPhys == rPhys) {
        newPhys    = rPhys;
        newCounter = rCtr + 1;
    } else {
        newPhys    = physNow;
        newCounter = 0;
    }

    // Also keep the process-level last updated.
    if (newPhys > s_lastPhysMs ||
        (newPhys == s_lastPhysMs && newCounter > s_lastCounter))
    {
        s_lastPhysMs  = newPhys;
        s_lastCounter = newCounter;
    }

    const QString result = format(newPhys, newCounter, deviceId);
    persistLast(result);
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

HybridLogicalClock::Components HybridLogicalClock::parse(const QString &hlc)
{
    Components c;
    if (hlc.isEmpty()) return c;

    // Format: "<16hex_phys>:<8hex_ctr>:<deviceId>"
    // deviceId can itself contain colons (UUIDs don't, but be safe with
    // a split-limit-of-3 approach).
    const int sep1 = hlc.indexOf(QLatin1Char(':'));
    if (sep1 < 0) return c;
    const int sep2 = hlc.indexOf(QLatin1Char(':'), sep1 + 1);
    if (sep2 < 0) return c;

    bool ok1 = false, ok2 = false;
    c.physMs   = hlc.left(sep1).toULongLong(&ok1, 16);
    c.counter  = hlc.mid(sep1 + 1, sep2 - sep1 - 1).toUInt(&ok2, 16);
    c.deviceId = hlc.mid(sep2 + 1);
    c.valid    = ok1 && ok2;
    return c;
}

QString HybridLogicalClock::format(quint64 physMs, quint32 counter, const QString &deviceId)
{
    // physMs: 16 hex digits (up to ~year 3K is fine with 13 digits, pad to 16)
    // counter: 8 hex digits
    return QStringLiteral("%1:%2:%3")
        .arg(physMs,  16, 16, QLatin1Char('0'))
        .arg(counter,  8, 16, QLatin1Char('0'))
        .arg(deviceId);
}

void HybridLogicalClock::persistLast(const QString &hlc)
{
    // Persist without the lock held (QSettings is thread-safe).
    // Called while lock is held — store separately.
    QSettings settings;
    settings.setValue(QStringLiteral("sync/last_hlc"), hlc);
}

QString HybridLogicalClock::loadLast()
{
    QSettings settings;
    return settings.value(QStringLiteral("sync/last_hlc")).toString();
}
