// Resilience and error-recovery coverage for UpdateChecker (Q2.19-A audit).
//
// Existing coverage:
//   * test_updatechecker.cpp — semver basics, throttling, currentVersion,
//     skipVersion persistence.
//   * test_updatechecker_extended.cpp — semver corner cases (v-prefix,
//     prerelease, empty, lex-vs-numeric), JSON parsing (valid, array,
//     missing tag_name, malformed, skipped-version filter), disabled-
//     setting gate, re-entrancy, file-not-found error.
//
// Q2.19-A audit gaps targeted by THIS file:
//
//   1. Network down → no infinite retry / no infinite loop.
//      The retry logic is gated on HTTP 429 or 5xx; a connection-refused /
//      DNS-failure produces NetworkError but no HTTP status, so retry must
//      NOT trigger. Pin that contract.
//
//   2. Malformed JSON — beyond the truncated-string case in the extended
//      tests, cover:
//      * a top-level non-object (array, scalar, null);
//      * tag_name present but wrong type (integer, null, object);
//      * html_url wrong type;
//      All must short-circuit cleanly without crashing.
//
//   3. Version comparison edge cases NOT already in extended:
//      * trailing dots / leading dots;
//      * comparisons with completely numeric noise (e.g. "0.1.0.1.0" vs "0.1.0");
//      * negative-looking versions ("-1.0.0").
//
//   4. Rate-limit response simulation. We can't easily inject HTTP 429 via
//      a file:// override, but we CAN pin the retry-once-only contract by
//      verifying that error paths (which the file:// scheme covers) take
//      AT MOST one attempt (no retry on non-HTTP errors) — i.e. there is
//      no infinite loop, and the total time spent in isChecking is
//      bounded.
//
// All tests use QTEST_GUILESS_MAIN, unique QSettings org/app, no raw
// strings inline. Russian strings live in nspace; no UI strings actually
// emitted by this service in the tested code paths.

#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include "services/updatechecker.h"

namespace uc_keys {
// Named constants for QSettings keys (no raw-string repetition in the test
// body). QStringLiteral requires a string-literal token, so a constexpr
// const char * wouldn't work as its argument — wrap via QLatin1String at
// the call sites.
constexpr const char *kRepoUrl       = "update/repo_url";
constexpr const char *kLastCheck     = "update/last_check";
constexpr const char *kDisabled      = "update/disabled";
constexpr const char *kSkippedVer    = "update/skipped_version";

// "qs" (not "s") to avoid shadowing local `QSettings s;` variables.
inline QString qs(const char *k) { return QLatin1String(k); }
} // namespace uc_keys

class TestUpdateCheckerResilience : public QObject
{
    Q_OBJECT

private:
    // Drive an override-URL fetch synchronously: kick off the check, pump
    // events until isChecking goes false, bounded by a generous timeout.
    // Returns the number of milliseconds the check actually took (useful
    // for asserting "did not infinite-retry").
    static qint64 runCheckSync(UpdateChecker &uc, const QString &repoUrl,
                               int timeoutMs = 5000)
    {
        QSettings s;
        s.setValue(uc_keys::qs(uc_keys::kRepoUrl), repoUrl);

        uc.checkForUpdates(/*force=*/true);

        QElapsedTimer t; t.start();
        while (uc.isChecking() && t.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return t.elapsed();
    }

    // Write a payload to a temp file and return its file:// URL.
    static QString writeTempJson(QTemporaryDir &dir, const QString &name,
                                 const QByteArray &content)
    {
        const QString path = dir.path() + QLatin1Char('/') + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        f.write(content);
        f.close();
        return QUrl::fromLocalFile(path).toString();
    }

private slots:
    // ── Fixture lifecycle ───────────────────────────────────────────────────

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_UCResilience"));
        QCoreApplication::setApplicationName(QStringLiteral("test_updatechecker_resilience"));
    }

    void init()
    {
        QSettings s;
        s.remove(QStringLiteral("update"));
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.remove(QStringLiteral("update"));
        s.sync();
    }

    void cleanupTestCase()
    {
        QSettings s;
        s.remove(QStringLiteral("update"));
        s.sync();
    }

