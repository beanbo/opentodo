@echo off
rem ─ OpenTodo packaging driver (Windows) ────────────────────────────────────
rem
rem Produces:
rem   build\release\OpenTodo-<version>-win64.zip          (always)
rem   build\release\OpenTodo-<version>-win64.exe          (if NSIS installed)
rem
rem Run from the project root. Uses the Qt MinGW kit (paths below) — adjust
rem if you have Qt installed somewhere else.
rem
rem To enable the NSIS .exe installer, install NSIS first:
rem   winget install --id NSIS.NSIS
rem then re-run this script. CMake will pick it up on next configure.
rem
rem Set OPENTODO_BUILD_DIR to override the build location (default: build\release).
rem ──────────────────────────────────────────────────────────────────────────

setlocal EnableDelayedExpansion

set "QT_BIN=C:\Qt\6.11.0\mingw_64\bin"
set "QT_TOOLS=C:\Qt\Tools\mingw1310_64\bin"
set "CMAKE_BIN=C:\Qt\Tools\CMake_64\bin"
set "NINJA_BIN=C:\Qt\Tools\Ninja"
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%QT_TOOLS%;%QT_BIN%;%PATH%"

if "%OPENTODO_BUILD_DIR%"=="" set "OPENTODO_BUILD_DIR=build\release"
set "ROOT=%~dp0.."

pushd "%ROOT%" || exit /b 1

echo === [1/3] Configure (Release) ===
cmake -S . -B "%OPENTODO_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail

echo.
echo === [2/3] Build OpenTodo target ===
cmake --build "%OPENTODO_BUILD_DIR%" --target OpenTodo --parallel || goto :fail

echo.
echo === [3/3] cpack — generate distributables ===
pushd "%OPENTODO_BUILD_DIR%" || goto :fail
cpack -B "%CD%\_CPack_Packages" || goto :failpopcpack
popd

echo.
echo === Outputs ===
for %%F in ("%OPENTODO_BUILD_DIR%\OpenTodo-*-win64.zip" "%OPENTODO_BUILD_DIR%\OpenTodo-*-win64.exe") do (
    if exist "%%F" echo   %%F
)

popd
endlocal
exit /b 0

:failpopcpack
popd
:fail
echo.
echo *** Packaging FAILED — see output above ***
popd
endlocal
exit /b 1
