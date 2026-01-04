# Building from source on Windows

Building node-webrtc from source on Windows is trickier than on macOS/Linux
(tooling isn’t fully reproducible, and WebRTC is sensitive to SDK/toolchain
versions). This doc captures a working setup for:

- **Native**: win32-x64
- **Experimental**: win32-arm64 (cross-compile from win32-x64)

## Prerequisites

### Required software

- Git
- Ninja
- CMake (3.15+)
- Visual Studio Build Tools 2019/2022 (or full Visual Studio) with C++ tools

### Python

- Use **Python 3.10–3.12**
  - Python 3.13+ breaks some Chromium/WebRTC scripts (e.g. `vs_toolchain.py`
    imports `pipes`).

### Windows SDK

- Install **Windows 10 SDK 10.0.19041.0** including **“Debugging Tools for Windows”**
  - WebRTC expects `dbghelp.dll` under:
    - `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbghelp.dll`

### Notes

- **Long paths** (WebRTC checkout can exceed MAX_PATH):

```
git config --global core.longpaths true
```

- **Symlinks**: the WebRTC download step uses `mklink`. Enable Developer Mode or
  grant “Create symbolic links” to your user.

## Build (win32-x64)

Use a VS dev prompt (x64). Example:

```
set TARGET_ARCH=x64
set "RC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\rc.exe"
npm run build
```

## Build (win32-arm64) — experimental cross-compile

### 1) Install ARM64 MSVC build tools

In **Visual Studio Installer** → your Build Tools/VS install → **Modify**:

- Workloads:
  - **Desktop development with C++**
- Individual components (search “ARM64”):
  - **MSVC v142/v143 … ARM64 build tools**
  - A Windows SDK (keep 10.0.19041.0 installed)

### 2) Open an ARM64-targeting dev environment

From a Developer Command Prompt, run:

```
"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64_arm64
```

If `cl` still resolves to x64 (toolset mismatch), pin a toolset that has ARM64:

```
"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64_arm64 -vcvars_ver=14.29.30037
```

Verify you’re actually using ARM64 tools:

```
where cl
cl
where link
```

You want the first `where cl` entry to be something like:
`...\bin\HostX64\arm64\cl.exe`, and `cl` should say `... for ARM64`.

### 3) Build

Important: set `RC` to the Windows SDK `rc.exe`. When running under `npm run`,
`node_modules\.bin` can shadow `rc` with an npm binary.

```
cd c:\workspace\node-webrtc
set TARGET_ARCH=arm64
set "RC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\rc.exe"
npm run build
```

This should produce `build-win32-arm64/wrtc.node`.

## Troubleshooting (Windows-specific)

### LNK1107 “invalid or corrupt file” on `peerconnection.lib`

If WebRTC is built with `use_lld=true`, it may produce **thin** `.lib` archives
(`!<thin>`) that MSVC `link.exe` can’t read.

Fix:

```
rmdir /s /q build-win32-x64\external\libwebrtc\build\Release
del build-win32-x64\external\libwebrtc\stamp\project_libwebrtc-configure 2>nul
del build-win32-x64\external\libwebrtc\stamp\project_libwebrtc-build 2>nul
npm run build
```

### `__delayLoadHelper2` unresolved (LNK2001)

Delay-load requires `delayimp.lib` (linked in `CMakeLists.txt`).

### C++ errors on Windows that are patched in this repo

- WebRTC `std::result_of` warning-as-error is patched during `scripts/build-webrtc.bat`
- Some Windows GN/toolchain quirks are patched during configure via `scripts/patch-webrtc-windows.py`