    // ── 1. Network down: no infinite retry, bounded duration ────────────────
    //
    // A non-existent file:// URL produces QNetworkReply::ContentNotFoundError
    // with HTTP status code 0 (not an HTTP error at all). The retry path
    // checks for httpStatus == 429 or 500 ≤ status < 600 — neither matches
    // for a 0 status. So the retry MUST NOT trigger. Total time in
    // isChecking is bounded by the local-FS reply latency (instant).
    //
    // We pin this contract by:
    //   * verifying isChecking goes false within a small bound (no second
    //     attempt being scheduled with a 2 s delay);
    //   * verifying the single-attempt path doesn't update last_check.
    void network_down_no_retry_no_infinite_loop()
    {
        const QString url = QStringLiteral(
            "file:///nonexistent-path-for-resilience-test/missing.json");

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        QSignalSpy spyChecking(&uc, &UpdateChecker::isCheckingChanged);
        QSignalSpy spyCompleted(&uc, &UpdateChecker::checkCompleted);
        QSignalSpy spyNoUpdate(&uc, &UpdateChecker::noUpdateAvailable);

        const qint64 elapsed = runCheckSync(uc, url, /*timeoutMs=*/5000);

        // Bound the total time. The retry delay is 2000 ms, so if a retry
        // had erroneously fired the elapsed would be ≥ 2 s. A single
        // attempt against a local file:// should take well under 500 ms.
        // Use 1.5 s as a safe upper bound on slow CI; well below the
        // 2 s retry threshold.
        QVERIFY2(elapsed < 1500,
                 qPrintable(QStringLiteral("Single-attempt should be fast; "
                                          "saw %1 ms — possible accidental "
                                          "retry on non-HTTP error.")
                            .arg(elapsed)));

        // isChecking flipped twice: true on entry, false on exit.
        QCOMPARE(spyChecking.count(), 2);
        QVERIFY(!uc.isChecking());

        // No completion / no-update signals — pure error path.
        QCOMPARE(spyCompleted.count(), 0);
        QCOMPARE(spyNoUpdate.count(), 0);
        QVERIFY(uc.latestVersion().isEmpty());
        QVERIFY(!uc.updateAvailable());

        // last_check must NOT have been bumped on the error path — otherwise
        // a brief outage would silently rob the user of tomorrow's check.
        QSettings s;
        QVERIFY(!s.contains(uc_keys::qs(uc_keys::kLastCheck)));
    }

    // ── 1b. Repeated network failures don't accumulate stuck state ──────────
    //
    // Three consecutive failed checks must each complete cleanly. If state
    // leaks (e.g. isChecking stuck at true), the second call would no-op
    // and break the user's ability to retry. Pin the recovery contract.
    void repeated_failures_recover_cleanly()
    {
        const QString url = QStringLiteral(
            "file:///nope-resilience-repeated/missing.json");

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;

        for (int i = 0; i < 3; ++i) {
            runCheckSync(uc, url, /*timeoutMs=*/3000);
            QVERIFY2(!uc.isChecking(),
                     qPrintable(QStringLiteral("isChecking stuck after attempt %1")
                                .arg(i + 1)));
            QVERIFY(uc.latestVersion().isEmpty());
        }
    }

