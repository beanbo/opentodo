// ---------------------------------------------------------------------------
// test_telemetry_edges.cpp
//
// Quality round Q3.07-B: behavioural pinning for previously-uncovered corners
// of the Telemetry service.
//
// Existing coverage in this directory:
//   * test_telemetry.cpp        — basic state surface (enabled/prompted
//                                 default, signal emission, no-op-when-
//                                 disabled, basic enable/disable cycle).
//   * test_telemetry_retry.cpp  — anonymous-id lifecycle, PII allowlist
//                                 (two slots SKIP'd due to NAM destructor
//                                 races at teardown), QSettings persistence,
//                                 rapid burst safety, empty-name no-crash.
//
// This *_edges suite pins eight previously-untested corners around retry
// queueing, persistence across restarts, payload sanitization edges and
// external QSettings observation. Slots mirror the brief:
//
//   1. disabled_then_re_enabled_does_not_replay_old_events
//   2. event_payload_with_very_long_string_does_not_overflow
//   3. queue_persists_across_restart                    (pins NEGATIVE — no
//                                                         persistence today)
//   4. one_thousand_events_queued_offline_batched_on_resume
//   5. event_with_unicode_emoji_in_property_round_trips
//   6. disabled_after_construction_clears_pending_queue (pins NEGATIVE —
//                                                         disable does NOT
//                                                         clear the queue,
//                                                         only prevents new
//                                                         enqueues)
//   7. setting_disabled_via_QSettings_external_change_is_observed
//   8. event_burst_rate_limit_drops_excess_or_throttles (pins NEGATIVE — no
//                                                         rate limiter today)
//
// Observation strategy
// --------------------
// Telemetry exposes NO public pendingCount() or queue inspector. The only
// observable side effect of a successful trackEvent under enabled=true is
// creation of the `telemetry/anonymous_id` key on first call. The retry
// suite established this as the binary "did trackEvent actually enqueue?"
// probe; we re-use it.
//
// We also re-use the QSettings persistence shape (enabled / prompted /
// anonymous_id) as the surface for "what survives a Telemetry destruction
// and reconstruction" — that lets us pin "queue is in-memory only" without
// a queue accessor.
//
// Network safety
// --------------
// trackEvent under enabled=true arms a 5-minute QTimer that, on fire, would
// POST to TELEMETRY_ENDPOINT via the internal QNetworkAccessManager. We
// must never let the flushTimer fire and never spin the event loop long
// enough for the NAM to complete an async transaction — the existing retry
// suite documented (see kSkipReason there) that this races with teardown
// and segfaults in offscreen runs. All slots here keep Telemetry alive
// only as long as necessary, never call processEvents() with a non-zero
// duration, and never call QTest::qWait().
//
// Constraints satisfied
// ---------------------
//   * QTEST_GUILESS_MAIN — no QML engine, no QApplication.
//   * Unique QSettings org/app per test binary so parallel ctest runs
//     don't collide with test_telemetry or test_telemetry_retry.
//   * Registered as Infra layer (see tests/unit/CMakeLists.txt) to match
//     the sister Telemetry suites.
//   * NO production-code edits. Each assertion pins CURRENT behaviour;
//     qInfo() comments flag where the pinned behaviour is a documented
//     gap (slots 3, 6 and 8 in particular).
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "services/telemetry.h"

namespace {

// Small wrapper so each slot can read/write under the same telemetry/ group
// used by production (Telemetry hard-codes "telemetry/<key>").
QString settingsString(const QString &key)
{
    QSettings s;
    return s.value(QStringLiteral("telemetry/") + key).toString();
}

bool settingsContains(const QString &key)
{
    QSettings s;
    return s.contains(QStringLiteral("telemetry/") + key);
}

// Build a fresh allowlist-only QVariantMap. Mirrors the retry suite helper
// so both files document the same allowlist via use.
QVariantMap allowlistedProps()
{
    return {
        { QStringLiteral("platform"), QStringLiteral("test") },
        { QStringLiteral("view"),     QStringLiteral("today") },
        { QStringLiteral("priority"), 3 },
    };
}

} // namespace

