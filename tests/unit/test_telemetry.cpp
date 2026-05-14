#include <QtTest>
#include <QSettings>

#include "services/telemetry.h"

class TestTelemetry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_telemetry"));
    }

    void init()
    {
        // Reset telemetry settings before each test for isolation
        QSettings s;
        s.remove(QStringLiteral("telemetry"));
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("telemetry"));
    }

    // ── Default state ─────────────────────────────────────────────────────────

    void disabled_by_default()
    {
        Telemetry t;
        QVERIFY(!t.enabled());
    }

    void not_prompted_by_default()
    {
        Telemetry t;
        QVERIFY(!t.prompted());
    }

    // ── trackEvent: no-op when disabled ──────────────────────────────────────

    void track_does_nothing_when_disabled()
    {
        Telemetry t;
        QVERIFY(!t.enabled());
        // Must not crash; nothing is queued or sent
        t.trackEvent(QStringLiteral("test_event"), {});
        t.trackEvent(QStringLiteral("another"), {{ QStringLiteral("key"), QStringLiteral("value") }});
    }

    // ── enable / disable cycle ────────────────────────────────────────────────

    void enable_then_disable_works()
    {
        Telemetry t;
        t.setEnabled(true);
        QVERIFY(t.enabled());
        t.setEnabled(false);
        QVERIFY(!t.enabled());
    }

    void setEnabled_emits_signal()
    {
        Telemetry t;
        QSignalSpy spy(&t, &Telemetry::enabledChanged);
        t.setEnabled(true);
        QCOMPARE(spy.count(), 1);
        t.setEnabled(false);
        QCOMPARE(spy.count(), 2);
    }

    void setEnabled_no_signal_when_same_value()
    {
        Telemetry t;
        // default is false; setting false again should not fire
        QSignalSpy spy(&t, &Telemetry::enabledChanged);
        t.setEnabled(false);
        QCOMPARE(spy.count(), 0);
    }

    // ── markPrompted ─────────────────────────────────────────────────────────

    void markPrompted_persists()
    {
        Telemetry t;
        QVERIFY(!t.prompted());
        t.markPrompted();
        // Re-read through a fresh instance to verify QSettings persistence
        Telemetry t2;
        QVERIFY(t2.prompted());
    }

    // ── trackEvent is a no-op when enabled but no event loop running ─────────

    void track_event_when_enabled_does_not_crash()
    {
        Telemetry t;
        t.setEnabled(true);
        // No flush actually happens here (no event loop), but must not crash
        t.trackEvent(QStringLiteral("app_start"), {{ QStringLiteral("platform"), QStringLiteral("win") }});
        QVERIFY(true);  // reaching here = no crash
    }
};

QTEST_GUILESS_MAIN(TestTelemetry)
#include "test_telemetry.moc"
