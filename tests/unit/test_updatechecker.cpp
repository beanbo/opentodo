#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

#include "services/updatechecker.h"

class TestUpdateChecker : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_UpdateChecker"));
        QCoreApplication::setApplicationName(QStringLiteral("test_updatechecker"));
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("update"));
    }

    // ── semver comparison ─────────────────────────────────────────────────────

    void semver_compare_basic()
    {
        QVERIFY(UpdateChecker::isVersionGreater(QStringLiteral("0.2.0"), QStringLiteral("0.1.0")));
        QVERIFY(!UpdateChecker::isVersionGreater(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")));
        QVERIFY(UpdateChecker::isVersionGreater(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")));
        QVERIFY(!UpdateChecker::isVersionGreater(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")));
    }

    void semver_compare_patch()
    {
        QVERIFY(UpdateChecker::isVersionGreater(QStringLiteral("0.1.1"), QStringLiteral("0.1.0")));
        QVERIFY(!UpdateChecker::isVersionGreater(QStringLiteral("0.1.0"), QStringLiteral("0.1.1")));
    }

    void semver_compare_major()
    {
        QVERIFY(UpdateChecker::isVersionGreater(QStringLiteral("2.0.0"), QStringLiteral("1.99.99")));
        QVERIFY(!UpdateChecker::isVersionGreater(QStringLiteral("1.0.0"), QStringLiteral("2.0.0")));
    }

    void semver_equal_returns_false()
    {
        QVERIFY(!UpdateChecker::isVersionGreater(QStringLiteral("1.2.3"), QStringLiteral("1.2.3")));
    }

    // ── currentVersion ────────────────────────────────────────────────────────

    void currentVersion_returns_app_version()
    {
        QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
        UpdateChecker uc;
        QCOMPARE(uc.currentVersion(), QStringLiteral("1.2.3"));
    }

    // ── initial state ─────────────────────────────────────────────────────────

    void updateAvailable_false_initially()
    {
        UpdateChecker uc;
        QVERIFY(!uc.updateAvailable());
    }

    void isChecking_false_initially()
    {
        UpdateChecker uc;
        QVERIFY(!uc.isChecking());
    }

    void latestVersion_empty_initially()
    {
        UpdateChecker uc;
        QVERIFY(uc.latestVersion().isEmpty());
    }

    void releaseUrl_empty_initially()
    {
        UpdateChecker uc;
        QVERIFY(uc.releaseUrl().isEmpty());
    }

    // ── skipVersion ───────────────────────────────────────────────────────────

    void skipVersion_persists_to_settings()
    {
        UpdateChecker uc;
        uc.skipVersion(QStringLiteral("0.5.0"));
        QSettings s;
        QCOMPARE(s.value(QStringLiteral("update/skipped_version")).toString(),
                 QStringLiteral("0.5.0"));
    }

    // ── 24-h throttle: checkForUpdates without force should not check again ──

    void checkForUpdates_throttled_when_recent()
    {
        QSettings s;
        // Simulate a check that happened just now
        s.setValue(QStringLiteral("update/last_check"),
                   QDateTime::currentSecsSinceEpoch());

        UpdateChecker uc;
        // Non-force call should be a no-op — isChecking stays false
        uc.checkForUpdates(false);
        QVERIFY(!uc.isChecking());
    }
};

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
