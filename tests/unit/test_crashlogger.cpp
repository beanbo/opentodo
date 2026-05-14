// Unit tests for CrashLogger — the global Qt message handler that writes
// every qDebug/qInfo/qWarning/qCritical line to a rotating $APPDATA/error.log.
//
// The class exposes only a tiny public surface (install(), logFilePath(),
// logFileSize(), logFilePathRotated(), logFileModifiedIso(),
// openLogFolderInExplorer()), and a lot of behaviour is implicit in
// constructor/destructor side-effects:
//
//   * the constructor creates the AppData dir if missing;
//   * the constructor rotates error.log → error.log.1 if the existing file
//     exceeds 1 MiB;
//   * install() registers a static handler and remembers the previous one;
//   * the destructor restores the previous handler and closes the file.
//
// Because all of this is global static state shared across instances, every
// test must
//   (1) call QStandardPaths::setTestModeEnabled(true) so the log lands in a
//       throwaway AppData under the user's profile (sandboxed by Qt itself),
//   (2) wipe the log file (and any .1 backup) before construction,
//   (3) destroy the CrashLogger before the test slot returns so the static
//       handler is restored before the next slot starts.
//
// The tests intentionally avoid touching qFatal — calling it would abort
// the test binary regardless of how the handler is implemented.

#include <QtTest>
#include <QObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QRegularExpression>
#include <QtGlobal>
#include <QLoggingCategory>

#include <algorithm>
#include <thread>
#include <vector>

#include "services/crashlogger.h"

class TestCrashLogger : public QObject
{
    Q_OBJECT

private:
    // The directory CrashLogger writes to is exactly what the constructor
    // computes via QStandardPaths::writableLocation(AppDataLocation). We
    // mirror that here so the tests can find the file. With test mode
    // enabled this lives under the user's QStandardPaths test root, NOT the
    // real user APPDATA — see initTestCase().
    static QString logDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    static QString logPath()
    {
        return logDir() + QStringLiteral("/error.log");
    }

    static QString rotatedPath()
    {
        return logPath() + QStringLiteral(".1");
    }

    // Erase any leftover log files between test slots so each slot starts
    // from a clean filesystem state. The CrashLogger destructor already
    // restored the handler and closed the file so this is safe.
    static void wipeLogFiles()
    {
        QFile::remove(logPath());
        QFile::remove(rotatedPath());
    }

