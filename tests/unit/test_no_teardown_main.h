#pragma once

// QTEST_MAIN / QTEST_GUILESS_MAIN variants that skip process teardown.
//
// Tests that build a QQmlEngine / QQuickView or a SyncEngine can wedge in
// their destructors under QtTest on Windows (the QML engine's queued
// deleteLaters, the QSettings flush, SyncEngine's network threads blocking
// the QGuiApplication destructor until STATUS_FAIL_FAST_EXCEPTION). These
// tests used to std::_Exit(0) from their last slot or cleanupTestCase(),
// which had two flaws: the exit code was 0 even when a slot had failed (the
// Totals line was never printed either), and any early QVERIFY return that
// skipped the _Exit fell through to the normal teardown and hung — that is
// what wedged the first GitHub Actions run for hours.
//
// Here QTest::qExec() runs to completion (every slot, cleanupTestCase,
// results and Totals logged) and only then the process leaves through
// std::_Exit with qExec's own return value — the number of failed tests.

#include <QtTest>

#include <cstdio>
#include <cstdlib>

#define OPENTODO_QTEST_MAIN_WRAPPER_NO_TEARDOWN(TestObject, ...) \
int main(int argc, char *argv[]) \
{ \
    QT_PREPEND_NAMESPACE(QTest::Internal::callInitMain)<TestObject>(); \
    __VA_ARGS__ \
    TestObject tc; \
    QTEST_SET_MAIN_SOURCE_PATH \
    const int failures = QTest::qExec(&tc, argc, argv); \
    std::fflush(stdout); \
    std::fflush(stderr); \
    std::_Exit(failures); \
}

#define OPENTODO_QTEST_MAIN_NO_TEARDOWN(TestObject) \
    OPENTODO_QTEST_MAIN_WRAPPER_NO_TEARDOWN(TestObject, QTEST_MAIN_SETUP())

#define OPENTODO_QTEST_GUILESS_MAIN_NO_TEARDOWN(TestObject) \
    OPENTODO_QTEST_MAIN_WRAPPER_NO_TEARDOWN(TestObject, QTEST_QAPP_SETUP(QCoreApplication))
