"""Compile the shared Stencil C++ core into a loadable shared library for ctypes.

This mirrors how the Zig CLI (cli/build.zig) recompiles the same `core/` sources
directly rather than linking the CMake static library — pystencil does the same so it
needs no CMake at install time, just a system C++ compiler reachable as `c++` (or the
$CXX override). The CLI's own stb decoder and encoder units ride along, over the pinned
headers build_stb.py obtains, so PNG, JPEG and BMP decode through the very code the CLI
runs; without them the core still builds, JPEG is left out and PNG and BMP decode in Python. The build is intentionally tiny and
stdlib-only (subprocess), matching the project's "no third-party deps" constraint.

Output lands in pystencil/pystencil/_native/<platform lib name> so _native.py can find a
locally built artifact without any packaging/install step. The shared tables the package
reads (pystencil/_data/) are copied here from the repo's common/config, never checked in.
"""

from __future__ import annotations

import contextlib
import importlib.util
import os
import platform
from pathlib import Path


# Repo layout: this file is pystencil/build.py, so the C++ core is two levels up under core/.
_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
CORE_DIR = _REPO_ROOT / "core"
CLI_DIR = _REPO_ROOT / "cli"
# shim.c and pin.json are tracked; the headers land in the gitignored cache/, verified.
STB_DIR = _HERE / "stb"
STB_CACHE = STB_DIR / "cache"
STB_PIN = STB_DIR / "pin.json"

# Where the freshly built shared library is written (importable package data dir).
NATIVE_DIR = _HERE / "pystencil" / "_native"

# Seconds one compile of the whole core may take before it is abandoned.
BUILD_TIMEOUT = 600


# SYNC: keep this list identical to STENCIL_CORE_SOURCES in core/CMakeLists.txt and
# cli/build.zig. cliApi.cpp is appended below — the extern "C" ABI this binding wraps.
STENCIL_CORE_SOURCES = [
  "geometry/pointMath.cpp",
  "geometry/hitTest.cpp",
  "geometry/cropGeometry.cpp",
  "geometry/cropSnap.cpp",
  "geometry/lineChain.cpp",
  "color/color.cpp",
  "color/colorNames.cpp",
  "raster/imageOps.cpp",
  "raster/rasterize.cpp",
  "raster/strokeCoverage.cpp",
  "raster/markers.cpp",
  "raster/imageFilter.cpp",
  "raster/downscale.cpp",
  "parse/formulaContext.cpp",
  "parse/formulaParser.cpp",
  "parse/DurationParser.cpp",
  "parse/lengthTokens.cpp",
  "parse/cropSpec.cpp",
  "page/pageMetrics.cpp",
  "page/localeUnit.cpp",
  "format/tooltipRows.cpp",
  "state/HistoryStack.cpp",
  "state/lineMerge.cpp",
  "state/ProjectsStore.cpp",
  "state/zoomPan.cpp",
  "state/holdDraw.cpp",
  "script/diagnostics.cpp",
  "script/lexer.cpp",
  "script/parser.cpp",
  "script/values.cpp",
  "script/args.cpp",
  "script/crop.cpp",
  "script/lineStyle.cpp",
  "script/templates.cpp",
  "script/undo.cpp",
  "script/program/scriptHistory.cpp",
  "script/lower.cpp",
  "script/program/scriptProgram.cpp",
  "script/dump.cpp",
  "json/jsText.cpp",
  "json/jsNumber.cpp",
  "json/jsonValue.cpp",
  "json/jsonScan.cpp",
  "json/jsonReader.cpp",
  "json/jsonWriter.cpp",
  "opplan/planGrammars.cpp",
  "opplan/planPath.cpp",
  "opplan/planSchema.cpp",
  "opplan/planSchemaCheck.cpp",
  "opplan/planChecks.cpp",
  "opplan/planFields.cpp",
  "opplan/planRules.cpp",
  "opplan/planResult.cpp",
  "opplan/planWalker.cpp",
  "opplan/planWalkerAsk.cpp",
  "opplan/planWalk.cpp",
]

# The extern "C" surface we bind via ctypes (caller-owned RGBA8 buffers + C strings).
ABI_SOURCE = "cliApi.cpp"

# Compiled as C, each its own unit: the decoder's narrowing (STBI_NO_*, STBI_MAX_DIMENSIONS)
# lives in the CLI's stb_read_impl.c alone; shim.c holds its allocator hooks and this ABI.
CODEC_SOURCES = [
  CLI_DIR / "src" / "media" / "stb_read_impl.c",
  CLI_DIR / "src" / "media" / "stb_write_impl.c",
  STB_DIR / "shim.c",
]

