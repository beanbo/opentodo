# Packaging OpenTodo

Two distribution formats are produced by `cpack`:

| Format | Output | Requires |
|---|---|---|
| **Portable .zip** | `build/release/OpenTodo-<version>-win64.zip` | Qt 6 MinGW kit (already required to build) |
| **Windows installer .exe** (NSIS) | `build/release/OpenTodo-<version>-win64.exe` | NSIS installed on `PATH` (see below) |

The .zip is a self-contained folder a user can unpack anywhere and double-click `OpenTodo.exe`. The .exe installer adds Start Menu / Desktop shortcuts and registers an uninstaller in Windows Settings → Apps.

## Quick start (Windows)

```bat
tools\package.bat
```

That single command runs `cmake → ninja → cpack` and drops the artefacts under `build\release\`. Use `OPENTODO_BUILD_DIR=build\custom` to package elsewhere.

## What ends up inside

`windeployqt` (Qt's official tool) is invoked at install-time to bundle the runtime Qt DLLs, the `platforms\qwindows.dll` platform plugin, the QML module tree, and (on a MinGW build) the C++ runtime DLLs. The resulting tree:

```
OpenTodo/
  OpenTodo.exe
  Qt6Core.dll  Qt6Gui.dll  Qt6Quick.dll  Qt6Qml.dll  Qt6Network.dll  …
  platforms/   qwindows.dll
  styles/      qmodernwindowsstyle.dll
  iconengines/ qsvgicon.dll
  imageformats/ qjpeg.dll  qsvg.dll  …
  qml/         Qt5Compat/  QtQuick/  QtQuick/Controls/  …
  LICENSE
  NOTICE
  README.md
```

## Enabling the NSIS installer

NSIS is not bundled with Qt. Install it once:

```bat
winget install --id NSIS.NSIS
```

Or download the `nsis-X.XX-setup.exe` from <https://nsis.sourceforge.io/Download>. Then re-run `tools\package.bat` — CMake re-detects `makensis` at configure time and adds the `NSIS` generator to the CPack list.

If NSIS is not present, the script still produces the portable .zip; only the .exe installer is skipped.

## Smart screen / unsigned binaries

The .exe installer is **not yet code-signed**. First-run users will see Microsoft's “Windows protected your PC” SmartScreen banner and have to click **More info → Run anyway**. The portable .zip avoids SmartScreen entirely.

Code-signing is on the backlog (`Q3.13` — needs an EV or OV certificate from DigiCert / Sectigo or similar). Once a certificate is available, the recipe is:

```bat
signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 ^
    /f path\to\OpenTodo.pfx /p <password> ^
    build\release\OpenTodo-<version>-win64.exe
```

Re-run for the .zip's `OpenTodo.exe` inside as well (extract → sign → repack).

## Manual / step-by-step

If you need finer control than `tools\package.bat` offers:

```bat
:: 1. Configure once
cmake -S . -B build\release -G Ninja -DCMAKE_BUILD_TYPE=Release

:: 2. Build (only OpenTodo, skip tests)
cmake --build build\release --target OpenTodo --parallel

:: 3. Generate packages
pushd build\release
cpack                           :: produces ZIP + (if NSIS) NSIS
cpack -G ZIP                    :: ZIP only
cpack -G NSIS                   :: installer only
cpack --config CPackConfig.cmake -B _CPack_Packages   :: pristine staging dir
popd
```

## Source archive

```bat
cpack --config CPackSourceConfig.cmake
```

Outputs `OpenTodo-<version>-source.zip` excluding `.git/`, `build*/`, `.claude/`, `_CPack_Packages/`, `cppcheck*.log`, and `*.user` files.

## Linux / macOS

The CPack block is currently gated to `WIN32` only — it produces Windows artefacts. For Linux you'd typically `cpack -G DEB` or `-G RPM`; for macOS, `-G DragNDrop` (.dmg). Both want the same `install()` + `windeployqt-equivalent` plumbing — `linuxdeployqt` and `macdeployqt` respectively. Adding either is half a day of CMake but out of scope until someone needs it.

## Android / WASM

Different toolchains entirely:

- **Android**: `androiddeployqt` driven by Qt's Gradle integration produces `.apk` / `.aab`. See `android/build.gradle`.
- **WASM**: `cmake --build build-wasm` already emits a static `OpenTodo.html` + `.js` + `.wasm` + `.data` bundle that you can host as-is on any static-file server. See `docs/WASM.md`.

Neither runs through `cpack`.
