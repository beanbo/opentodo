@echo off
REM ============================================================================
REM OpenTodo standalone cppcheck runner (Windows).
REM
REM Run from the project root:
REM     tools\run-cppcheck.bat
REM
REM Output is streamed to cppcheck.log in the working directory. Exit code is
REM cppcheck's own — non-zero only on internal errors, not on findings.
REM
REM Suppressions are kept in sync with the `cppcheck` CMake custom target in
REM CMakeLists.txt — update both together.
REM ============================================================================

where cppcheck >nul 2>&1
if errorlevel 1 (
    echo cppcheck not found in PATH.
    echo Install from https://cppcheck.sourceforge.io/ and add it to PATH,
    echo or call the .exe directly e.g. "C:\Program Files\Cppcheck\cppcheck.exe".
    exit /b 1
)

cppcheck ^
    --enable=all ^
    --inconclusive ^
    --library=qt ^
    --suppress=missingIncludeSystem ^
    --suppress=unusedFunction ^
    --suppress=useStlAlgorithm ^
    --suppress=cstyleCast ^
    --suppress=unmatchedSuppression ^
    --inline-suppr ^
    --std=c++20 ^
    --language=c++ ^
    -I src ^
    --quiet ^
    src 2>cppcheck.log

echo Done. See cppcheck.log