# Include dirs mirror STENCIL_CORE_INCLUDE_DIRS: the core root (for models.hpp + the ABI
# headers) plus each concern group, so headers are included bare regardless of group.
INCLUDE_DIRS = [".", "abi", "geometry", "raster", "color", "parse", "page", "format",
                "state", "script", "script/program", "json", "opplan"]


def lib_filename() -> str:
  """Platform-correct shared-library file name for the built core."""
  system = platform.system()
  if system == "Darwin":
    return "libstencilcore.dylib"
  if system == "Windows":
    return "stencilcore.dll"
  return "libstencilcore.so"


def lib_path() -> Path:
  """Absolute path where build() writes (and _native.py expects) the shared library."""
  return NATIVE_DIR / lib_filename()


def __load_sibling(name: str):
  """Import a build_*.py beside this file by path, as _native.py imports this one."""
  spec = importlib.util.spec_from_file_location("pystencil_" + name, _HERE / (name + ".py"))
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


_compile = __load_sibling("build_compile")
_stb = __load_sibling("build_stb")
_data = __load_sibling("build_data")


def sync_data() -> Path:
  """The package's copies of the shared tables, refreshed from common/config (build_data.py)."""
  return _data.sync(_REPO_ROOT / "common" / "config", _HERE / "pystencil" / "_data")


def stb_headers(verbose: bool = False):
  """The verified stb header dir, or None: that build then leaves the stb codec out."""
  return _stb.ensure(STB_CACHE, STB_PIN, CLI_DIR / "build.zig.zon", verbose)


def build_inputs() -> list:
  """Every file whose edit invalidates the built artifact: the compiled sources, the
  headers and .inc bodies they include (INCLUDE_DIRS, non-recursive so third_party/
  stays out), the codec units, the stb pin and whatever headers are cached, this script
  (it carries the source list), build_compile.py (the flags) and build_stb.py."""
  paths = [CORE_DIR / rel for rel in STENCIL_CORE_SOURCES + [ABI_SOURCE]]
  for inc in INCLUDE_DIRS:
    for pattern in ("*.hpp", "*.h", "*.inc"):
      paths.extend(sorted((CORE_DIR / inc).glob(pattern)))
  paths.extend(CODEC_SOURCES + [STB_PIN])
  paths.extend(sorted(STB_CACHE.glob("*.h")))
  paths.append(Path(__file__).resolve())
  paths.append(_HERE / "build_compile.py")
  paths.append(_HERE / "build_stb.py")
  return paths


def is_stale(out: Path, inputs=None) -> bool:
  """True when the artifact is missing or older than any build input.

  Existence alone is not enough: a stale .dylib silently makes every core-parity test
  run against the previous edit. Missing inputs are ignored (the compile reports them).
  """
  try:
    built = out.stat().st_mtime
  except OSError:
    return True
  for src in build_inputs() if inputs is None else inputs:
    try:
      if Path(src).stat().st_mtime > built:
        return True
    except OSError:
      continue
  return False


def build(force: bool = False, verbose: bool = False) -> Path:
  """Compile the core and the stb codec units into one shared library; return its path.

  Skips the compile when the artifact is newer than every build input, unless `force`.
  Under a file lock the staleness is checked again, so a racing process reuses the
  library the first one built; the compile writes a temp file that replaces the artifact
  in one rename, so no loader ever maps a half-written library. Raises RuntimeError
  carrying the compiler's stderr if the build fails or outlives ``BUILD_TIMEOUT``.
  """
  sync_data()
  out = lib_path()
  if not force and not is_stale(out):
    return out

  NATIVE_DIR.mkdir(parents=True, exist_ok=True)
  with _compile.build_lock(out.with_name(out.name + ".lock")):
    if not force and not is_stale(out):
      return out
    tmp = out.with_name("%s.%d.tmp" % (out.name, os.getpid()))
    stb = stb_headers(verbose)
    try:
      _compile.compile_core(tmp, verbose, CORE_DIR, STENCIL_CORE_SOURCES + [ABI_SOURCE],
                            INCLUDE_DIRS, BUILD_TIMEOUT, CODEC_SOURCES if stb else [],
                            [stb] if stb else [])
      os.replace(tmp, out)
    finally:
      with contextlib.suppress(OSError):
        tmp.unlink()
  return out


if __name__ == "__main__":
  path = build(force=True, verbose=True)
  print(path)
