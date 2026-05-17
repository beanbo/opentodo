#!/usr/bin/env bash
# =============================================================================
#  OpenTodo code coverage build script (Linux / macOS)
#
#  Configures a dedicated build-coverage/ tree with OPENTODO_ENABLE_COVERAGE
#  on, builds, runs ctest, and renders an HTML report at
#    build-coverage/coverage/index.html
#
#  Prerequisites:
#    - Qt 6 (matching the project requirements) on PATH
#    - GCC 11+ OR Clang 14+
#    - For GCC:   gcovr           (pip install gcovr)
#    - For Clang: llvm-profdata + llvm-cov (in your LLVM toolchain)
#
#  Toolchain select: OPENTODO_COVERAGE_TOOLCHAIN=gcc|clang  (default: gcc).
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SOURCE_DIR}/build-coverage"

TOOLCHAIN="${OPENTODO_COVERAGE_TOOLCHAIN:-gcc}"

case "${TOOLCHAIN}" in
    gcc)
        if ! command -v gcovr >/dev/null 2>&1; then
            echo "[coverage] ERROR: gcovr not found on PATH." >&2
            echo "[coverage] Install with: pip install gcovr" >&2
            exit 1
        fi
        ;;
    clang)
        for tool in llvm-profdata llvm-cov; do
            if ! command -v "${tool}" >/dev/null 2>&1; then
                echo "[coverage] ERROR: ${tool} not found on PATH." >&2
                echo "[coverage] Install the LLVM toolchain (e.g. apt-get install llvm)." >&2
                exit 1
            fi
        done
        export CC="${CC:-clang}"
        export CXX="${CXX:-clang++}"
        ;;
    *)
        echo "[coverage] ERROR: unknown OPENTODO_COVERAGE_TOOLCHAIN='${TOOLCHAIN}' (expected: gcc | clang)" >&2
        exit 1
        ;;
esac

# -- Fresh build tree so .gcno/.gcda or .profraw don't get mixed with stale data
if [[ -d "${BUILD_DIR}" ]]; then
    echo "[coverage] Removing existing build-coverage/ tree..."
    rm -rf "${BUILD_DIR}"
fi

# -- Configure: Debug + coverage instrumentation ------------------------------
echo "[coverage] Configuring with OPENTODO_ENABLE_COVERAGE=ON, CMAKE_BUILD_TYPE=Debug (toolchain: ${TOOLCHAIN})..."
cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DOPENTODO_ENABLE_COVERAGE=ON \
    -DCMAKE_BUILD_TYPE=Debug

# -- Build (libraries + executable + tests) -----------------------------------
echo "[coverage] Building..."
cmake --build "${BUILD_DIR}" --parallel

# -- Run tests + render report -------------------------------------------------
echo "[coverage] Running tests and generating coverage report..."
cmake --build "${BUILD_DIR}" --target coverage_report

echo
echo "[coverage] Done."
echo "[coverage] Open the report: ${BUILD_DIR}/coverage/index.html"
