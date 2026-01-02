# Build from Source

## Prerequisites

### macOS/Linux

On macOS and Linux, a reproducible build environment is provided in the form of a Nix dev shell. You can install Nix using the [Lix installer](https://lix.systems/install/), then activate the build environment using `nix develop`.

### Windows

node-webrtc uses [cmake-js](https://github.com/cmake-js/cmake-js) to build
from source. When building from source, in addition to the prerequisites
required by cmake-js, you will need

- Git
- Ninja
- CMake 3.15 or newer
- Microsoft Visual Studio 2022 or newer, with the Clang toolchain installed
- Check the [additional prerequisites listed by WebRTC](https://webrtc.github.io/webrtc-org/native-code/development/prerequisite-sw/) - although their install is automated by the CMake scripts provided
- Also follow the steps in [build-from-source-windows.md](./build-from-source-windows.md)

## Building

Once you have the prerequisites, just clone the repository, run `npm install` to install the Javascript dependencies, and `npm run build` to build node-webrtc for your host platform.

```
git clone https://github.com/node-webrtc/node-webrtc.git
cd node-webrtc
npm install
nix develop # macOS/Linux only
npm run build
```

### Subsequent Builds

Subsequent builds can be triggered with `cmake`, e.g. on MacOS:

```
cmake --build build-darwin-arm64
```

You can pass either `--debug` or `--release` to build a debug or release build
of node-webrtc (and the underlying WebRTC library). Refer to the CMake
documentation for additional command line options.

### Cross-Compiling

The supported cross-compilation directions are:

- MacOS arm64 ➡️ MacOS x64
- MacOS x64 ➡️ ️MacOS arm64
- Linux x64 ➡️ Linux arm64

To cross-compile:

1. Set `TARGET_ARCH` to the target architecture (e.g., `"arm64"` or `"x64"`)
2. Re-run `npm run build`

#### macOS Cross-Compilation

**Important:** On macOS, you must build **outside** the Nix shell when cross-compiling. The Nix-provided clang cannot cross-compile between x64 and arm64.

```bash
# Exit the Nix shell first if you're in one
exit

# Then build with the target architecture
TARGET_ARCH="arm64" npm run build   # Build for Apple Silicon from Intel
TARGET_ARCH="x64" npm run build     # Build for Intel from Apple Silicon
```

#### The `nix.gni` File

The `nix.gni` file in the project root configures the toolchain used to build libwebrtc. The Nix shell hook automatically generates this file with Nix-specific paths when you enter the shell.

For cross-compilation outside Nix (or if you encounter toolchain issues), manually configure `nix.gni`:

```gni
is_clang=true
use_lld=false
clang_use_chrome_plugins=false
clang_base_path="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr"
mac_sdk_path="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
treat_warnings_as_errors=false
```

You can find your SDK path with:
```bash
xcrun --show-sdk-path
```

#### Troubleshooting Cross-Compilation

**"NEON intrinsics not available" or similar ARM errors:**
This happens when the Nix clang (which is x64-only) is being used for an arm64 build. Solution: exit the Nix shell and rebuild.

**"builtin __has_trivial_* is deprecated" errors:**
Newer versions of Apple Clang (16+) treat deprecated builtin warnings as errors. Add `treat_warnings_as_errors=false` to `nix.gni`.

**CMake generator mismatch error:**
If you see "Does not match the generator used previously", delete the build directory and rebuild:
```bash
rm -rf build-darwin-arm64
TARGET_ARCH="arm64" npm run build
```

**Stale libwebrtc configuration:**
If libwebrtc was configured with wrong toolchain paths, clean just the libwebrtc build:
```bash
rm -rf build-darwin-arm64/external/libwebrtc/build
rm -f build-darwin-arm64/external/libwebrtc/stamp/project_libwebrtc-configure
rm -f build-darwin-arm64/external/libwebrtc/stamp/project_libwebrtc-build
npm run build
```

### Debug Builds

1. Set `DEBUG=1`
2. Re-run `npm run build`

This is only fully-supported on macOS/Linux; Windows support is a bit dicey thanks to CMake.

To run a release build after doing a debug build, `unset DEBUG`.

## Other Notes

### Linux

On Linux, we compile libwebrtc's sources with libwebrtc's Clang toolchain and libwebrtc's sysroot. We statically link against its libc++ and libc++abi. This is a little dangerous (due to ABI concerns) but we match Clang major versions so it's probably OK?

### macOS

On macOS, we compile libwebrtc's sources with our own clang toolchain and our own sysroot. We statically link against its libc++ and libc++abi.

### Windows

We use the Clang toolchain and the Ninja generator on Windows in order to have
similar support for the `clangd` language server and `compile_commands.json`;
Visual Studio proper has not been tested.

To fix the error `Filename too long`, when downloading libwebrtc, use
(optionally with `--global` or `--system` switches to set for more than just
this project):

```
git config core.longpaths true
```

Creating symbolic links with MKLINK is used by the build script but is disabled
for non-Administrative users by default with a local security policy. On
Windows 10, fix this with Run (Windows-R) then `gpedit.msc`. Edit key "Local
Computer Policy -> Windows Settings -> Security Settings -> Local Policies ->
User Rights Assignment -> Create Symbolic Links" and add your user name. Log
out and in to change the policy. Note the [associated security
vunerability](https://docs.microsoft.com/en-us/windows/security/threat-protection/security-policy-settings/create-symbolic-links#vulnerability).

The Windows SDK debugging tools should be installed. One way to achieve this is
to [Download the Windows Driver
Kit](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk).

# Test

## Unit & Integration Tests

Once everything is built, run

```
npm test
```

> [!WARNING]
> If testing a release build, make sure to remove the debug build in `build-{os}-{arch}/Debug/wrtc.node` first! This takes precedence in module resolution.
