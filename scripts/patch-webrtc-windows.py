#!/usr/bin/env python3
"""
Patch upstream WebRTC sources to build cleanly on Windows in this repo.

This script is intended to be run from the WebRTC source directory
(`build-win32-x64/external/libwebrtc/download/src`), which contains
`build/toolchain/win/BUILD.gn`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def patch_gn_sys_lib_flags(src_dir: Path) -> None:
    """
    On older WebRTC branches (e.g. M98), //build/toolchain/win/BUILD.gn assigns
    sys_lib_flags unconditionally in the win_clang toolchain:

        sys_lib_flags =
            "-libpath:$_clang_lib_dir ${win_toolchain_data.libpath_flags}"

    But sys_lib_flags is only consumed when use_lld=true, so newer GN versions
    error out ("Assignment had no effect") when use_lld=false.

    Fix: gate the assignment behind if (use_lld) { ... }.
    """
    gn_path = src_dir / "build" / "toolchain" / "win" / "BUILD.gn"
    if not gn_path.exists():
        raise FileNotFoundError(f"Expected WebRTC GN file not found: {gn_path}")

    s = gn_path.read_text(encoding="utf-8", errors="replace")

    # Idempotency: If we've already wrapped sys_lib_flags in an if (use_lld) block,
    # do nothing.
    if re.search(r"(?m)^\s*if \(use_lld\) \{\r?\n\s*sys_lib_flags\s*=", s):
        return

    # Match exactly the 2-line assignment block.
    pat = re.compile(
        r"(?m)^(\s*)sys_lib_flags\s*=\s*\r?\n"
        r"\s*\"-libpath:\$_clang_lib_dir \$\{win_toolchain_data\.libpath_flags\}\"\s*$"
    )

    def repl(m: re.Match[str]) -> str:
        i = m.group(1)
        return (
            f"{i}if (use_lld) {{\n"
            f"{i}  sys_lib_flags =\n"
            # NOTE: The `${...}` is GN syntax. Double braces are to escape Python
            # f-string interpolation so the text is written literally.
            f"{i}      \"-libpath:$_clang_lib_dir ${{win_toolchain_data.libpath_flags}}\"\n"
            f"{i}}}"
        )

    s2, n = pat.subn(repl, s, count=1)
    if n != 1:
        raise RuntimeError(
            "Failed to patch BUILD.gn (sys_lib_flags assignment block not found)."
        )

    gn_path.write_text(s2, encoding="utf-8")


def patch_vs_toolchain_arm64_redist_selection(src_dir: Path) -> None:
    """
    WebRTC's build/vs_toolchain.py selects the newest VC Redist/MSVC/14.* folder
    when copying ARM64 CRT DLLs. Some VS installs include an ARM64 redist only
    for an *older* toolset version (e.g. 14.29.30036 has arm64/, but 14.29.30133
    does not), which makes copy_dlls fail with FileNotFoundError.

    Fix: when selecting the Redist component root, prefer a version directory
    that contains an `arm64` subdirectory.
    """
    vs_path = src_dir / "build" / "vs_toolchain.py"
    if not vs_path.exists():
        raise FileNotFoundError(f"Expected WebRTC file not found: {vs_path}")

    s = vs_path.read_text(encoding="utf-8", errors="replace")

    # Idempotency: if we've already inserted the arm64 redist selection logic,
    # do nothing.
    if "component == 'Redist'" in s and "os.path.join(directory, 'arm64')" in s:
        return

    needle = (
        "  for directory in vc_component_msvc_contents:\n"
        "    if os.path.isdir(directory):\n"
        "      return directory\n"
    )
    if needle not in s:
        raise RuntimeError(
            "Failed to patch vs_toolchain.py (unexpected FindVCComponentRoot body)."
        )

    replacement = (
        "  for directory in vc_component_msvc_contents:\n"
        "    if not os.path.isdir(directory):\n"
        "      continue\n"
        "    # When building ARM64, prefer a Redist version that actually has arm64 CRTs.\n"
        "    if component == 'Redist' and not os.path.isdir(os.path.join(directory, 'arm64')):\n"
        "      continue\n"
        "    return directory\n"
    )

    vs_path.write_text(s.replace(needle, replacement), encoding="utf-8")


def patch_vs_toolchain_arm64_debugger_fallback(src_dir: Path) -> None:
    """
    When cross-compiling for ARM64 on an x64 host, some Windows SDK installs only
    ship Debugging Tools under Debuggers\\x64 (no Debuggers\\arm64). Upstream
    vs_toolchain.py treats dbghelp.dll as mandatory and fails.

    Fix: if target_cpu == 'arm64' and Debuggers\\arm64\\dbghelp.dll is missing,
    fall back to Debuggers\\x64\\dbghelp.dll (sufficient for build-time tools).
    """
    vs_path = src_dir / "build" / "vs_toolchain.py"
    if not vs_path.exists():
        raise FileNotFoundError(f"Expected WebRTC file not found: {vs_path}")

    s = vs_path.read_text(encoding="utf-8", errors="replace")

    # Idempotency.
    if "Debuggers', 'x64', debug_file" in s and "target_cpu == 'arm64'" in s:
        return

    needle = "    full_path = os.path.join(win_sdk_dir, 'Debuggers', target_cpu, debug_file)\n"
    if needle not in s:
        raise RuntimeError(
            "Failed to patch vs_toolchain.py (_CopyDebugger full_path line not found)."
        )

    replacement = (
        "    full_path = os.path.join(win_sdk_dir, 'Debuggers', target_cpu, debug_file)\n"
        "    if target_cpu == 'arm64' and not os.path.exists(full_path):\n"
        "      full_path = os.path.join(win_sdk_dir, 'Debuggers', 'x64', debug_file)\n"
    )

    vs_path.write_text(s.replace(needle, replacement), encoding="utf-8")


def patch_setup_toolchain_cross_host_libs(src_dir: Path) -> None:
    """
    WebRTC's build/toolchain/win/setup_toolchain.py shells out to vcvarsall.bat
    to construct environment blocks for x86/x64/arm/arm64.

    In x64->arm64 cross-compilation prompts, the parent environment often has
    VCToolsVersion pinned to a toolset that contains ARM64 binaries, but may not
    contain a complete x64 lib directory. When setup_toolchain.py tries to
    generate the *host* x64 environment, that can result in a LIB that only
    contains Windows SDK paths (no msvcrt.lib), causing an assertion failure.

    Fix: When generating the x64 environment inside a cross-target prompt,
    clear the VCTools* selection variables so vcvarsall can pick a complete x64
    toolset.
    """
    st_path = src_dir / "build" / "toolchain" / "win" / "setup_toolchain.py"
    if not st_path.exists():
        raise FileNotFoundError(f"Expected WebRTC file not found: {st_path}")

    s = st_path.read_text(encoding="utf-8", errors="replace")

    # Idempotency.
    if "VSCMD_ARG_TGT_ARCH" in s and "VCToolsVersion" in s and "cross-target" in s:
        return

    marker = '    cpu_arg = "amd64"\n'
    if marker not in s:
        raise RuntimeError(
            "Failed to patch setup_toolchain.py (cpu_arg marker not found)."
        )

    injected = (
        "    # In cross-target prompts (e.g. x64->arm64), the parent env may have\n"
        "    # VCToolsVersion pinned to an ARM64-only toolset. Clear toolset\n"
        "    # selection variables when generating the host x64 environment so\n"
        "    # vcvarsall picks a complete x64 toolset (msvcrt.lib, etc.).\n"
        "    tgt = os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower()\n"
        "    if cpu == 'x64' and tgt not in ('', 'x64'):\n"
        "      for k in ('VCToolsVersion', 'VCToolsInstallDir', 'VCToolsRedistDir'):\n"
        "        os.environ.pop(k, None)\n"
        "\n"
        + marker
    )

    st_path.write_text(s.replace(marker, injected, 1), encoding="utf-8")


def main() -> int:
    src_dir = Path.cwd()
    patch_gn_sys_lib_flags(src_dir)
    patch_vs_toolchain_arm64_redist_selection(src_dir)
    patch_vs_toolchain_arm64_debugger_fallback(src_dir)
    patch_setup_toolchain_cross_host_libs(src_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


