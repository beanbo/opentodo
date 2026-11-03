# OpenTodo — WebAssembly Build

This document explains how to build OpenTodo as a WebAssembly application that
runs directly in the browser.

## Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| Qt WASM kit | 6.11.0 `wasm_singlethread` | See install instructions below |
| Qt host tools | 6.11.0 `mingw_64` (Windows) or `linux_gcc_64` (Linux) | Already present if you build for desktop |
| Emscripten | 4.0.7 | Qt 6.11.0 requires this exact version |
| CMake | 3.21+ | Bundled with Qt, or system cmake |
| Python | 3.x | For `python -m http.server` |

### 1. Install Emscripten 4.0.7

```bat
git clone https://github.com/emscripten-core/emsdk.git C:\emsdk
cd C:\emsdk
emsdk install 4.0.7
emsdk activate 4.0.7
```

On Linux / macOS replace `emsdk` with `./emsdk`.

### 2. Install Qt 6.11.0 wasm_singlethread

Using [aqtinstall](https://github.com/miurahr/aqtinstall):

```
pip install aqtinstall
python -m aqt install-qt all_os wasm 6.11.0 wasm_singlethread --outputdir C:\Qt
```

The host Qt (desktop) must also be installed at the same `--outputdir`:

```
python -m aqt install-qt windows desktop 6.11.0 win64_mingw --outputdir C:\Qt
```

## Building locally

The easiest way is to use the provided script:

```bat
tools\build-wasm.bat
```

The script:
1. Checks that `EMSDK` is set (or tries `C:\emsdk\emsdk_env.bat` as a fallback)
2. Verifies `emcc` is on `PATH`
3. Runs `cmake` with the Qt WASM toolchain
4. Runs `cmake --build`
5. Prints instructions for serving

### Manual build

```bat
REM In a terminal where emsdk_env.bat has been sourced (sets EMSDK):
cmake -S . -B build-wasm -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=C:/Qt/6.11.0/wasm_singlethread/lib/cmake/Qt6/qt.toolchain.cmake ^
    -DQT_CHAINLOAD_TOOLCHAIN_FILE=%EMSDK%/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ^
    -DQT_HOST_PATH=C:/Qt/6.11.0/mingw_64 ^
    -DBUILD_TESTING=OFF ^
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

> **Why `QT_CHAINLOAD_TOOLCHAIN_FILE`?** Qt's WASM toolchain assumes Emscripten
> sits at `C:\Utils\emsdk` by default and emits a confusing
> "Cannot find Emscripten.cmake" if it's elsewhere. Override the chainload
> path explicitly to point at your real `emsdk` install.

> **Why `-G Ninja`?** On Windows, CMake otherwise picks the Visual Studio
> generator which doesn't understand the Emscripten toolchain.

## Serving and testing

Qt WASM apps must be served over HTTP (they cannot be loaded from `file://`).

```
python -m http.server -d build-wasm 8000
```

Then open **http://localhost:8000/OpenTodo.html** in Chrome or Firefox.

> **Chrome tip**: If you see a `SharedArrayBuffer` / COOP/COEP error on a local
> server that does not set the required headers, launch Chrome with:
> `chrome --enable-features=SharedArrayBuffer`
> or use Firefox which is more permissive for localhost.

## File persistence

On the desktop, OpenTodo stores `state.json` in `%APPDATA%\OpenTodo\`.
On WASM, `FileManager` mounts an **IndexedDB-backed virtual filesystem** (IDBFS,
linked via `-lidbfs.js`) at the same logical path before the first load. The
restore from IndexedDB is asynchronous, so the first load (and any save) waits
until it completes; every successful save is then flushed back to IndexedDB
(`FS.syncfs`). Data persists across page reloads within the same browser
profile. Clearing browser storage will erase local data.

## Known limitations

| Feature | Status | Notes |
|---|---|---|
| Global hotkeys (Ctrl+Alt+A) | Not available | Browser security model prohibits global hotkeys; Quick Add still works via the in-app button |
| System tray icon | Not available | No OS tray in the browser |
| Native notifications | Limited | Logs to browser console; QML in-app banners work normally |
| Google Drive sync (OAuth) | Not available in v1 | The loopback OAuth flow requires a local TCP server which is not available in WASM. A future version will use OAuth redirect to a dedicated callback URL |
| File picker (export/import) | Works | Uses the browser File System Access API via Qt WASM |
| Location-based reminders | Not available | `Qt6::Positioning` is not shipped in the WASM kit |
| Multi-threading | Not used | The `wasm_singlethread` build does not require `SharedArrayBuffer` or COOP/COEP headers |
