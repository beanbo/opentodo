// libFuzzer harness for DateParser.
//
// Build: cmake -B build-fuzz -DOPENTODO_ENABLE_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
//        cmake --build build-fuzz --target fuzz_dateparser
// Run:   ./build-fuzz/fuzz_dateparser -max_total_time=300 tests/fuzz/corpus/dateparser/
//
// Pumps arbitrary bytes into both the English and Russian rule sets via the
// static parseEnglish / parseRussian entry points (skipping the QObject layer
// avoids any need for a QCoreApplication at fuzz time).  Also exercises the
// public Q_INVOKABLE parse() which routes through the language switch.
//
// Reference date is fixed so that fuzz reproducers are deterministic.

#include "nlp/dateparser.h"

#include <QDate>
#include <QString>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;

    const QString input = QString::fromUtf8(
        reinterpret_cast<const char *>(data),
        static_cast<int>(size));

    // Pin the reference date so a crashing input reproduces every run.
    static const QDate reference(2026, 5, 17);

    try { (void)DateParser::parseEnglish(input, reference); } catch (...) {}
    try { (void)DateParser::parseRussian(input, reference); } catch (...) {}

    // Exercise the dispatch wrapper too. The QML-facing entry point creates
    // a temporary DateParser; cheap, and verifies the dispatch path doesn't
    // crash on the same inputs the static rule entries survive.
    try {
        DateParser parser;
        (void)parser.parse(input, QStringLiteral("en"));
        (void)parser.parse(input, QStringLiteral("ru"));
    } catch (...) {
    }

    return 0;
}
