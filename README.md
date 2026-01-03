<h1 align="center">
  <img height="120px" src="https://upload.wikimedia.org/wikipedia/commons/d/d9/Node.js_logo.svg">&nbsp;&nbsp;&nbsp;&nbsp;
  <img height="120px" src="https://webrtc.github.io/webrtc-org/assets/images/webrtc-logo-vert-retro-dist.svg">
</h1>

[![NPM](https://img.shields.io/npm/v/@roamhq/wrtc.svg)](https://www.npmjs.com/package/@roamhq/wrtc)

node-webrtc is a Node.js Native Addon that provides bindings to [WebRTC
M98](https://webrtc.googlesource.com/src/+/branch-heads/4758). This project is
aiming for spec-compliance and will eventually be tested using the W3C's
[web-platform-tests](https://github.com/web-platform-tests/wpt) project. A
number of [nonstandard APIs](docs/nonstandard-apis.md) for testing are also
included.

## Install

```
npm install @roamhq/wrtc
```

Installing from NPM downloads a prebuilt binary for your operating system ×
architecture, based on optional dependency filters.

To install a debug build or cross-compile, you should [build from
source](docs/build-from-source.md).

## Supported Platforms

The following platforms are confirmed to work with node-webrtc and have
prebuilt binaries available. Since node-webrtc targets [N-API version
3](https://nodejs.org/api/n-api.html), there may be additional platforms
supported that are not listed here. If your platform is not supported, you may
still be able to [build from source](docs/build-from-source.md).

<table>
  <thead>
    <tr>
      <td colspan="2" rowspan="2"></td>
      <th colspan="2">Linux</th>
      <th colspan="2">macOS</th>
      <th>Windows</th>
    </tr>
    <tr>
      <th>x64</th>
      <th>arm64</th>
      <th>x64</th>
      <th>arm64</th>
      <th>x64</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th rowspan="2">Node</th>
      <th>20</th>
      <td align="center">✓</td>
      <td align="center">?</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
    <tr>
      <th>22</th>
      <td align="center">✓</td>
      <td align="center">?</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
      <td align="center">✓</td>
    </tr>
  </tbody>
</table>

## Build from Source

For most users, `npm install` will download a prebuilt binary. To build from source:

```bash
git clone https://github.com/node-webrtc/node-webrtc.git
cd node-webrtc
npm install

# Option 1: Use Nix (recommended for native builds)
nix develop
npm run build

# Option 2: Use system toolchain (required for cross-compilation on macOS)
npm run build
```

### Windows (x64) Build Notes

Windows builds are a bit trickier than macOS/Linux. See
[docs/build-from-source-windows.md](docs/build-from-source-windows.md) for the
full checklist and Windows-specific troubleshooting.

### Cross-Compilation (macOS)

To cross-compile for a different architecture on macOS, you must build **outside** the Nix shell using the system Xcode toolchain:

```bash
# Exit Nix shell if active, then:
TARGET_ARCH="arm64" npm run build   # Build for Apple Silicon
TARGET_ARCH="x64" npm run build     # Build for Intel
```

The build system uses `nix.gni` to configure the libwebrtc toolchain. For cross-compilation with system Xcode, ensure it contains:

```gni
is_clang=true
use_lld=false
clang_use_chrome_plugins=false
clang_base_path="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr"
mac_sdk_path="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
treat_warnings_as_errors=false
```

> **Note:** The Nix shell regenerates `nix.gni` on entry with Nix-specific paths. These paths cannot cross-compile, so always build outside the Nix shell when targeting a different architecture.

See [docs/build-from-source.md](docs/build-from-source.md) for full details.

## Examples

See [node-webrtc/node-webrtc-examples](https://github.com/node-webrtc/node-webrtc-examples).
