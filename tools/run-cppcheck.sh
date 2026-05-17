#!/usr/bin/env bash
# =============================================================================
# OpenTodo standalone cppcheck runner (Linux/macOS).
#
# Run from the project root:
#     tools/run-cppcheck.sh
#
# Output is streamed to cppcheck.log in the working directory. Exit code is
# cppcheck's own — non-zero only on internal errors, not on findings.
#
# Suppressions are kept in sync with the `cppcheck` CMake custom target in
# CMakeLists.txt — update both together.
# =============================================================================

set -e

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not found in PATH."
    echo "Install via your package manager (apt install cppcheck, brew install cppcheck, ...)"
    exit 1
fi

cppcheck \
    --enable=all \
    --inconclusive \
    --library=qt \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=useStlAlgorithm \
    --suppress=cstyleCast \
    --suppress=unmatchedSuppression \
    --inline-suppr \
    --std=c++20 \
    --language=c++ \
    -I src/ \
    --quiet \
    src/ 2>cppcheck.log

echo "Done. See cppcheck.log"