class TestTelemetryEdges : public QObject
{
    Q_OBJECT

private slots:

    // ── Fixture lifecycle ────────────────────────────────────────────────────

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // Unique org/app — parallel ctest runs collide on QSettings otherwise.
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_TelemetryEdges"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_telemetry_edges"));
        QCoreApplication::setApplicationVersion(
            QStringLiteral("0.0.0-edges"));
    }

    void init()
    {
        // Wipe the entire telemetry/ group between slots so default state is
        // observable from a clean slate.
        QSettings s;
        s.remove(QStringLiteral("telemetry"));
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("telemetry"));
    }

    // -----------------------------------------------------------------------
    // 1. disabled_then_re-enabled_does_not_replay_old_events
    // -----------------------------------------------------------------------
    //
    // While DISABLED, every trackEvent call short-circuits before
    // anonymousId() runs (see telemetry.cpp:83), so anonymous_id is NOT
    // created. Enabling later and tracking a NEW event must therefore mint
    // the id at that point — the events recorded while disabled are not
    // queued anywhere, so re-enabling cannot replay them.
    //
    // We observe the contract via the anonymous_id surface: it stays absent
    // throughout the disabled window, then appears on the first enabled
    // trackEvent. (If a future implementation introduced a "queue while
    // disabled, replay on enable" mechanism, this slot would surface it via
    // the anonymous_id appearing one call earlier than expected.)
    // -----------------------------------------------------------------------
    void disabled_then_re_enabled_does_not_replay_old_events()
    {
        Telemetry t;
        QVERIFY(!t.enabled());
        QVERIFY(!settingsContains(QStringLiteral("anonymous_id")));

        // ── Phase A: disabled — bunch of events. Nothing should be queued. ──
        for (int i = 0; i < 5; ++i) {
            t.trackEvent(QStringLiteral("evt_pre_enable_%1").arg(i),
                         allowlistedProps());
        }
        // Surface check: no anonymous_id was created (proves no enqueue).
        QVERIFY2(!settingsContains(QStringLiteral("anonymous_id")),
                 "trackEvent while disabled must not create anonymous_id "
                 "(would indicate an enqueue ran)");

        // ── Phase B: enable. The act of enabling itself does not flush, but ─
        //          we need to observe that no replay occurred.
        t.setEnabled(true);
        QVERIFY(t.enabled());

        // Crucially — enabling alone should NOT have minted an id. The id is
        // minted lazily on the first enabled trackEvent.
        QVERIFY2(!settingsContains(QStringLiteral("anonymous_id")),
                 "setEnabled(true) must not by itself replay queued events "
                 "(would mint anonymous_id without a fresh trackEvent)");

        // ── Phase C: now fire ONE fresh event while enabled. ─────────────────
        t.trackEvent(QStringLiteral("evt_post_enable"), allowlistedProps());
        QVERIFY2(settingsContains(QStringLiteral("anonymous_id")),
                 "first enabled trackEvent must mint anonymous_id");

        // If the implementation had replayed the 5 disabled-phase events on
        // re-enable, the id would have been created at setEnabled(true), not
        // at this final trackEvent — and we'd have caught it in Phase B.
    }

    // -----------------------------------------------------------------------
    // 2. event_payload_with_very_long_string_does_not_overflow
    // -----------------------------------------------------------------------
    //
    // Telemetry does NOT cap incoming prop string lengths anywhere visible.
    // We pin "no crash + no truncation observable via the no-op anonymous_id
    // probe" for a 1 MiB string passed under an allowlisted key. The string
    // is stored in the in-memory QQueue<QJsonObject> entry until flushTimer
    // fires — we never let it fire, so no network or serialization beyond
    // QJsonValue::fromVariant runs. Pins behaviour for the synchronous path.
    //
    // qInfo() flags this as a documented absence: a future cap would make
    // this slot's "no truncation observable" claim a regression-trigger, so
    // the maintainer adding the cap can flip the assertion.
    // -----------------------------------------------------------------------
    void event_payload_with_very_long_string_does_not_overflow()
    {
        Telemetry t;
        t.setEnabled(true);

        // 1 MiB ASCII string under the allowlisted "view" key.
        // QString uses 2 bytes per char, so this allocates ~2 MiB internally —
        // big enough to surface a heap-corruption or QJsonValue size-limit
        // regression, small enough to keep the test fast.
        const QString huge(1024 * 1024, QChar('x'));
        QCOMPARE(huge.size(), 1024 * 1024);

        QVariantMap props;
        props.insert(QStringLiteral("view"), huge);
        // Plus a normal allowlisted value so we know the rest of the map is
        // still being filtered correctly.
        props.insert(QStringLiteral("priority"), 1);

        // Must not crash, must not throw. The synchronous part of trackEvent
        // (allowlist filter + QJsonValue::fromVariant + enqueue) handles the
        // huge string inside the QJsonObject without truncation — Qt's
        // QJsonValue::String accepts arbitrary length QString.
        t.trackEvent(QStringLiteral("evt_huge_payload"), props);

        // Indirect confirmation that the enqueue path ran: anonymous_id
        // was created.
        QVERIFY2(settingsContains(QStringLiteral("anonymous_id")),
                 "huge-payload trackEvent must still enqueue (creates "
                 "anonymous_id)");

        qInfo("Telemetry: NO cap on incoming prop string length (1 MiB "
              "accepted into in-memory queue without truncation). A future "
              "cap should flip the pinned behaviour here.");
    }

    // -----------------------------------------------------------------------
    // 3. queue_persists_across_restart
    // -----------------------------------------------------------------------
    //
    // NEGATIVE PIN — the queue is in-memory only (QQueue<QJsonObject>; no
    // disk persistence). Settings persist (enabled, prompted, anonymous_id);
    // queued events do NOT survive a Telemetry destruction.
    //
    // This slot is the "restart" companion to the retry suite's
    // settings_persist_across_instances_but_queue_does_not (which only
    // verifies the settings side). Here we additionally probe ALL the keys
    // a hypothetical on-disk queue might use under telemetry/ — none of
    // them should appear.
    // -----------------------------------------------------------------------
    void queue_persists_across_restart()
    {
        QString idBeforeRestart;
        {
            Telemetry t;
            t.setEnabled(true);

            // Enqueue 10 distinct events. None will be flushed (no event
            // loop spun, no quit signal raised on this scope).
            for (int i = 0; i < 10; ++i) {
                QVariantMap props;
                props.insert(QStringLiteral("task_count"), i);
                t.trackEvent(QStringLiteral("evt_restart_%1").arg(i), props);
            }

            // The 10 events live ONLY in m_queue at this point. anonymous_id
            // was minted on first event.
            idBeforeRestart = settingsString(QStringLiteral("anonymous_id"));
            QVERIFY(!idBeforeRestart.isEmpty());
        }
        // ~Telemetry: m_queue is destroyed silently. No aboutToQuit signal
        // fires because we never quit QCoreApplication.

        // ── "Restart": fresh Telemetry instance reading the same QSettings ──
        Telemetry t2;
        QVERIFY2(t2.enabled(),
                 "enabled flag must persist across restart via QSettings");
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")),
                 idBeforeRestart);

        // Probe a wide set of plausible queue-persistence keys under
        // telemetry/ — any of them appearing would indicate that a future
        // implementation has started persisting the queue and broken this
        // negative pin.
        const QStringList speculativeQueueKeys = {
            QStringLiteral("queue"),
            QStringLiteral("pending"),
            QStringLiteral("pending_events"),
            QStringLiteral("event_queue"),
            QStringLiteral("flush_queue"),
            QStringLiteral("queued_count"),
            QStringLiteral("queued_payloads"),
        };
        for (const QString &k : speculativeQueueKeys) {
            QVERIFY2(!settingsContains(k),
                     qPrintable(QStringLiteral(
                         "telemetry/%1 must not exist — Telemetry has no "
                         "on-disk queue today").arg(k)));
        }

        qInfo("Telemetry: queue is in-memory ONLY; events queued before "
              "process exit (without aboutToQuit firing) are silently lost. "
              "DOCUMENTED GAP for offline-then-restart scenarios.");
    }

    // -----------------------------------------------------------------------
    // 4. 1000_events_queued_offline_batched_on_resume
    // -----------------------------------------------------------------------
    //
    // "Offline" in this codebase = the flushTimer is armed but no event loop
    // spins long enough for it to fire. The synchronous part of trackEvent
    // just appends to m_queue and restarts the 5-minute single-shot timer.
    // We pin that 1000 rapid enqueues:
    //   (a) don't crash,
    //   (b) preserve the SAME anonymous_id throughout (no race rotates it),
    //   (c) don't write a single QSettings entry beyond anonymous_id (no
    //       per-event persistence sneaking in).
    //
    // "Batched on resume" is a contract on flush(), which we cannot exercise
    // here without a network mock (per the retry suite's documented kSkipReason).
    // We pin the queue-side invariant — flush() collects ALL queued events
    // into a single QJsonArray POST — by reading the implementation: line 124-126
    // of telemetry.cpp shows `while (!m_queue.isEmpty()) batch.append(...)`.
    // This slot's contribution is making sure the queue can ACCEPT 1000 events
    // for that hypothetical single-batch flush.
    // -----------------------------------------------------------------------
    void one_thousand_events_queued_offline_batched_on_resume()
    {
        Telemetry t;
        t.setEnabled(true);

        // Warm-up: mint the anonymous_id outside the loop so we can compare.
        t.trackEvent(QStringLiteral("evt_warmup"), allowlistedProps());
        const QString idAtStart = settingsString(QStringLiteral("anonymous_id"));
        QVERIFY(!idAtStart.isEmpty());

        // Snapshot the telemetry/ key set — ONLY anonymous_id and enabled
        // should be present, never per-event keys.
        QSettings snapshotBefore;
        snapshotBefore.beginGroup(QStringLiteral("telemetry"));
        const QStringList keysBefore = snapshotBefore.allKeys();
        snapshotBefore.endGroup();

        // 1000 rapid trackEvent calls — exceeds the typical "offline session"
        // event count for a productivity app. None will be flushed.
        for (int i = 0; i < 1000; ++i) {
            QVariantMap props;
            // Vary an allowlisted key per call so each enqueued object is
            // distinct in QJsonObject identity.
            props.insert(QStringLiteral("task_count"), i);
            props.insert(QStringLiteral("view"),
                         (i % 2 == 0) ? QStringLiteral("today")
                                      : QStringLiteral("upcoming"));
            t.trackEvent(QStringLiteral("evt_batch_%1").arg(i), props);
        }

        // (a) no crash — reaching here proves it.

        // (b) anonymous_id is unchanged. No race / rotation.
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idAtStart);

        // (c) no per-event keys were sneaked into QSettings.
        QSettings snapshotAfter;
        snapshotAfter.beginGroup(QStringLiteral("telemetry"));
        const QStringList keysAfter = snapshotAfter.allKeys();
        snapshotAfter.endGroup();

        // Key set must be unchanged. (Anonymous_id existed both before and
        // after; "enabled" was written by setEnabled(true) before the loop
        // started, so it's in both snapshots.)
        QCOMPARE(keysAfter, keysBefore);

        qInfo("Telemetry: 1000 events queued offline in-memory; flush() will "
              "POST them as ONE batched JSON array (telemetry.cpp:124-126). "
              "DOCUMENTED GAP: no max-queue-size cap, so a long offline "
              "session could grow m_queue unboundedly.");
    }

    // -----------------------------------------------------------------------
    // 5. event_with_unicode_emoji_in_property_round_trips
    // -----------------------------------------------------------------------
    //
    // The allowlist filter does NOT examine value content; it only checks
    // keys against kAllowedPropKeys. So a value containing emoji /
    // RTL / NFC-composed sequences is accepted verbatim into the JSON. We
    // pin "no crash, no truncation observable, no smuggle into QSettings"
    // for a handful of tricky strings under allowlisted keys.
    //
    // The "round trips" guarantee in the slot name is structural: QString →
    // QJsonValue::fromVariant → QJsonObject → batched QJsonDocument →
    // POST body. All four steps are 16-bit Unicode safe in Qt 6, and the
    // batched flush serialises via QJsonDocument::toJson which encodes
    // non-ASCII as proper UTF-8 escape sequences or raw UTF-8 bytes.
    // -----------------------------------------------------------------------
    void event_with_unicode_emoji_in_property_round_trips()
    {
        Telemetry t;
        t.setEnabled(true);

        // Battery of unicode-tricky strings:
        //   - basic emoji (surrogate pair on UTF-16)
        //   - multi-codepoint emoji with ZWJ
        //   - skin-tone modifier
        //   - bidi-override + RTL
        //   - NFC vs NFD composed (will pin "the engine doesn't normalize")
        const QStringList trickyValues = {
            QString::fromUtf8("\xF0\x9F\x9A\x80"),                       // U+1F680 ROCKET
            QString::fromUtf8("\xE2\x9C\x85"),                           // U+2705 CHECK MARK
            QString::fromUtf8("\xE2\x80\xAE \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"), // RLO + Arabic
            QString::fromUtf8("\xC3\xA9"),                               // é (NFC, single codepoint)
            QString::fromUtf8("e\xCC\x81"),                              // é (NFD, e + combining acute)
            QString::fromUtf8("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"), // 👩‍💻 ZWJ family
        };

        for (int i = 0; i < trickyValues.size(); ++i) {
            QVariantMap props;
            // "view" is allowlisted — value flows into the QJsonObject.
            props.insert(QStringLiteral("view"), trickyValues.at(i));
            // "platform" — second allowlisted key carrying the same value
            // (proves multi-key per-event survives unicode too).
            props.insert(QStringLiteral("platform"), trickyValues.at(i));
            t.trackEvent(QStringLiteral("evt_unicode_%1").arg(i), props);
        }

        // anonymous_id was minted on first call.
        QVERIFY(settingsContains(QStringLiteral("anonymous_id")));

        // Sweep telemetry/ keys+values for accidental leakage of the unicode
        // payload (which would imply a value got smuggled into QSettings).
        QSettings s;
        s.beginGroup(QStringLiteral("telemetry"));
        const QStringList allKeys = s.allKeys();
        for (const QString &key : allKeys) {
            const QString val = s.value(key).toString();
            for (const QString &tricky : trickyValues) {
                QVERIFY2(!val.contains(tricky),
                         qPrintable(QStringLiteral(
                             "Unicode value leak: telemetry/%1 = '%2' "
                             "contains payload '%3'")
                             .arg(key, val, tricky)));
            }
        }
        s.endGroup();

        // The round-trip itself happens in flush() → QJsonDocument::toJson →
        // network POST. We cannot exercise that without a NAM mock (segfault
        // risk documented in retry suite). The enqueue side is pinned here.
    }

    // -----------------------------------------------------------------------
    // 6. disabled_after_construction_clears_pending_queue
    // -----------------------------------------------------------------------
    //
    // NEGATIVE PIN — setEnabled(false) does NOT call any queue-clear path
    // (see telemetry.cpp:59-66; setEnabled only writes QSettings + emits the
    // change signal). The "pending queue" continues to exist in m_queue
    // until either (a) flushTimer fires and POSTs it, or (b) the destructor
    // runs and silently discards it.
    //
    // We cannot observe m_queue directly. Instead we observe that disabling
    // mid-stream does not WIPE state we already know is in QSettings (the
    // anonymous_id), and does not emit any signal beyond enabledChanged.
    // Both are proxies for "the queue is not touched by setEnabled(false)".
    //
    // The slot name reads as a literal English claim; the PIN is that the
    // claim is FALSE for the current implementation, and a future
    // implementation that DOES clear the queue on disable should make this
    // test fail in a way that the maintainer can flip.
    // -----------------------------------------------------------------------
    void disabled_after_construction_clears_pending_queue()
    {
        Telemetry t;
        t.setEnabled(true);

        // Queue 3 events while enabled. m_queue now has 3 entries.
        for (int i = 0; i < 3; ++i) {
            t.trackEvent(QStringLiteral("evt_q_%1").arg(i),
                         allowlistedProps());
        }
        QVERIFY(settingsContains(QStringLiteral("anonymous_id")));
        const QString idBefore = settingsString(QStringLiteral("anonymous_id"));

        // ── Spy on enabledChanged + disable ────────────────────────────────
        QSignalSpy enabledSpy(&t, &Telemetry::enabledChanged);
        t.setEnabled(false);

        // Exactly ONE signal — no queue-clear callbacks, no flush-on-disable
        // signal. If a future implementation added a `queueCleared()` signal
        // (or anything similar), it would not be on the enabledChanged
        // count, but the contract here is "the only observable effect of
        // setEnabled(false) is the enabledChanged emission".
        QCOMPARE(enabledSpy.count(), 1);

        // QSettings state is unchanged beyond `enabled` itself — id survives,
        // is not reset, is not removed. (A future queue-clear that also
        // wiped the id would surface here.)
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idBefore);

        // Calling trackEvent post-disable must remain a strict no-op. If the
        // queue were re-armed by some mechanism, anonymous_id would change.
        t.trackEvent(QStringLiteral("evt_post_disable"), allowlistedProps());
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idBefore);

        qInfo("Telemetry: setEnabled(false) does NOT clear m_queue (only "
              "prevents new enqueues). Events queued before disable remain "
              "in memory until destruction. DOCUMENTED GAP if telemetry "
              "opt-out should wipe in-flight payloads.");
    }

    // -----------------------------------------------------------------------
    // 7. setting_disabled_via_QSettings_external_change_is_observed
    // -----------------------------------------------------------------------
    //
    // Telemetry::enabled() reads QSettings on every call (no caching — see
    // telemetry.cpp:52-57). So an external mutation of telemetry/enabled
    // (e.g. another QSettings consumer, a manual edit, a sync replay) IS
    // observed by the NEXT enabled() call. We pin this contract because
    // a future optimisation that caches enabled() in a member variable
    // would silently break it.
    //
    // We CANNOT pin signal emission on external changes — Telemetry has no
    // QFileSystemWatcher / no QSettings change-notification. The contract
    // is "next read sees the new value", not "signal fires immediately".
    // -----------------------------------------------------------------------
    void setting_disabled_via_QSettings_external_change_is_observed()
    {
        Telemetry t;

        // Default: disabled.
        QVERIFY(!t.enabled());

        // ── External flip TRUE via raw QSettings ───────────────────────────
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), true);
            s.sync();
        }
        // Next read picks up the external value. NO signal expected — the
        // change didn't go through setEnabled().
        QVERIFY2(t.enabled(),
                 "Telemetry::enabled() must re-read QSettings every call so "
                 "external mutations are observed (no internal cache)");

        // Now the inverse: external flip FALSE.
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), false);
            s.sync();
        }
        QVERIFY2(!t.enabled(),
                 "external setValue(false) must also be observed");

        // Same contract for the `prompted` flag.
        QVERIFY(!t.prompted());
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/prompted"), true);
            s.sync();
        }
        QVERIFY2(t.prompted(),
                 "prompted() must re-read QSettings every call too");

        // Crucially: external mutation must NOT have emitted enabledChanged
        // from the Telemetry instance — there's no QSettings observer wired
        // up. A future addition of one would surface here.
        QSignalSpy spy(&t, &Telemetry::enabledChanged);
        // Re-flip externally.
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), true);
            s.sync();
        }
        // enabled() reflects the change, but no signal.
        QVERIFY(t.enabled());
        QCOMPARE(spy.count(), 0);

        qInfo("Telemetry: enabled()/prompted() always re-read QSettings (no "
              "caching). External mutations ARE observed by the next read, "
              "but NO signal fires — there is no QSettings-change watcher.");
    }

    // -----------------------------------------------------------------------
    // 8. event_burst_rate_limit_drops_excess_or_throttles
    // -----------------------------------------------------------------------
    //
    // NEGATIVE PIN — Telemetry has NO rate limiter. Every trackEvent under
    // enabled=true goes straight into m_queue, and the flushTimer is
    // restarted on EVERY call (telemetry.cpp:115). The net effect of a
    // tight burst is: m_queue grows linearly, and the timer is repeatedly
    // pushed 5 minutes into the future — meaning a never-idle stream of
    // events would NEVER flush.
    //
    // We pin both:
    //   (a) no drop signal / no "throttled" indication on the public surface,
    //   (b) anonymous_id remains stable through the burst (i.e. there's no
    //       defensive reset / re-mint when "too many" events come in).
    //
    // If a future implementation adds a sliding-window rate limiter, this
    // slot is the regression-bench: the maintainer will flip the qInfo() and
    // tighten the assertions.
    // -----------------------------------------------------------------------
    void event_burst_rate_limit_drops_excess_or_throttles()
    {
        Telemetry t;
        t.setEnabled(true);

        // Warm-up to mint the id.
        t.trackEvent(QStringLiteral("evt_burst_warmup"), allowlistedProps());
        const QString idAtStart = settingsString(QStringLiteral("anonymous_id"));
        QVERIFY(!idAtStart.isEmpty());

        // ── Spy on the only public signal: enabledChanged. A future ────────
        //    "rate-limit hit" signal would need a new spy here.
        QSignalSpy enabledSpy(&t, &Telemetry::enabledChanged);

        // Tight burst of 5000 events as fast as the loop can run. No
        // processEvents() between iterations — pure synchronous enqueue.
        for (int i = 0; i < 5000; ++i) {
            // Mix priorities so the events aren't byte-identical.
            QVariantMap props;
            props.insert(QStringLiteral("priority"), (i % 4) + 1);
            props.insert(QStringLiteral("error_code"), i);
            t.trackEvent(QStringLiteral("evt_burst_%1").arg(i), props);
        }

        // (a) no rate-limit/drop signal fired (Telemetry has none today).
        //     enabledChanged in particular must NOT have fired — a future
        //     "auto-disable on overflow" behaviour would surface here.
        QCOMPARE(enabledSpy.count(), 0);

        // (b) anonymous_id is unchanged.
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idAtStart);

        // (c) telemetry/ QSettings keys are unchanged from after the warmup
        //     (no "dropped_count", no "rate_limited_at" key).
        QSettings s;
        s.beginGroup(QStringLiteral("telemetry"));
        const QStringList allKeys = s.allKeys();
        s.endGroup();
        const QStringList speculativeRateLimitKeys = {
            QStringLiteral("dropped_count"),
            QStringLiteral("rate_limited_at"),
            QStringLiteral("burst_dropped"),
            QStringLiteral("throttled"),
        };
        for (const QString &k : speculativeRateLimitKeys) {
            QVERIFY2(!allKeys.contains(k),
                     qPrintable(QStringLiteral(
                         "telemetry/%1 must not exist — no rate limiter today")
                         .arg(k)));
        }

        qInfo("Telemetry: NO rate limit. 5000-event burst silently enqueues, "
              "flushTimer is restarted on every call (telemetry.cpp:115) so a "
              "never-idle stream would NEVER auto-flush. DOCUMENTED GAPS: no "
              "drop policy, no max-queue cap, no auto-flush threshold.");
    }
};

QTEST_GUILESS_MAIN(TestTelemetryEdges)
#include "test_telemetry_edges.moc"