    // Read the active log as a UTF-8 string. Returns empty on failure.
    static QString readLog()
    {
        QFile f(logPath());
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll());
    }

    // Force the active log to size `bytes` by writing zero-filled padding.
    // Used to set up the rotation test without spinning thousands of
    // qWarning() calls (which would flood stderr and slow the suite).
    static bool padLogToSize(qint64 bytes)
    {
        QFile f(logPath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
            return false;
        const QByteArray chunk(8192, '.');
        qint64 remaining = bytes - f.size();
        while (remaining > 0) {
            const qint64 toWrite = qMin<qint64>(remaining, chunk.size());
            f.write(chunk.constData(), toWrite);
            remaining -= toWrite;
        }
        f.flush();
        f.close();
        return true;
    }

private slots:
    void initTestCase()
    {
        // Per-test org/app keeps the test-mode AppDataLocation isolated from
        // other tests that toggle setTestModeEnabled (test_filemanager*,
        // test_taskexporter, test_qml_*) so the rotating error.log path
        // does not collide under `ctest -j`.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_crashlogger"));

        // Sandbox QStandardPaths into a per-user test location so we never
        // touch the real $APPDATA/OpenTodo/error.log.
        QStandardPaths::setTestModeEnabled(true);

        // Make sure the AppDataLocation parent exists — CrashLogger will
        // mkpath() too, but creating it up front lets us assert mkpath
        // succeeded by checking the dir before any test runs.
        QDir().mkpath(logDir());
    }

    void cleanupTestCase()
    {
        wipeLogFiles();
        QStandardPaths::setTestModeEnabled(false);
    }

    void cleanup()
    {
        // Run after every test slot. The slot itself MUST have destroyed
        // its CrashLogger by now (via stack scope) so the file handle is
        // closed and we can safely unlink the underlying file on Windows.
        wipeLogFiles();
    }

    // ── 1. Path resolution ────────────────────────────────────────────────────

    // Constructing a CrashLogger should
    //   (a) create the AppData directory if it didn't exist,
    //   (b) populate logFilePath() with a non-empty absolute path,
    //   (c) actually create the error.log file on disk (open for append
    //       creates the file if missing).
    void path_resolves_and_directory_is_created()
    {
        // Tear the directory down completely to prove the constructor will
        // recreate it. Use QDir::removeRecursively so a non-empty dir
        // doesn't block removal.
        QDir d(logDir());
        if (d.exists())
            d.removeRecursively();
        QVERIFY(!d.exists());

        CrashLogger logger;

        QVERIFY(QDir(logDir()).exists());

        const QString p = logger.logFilePath();
        QVERIFY(!p.isEmpty());
        QVERIFY(QFileInfo(p).isAbsolute());
        QVERIFY(QFile::exists(p));

        // logFilePathRotated() must just be path + ".1".
        QCOMPARE(logger.logFilePathRotated(), p + QStringLiteral(".1"));
    }

    // ── 2. install() / destructor handler round-trip ─────────────────────────

    // Capture the live Qt handler before construction, install ours,
    // then destroy the logger and confirm the handler pointer slot is
    // back where it was.  We probe the handler indirectly: replacing
    // the handler with a stub returns the previously-active handler,
    // and that returned pointer is what we compare against the original.
    void install_and_uninstall_round_trip_restores_previous_handler()
    {
        // A no-op handler we use purely as a probe.
        struct Probe {
            static void noop(QtMsgType, const QMessageLogContext &, const QString &) {}
        };

        // Snapshot whatever handler is currently active (may be Qt's
        // default or one set by the test runner). Restore immediately.
        QtMessageHandler original = qInstallMessageHandler(&Probe::noop);
        qInstallMessageHandler(original);

        {
            CrashLogger logger;
            logger.install();
            // CrashLogger::handleMessage is now active. We don't compare
            // the pointer directly — handleMessage is a private static
            // member — but we can confirm install did SOMETHING by
            // probing what came back when we replace it: the returned
            // handler should not be the original anymore.
            QtMessageHandler activeAfterInstall = qInstallMessageHandler(&Probe::noop);
            QVERIFY(reinterpret_cast<void*>(activeAfterInstall) != reinterpret_cast<void*>(original));
            // Put CrashLogger's handler back so the destructor's restore
            // operates on the state install() left behind.
            qInstallMessageHandler(activeAfterInstall);
        } // ~CrashLogger restores `original`.

        // After destruction, the active handler should once again be
        // the original. Probe by replacing-then-restoring.
        QtMessageHandler nowActive = qInstallMessageHandler(&Probe::noop);
        qInstallMessageHandler(nowActive);
        QCOMPARE(reinterpret_cast<void*>(nowActive), reinterpret_cast<void*>(original));
    }

    // ── 3. qWarning is captured with timestamp + WARN level prefix ───────────

    void qWarning_is_written_with_level_and_timestamp()
    {
        CrashLogger logger;
        logger.install();
        qWarning("hello-warning-message");
        // Destroy logger to flush and close the file before we read it.

        // We can't destroy in-place inside a slot without scoping; reopen
        // the read after destruction:
        // (Trick: explicitly invoke ~CrashLogger by re-scoping.)

        // Since logger is on the stack of THIS function we have to read
        // before it goes out of scope. The handler flushes after every
        // write, so the bytes are already on disk.
        const QString contents = readLog();
        QVERIFY2(contents.contains(QStringLiteral("hello-warning-message")),
                 qPrintable(contents));
        QVERIFY2(contents.contains(QStringLiteral("[WARN ]")),
                 qPrintable(contents));

        // Timestamp at the very start of the line, ISO-8601 with ms.
        // Format: [2026-05-09T12:34:56.789]
        QRegularExpression tsRe(QStringLiteral(
            R"(\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\] \[WARN \])"));
        QVERIFY2(tsRe.match(contents).hasMatch(), qPrintable(contents));
    }

    // ── 4. qCritical level prefix ────────────────────────────────────────────

    void qCritical_is_written_with_CRIT_level()
    {
        CrashLogger logger;
        logger.install();
        qCritical("oops-critical-event");

        const QString contents = readLog();
        QVERIFY2(contents.contains(QStringLiteral("oops-critical-event")),
                 qPrintable(contents));
        QVERIFY2(contents.contains(QStringLiteral("[CRIT ]")),
                 qPrintable(contents));
    }

    // ── 5. qDebug level — verify CrashLogger's actual policy ─────────────────

    // The handler maps QtDebugMsg → "DEBUG", but Qt's logging-category
    // filter rules can drop debug messages BEFORE they reach the handler.
    // To probe what CrashLogger actually does, force debug output on for
    // the default category, then check whether the line appears.
    //
    // Either outcome is acceptable as long as it's documented:
    //   * if the line appears, CrashLogger writes debug messages;
    //   * if it doesn't, it's because Qt filtered them upstream.
    void qDebug_policy_is_pass_through_from_handler_perspective()
    {
        // Enable debug for the default category so Qt routes it to our
        // handler instead of filtering it out. Local RAII restores the
        // previous (default) rules at end of scope so neighbouring tests
        // are not affected.
        struct FilterRulesRestore {
            ~FilterRulesRestore() { QLoggingCategory::setFilterRules(QString()); }
        } restore;
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true"));

        CrashLogger logger;
        logger.install();
        qDebug("low-noise-debug-marker");

        const QString contents = readLog();
        // CrashLogger does not filter by level — every message that reaches
        // its handleMessage() is written. Expect the marker to appear with
        // the [DEBUG] tag.
        QVERIFY2(contents.contains(QStringLiteral("low-noise-debug-marker")),
                 qPrintable(contents));
        QVERIFY2(contents.contains(QStringLiteral("[DEBUG]")),
                 qPrintable(contents));
    }

    // ── 6. Multi-line message preserves embedded newline ─────────────────────

    void multiline_message_preserves_internal_newline()
    {
        CrashLogger logger;
        logger.install();
        // Use the printf-style overload of qWarning so QDebug does not
        // re-escape the embedded newline as a literal "\\n" sequence.
        qWarning("first-line\nsecond-line");

        const QString contents = readLog();
        // The internal '\n' must survive — CrashLogger does not strip or
        // escape it. Look for both halves on what should now be two
        // separate lines, with "second-line" NOT preceded by a level tag
        // (i.e. it's a continuation, not a new log entry).
        // On Windows, QTextStream/QFile may translate '\n' into "\r\n" via
        // QIODevice::Text mode. Normalize before comparison.
        const QString normalized = QString(contents).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QVERIFY2(normalized.contains(QStringLiteral("first-line\nsecond-line")),
                 qPrintable(contents));

        // Sanity: only one [WARN ] tag should appear since this was a
        // single qWarning() call.
        QCOMPARE(contents.count(QStringLiteral("[WARN ]")), 1);
    }

    // ── 7. UTF-8 encoding for unicode payload (Cyrillic + emoji) ─────────────

    void unicode_message_is_utf8_encoded()
    {
        // Build the payload from raw UTF-8 bytes so we don't depend on
        // the source-file encoding picked up by QStringLiteral. The four
        // trailing bytes are the UTF-8 encoding of U+1F30D EARTH GLOBE
        // EUROPE-AFRICA (🌍).
        const QByteArray utf8(
            "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82" // Привет
            " "
            "\xD0\xBC\xD0\xB8\xD1\x80"                          // мир
            " "
            "\xF0\x9F\x8C\x8D");                                // 🌍
        const QString payload = QString::fromUtf8(utf8);

        CrashLogger logger;
        logger.install();
        qWarning("%s", utf8.constData());

        QFile f(logPath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        f.close();

        // Round-trip the bytes through UTF-8 decoding and verify the
        // payload is intact.
        const QString decoded = QString::fromUtf8(raw);
        QVERIFY2(decoded.contains(payload), qPrintable(decoded));

        // Belt-and-braces: the raw bytes should literally contain the
        // UTF-8 encoding of "Привет" verbatim, proving CrashLogger did
        // not transcode to anything else (Latin-1, UTF-16, etc.).
        QVERIFY(raw.contains(utf8));
    }

    // ── 8. Rotation: existing log >= 1 MiB triggers rename to .1 ─────────────

    // The rotation policy lives in the constructor: if error.log already
    // exists and is >= 1 MiB at construction time, it's renamed to
    // error.log.1 (overwriting any prior .1) and a fresh error.log is
    // opened. Rotation is NOT performed mid-write.
    void large_existing_log_is_rotated_on_next_construction()
    {
        // Pre-seed the log with > 1 MiB of content WITHOUT going through
        // CrashLogger (which would only rotate at the next construction
        // anyway). Keep the file closed so the next CrashLogger ctor can
        // rename it on Windows.
        QFile::remove(logPath());
        QFile::remove(rotatedPath());
        QVERIFY(padLogToSize(1 * 1024 * 1024 + 4096));
        const qint64 preSize = QFileInfo(logPath()).size();
        QVERIFY(preSize >= 1 * 1024 * 1024);

        // Constructing a CrashLogger should rotate the oversized file to
        // .1 and open a fresh, near-empty error.log.
        {
            CrashLogger logger;
            logger.install();
            qWarning("post-rotation-marker");

            // .1 should now exist with the pre-rotation bytes.
            QVERIFY2(QFile::exists(rotatedPath()),
                     qPrintable(QStringLiteral("expected rotated backup at %1")
                                .arg(rotatedPath())));
            QCOMPARE(QFileInfo(rotatedPath()).size(), preSize);

            // Active log should be small and contain only what we wrote
            // after the rotation (plus possibly some startup messages).
            const QString contents = readLog();
            QVERIFY2(contents.contains(QStringLiteral("post-rotation-marker")),
                     qPrintable(contents));
            QVERIFY(QFileInfo(logPath()).size() < preSize);
        }
    }

    // Sanity check that rotation overwrites the prior .1 backup rather
    // than failing the rename (QFile::rename returns false if target
    // exists, so the constructor must remove() the old .1 first).
    void rotation_overwrites_existing_backup()
    {
        QFile::remove(logPath());
        QFile::remove(rotatedPath());

        // Pre-create a stale .1 with sentinel content.
        {
            QFile prior(rotatedPath());
            QVERIFY(prior.open(QIODevice::WriteOnly));
            prior.write("STALE-BACKUP-MUST-BE-REPLACED");
            prior.close();
        }

        // Pre-seed an oversized active log.
        QVERIFY(padLogToSize(1 * 1024 * 1024 + 1024));

        {
            CrashLogger logger;
            Q_UNUSED(logger);
            // Stale sentinel must be gone; rotated file should now be
            // the >1 MiB former active log.
            QFile rotated(rotatedPath());
            QVERIFY(rotated.open(QIODevice::ReadOnly));
            const QByteArray rotatedBytes = rotated.read(64);
            rotated.close();
            QVERIFY2(!rotatedBytes.contains("STALE-BACKUP-MUST-BE-REPLACED"),
                     "rotation did not overwrite the stale backup");
        }
    }

    // ── 9. Concurrent writes serialise via the internal mutex ────────────────

    // Spin N threads each calling qWarning() in a tight loop and verify
    // that every line in the resulting file is well-formed (starts with
    // a timestamp, ends with the terminating newline) — i.e. the mutex
    // really did serialise interleaved writes.
    void concurrent_warnings_produce_well_formed_lines()
    {
        CrashLogger logger;
        logger.install();

        constexpr int kThreads = 4;
        constexpr int kPerThread = 25; // 100 lines total

        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([t]() {
                for (int i = 0; i < kPerThread; ++i) {
                    qWarning("CONCUR t=%d i=%d", t, i);
                }
            });
        }
        for (auto &w : workers)
            w.join();

        const QString contents = readLog();

        // Every line should start with '[' (timestamp opener) — if writes
        // had interleaved, we'd see lines starting with arbitrary bytes
        // from another write.
        const QStringList lines = contents.split(QLatin1Char('\n'),
                                                 Qt::SkipEmptyParts);
        QVERIFY(lines.size() >= kThreads * kPerThread);

        for (const QString &line : lines) {
            QVERIFY2(line.startsWith(QLatin1Char('[')),
                     qPrintable(QStringLiteral("garbled line: ") + line));
        }

        // Confirm we actually got ~100 CONCUR lines and none of them got
        // silently dropped.
        const int concurLines = std::count_if(
            lines.cbegin(), lines.cend(),
            [](const QString &l) { return l.contains(QStringLiteral("CONCUR")); });
        QCOMPARE(concurLines, kThreads * kPerThread);
    }

    // ── 10. After destruction, qWarning no longer reaches the file ───────────

    // The destructor restores the previous handler AND nulls s_logFile, so
    // any qWarning issued AFTER the logger goes out of scope must NOT
    // appear in error.log. We verify the byte size of the file is
    // unchanged across post-destruction warnings.
    void destruction_stops_writes_to_log_file()
    {
        qint64 sizeAfterDestruction = -1;
        {
            CrashLogger logger;
            logger.install();
            qWarning("written-while-installed");
        } // ~CrashLogger here.

        sizeAfterDestruction = QFileInfo(logPath()).size();
        QVERIFY(sizeAfterDestruction > 0);

        // Issue several warnings now that the handler is restored. They
        // should land in the previous handler (typically stderr) but
        // must NOT touch error.log.
        for (int i = 0; i < 10; ++i)
            qWarning("MUST-NOT-APPEAR-%d", i);

        QCOMPARE(QFileInfo(logPath()).size(), sizeAfterDestruction);

        const QString contents = readLog();
        QVERIFY2(contents.contains(QStringLiteral("written-while-installed")),
                 qPrintable(contents));
        QVERIFY2(!contents.contains(QStringLiteral("MUST-NOT-APPEAR")),
                 qPrintable(contents));
    }

    // ── 11. logFileSize() and logFileModifiedIso() reflect actual file ───────

    void logFileSize_and_modifiedIso_track_writes()
    {
        CrashLogger logger;
        logger.install();

        // Initial size may be 0 (file created but no writes yet) or > 0
        // if the act of constructing logged anything. Either way, capture
        // it and make sure subsequent writes increase it.
        const qint64 sizeBefore = logger.logFileSize();
        qWarning("size-test-1");
        qWarning("size-test-2");
        const qint64 sizeAfter = logger.logFileSize();
        QVERIFY2(sizeAfter > sizeBefore,
                 qPrintable(QStringLiteral("expected file growth, was %1 → %2")
                            .arg(sizeBefore).arg(sizeAfter)));

        // logFileModifiedIso() should be a non-empty ISO-8601 timestamp.
        const QString iso = logger.logFileModifiedIso();
        QVERIFY(!iso.isEmpty());
        // ISO-8601 starts with YYYY-MM-DD.
        QRegularExpression isoRe(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}T)"));
        QVERIFY2(isoRe.match(iso).hasMatch(), qPrintable(iso));
    }
};

QTEST_GUILESS_MAIN(TestCrashLogger)
#include "test_crashlogger.moc"
