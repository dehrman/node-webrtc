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


def main() -> int:
    src_dir = Path.cwd()
    patch_gn_sys_lib_flags(src_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


