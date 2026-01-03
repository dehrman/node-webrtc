# Building from source on Windows

Unfortunately, building node-webrtc from source on Windows is a bit trickier than on Linux or MacOS, because there is not a reproducible build environment. Also because libwebrtc itself seems a bit broken on this platform.

## Things you should do before building

These are things I had to do to get `npm run build` working on Windows. If you miss a step, the build might fail.

### Python setup

- Use **Python 3.10–3.12** (Python 3.13+ breaks Chromium/WebRTC scripts like `vs_toolchain.py` because the `pipes` module was removed).
  - Verify with `python --version` before building.

### CMake setup

+ Add the Visual Studio Clang directory to your `%PATH%`. The directory is `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\`.
+ Add the Windows SDK installation directory to your `%PATH%`. An example would be `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64`.
+ Modify `C:\Program Files\CMake\share\cmake-3.26\Modules\Windows-Clang.cmake` to not have `-fuse-ld=lld-link`.
+ Modify `node_modules/cmake-js/lib/toolset.js`, line 184, to have the flag `-DELAYLOAD:NODE.EXE` instead of `/DELAYLOAD:NODE.EXE`.
+ Set the environment variables `CC=clang.exe` and `CXX=clang++.exe`.

### Windows SDK setup

- Install the **Windows 10 SDK (10.0.19041.0)** including **"Debugging Tools for Windows"**.
  - The WebRTC build scripts look for `dbghelp.dll` under `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbghelp.dll`.

### Linker note (lld-link vs link.exe)

- `wrtc.node` is linked with **MSVC `link.exe`** by CMake. If WebRTC is built with `use_lld=true`, WebRTC may generate **thin** `.lib` archives (`!<thin>`), which **MSVC `link.exe` cannot read** (LNK1107).
  - If you change `use_lld` or hit LNK1107, delete `build-win32-x64/external/libwebrtc/build/Release` and rerun `npm run build` so WebRTC is rebuilt with a consistent toolchain.

### Windows-only failures we’ve fixed in this repo

- **GN `sys_lib_flags` “Assignment had no effect”** (during `gn gen`):
  - Older WebRTC branches can trip newer GN when `use_lld=false`.
  - This repo patches WebRTC’s `build/toolchain/win/BUILD.gn` during configure via `scripts/patch-webrtc-windows.py`.
- **LNK1107 “invalid or corrupt file” on `peerconnection.lib`** (linking `wrtc.node`):
  - Usually means WebRTC produced a **thin** archive (`!<thin>`) that MSVC `link.exe` can’t consume.
  - Fix: remove `build-win32-x64/external/libwebrtc/build/Release` and rebuild.
- **`__delayLoadHelper2` unresolved (LNK2001)**:
  - Delay-load requires `delayimp.lib` (linked in `CMakeLists.txt`).
- **MSVC C++17 vs C++20 designated initializers (C7555)**:
  - The addon targets C++17 on Windows; avoid C++20 designated initializers.
- **WebRTC C++17 `std::result_of` warning-as-error**:
  - WebRTC’s `rtc_base/task_utils/repeating_task.h` used deprecated `std::result_of`.
  - `scripts/build-webrtc.bat` applies a small patch during builds.

### Source code modifications

+ In `build-win32-x64/external/libwebrtc/download/src`:
  + In `third_party/abseil-cpp/absl/meta/type_traits.h`, lines 482 and 493, comment out the assertions that trigger for the "`std::pair` is not trivially constructible" error.
  + In `rtc_base/third_party/sigslot/sigslot.h`, line 295, make `pmethod` always be an array size 24.
+ In `build-win32-x64/external/catch2/src`:
  + In `single_include/catch2/catch.hpp`, line 8722, modify this to always be 32768.
 
### npm setup

npm can get "confused" about the right path for rc. Open up "x64 Native Tools Command Prompt for VS" from the Start menu and run this to do an npm build within the node-webrtc directory:

```
set TARGET_ARCH=x64
set "RC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\rc.exe"

rmdir /s /q build-win32-x64
npm run build
```

## Cross-compiling to arm64

- **Windows x64 ➜ Windows arm64**: not currently supported (we don’t ship a `toolchains/win32-arm64.toolchain`).
  - If you need a Windows arm64 build, build on a Windows-on-ARM64 machine (native `arm64`) and set `TARGET_ARCH=arm64`.

## Things to do for a debug build
You'll need to set the environment variable `DEBUG=1` if you want symbols. On PowerShell, this can be done with `$env:DEBUG=1`. Note that CMake will build these into the _same directory_, which is a bit annoying. TODO come up with a better way to switch between debug/release builds.
