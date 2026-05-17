# Building OpenTodo

This document covers the desktop / Android / WASM build paths and the optional
code-coverage workflow layered on top of them. For the high-level layered
architecture see [../CLAUDE.md](../CLAUDE.md); for WASM specifics see
[WASM.md](WASM.md).

## Toolchain

- Qt 6.11.0 (validated). Required modules: `Core Gui Quick Qml Network Widgets
  QuickControls2`. Optional: `Positioning` (geofenced reminders),
  `LabsSettings`, `LinguistTools`.
- CMake 3.21+.
- A C++20 compiler. On Windows the validated kit is **MinGW 13.1** as shipped
  with Qt (`C:\Qt\Tools\mingw1310_64`). GCC 11+, Clang 14+, MSVC 19.30+ also
  build cleanly.
- For WASM: Emscripten matching the Qt for WebAssembly build (Qt 6.11 → emsdk
  4.0.7). See [WASM.md](WASM.md) and
  [../tools/build-wasm.bat](../tools/build-wasm.bat).
- For Android: NDK + Java toolchain per the Qt Android docs.

## Standard build

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows multi-config generators put binaries under `build/release/`; on
single-config generators (Ninja, MinGW Makefiles) they sit directly under
`build/`. The test suite (Qt Test + QtQuickTest, 137+ tests under
[../tests/](../tests/)) is built when `BUILD_TESTING=ON` (the default).

## Optional CMake options

| Option                     | Default | Effect                                                                   |
|----------------------------|---------|--------------------------------------------------------------------------|
| `BUILD_TESTING`            | `ON`    | Compile the test suite and register it with CTest.                       |
| `OPENTODO_ENABLE_COVERAGE` | `OFF`   | Instrument all targets for line coverage (GCC / Clang only; MSVC warns). |

## Code coverage

OpenTodo supports gcov-style coverage for GCC and source-based coverage for
Clang via a single opt-in CMake option. Coverage is **off by default** because
the instrumentation forces `-O0`, balloons object size, and the `.gcno`/`.gcda`
files written next to every `.obj` pollute a normal incremental build.

### Quick start

Windows (Qt MinGW kit):

```bat
tools\coverage.bat
```

Linux / macOS:

```bash
tools/coverage.sh                                       # GCC (default)
OPENTODO_COVERAGE_TOOLCHAIN=clang tools/coverage.sh     # Clang
```

Both scripts blow away `build-coverage/`, reconfigure with
`-DOPENTODO_ENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug`, build, run `ctest`,
and render the HTML report at `build-coverage/coverage/index.html`.

### Manual workflow

```
cmake -S . -B build-coverage -DOPENTODO_ENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage
cmake --build build-coverage --target coverage_report
```

The `coverage_report` target runs `ctest` to populate the raw coverage data,
then renders HTML under `build-coverage/coverage/`. It is only registered when
`OPENTODO_ENABLE_COVERAGE=ON` *and* `BUILD_TESTING=ON`.

### Required report-side tools

These are looked up at configure time with `find_program` and are **not** hard
CMake dependencies — a fresh checkout configures cleanly without them. You only
hit the error when you invoke the `coverage_report` target.

| Compiler | Tool                       | Install                                                                                                                              |
|----------|----------------------------|--------------------------------------------------------------------------------------------------------------------------------------|
| GCC      | `gcovr`                    | `pip install gcovr`                                                                                                                  |
| Clang    | `llvm-profdata`, `llvm-cov`| Ship with the LLVM toolchain (`apt install llvm`, Homebrew `llvm`, etc.)                                                             |
| MSVC     | OpenCppCoverage (external) | https://github.com/OpenCppCoverage/OpenCppCoverage — run against a normal Debug build. The `OPENTODO_ENABLE_COVERAGE` option is a no-op under MSVC. |

### What's excluded from the report

- `tests/` — we measure source coverage, not the harness exercising it.
- `build/` and the CMake binary directory — generated MOC / RCC / UIC files
  inflate denominators without telling you anything useful about the project.

To narrow the report further, edit the `--exclude` / `-ignore-filename-regex`
arguments on the `coverage_report` target in
[../CMakeLists.txt](../CMakeLists.txt).

### CI tip

`gcovr --print-summary` (already wired into the GCC target) prints a one-line
`lines: NN%` summary to stdout. CI jobs can grep the build log for that line
to gate on coverage thresholds without parsing HTML.

## Cross-target builds

- **WASM**: [WASM.md](WASM.md) +
  [../tools/build-wasm.bat](../tools/build-wasm.bat).
- **Android**: open the project in Qt Creator with the Android kit configured;
  [../CLAUDE.md](../CLAUDE.md) lists the platform-conditional sources and the
  `QT_ANDROID_PACKAGE_SOURCE_DIR` mapping.
