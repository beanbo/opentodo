@echo off
REM ============================================================================
REM  OpenTodo WebAssembly build script
REM  Prerequisites:
REM    - Emscripten 4.0.7 installed at C:\emsdk
REM      (install: cd C:\emsdk && emsdk install 4.0.7 && emsdk activate 4.0.7)
REM    - Qt 6.11.0 wasm_singlethread installed at C:\Qt\6.11.0\wasm_singlethread
REM      (install: python -m aqt install-qt all_os wasm 6.11.0 wasm_singlethread --outputdir C:\Qt)
REM    - Qt 6.11.0 desktop host tools at C:\Qt\6.11.0\mingw_64
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION

REM -- Check EMSDK -----------------------------------------------------------------
if not defined EMSDK (
    if exist "C:\emsdk\emsdk_env.bat" (
        echo [build-wasm] Sourcing emsdk environment from C:\emsdk\emsdk_env.bat...
        call "C:\emsdk\emsdk_env.bat"
    ) else (
        echo [build-wasm] ERROR: EMSDK environment variable is not set and C:\emsdk\emsdk_env.bat was not found.
        echo [build-wasm] Please install emsdk: git clone https://github.com/emscripten-core/emsdk C:\emsdk
        echo [build-wasm]   then: cd C:\emsdk ^&^& emsdk install 4.0.7 ^&^& emsdk activate 4.0.7
        exit /b 1
    )
)

REM -- Verify emcc is accessible ---------------------------------------------------
where emcc >nul 2>&1
if errorlevel 1 (
    echo [build-wasm] ERROR: emcc not found in PATH after sourcing emsdk_env.bat.
    echo [build-wasm] Run: C:\emsdk\emsdk activate 4.0.7 --permanent
    exit /b 1
)

REM -- Locate build root -----------------------------------------------------------
set SCRIPT_DIR=%~dp0
set SOURCE_DIR=%SCRIPT_DIR%..
set BUILD_DIR=%SOURCE_DIR%\build-wasm

set QT_WASM=C:\Qt\6.11.0\wasm_singlethread
set QT_HOST=C:\Qt\6.11.0\mingw_64

if not exist "%QT_WASM%\lib\cmake\Qt6\qt.toolchain.cmake" (
    echo [build-wasm] ERROR: Qt WASM toolchain not found at %QT_WASM%
    echo [build-wasm] Install it with:
    echo [build-wasm]   python -m aqt install-qt all_os wasm 6.11.0 wasm_singlethread --outputdir C:\Qt
    exit /b 1
)

REM -- Configure -------------------------------------------------------------------
REM  Qt's WASM toolchain hardcodes a default Emscripten location
REM  (C:\Utils\emsdk\...). When emsdk lives elsewhere we must override
REM  with QT_CHAINLOAD_TOOLCHAIN_FILE pointing at the real Emscripten.cmake.
set EMSCRIPTEN_TOOLCHAIN=%EMSDK%\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake
echo [build-wasm] Configuring OpenTodo for WebAssembly...
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%QT_WASM%\lib\cmake\Qt6\qt.toolchain.cmake" ^
    -DQT_CHAINLOAD_TOOLCHAIN_FILE="%EMSCRIPTEN_TOOLCHAIN%" ^
    -DQT_HOST_PATH="%QT_HOST%" ^
    -DBUILD_TESTING=OFF ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [build-wasm] ERROR: CMake configuration failed.
    exit /b 1
)

REM -- Build -----------------------------------------------------------------------
echo [build-wasm] Building OpenTodo WASM...
cmake --build "%BUILD_DIR%" --parallel

if errorlevel 1 (
    echo [build-wasm] ERROR: Build failed.
    exit /b 1
)

REM -- Done -----------------------------------------------------------------------
echo.
echo [build-wasm] Build complete.
echo [build-wasm] Output files are in: %BUILD_DIR%
echo.
echo To serve and test in a browser:
echo   python -m http.server -d "%BUILD_DIR%" 8000
echo Then open: http://localhost:8000/OpenTodo.html
echo.
echo NOTE: Chrome may block SharedArrayBuffer without COOP/COEP headers.
echo       Use the --enable-features=SharedArrayBuffer flag if needed,
echo       or use Firefox which is more permissive for local testing.

endlocal