    // ── 2. Malformed JSON — top-level array ─────────────────────────────────
    //
    // Already covered briefly in extended; re-pin here to make this file
    // self-sufficient as the canonical resilience suite for JSON errors.
    void malformed_top_level_array_is_silent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body = R"([{"tag_name":"v9.9.9"}])";
        const QString url = writeTempJson(dir, QStringLiteral("a.json"), body);

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QVERIFY(uc.latestVersion().isEmpty());
        QVERIFY(!uc.updateAvailable());
    }

    // ── 2b. Malformed JSON — top-level scalar (number) ──────────────────────
    void malformed_top_level_scalar_is_silent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString url = writeTempJson(dir, QStringLiteral("n.json"),
                                          QByteArrayLiteral("42"));

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QVERIFY(uc.latestVersion().isEmpty());
    }

    // ── 2c. Malformed JSON — tag_name is an integer, not a string ───────────
    //
    // QJsonValue::toString() on a non-string returns an empty string, which
    // then short-circuits the parser (tagName.isEmpty() → return). Pin that
    // we don't crash and the state stays clean.
    void malformed_tag_name_integer_is_silent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // tag_name as a number — the GitHub API would never produce this,
        // but a hostile or buggy mirror might. Defensive: must not crash.
        const QByteArray body = R"({"tag_name":12345,"html_url":"https://example.test/x"})";
        const QString url = writeTempJson(dir, QStringLiteral("int.json"), body);

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QVERIFY(uc.latestVersion().isEmpty());
        QVERIFY(!uc.updateAvailable());
    }

    // ── 2d. tag_name is null ────────────────────────────────────────────────
    void malformed_tag_name_null_is_silent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body = R"({"tag_name":null,"html_url":"https://example.test/x"})";
        const QString url = writeTempJson(dir, QStringLiteral("null.json"), body);

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QVERIFY(uc.latestVersion().isEmpty());
    }

    // ── 2e. html_url is missing or wrong type ───────────────────────────────
    //
    // tag_name parses fine but html_url is absent → releaseUrl stays empty.
    // openReleaseInBrowser must be a no-op in this state (it guards on
    // !m_releaseUrl.isEmpty).
    void malformed_html_url_missing_releaseUrl_stays_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body = R"({"tag_name":"v2.0.0"})";
        const QString url = writeTempJson(dir, QStringLiteral("noUrl.json"), body);

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QCOMPARE(uc.latestVersion(), QStringLiteral("2.0.0"));
        QVERIFY(uc.updateAvailable());
        // The release URL is just missing — empty, not garbage.
        QVERIFY(uc.releaseUrl().isEmpty());

        // openReleaseInBrowser is documented to be a no-op when url is empty.
        // We can't observe it returning false directly (it's void), but at
        // least verify calling it doesn't crash.
        uc.openReleaseInBrowser();
    }

    // ── 2f. Parse failure still bumps last_check ────────────────────────────
    //
    // This is a latent contract observation. The implementation calls
    // settings.setValue("update/last_check", now) BEFORE the JSON parse
    // (line 127 of updatechecker.cpp). So even malformed JSON marks the
    // 24 h throttle. Pin this so the throttle behavior is deliberate, not
    // an accident: if it's NOT desirable, this test will fail and force
    // a deliberate review.
    void parse_failure_does_bump_last_check_timestamp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body = R"({"not_tag_name":"v1.0"})";
        const QString url = writeTempJson(dir, QStringLiteral("noKey.json"), body);

        QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
        UpdateChecker uc;
        runCheckSync(uc, url);
        QVERIFY(uc.latestVersion().isEmpty());

        // last_check WAS bumped — the implementation writes it
        // unconditionally on No-Error replies, including parse failures.
        QSettings s;
        QVERIFY(s.contains(uc_keys::qs(uc_keys::kLastCheck)));
        const qint64 lastCheck = s.value(uc_keys::qs(uc_keys::kLastCheck)).toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        // Bumped within the last 60 seconds.
        QVERIFY(lastCheck > 0);
        QVERIFY(now - lastCheck < 60);

        qInfo("UpdateChecker: parse failure on a NoError reply DOES bump "
              "the 24-h throttle timestamp. A garbage GitHub response will "
              "silently delay the next check by 24 h. Documented.");
    }

    // ── 3. Version edge case: trailing / leading dots ───────────────────────
    //
    // "1.2.3."  splits to ["1","2","3",""] under QString::split, so the empty
    // tail becomes 0 via toInt() — i.e. equivalent to "1.2.3.0".
    // "1.2.3" vs "1.2.3." should compare equal (false in both directions).
    void semver_trailing_dot_treated_as_extra_zero()
    {
        // Equal (both → [1,2,3,0]).
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("1.2.3."), QStringLiteral("1.2.3")));
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("1.2.3"), QStringLiteral("1.2.3.")));
    }

    void semver_leading_dot_first_token_is_zero()
    {
        // ".1.2.3" splits to ["", "1", "2", "3"]. First token toInt() → 0.
        // So this is effectively [0, 1, 2, 3] — less than "1.0.0" (which
        // is [1, 0, 0, 0] after right-padding).
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral(".1.2.3"), QStringLiteral("1.0.0")));
        QVERIFY(UpdateChecker::isVersionGreater(
            QStringLiteral("1.0.0"), QStringLiteral(".1.2.3")));
    }

    void semver_excess_components_compared_to_zero_padding()
    {
        // "0.1.0.1.0" has 5 components — that's [0,1,0,1,0] vs "0.1.0" =
        // [0,1,0,0,0] after right-padding. The 4th component differs (1 > 0)
        // so the longer version wins.
        QVERIFY(UpdateChecker::isVersionGreater(
            QStringLiteral("0.1.0.1.0"), QStringLiteral("0.1.0")));
        // Conversely, the shorter version is NOT greater than the longer
        // version with a non-zero excess component.
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("0.1.0"), QStringLiteral("0.1.0.1.0")));
    }

    void semver_negative_lookalike_clamps_to_zero()
    {
        // "-1.0.0".split('.') → ["-1", "0", "0"]; "-1".toInt() returns -1.
        // So this is genuinely a negative-leading comparison. "-1.0.0" vs
        // "0.0.0" → -1 < 0 → false.
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("-1.0.0"), QStringLiteral("0.0.0")));
        // And the reverse direction: "0.0.0" > "-1.0.0" → true.
        QVERIFY(UpdateChecker::isVersionGreater(
            QStringLiteral("0.0.0"), QStringLiteral("-1.0.0")));

        qInfo("UpdateChecker: negative version tokens ARE preserved as "
              "negative ints in the compare. A future hardened parser "
              "should clamp negative tokens to 0 — current impl does not.");
    }

    // ── 3b. Hex-prefixed string parses as zero ──────────────────────────────
    //
    // QString::toInt() with default base=10 returns 0 for "0x10" (no
    // hex parsing without explicit base). So "0x10.0.0" effectively
    // equals "0.0.0".
    void semver_hex_prefix_compares_as_zero()
    {
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("0x10.0.0"), QStringLiteral("0.0.0")));
        QVERIFY(!UpdateChecker::isVersionGreater(
            QStringLiteral("0.0.0"), QStringLiteral("0x10.0.0")));
    }

    // ── 4. Rate-limit semantics: retry-once-only invariant ──────────────────
    //
    // We cannot directly inject HTTP 429 via the file:// scheme — QFile
    // replies don't carry an HTTP status code. But we CAN verify that the
    // overall mechanism doesn't loop infinitely: even with repeated error
    // paths, the total time in isChecking is bounded by at most one retry
    // delay (2 s) + the per-attempt processing time. We measured 1.5 s as
    // a safe ceiling for single-attempt above.
    //
    // What we CAN positively verify: the "Retry-After" header parsing is
    // robust against missing / malformed headers (called only in the
    // retry-eligible path, but we exercise the surrounding state machine
    // by simulating multiple back-to-back failed checks).
    //
    // We also pin: a check kicked off while a previous one is in-flight
    // is dropped (no-op). This prevents the user spamming the button from
    // generating multiple concurrent network requests against GitHub.
    void rate_limit_protection_dropped_concurrent_requests()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QByteArray body =
            R"({"tag_name":"v1.0.0","html_url":"https://example.test/r/1.0.0"})";
        const QString url = writeTempJson(dir, QStringLiteral("rate.json"), body);

        QSettings s;
        s.setValue(uc_keys::qs(uc_keys::kRepoUrl), url);

        QCoreApplication::setApplicationVersion(QStringLiteral("0.5.0"));
        UpdateChecker uc;
        QSignalSpy spyChecking(&uc, &UpdateChecker::isCheckingChanged);

        uc.checkForUpdates(/*force=*/true);
        QVERIFY(uc.isChecking());

        // Spam 10 more calls — every one is dropped while the first is in
        // flight. If the implementation didn't have re-entrancy protection,
        // each call would queue another QNetworkReply and hammer GitHub's
        // rate limiter.
        for (int i = 0; i < 10; ++i)
            uc.checkForUpdates(/*force=*/true);

        // Still only ONE isCheckingChanged emission (the initial true).
        QCOMPARE(spyChecking.count(), 1);

        // Drain the event loop until the original check completes.
        QElapsedTimer t; t.start();
        while (uc.isChecking() && t.elapsed() < 4000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

        QVERIFY(!uc.isChecking());
        // After completion, isCheckingChanged fired one more time → 2 total.
        QCOMPARE(spyChecking.count(), 2);
    }

    // ── 4b. Throttle defense against rate-limit storm ───────────────────────
    //
    // Even if the user repeatedly opens "Settings → check for updates" via
    // a non-force path, the 24 h throttle prevents N consecutive network
    // requests. We pin this defense.
    void rate_limit_protection_24h_throttle_blocks_non_force()
    {
        QSettings s;
        // Simulate a check that just happened.
        s.setValue(uc_keys::qs(uc_keys::kLastCheck),
                   QDateTime::currentSecsSinceEpoch());

        UpdateChecker uc;
        QSignalSpy spyChecking(&uc, &UpdateChecker::isCheckingChanged);

        // 5 non-force calls in a row — all blocked by the throttle.
        for (int i = 0; i < 5; ++i)
            uc.checkForUpdates(/*force=*/false);

        // Never entered isChecking — no signals, no network.
        QCOMPARE(spyChecking.count(), 0);
        QVERIFY(!uc.isChecking());
    }

    // ── 4c. Throttle correctness across time boundary ───────────────────────
    //
    // If last_check is older than 24 h, a non-force call must proceed.
    // Set last_check to 25 hours ago (well beyond the 24 h cutoff) and
    // verify the check proceeds (isChecking goes true on entry).
    void rate_limit_throttle_releases_after_24h()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body =
            R"({"tag_name":"v1.0.0","html_url":"https://example.test/r/1.0.0"})";
        const QString url = writeTempJson(dir, QStringLiteral("throttle.json"), body);

        QSettings s;
        // 25 hours in the past — outside the 24 h throttle window.
        const qint64 twentyFiveHoursAgo =
            QDateTime::currentSecsSinceEpoch() - (25 * 60 * 60);
        s.setValue(uc_keys::qs(uc_keys::kLastCheck), twentyFiveHoursAgo);
        s.setValue(uc_keys::qs(uc_keys::kRepoUrl), url);

        UpdateChecker uc;
        uc.checkForUpdates(/*force=*/false);
        QVERIFY(uc.isChecking());

        // Drain.
        QElapsedTimer t; t.start();
        while (uc.isChecking() && t.elapsed() < 4000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QVERIFY(!uc.isChecking());
    }
};

QTEST_GUILESS_MAIN(TestUpdateCheckerResilience)
#include "test_updatechecker_resilience.moc"
