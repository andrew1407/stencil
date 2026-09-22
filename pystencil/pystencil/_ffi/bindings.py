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

  _d = ctypes.c_double
  lib.stencil_cli_validateFormulaCtx.restype = ctypes.c_int
  lib.stencil_cli_validateFormulaCtx.argtypes = [_cstr, _d, _d, _d, _d, _d, _d, _cstr]

  lib.stencil_cli_applyFormulaCtx.restype = ctypes.c_double
  lib.stencil_cli_applyFormulaCtx.argtypes = [
    _cstr,
    ctypes.c_int,
    _d,
    ctypes.c_int,
    _d, _d, _d, _d, _d, _d,   # x, y, pageWcm, pageHcm, imageW, imageH
    _cstr,
  ]

  lib.stencil_cli_parseDuration.restype = ctypes.c_int
  lib.stencil_cli_parseDuration.argtypes = [_cstr, ctypes.POINTER(ctypes.c_longlong)]

  _bind_script(lib)


def _bind_script(lib: ctypes.CDLL) -> None:
  """The .stc script family. Every ``_cstr`` restype here points INTO the handle, so
  ctypes' own bytes copy is what keeps it valid past ``scriptDestroy``."""
  _i = ctypes.c_int
  _d = ctypes.c_double
  _cstrp = ctypes.POINTER(_cstr)

  lib.stencil_cli_scriptParse.restype = _i
  lib.stencil_cli_scriptParse.argtypes = [_cstr, _i]

  lib.stencil_cli_scriptDestroy.restype = None
  lib.stencil_cli_scriptDestroy.argtypes = [_i]

  lib.stencil_cli_scriptErrorCount.restype = _i
  lib.stencil_cli_scriptErrorCount.argtypes = [_i]

  lib.stencil_cli_scriptDiagCount.restype = _i
  lib.stencil_cli_scriptDiagCount.argtypes = [_i]

  lib.stencil_cli_scriptDiagAt.restype = _cstr
  lib.stencil_cli_scriptDiagAt.argtypes = [_i, _i, _intp, _intp, _intp, _intp, _cstrp]

  lib.stencil_cli_scriptTokenCount.restype = _i
  lib.stencil_cli_scriptTokenCount.argtypes = [_i]

  lib.stencil_cli_scriptTokenAt.restype = _i
  lib.stencil_cli_scriptTokenAt.argtypes = [_i, _i, _intp, _intp, _intp, _intp]

  lib.stencil_cli_scriptBlockCount.restype = _i
  lib.stencil_cli_scriptBlockCount.argtypes = [_i]

  lib.stencil_cli_scriptBlockAt.restype = _cstr
  lib.stencil_cli_scriptBlockAt.argtypes = [_i, _i, _intp, _intp, _intp, _intp]

  lib.stencil_cli_scriptOpCount.restype = _i
  lib.stencil_cli_scriptOpCount.argtypes = [_i]

  lib.stencil_cli_scriptOpAt.restype = _i
  lib.stencil_cli_scriptOpAt.argtypes = [
    _i, _i, _intp, _intp, _intp, _intp, _intp, _intp, _intp,
  ]

  lib.stencil_cli_scriptOpStr.restype = _cstr
  lib.stencil_cli_scriptOpStr.argtypes = [_i, _i, _i]

  lib.stencil_cli_scriptOpTokCount.restype = _i
  lib.stencil_cli_scriptOpTokCount.argtypes = [_i, _i]

  lib.stencil_cli_scriptOpTok.restype = _cstr
  lib.stencil_cli_scriptOpTok.argtypes = [_i, _i, _i]

  lib.stencil_cli_scriptOpNum.restype = _i
  lib.stencil_cli_scriptOpNum.argtypes = [_i, _i, _i, _dblp]

  lib.stencil_cli_scriptOpResolve.restype = _i
  lib.stencil_cli_scriptOpResolve.argtypes = [_i, _i, _d, _d, _d, _d, _dblp, _i]

  lib.stencil_cli_scriptDump.restype = _cstr
  lib.stencil_cli_scriptDump.argtypes = [_i]
