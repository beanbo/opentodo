@echo off
REM ============================================================================
REM  OpenTodo code coverage build script (Windows / Qt MinGW kit)
REM
REM  Configures a dedicated `build-coverage/` tree with OPENTODO_ENABLE_COVERAGE
REM  on, builds, runs ctest, and renders an HTML report at
REM    build-coverage/coverage/index.html
REM
REM  Prerequisites:
REM    - Qt 6.11.0 MinGW kit on PATH (qtenv.bat or equivalent)
REM    - GCC 11+ (Qt's MinGW 13.1 works — the project's validated kit)
REM    - gcovr installed:   pip install gcovr
REM
REM  MSVC is NOT supported here — Visual C++ has no built-in line coverage.
REM  Use Microsoft's external OpenCppCoverage tool against a normal Debug build.
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION

set SCRIPT_DIR=%~dp0
set SOURCE_DIR=%SCRIPT_DIR%..
set BUILD_DIR=%SOURCE_DIR%\build-coverage

REM -- Sanity: gcovr available? ----------------------------------------------------
where gcovr >nul 2>&1
if errorlevel 1 (
    echo [coverage] ERROR: gcovr not found on PATH.
    echo [coverage] Install with: pip install gcovr
    exit /b 1
)

REM -- Fresh build tree so .gcno/.gcda files don't get mixed with stale data ------
if exist "%BUILD_DIR%" (
    echo [coverage] Removing existing build-coverage/ tree...
    rmdir /s /q "%BUILD_DIR%"
)

REM -- Configure: Debug + coverage instrumentation --------------------------------
echo [coverage] Configuring with OPENTODO_ENABLE_COVERAGE=ON, CMAKE_BUILD_TYPE=Debug...
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G "MinGW Makefiles" ^
    -DOPENTODO_ENABLE_COVERAGE=ON ^
    -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo [coverage] ERROR: CMake configuration failed.
    exit /b 1
)

REM -- Build (libraries + executable + tests) -------------------------------------
echo [coverage] Building...
cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    echo [coverage] ERROR: Build failed.
    exit /b 1
)

REM -- Run tests + render report --------------------------------------------------
echo [coverage] Running tests and generating coverage report...
cmake --build "%BUILD_DIR%" --target coverage_report
if errorlevel 1 (
    echo [coverage] ERROR: Coverage report generation failed.
    exit /b 1
)

echo.
echo [coverage] Done.
echo [coverage] Open the report: %BUILD_DIR%\coverage\index.html
echo.

endlocal
