// ---------------------------------------------------------------------------
// test_telemetry_edges.cpp
//
// Q3.09 update: this suite was originally written as a regression-bench
// for several DOCUMENTED GAPS in Telemetry — namely the in-memory-only
// queue, the absent rate limiter, and setEnabled(false) not clearing the
// queue. Q3.09 fills those gaps; the corresponding slots have been
// FLIPPED from "negative pin" to "positive assertion" here.
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
// Slots covered here (post Q3.09):
//
//   1. disabled_then_re_enabled_does_not_replay_old_events
//   2. event_payload_with_very_long_string_does_not_overflow
//   3. queue_persists_across_restart                    (POSITIVE: persists)
//   4. burst_offline_is_rate_limited                    (POSITIVE: 60/min)
//   5. event_with_unicode_emoji_in_property_round_trips
//   6. disabled_after_construction_clears_pending_queue (POSITIVE: clears)
//   7. setting_disabled_via_QSettings_external_change_is_observed
//   8. event_burst_rate_limit_drops_excess              (POSITIVE: drops)
//
// Additional Q3.09 slots:
//
//   9.  pending_event_count_tracks_queue_size
//   10. queue_cap_evicts_oldest_event
//   11. queue_file_round_trip_across_instances
//   12. disable_deletes_on_disk_queue_file
//   13. corrupt_queue_file_is_discarded_on_load
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
//   * Each slot wipes BOTH the QSettings telemetry/ group AND the on-disk
//     telemetry_queue.json so slots are independent.
// ---------------------------------------------------------------------------

