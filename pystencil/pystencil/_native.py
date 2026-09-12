"""Locate, (lazily) build, and ctypes-load the shared Stencil core library.

Resolution order:
 1. $STENCIL_CORE_LIB — an explicit path to a prebuilt shared lib (CI / packaging).
 2. The locally built artifact under pystencil/_native/ (built on demand via build.py).

The loaded CDLL is cached in a module global so every Core / get_core() shares one handle.
"""

from __future__ import annotations

import ctypes
import importlib.util
import os
from pathlib import Path

from ._types import NoneType


# build.py sits at the package root (pystencil/build.py), one dir above this file's package.
_BUILD_PY = Path(__file__).resolve().parent.parent / "build.py"


_CDLL: (ctypes.CDLL | NoneType) = None


def __load_build():
  """Import pystencil/build.py by path, leaving sys.path (and sys.modules) untouched."""
  spec = importlib.util.spec_from_file_location("pystencil._build", _BUILD_PY)
  if spec is None or spec.loader is None:
    raise FileNotFoundError("build script not found at %s" % _BUILD_PY)
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def find_or_build(build_if_missing: bool = True) -> str:
  """Return a filesystem path to the shared core library.

  Honours $STENCIL_CORE_LIB first. Otherwise looks for the locally built artifact and,
  when missing and allowed, imports build.py to compile it. Raises FileNotFoundError if
  nothing is found and building is disabled.
  """
  override = os.environ.get("STENCIL_CORE_LIB")
  if override:
    path = Path(override)
    if not path.exists():
      raise FileNotFoundError(
        "STENCIL_CORE_LIB points at a missing file: %s" % override
      )
    return str(path)

  # build.py knows the platform name and output location. Loaded lazily (a prebuilt-lib
  # deployment needn't ship it) and BY PATH: putting the package root on sys.path would
  # let this library shadow a caller's own top-level `build` module.
  _build = __load_build()

  expected = _build.lib_path()
  if expected.exists() and not _build.is_stale(expected): return str(expected)

  if not build_if_missing:
    # Stale but present: the caller refused a compile, so hand back what exists.
    if expected.exists(): return str(expected)
    raise FileNotFoundError(
      "stencil core library not built yet (expected at %s)" % expected
    )

  return str(_build.build())


def load_library() -> ctypes.CDLL:
  """Load (once) and return the ctypes CDLL handle for the shared core."""
  global _CDLL
  if _CDLL is not None: return _CDLL

  path = find_or_build(build_if_missing=True)
  _CDLL = ctypes.CDLL(path)
  return _CDLL
