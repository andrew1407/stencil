"""Compile the shared Stencil C++ core into a loadable shared library for ctypes.

This mirrors how the Zig CLI (cli/build.zig) recompiles the same `core/` sources
directly rather than linking the CMake static library — pystencil does the same so it
needs no CMake at install time, just a system C++ compiler reachable as `c++` (or the
$CXX override). The build is intentionally tiny and stdlib-only (subprocess), matching
the project's "no third-party deps" constraint.

Output lands in pystencil/pystencil/_native/<platform lib name> so _native.py can find a
locally built artifact without any packaging/install step.
"""

from __future__ import annotations

import os
import platform
import subprocess
import sys
from pathlib import Path


# Repo layout: this file is pystencil/build.py, so the C++ core is two levels up under core/.
_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
CORE_DIR = _REPO_ROOT / "core"

# Where the freshly built shared library is written (importable package data dir).
NATIVE_DIR = _HERE / "pystencil" / "_native"


# ──────────────────────────────────────────────────────────────────────────────
# SYNC: keep this list identical to STENCIL_CORE_SOURCES in core/CMakeLists.txt and
# cli/build.zig. Adding/removing/renaming a core .cpp means editing all three places.
# (cliApi.cpp is appended below — it is the extern "C" ABI this binding wraps, the same
# role wasmApi.cpp plays for the browser. CMake lists it separately for the test exe.)
# ──────────────────────────────────────────────────────────────────────────────
STENCIL_CORE_SOURCES = [
    "geometry/geometry.cpp",
    "geometry/cropGeometry.cpp",
    "geometry/imageOps.cpp",
    "geometry/rasterize.cpp",
    "color/color.cpp",
    "color/colorNames.cpp",
    "color/imageFilter.cpp",
    "parse/formulaParser.cpp",
    "parse/lengthTokens.cpp",
    "parse/cropSpec.cpp",
    "page/pageMetrics.cpp",
    "page/tooltipRows.cpp",
    "page/localeUnit.cpp",
    "page/hotkeyFormat.cpp",
    "state/historyStack.cpp",
    "state/projectsStore.cpp",
    "state/zoomPan.cpp",
    "state/holdDraw.cpp",
]

# The extern "C" surface we bind via ctypes (caller-owned RGBA8 buffers + C strings).
ABI_SOURCE = "cliApi.cpp"

# Include dirs mirror STENCIL_CORE_INCLUDE_DIRS: the core root (for models.hpp + the ABI
# headers) plus each concern group, so headers are included bare regardless of group.
INCLUDE_DIRS = [".", "geometry", "color", "parse", "page", "state"]


def lib_filename() -> str:
    """Platform-correct shared-library file name for the built core."""
    system = platform.system()
    if system == "Darwin":
        return "libstencilcore.dylib"
    if system == "Windows":
        return "stencilcore.dll"
    # Linux and other Unixes use the ELF .so convention.
    return "libstencilcore.so"


def lib_path() -> Path:
    """Absolute path where build() writes (and _native.py expects) the shared library."""
    return NATIVE_DIR / lib_filename()


def _compiler() -> str:
    """The C++ driver to invoke. Honour $CXX so callers can pin a toolchain; default c++."""
    return os.environ.get("CXX", "c++")


def build(force: bool = False, verbose: bool = False) -> Path:
    """Compile the core into one shared library and return its path.

    Skips the compile when an up-to-date artifact already exists unless `force`.
    Raises RuntimeError carrying the compiler's stderr if the build fails.
    """
    out = lib_path()
    if out.exists() and not force:
        return out

    NATIVE_DIR.mkdir(parents=True, exist_ok=True)

    sources = STENCIL_CORE_SOURCES + [ABI_SOURCE]

    # Single-shot compile+link of all translation units into one PIC shared object, run
    # from core/ so the relative source/include paths resolve. This is the exact command
    # verified to work on this machine (clang 21 -> ~132KB dylib).
    cmd = [_compiler(), "-std=c++17", "-O2", "-fPIC", "-shared"]
    for inc in INCLUDE_DIRS:
        cmd.append("-I" + inc)
    cmd.extend(sources)
    cmd.extend(["-o", str(out)])

    if verbose:
        print("building:", " ".join(cmd), file=sys.stderr)

    proc = subprocess.run(
        cmd,
        cwd=str(CORE_DIR),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "failed to compile stencil core shared library "
            "(compiler=%s):\n%s" % (_compiler(), proc.stderr)
        )
    if verbose and proc.stderr:
        print(proc.stderr, file=sys.stderr)

    return out


if __name__ == "__main__":
    # `python3 build.py` -> build (forcing a fresh compile) and print the artifact path.
    path = build(force=True, verbose=True)
    print(path)