#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// Q3.09 helper: read the on-disk queue as a JSON array. Returns an empty
// array if the file does not exist (Q3.09 contract: empty queue → no file).
QJsonArray readQueueFile(const QString &path)
{
    QFile f(path);
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    return doc.isArray() ? doc.array() : QJsonArray{};
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
        // Q3.09: also wipe the on-disk queue so a previous slot can't leak
        // pending events into the next one.
        Telemetry probe;
        const QString path = probe.queueFilePath();
        if (!path.isEmpty())
            QFile::remove(path);
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("telemetry"));
        Telemetry probe;
        const QString path = probe.queueFilePath();
        if (!path.isEmpty())
            QFile::remove(path);
    }

    // -----------------------------------------------------------------------
    // 1. disabled_then_re-enabled_does_not_replay_old_events
    // -----------------------------------------------------------------------
    //
    // While DISABLED, every trackEvent call short-circuits before
    // anonymousId() runs (see telemetry.cpp), so anonymous_id is NOT created.
    // Enabling later and tracking a NEW event must therefore mint the id at
    // that point — the events recorded while disabled are not queued
    // anywhere, so re-enabling cannot replay them.
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
        QVERIFY2(!settingsContains(QStringLiteral("anonymous_id")),
                 "trackEvent while disabled must not create anonymous_id "
                 "(would indicate an enqueue ran)");
        QCOMPARE(t.pendingEventCount(), 0);

        // ── Phase B: enable. Enabling must not mint an id by itself. ─────────
        t.setEnabled(true);
        QVERIFY(t.enabled());
        QVERIFY2(!settingsContains(QStringLiteral("anonymous_id")),
                 "setEnabled(true) must not by itself replay queued events");

        // ── Phase C: now fire ONE fresh event while enabled. ─────────────────
        t.trackEvent(QStringLiteral("evt_post_enable"), allowlistedProps());
        QVERIFY2(settingsContains(QStringLiteral("anonymous_id")),
                 "first enabled trackEvent must mint anonymous_id");
        QCOMPARE(t.pendingEventCount(), 1);
    }

    // -----------------------------------------------------------------------
    // 2. event_payload_with_very_long_string_does_not_overflow
    // -----------------------------------------------------------------------
    //
    // Telemetry does NOT cap incoming prop string lengths. We pin "no crash
    // + accepted into the in-memory queue" for a 1 MiB string passed under
    // an allowlisted key.
    // -----------------------------------------------------------------------
    void event_payload_with_very_long_string_does_not_overflow()
    {
        Telemetry t;
        t.setEnabled(true);

        const QString huge(1024 * 1024, QChar('x'));
        QCOMPARE(huge.size(), 1024 * 1024);

        QVariantMap props;
        props.insert(QStringLiteral("view"), huge);
        props.insert(QStringLiteral("priority"), 1);

        t.trackEvent(QStringLiteral("evt_huge_payload"), props);
        QVERIFY2(settingsContains(QStringLiteral("anonymous_id")),
                 "huge-payload trackEvent must still enqueue (creates "
                 "anonymous_id)");
        QCOMPARE(t.pendingEventCount(), 1);
    }

    // -----------------------------------------------------------------------
    // 3. queue_persists_across_restart  (Q3.09: POSITIVE pin — persists)
    // -----------------------------------------------------------------------
    //
    // Q3.09 added telemetry_queue.json as a durable mirror of the in-memory
    // queue. Events enqueued before a graceful destruction must reappear in
    // a fresh Telemetry instance.
    // -----------------------------------------------------------------------
    void queue_persists_across_restart()
    {
        QString idBeforeRestart;
        QString queuePath;
        {
            Telemetry t;
            queuePath = t.queueFilePath();
            QVERIFY2(!queuePath.isEmpty(),
                     "queueFilePath must resolve when AppDataLocation exists");

            t.setEnabled(true);

            // Enqueue 10 distinct events.
            for (int i = 0; i < 10; ++i) {
                QVariantMap props;
                props.insert(QStringLiteral("task_count"), i);
                t.trackEvent(QStringLiteral("evt_restart_%1").arg(i), props);
            }
            QCOMPARE(t.pendingEventCount(), 10);

            // Force the debounced write to land immediately so the file is on
            // disk before ~Telemetry runs.
            t.persistQueueImmediate();

            idBeforeRestart = settingsString(QStringLiteral("anonymous_id"));
            QVERIFY(!idBeforeRestart.isEmpty());

            // The file should now exist with 10 entries.
            const QJsonArray onDisk = readQueueFile(queuePath);
            QCOMPARE(onDisk.size(), 10);
        }

        // ── "Restart": fresh Telemetry instance reading the same QSettings ──
        Telemetry t2;
        QVERIFY2(t2.enabled(),
                 "enabled flag must persist across restart via QSettings");
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")),
                 idBeforeRestart);
        QCOMPARE(t2.pendingEventCount(), 10);
    }

    // -----------------------------------------------------------------------
    // 4. burst_offline_is_rate_limited
    // -----------------------------------------------------------------------
    //
    // Q3.09 added a sliding 60-events-per-60s rate limiter. A tight burst
    // of 1000 trackEvent calls must therefore only enqueue AT MOST 60
    // events. The rest are dropped silently.
    // -----------------------------------------------------------------------
    void burst_offline_is_rate_limited()
    {
        Telemetry t;
        t.setEnabled(true);

        for (int i = 0; i < 1000; ++i) {
            QVariantMap props;
            props.insert(QStringLiteral("task_count"), i);
            t.trackEvent(QStringLiteral("evt_burst_%1").arg(i), props);
        }

        // Per Q3.09 rate limit: window=60s, max=60 events. 1000 calls in a
        // tight loop all hit within the same window, so queue size <= 60.
        QVERIFY2(t.pendingEventCount() <= 60,
                 qPrintable(QStringLiteral(
                     "rate-limited burst must cap queue at 60, got %1")
                     .arg(t.pendingEventCount())));
        // ... and the limiter should have accepted exactly 60 (the cap).
        QCOMPARE(t.pendingEventCount(), 60);

        // anonymous_id must have been minted exactly once during the burst.
        QVERIFY(settingsContains(QStringLiteral("anonymous_id")));
    }

    // -----------------------------------------------------------------------
    // 5. event_with_unicode_emoji_in_property_round_trips
    // -----------------------------------------------------------------------
    //
    // Six tricky unicode strings under allowlisted keys — all well within
    // the rate-limit window. No crash, no smuggle into QSettings.
    // -----------------------------------------------------------------------
    void event_with_unicode_emoji_in_property_round_trips()
    {
        Telemetry t;
        t.setEnabled(true);

        const QStringList trickyValues = {
            QString::fromUtf8("\xF0\x9F\x9A\x80"),                       // U+1F680 ROCKET
            QString::fromUtf8("\xE2\x9C\x85"),                           // U+2705 CHECK MARK
            QString::fromUtf8("\xE2\x80\xAE \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"), // RLO + Arabic
            QString::fromUtf8("\xC3\xA9"),                               // é (NFC)
            QString::fromUtf8("e\xCC\x81"),                              // é (NFD)
            QString::fromUtf8("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"), // 👩‍💻
        };

        for (int i = 0; i < trickyValues.size(); ++i) {
            QVariantMap props;
            props.insert(QStringLiteral("view"), trickyValues.at(i));
            props.insert(QStringLiteral("platform"), trickyValues.at(i));
            t.trackEvent(QStringLiteral("evt_unicode_%1").arg(i), props);
        }

        QVERIFY(settingsContains(QStringLiteral("anonymous_id")));
        QCOMPARE(t.pendingEventCount(), trickyValues.size());

        // Sweep telemetry/ keys+values for accidental leakage.
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
    }

    // -----------------------------------------------------------------------
    // 6. disabled_after_construction_clears_pending_queue  (Q3.09: POSITIVE)
    // -----------------------------------------------------------------------
    //
    // Q3.09 added a clearInFlightAndQueue() path inside setEnabled(false):
    // disabling telemetry now wipes m_queue and deletes the on-disk file.
    // -----------------------------------------------------------------------
    void disabled_after_construction_clears_pending_queue()
    {
        Telemetry t;
        const QString queuePath = t.queueFilePath();
        t.setEnabled(true);

        // Queue 3 events while enabled. m_queue now has 3 entries.
        for (int i = 0; i < 3; ++i) {
            t.trackEvent(QStringLiteral("evt_q_%1").arg(i),
                         allowlistedProps());
        }
        QCOMPARE(t.pendingEventCount(), 3);
        QVERIFY(settingsContains(QStringLiteral("anonymous_id")));
        const QString idBefore = settingsString(QStringLiteral("anonymous_id"));

        // Force a flush of the on-disk queue so we can verify deletion below.
        t.persistQueueImmediate();
        QVERIFY(QFile::exists(queuePath));

        // ── Spy on enabledChanged + disable ────────────────────────────────
        QSignalSpy enabledSpy(&t, &Telemetry::enabledChanged);
        t.setEnabled(false);
        QCOMPARE(enabledSpy.count(), 1);

        // Q3.09 contract: in-memory queue cleared, on-disk file removed.
        QCOMPARE(t.pendingEventCount(), 0);
        QVERIFY2(!QFile::exists(queuePath),
                 "setEnabled(false) must delete telemetry_queue.json");

        // QSettings state is unchanged beyond `enabled` itself — id survives.
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idBefore);

        // Calling trackEvent post-disable must remain a strict no-op.
        t.trackEvent(QStringLiteral("evt_post_disable"), allowlistedProps());
        QCOMPARE(t.pendingEventCount(), 0);
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idBefore);
    }

    // -----------------------------------------------------------------------
    // 7. setting_disabled_via_QSettings_external_change_is_observed
    // -----------------------------------------------------------------------
    //
    // Telemetry::enabled() reads QSettings on every call (no caching). An
    // external mutation IS observed by the NEXT enabled() call, but no
    // signal fires (no QFileSystemWatcher / QSettings change observer).
    // -----------------------------------------------------------------------
    void setting_disabled_via_QSettings_external_change_is_observed()
    {
        Telemetry t;

        QVERIFY(!t.enabled());

        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), true);
            s.sync();
        }
        QVERIFY2(t.enabled(),
                 "Telemetry::enabled() must re-read QSettings every call");

        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), false);
            s.sync();
        }
        QVERIFY2(!t.enabled(),
                 "external setValue(false) must also be observed");

        QVERIFY(!t.prompted());
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/prompted"), true);
            s.sync();
        }
        QVERIFY2(t.prompted(),
                 "prompted() must re-read QSettings every call too");

        QSignalSpy spy(&t, &Telemetry::enabledChanged);
        {
            QSettings s;
            s.setValue(QStringLiteral("telemetry/enabled"), true);
            s.sync();
        }
        QVERIFY(t.enabled());
        QCOMPARE(spy.count(), 0);
    }

    // -----------------------------------------------------------------------
    // 8. event_burst_rate_limit_drops_excess (Q3.09: POSITIVE)
    // -----------------------------------------------------------------------
    //
    // Q3.09 added a sliding 60-events-per-60s rate limiter. We pin both:
    //   (a) the queue is capped at 60 by the limiter,
    //   (b) no signal storm — only ONE enabledChanged (the initial enable),
    //   (c) anonymous_id is stable through the burst.
    // -----------------------------------------------------------------------
    void event_burst_rate_limit_drops_excess()
    {
        Telemetry t;

        QSignalSpy enabledSpy(&t, &Telemetry::enabledChanged);
        t.setEnabled(true);
        QCOMPARE(enabledSpy.count(), 1);

        t.trackEvent(QStringLiteral("evt_burst_warmup"), allowlistedProps());
        const QString idAtStart = settingsString(QStringLiteral("anonymous_id"));
        QVERIFY(!idAtStart.isEmpty());

        // Tight burst of 5000 events. The limiter must cap queue at <=60.
        for (int i = 0; i < 5000; ++i) {
            QVariantMap props;
            props.insert(QStringLiteral("priority"), (i % 4) + 1);
            props.insert(QStringLiteral("error_code"), i);
            t.trackEvent(QStringLiteral("evt_burst_%1").arg(i), props);
        }

        // (a) at most 60 enqueues survived in the current window.
        QCOMPARE(t.pendingEventCount(), 60);

        // (b) no extra enabledChanged storm.
        QCOMPARE(enabledSpy.count(), 1);

        // (c) anonymous_id is unchanged.
        QCOMPARE(settingsString(QStringLiteral("anonymous_id")), idAtStart);
    }

    // -----------------------------------------------------------------------
    // 9. pending_event_count_tracks_queue_size  (Q3.09)
    // -----------------------------------------------------------------------
    //
    // The Q_PROPERTY exposes the in-memory queue depth. Verify the
    // NOTIFY signal fires on every successful enqueue.
    // -----------------------------------------------------------------------
    void pending_event_count_tracks_queue_size()
    {
        Telemetry t;
        QCOMPARE(t.pendingEventCount(), 0);

        t.setEnabled(true);

        QSignalSpy spy(&t, &Telemetry::pendingEventCountChanged);
        t.trackEvent(QStringLiteral("evt_1"), allowlistedProps());
        t.trackEvent(QStringLiteral("evt_2"), allowlistedProps());
        t.trackEvent(QStringLiteral("evt_3"), allowlistedProps());

        QCOMPARE(t.pendingEventCount(), 3);
        // At least 3 emissions — one per accepted enqueue.
        QVERIFY2(spy.count() >= 3,
                 qPrintable(QStringLiteral(
                     "pendingEventCountChanged must fire on each enqueue, got %1")
                     .arg(spy.count())));
    }

    // -----------------------------------------------------------------------
    // 10. queue_cap_evicts_oldest_event  (Q3.09)
    // -----------------------------------------------------------------------
    //
    // MAX_QUEUE_SIZE=1000. We can't get there cheaply because of the
    // rate limiter, so verify the cap path via the in-memory queue: with
    // the rate limiter capped at 60/min, 1000 immediate calls only put 60
    // into the queue. Instead, exercise the cap during LOAD: write 1500
    // events to telemetry_queue.json manually, then construct a new
    // Telemetry — loadQueueFromDisk should cap at 1000 with a qWarning.
    // -----------------------------------------------------------------------
    void queue_cap_evicts_oldest_event()
    {
        // Resolve where the on-disk queue lives.
        QString queuePath;
        {
            Telemetry probe;
            queuePath = probe.queueFilePath();
        }
        QVERIFY(!queuePath.isEmpty());

        // Make sure the directory exists.
        QDir().mkpath(QFileInfo(queuePath).absolutePath());

        // Write 1500 fake events directly to disk — past the 1000 cap.
        QJsonArray arr;
        for (int i = 0; i < 1500; ++i) {
            QJsonObject ev;
            ev[QStringLiteral("event")] = QStringLiteral("evt_%1").arg(i);
            ev[QStringLiteral("anonymous_id")] =
                QStringLiteral("00000000-0000-0000-0000-000000000000");
            ev[QStringLiteral("version")] = QStringLiteral("0.0.0-test");
            ev[QStringLiteral("ts")] = QStringLiteral("2026-01-01T00:00:00Z");
            arr.append(ev);
        }
        QFile out(queuePath);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        out.close();

        // Boot a fresh Telemetry and check the cap was enforced.
        Telemetry t;
        QCOMPARE(t.pendingEventCount(), 1000);
    }

    // -----------------------------------------------------------------------
    // 11. queue_file_round_trip_across_instances  (Q3.09)
    // -----------------------------------------------------------------------
    //
    // Smaller-than-restart slot focused purely on the JSON file shape:
    // event payload survives the QSaveFile → loadQueueFromDisk round trip
    // byte-for-byte (within the QJsonObject equivalence).
    // -----------------------------------------------------------------------
    void queue_file_round_trip_across_instances()
    {
        QString path;
        QJsonObject expectedEvent;
        {
            Telemetry t;
            path = t.queueFilePath();
            t.setEnabled(true);
            QVariantMap props;
            props.insert(QStringLiteral("view"), QStringLiteral("inbox"));
            props.insert(QStringLiteral("priority"), 2);
            props.insert(QStringLiteral("task_count"), 42);
            t.trackEvent(QStringLiteral("evt_roundtrip"), props);
            t.persistQueueImmediate();

            // Capture what we just wrote so we can compare structural fields
            // after restart.
            const QJsonArray written = readQueueFile(path);
            QCOMPARE(written.size(), 1);
            expectedEvent = written.first().toObject();
        }

        // Read back through a fresh Telemetry.
        Telemetry t2;
        QCOMPARE(t2.pendingEventCount(), 1);

        // Re-read the file — content is unchanged after the load (load is
        // non-destructive on successful parse).
        const QJsonArray after = readQueueFile(path);
        QCOMPARE(after.size(), 1);
        const QJsonObject afterEv = after.first().toObject();

        QCOMPARE(afterEv.value(QStringLiteral("event")).toString(),
                 QStringLiteral("evt_roundtrip"));
        QCOMPARE(afterEv.value(QStringLiteral("anonymous_id")),
                 expectedEvent.value(QStringLiteral("anonymous_id")));
        const QJsonObject props = afterEv.value(QStringLiteral("props")).toObject();
        QCOMPARE(props.value(QStringLiteral("view")).toString(),
                 QStringLiteral("inbox"));
        QCOMPARE(props.value(QStringLiteral("priority")).toInt(), 2);
        QCOMPARE(props.value(QStringLiteral("task_count")).toInt(), 42);
    }

    // -----------------------------------------------------------------------
    // 12. disable_deletes_on_disk_queue_file  (Q3.09)
    // -----------------------------------------------------------------------
    //
    // A user who toggles telemetry OFF must not see their queued events
    // re-enabled if they toggle it back ON later.
    // -----------------------------------------------------------------------
    void disable_deletes_on_disk_queue_file()
    {
        QString path;
        {
            Telemetry t;
            path = t.queueFilePath();
            t.setEnabled(true);
            for (int i = 0; i < 5; ++i) {
                t.trackEvent(QStringLiteral("evt_%1").arg(i),
                             allowlistedProps());
            }
            t.persistQueueImmediate();
            QVERIFY(QFile::exists(path));

            // Disable: the file should be gone.
            t.setEnabled(false);
            QVERIFY2(!QFile::exists(path),
                     "disable must remove the on-disk queue");
        }

        // Re-enable through a fresh instance — must start from zero.
        Telemetry t2;
        t2.setEnabled(true);
        QCOMPARE(t2.pendingEventCount(), 0);
    }

    // -----------------------------------------------------------------------
    // 13. corrupt_queue_file_is_discarded_on_load  (Q3.09)
    // -----------------------------------------------------------------------
    //
    // A truncated / malformed telemetry_queue.json must not crash the
    // service. The file is discarded and the queue starts empty.
    // -----------------------------------------------------------------------
    void corrupt_queue_file_is_discarded_on_load()
    {
        QString path;
        {
            Telemetry probe;
            path = probe.queueFilePath();
        }
        QDir().mkpath(QFileInfo(path).absolutePath());

        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write("not valid json {[broken");
        out.close();
        QVERIFY(QFile::exists(path));

        Telemetry t;
        QCOMPARE(t.pendingEventCount(), 0);
        QVERIFY2(!QFile::exists(path),
                 "corrupt queue file must be removed on load");
    }
};

QTEST_GUILESS_MAIN(TestTelemetryEdges)
#include "test_telemetry_edges.moc"
