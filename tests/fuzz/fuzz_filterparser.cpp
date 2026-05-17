// libFuzzer harness for FilterParser.
//
// Build: cmake -B build-fuzz -DOPENTODO_ENABLE_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
//        cmake --build build-fuzz --target fuzz_filterparser
// Run:   ./build-fuzz/fuzz_filterparser -max_total_time=300 tests/fuzz/corpus/filterparser/
//
// The harness pumps arbitrary bytes into FilterParser::parse(QString).  The
// parser must never crash, hang, or trip ASan/UBSan regardless of input.
// Any returned AST is freed on the spot so leak detection remains useful.

#include "filter/filterparser.h"
#include "filter/tokens.h"

#include <QString>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Cap input size: libFuzzer prefers a soft cap so the corpus stays fast.
    // 4 KiB is comfortably larger than any realistic saved-filter query.
    if (size > 4096) return 0;

    const QString input = QString::fromUtf8(
        reinterpret_cast<const char *>(data),
        static_cast<int>(size));

    FilterParser parser;
    AstNode *ast = nullptr;
    try {
        ast = parser.parse(input);
    } catch (...) {
        // FilterParser is not declared noexcept; swallow anything so the
        // fuzzer only flags genuine crashes / sanitizer hits.
    }
    delete ast;

    // Also exercise the isValid() entry point which re-parses internally.
    try {
        (void)parser.isValid(input);
    } catch (...) {
    }

    return 0;
}
