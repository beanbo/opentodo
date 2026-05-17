// libFuzzer harness for QuickAddParser.
//
// Build: cmake -B build-fuzz -DOPENTODO_ENABLE_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
//        cmake --build build-fuzz --target fuzz_quickaddparser
// Run:   ./build-fuzz/fuzz_quickaddparser -max_total_time=300 tests/fuzz/corpus/quickaddparser/
//
// QuickAddParser::parse internally invokes DateParser (both EN + RU), so a
// single harness exercises a wide slice of the pipeline:
//   1. Project (#…) / label (#… or @…) extraction
//   2. Priority (p1..p4, !1..!4) extraction
//   3. Suffix + prefix date probing against DateParser
//
// QuickAddParser is a plain static-method class with no QObject inheritance,
// so no QCoreApplication is required.

#include "nlp/quickaddparser.h"

#include <QString>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;

    const QString input = QString::fromUtf8(
        reinterpret_cast<const char *>(data),
        static_cast<int>(size));

    try {
        (void)QuickAddParser::parse(input);
    } catch (...) {
        // QuickAddParser is not declared noexcept; absorb anything that escapes.
    }

    return 0;
}
