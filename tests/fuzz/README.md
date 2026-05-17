# libFuzzer harnesses

Coverage-guided fuzzers for OpenTodo's three free-form user-input parsers:

| Harness                 | Target                                        | Source                          |
| ----------------------- | --------------------------------------------- | ------------------------------- |
| `fuzz_filterparser`     | `FilterParser::parse` / `isValid`             | `src/filter/filterparser.cpp`   |
| `fuzz_dateparser`       | `DateParser::parseEnglish` / `parseRussian`   | `src/nlp/{rules_en,rules_ru}.cpp` |
| `fuzz_quickaddparser`   | `QuickAddParser::parse` (drives DateParser)   | `src/nlp/quickaddparser.cpp`    |

All three accept arbitrary user input from the UI, so the contract is simple:
**they must never crash, hang, abort, or trip ASan/UBSan**, regardless of input.

## Requirements

- **Clang** (any reasonably recent version). libFuzzer is shipped as an LLVM
  compiler-rt component and is not available with MSVC or GCC.
- **AddressSanitizer + UndefinedBehaviorSanitizer** are linked in by default.
  Clang's compiler-rt provides both; no extra package needed.
- Build is **opt-in**: the targets are excluded from the default `all` and
  guarded by a CMake option, so a normal `cmake --build` under MSVC keeps
  working.

> **Windows note.** On a fresh Windows install without LLVM, `clang++` is not
> on PATH and these harnesses cannot be built. Install LLVM (e.g. via the
> official MSI from <https://releases.llvm.org/> or `winget install LLVM.LLVM`)
> and re-open the shell so `clang++ --version` resolves before configuring.
> The recommended runner platform is Linux (Clang ships in every distro;
> ASan/UBSan/libFuzzer work out of the box).

## Build

```bash
cmake -B build-fuzz \
      -DOPENTODO_ENABLE_FUZZ=ON \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang

cmake --build build-fuzz --target fuzz_all       # builds all three
# or one at a time:
cmake --build build-fuzz --target fuzz_dateparser
```

Each harness lands in `build-fuzz/tests/fuzz/`.

## Run

Seed corpora live next to this file:

```
tests/fuzz/corpus/
├── filterparser/
├── dateparser/
└── quickaddparser/
```

Run a single harness for five minutes against its seed corpus:

```bash
./build-fuzz/tests/fuzz/fuzz_dateparser \
    -max_total_time=300 \
    tests/fuzz/corpus/dateparser/
```

Useful libFuzzer flags:

| Flag                                | Meaning                                                |
| ----------------------------------- | ------------------------------------------------------ |
| `-max_total_time=300`               | Hard stop after 5 min wall-clock                       |
| `-max_len=4096`                     | Match the in-harness cap                               |
| `-workers=4 -jobs=8`                | Parallel fuzz processes                                |
| `-rss_limit_mb=2048`                | Kill the worker on excessive RAM use                   |
| `-print_pcs=1 -print_final_stats=1` | Diagnostics on crash / exit                            |
| `-detect_leaks=0`                   | Disable LeakSanitizer if it produces spurious reports  |

The seed corpus is also a **regression corpus**: when a crash is found, drop
the minimized reproducer back into `tests/fuzz/corpus/<parser>/` so future
runs cover it too.

## Triaging a crash

When libFuzzer finds a crash it writes the offending input to
`crash-<sha1>` in the current working directory:

```
INFO: A corpus is not provided, starting from an empty corpus
==12345==ERROR: AddressSanitizer: heap-buffer-overflow ...
artifact_prefix='./'; Test unit written to ./crash-deadbeef...
```

**Minimize the reproducer** so the saved input is as small as possible:

```bash
./fuzz_dateparser -minimize_crash=1 \
    -runs=10000 \
    crash-deadbeef0123456789abcdef
# writes minimized-from-crash-deadbeef... to cwd
```

**Re-run on the minimized input** to confirm:

```bash
./fuzz_dateparser minimized-from-crash-deadbeef...
```

**Convert to a regression test.** Once you have a stable reproducer:

1. Read the bytes into a hex/printable string.
2. Add a slot to the appropriate unit test
   (`tests/unit/test_dateparser_*.cpp`, `test_filterparser*.cpp`, or
   `test_quickaddparser*.cpp`) asserting that `parse(<input>)` returns
   cleanly (whatever "cleanly" means for the parser — typically
   `result.valid == false` and no crash).
3. Copy the minimized input into `tests/fuzz/corpus/<parser>/` so the
   fuzzer remembers it across runs.
4. Fix the underlying bug, re-run the harness for a few minutes against
   the seed corpus, then push.

## Quick continuous run

Most useful for CI smoke or local "did I just regress something":

```bash
for p in filterparser dateparser quickaddparser; do
    ./build-fuzz/tests/fuzz/fuzz_$p \
        -max_total_time=120 \
        -print_final_stats=1 \
        tests/fuzz/corpus/$p/ || exit 1
done
```

Two minutes per parser × three parsers ≈ 6 min and reliably re-finds any
crash that's already in the seed corpus.
