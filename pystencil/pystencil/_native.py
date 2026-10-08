"""Locate, lazily build, and ctypes-load the shared core: ``$STENCIL_CORE_LIB`` first, else
the artifact ``build.py`` writes under ``pystencil/_native/``. One CDLL per process.
"""

from __future__ import annotations

import ctypes
import importlib.util
import os
import threading
from pathlib import Path

from ._ffi.types import NoneType


_BUILD_PY = Path(__file__).resolve().parent.parent / "build.py"


_CDLL: (ctypes.CDLL | NoneType) = None

_LOCK = threading.RLock()  # guards _CDLL and core.py's _CORE; reentrant for get_core()


def __load_build():
  """Import pystencil/build.py by path, leaving sys.path (and sys.modules) untouched."""
  spec = importlib.util.spec_from_file_location("pystencil._build", _BUILD_PY)
  if spec is None or spec.loader is None:
    raise FileNotFoundError("build script not found at %s" % _BUILD_PY)
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def sync_data() -> None:
  """Refresh _data/ from a checkout's common/config before any module reads it; an install has no build_data.py."""
  script = _BUILD_PY.parent / "build_data.py"
  if not script.exists(): return
  spec = importlib.util.spec_from_file_location("pystencil._build_data", script)
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  module.sync(_BUILD_PY.parent.parent / "common" / "config", Path(__file__).resolve().parent / "_data")


def find_or_build(build_if_missing: bool = True) -> str:
  """The shared library's path, building a missing or stale one when allowed; a stale one
  comes back as is when not. ``FileNotFoundError`` when there is nothing to return."""
  override = os.environ.get("STENCIL_CORE_LIB")
  if override:
    path = Path(override)
    if not path.exists():
      raise FileNotFoundError(
        "STENCIL_CORE_LIB points at a missing file: %s" % override
      )
    return str(path)

  # Loaded lazily and BY PATH: putting the package root on sys.path would let this library
  # shadow a caller's own top-level `build` module.
  _build = __load_build()

  expected = _build.lib_path()
  if expected.exists() and not _build.is_stale(expected): return str(expected)

  if not build_if_missing:
    if expected.exists(): return str(expected)
    raise FileNotFoundError(
      "stencil core library not built yet (expected at %s)" % expected
    )

  return str(_build.build())


def load_library() -> ctypes.CDLL:
  """The process's one CDLL, double-checked under :data:`_LOCK` so racing first callers
  build once."""
  global _CDLL
  if _CDLL is not None: return _CDLL
  with _LOCK:
    if _CDLL is not None: return _CDLL
    path = find_or_build(build_if_missing=True)
    _CDLL = ctypes.CDLL(path)
    return _CDLL
