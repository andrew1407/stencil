"""Locate, (lazily) build, and ctypes-load the shared Stencil core library.

Resolution order:
  1. $STENCIL_CORE_LIB — an explicit path to a prebuilt shared lib (CI / packaging).
  2. The locally built artifact under pystencil/_native/ (built on demand via build.py).

The loaded CDLL is cached in a module global so every Core / get_core() shares one handle.
"""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path
from typing import Optional


# build.py sits at the package root (pystencil/build.py), one dir above this file's package.
# Put that dir on sys.path so `import build` resolves regardless of the caller's cwd.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))


# Cached handles so repeated loads are cheap and consistent across the process.
_CDLL: Optional[ctypes.CDLL] = None


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

    # build.py lives at the package root (pystencil/build.py); it knows the platform name
    # and output location. Import it lazily so a prebuilt-lib deployment needn't ship it.
    import build as _build  # type: ignore

    expected = _build.lib_path()
    if expected.exists():
        return str(expected)

    if not build_if_missing:
        raise FileNotFoundError(
            "stencil core library not built yet (expected at %s)" % expected
        )

    return str(_build.build())


def load_library() -> ctypes.CDLL:
    """Load (once) and return the ctypes CDLL handle for the shared core."""
    global _CDLL
    if _CDLL is not None:
        return _CDLL

    path = find_or_build(build_if_missing=True)
    _CDLL = ctypes.CDLL(path)
    return _CDLL
