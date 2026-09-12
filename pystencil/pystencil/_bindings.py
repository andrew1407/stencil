"""ctypes .argtypes/.restype declarations for every stencil_cli_* entry point.

Split out of core.py: this is the whole ABI signature table and nothing else. Every
bound function needs its types set exactly once, before first use — the rasterizeLine
call in particular FAILS SILENTLY without argtypes on its double* parameter on 64-bit,
which is silent memory corruption rather than an error.
"""

from __future__ import annotations

import ctypes


# Shorthands for the ctypes pointer types used across the ABI.
_u8p = ctypes.POINTER(ctypes.c_uint8)
_intp = ctypes.POINTER(ctypes.c_int)
_dblp = ctypes.POINTER(ctypes.c_double)
_cstr = ctypes.c_char_p


def bind(lib: ctypes.CDLL) -> None:
  """Set .argtypes/.restype on every ABI function exactly once (required for safety)."""
  lib.stencil_cli_parseColor.restype = ctypes.c_int
  lib.stencil_cli_parseColor.argtypes = [_cstr, _intp, _intp, _intp, _intp]

  lib.stencil_cli_namedPageSize.restype = ctypes.c_int
  lib.stencil_cli_namedPageSize.argtypes = [_cstr, _dblp, _dblp]

  lib.stencil_cli_pageFormats.restype = _cstr
  lib.stencil_cli_pageFormats.argtypes = list()

  lib.stencil_cli_defaultBlankSizePx.restype = None
  lib.stencil_cli_defaultBlankSizePx.argtypes = [
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_double,
    _intp,
    _intp,
  ]

  lib.stencil_cli_resolveCrop.restype = ctypes.c_int
  lib.stencil_cli_resolveCrop.argtypes = [
    _cstr,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.c_int,
    _intp,
    _intp,
    _intp,
    _intp,
  ]

  lib.stencil_cli_cropImageRGBA.restype = None
  lib.stencil_cli_cropImageRGBA.argtypes = [
    _u8p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    _u8p,
  ]

  lib.stencil_cli_normalizeQuarters.restype = ctypes.c_int
  lib.stencil_cli_normalizeQuarters.argtypes = [ctypes.c_int]

  lib.stencil_cli_rotatedDims.restype = None
  lib.stencil_cli_rotatedDims.argtypes = [
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    _intp,
    _intp,
  ]

  lib.stencil_cli_rotateImageRGBA.restype = None
  lib.stencil_cli_rotateImageRGBA.argtypes = [
    _u8p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    _u8p,
  ]

  lib.stencil_cli_fillRGBA.restype = None
  lib.stencil_cli_fillRGBA.argtypes = [
    _u8p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
  ]

  lib.stencil_cli_applyFilter.restype = None
  lib.stencil_cli_applyFilter.argtypes = [
    _cstr,
    _u8p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
  ]

  lib.stencil_cli_applyContour.restype = None
  lib.stencil_cli_applyContour.argtypes = [_u8p, ctypes.c_int, ctypes.c_int]

  # The double* parameter here is the one that MUST be typed or the call corrupts.
  lib.stencil_cli_rasterizeLine.restype = None
  lib.stencil_cli_rasterizeLine.argtypes = [
    _u8p,
    ctypes.c_int,
    ctypes.c_int,
    _dblp,
    ctypes.c_int,
    _cstr,
    ctypes.c_double,
    ctypes.c_double,
    _cstr,
    ctypes.c_int,
    _cstr,
    _cstr,   # pointColor — "" inherits `color` (Line::pointColor)
  ]

  lib.stencil_cli_validateFormula.restype = ctypes.c_int
  lib.stencil_cli_validateFormula.argtypes = [_cstr, ctypes.c_int]

  lib.stencil_cli_applyFormula.restype = ctypes.c_double
  lib.stencil_cli_applyFormula.argtypes = [
    _cstr,
    ctypes.c_int,
    ctypes.c_double,
    ctypes.c_int,
  ]

  lib.stencil_cli_parseDuration.restype = ctypes.c_int
  lib.stencil_cli_parseDuration.argtypes = [_cstr, ctypes.POINTER(ctypes.c_longlong)]
