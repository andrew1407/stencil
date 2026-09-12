"""ctypes binding over the stencil_cli_* extern "C" ABI (core/cliApi.h) -> class Core.

The scalar half lives here (colour, page sizing, formula, duration); the pixel-buffer
half is :class:`pystencil._rasterops.RasterOps`, and the marshalling rules — including
the caller-owns-every-buffer memory model — are in :mod:`pystencil._marshal`. Every
bound function gets explicit .argtypes/.restype (the rasterize call in particular FAILS
silently without argtypes on the double* parameter on 64-bit).
"""

from __future__ import annotations

import ctypes
from typing import List, Optional, Tuple

from . import _native
from ._bindings import bind
from ._marshal import _encode
from ._rasterops import RasterOps


class Core(RasterOps):
  """Thin, typed wrapper around the shared core's CLI ABI.

  Construct via Core.load(); each method maps 1:1 to a stencil_cli_* entry point and
  handles the ctypes marshalling so callers work in plain Python types.
  """

  def __init__(self, lib: ctypes.CDLL) -> None:
    self._lib = lib
    bind(lib)

  # ── construction ──────────────────────────────────────────────────────────
  @classmethod
  def load(cls, lib_path: Optional[str] = None, build: bool = True) -> "Core":
    """Find/build/dlopen the shared core and return a ready Core.

    `lib_path` overrides discovery with an explicit prebuilt library; `build=False`
    refuses to compile and requires an already-built (or overridden) artifact.
    """
    if lib_path is not None:
      lib = ctypes.CDLL(lib_path)
    elif build:
      lib = _native.load_library()
    else:
      path = _native.find_or_build(build_if_missing=False)
      lib = ctypes.CDLL(path)
    return cls(lib)

  # ── colour ────────────────────────────────────────────────────────────────
  def parse_color(self, spec: str) -> Optional[Tuple[int, int, int, int]]:
    """Parse a CSS colour to an (r,g,b,a) 0..255 tuple, or None if unrecognized."""
    r = ctypes.c_int()
    g = ctypes.c_int()
    b = ctypes.c_int()
    a = ctypes.c_int()
    ok = self._lib.stencil_cli_parseColor(
      _encode(spec),
      ctypes.byref(r),
      ctypes.byref(g),
      ctypes.byref(b),
      ctypes.byref(a),
    )
    if not ok:
      return None
    return (r.value, g.value, b.value, a.value)

  # ── page sizing ───────────────────────────────────────────────────────────
  def named_page_size(self, name: str) -> Optional[Tuple[float, float]]:
    """Return (width_cm, height_cm) for a known page name (e.g. "A4"), else None."""
    wcm = ctypes.c_double()
    hcm = ctypes.c_double()
    ok = self._lib.stencil_cli_namedPageSize(
      _encode(name),
      ctypes.byref(wcm),
      ctypes.byref(hcm),
    )
    if not ok:
      return None
    return (wcm.value, hcm.value)

  def page_formats(self) -> List[str]:
    """The canonical page-format names ("A0".."C10", no "custom") in canonical order."""
    raw = self._lib.stencil_cli_pageFormats()
    return raw.decode("utf-8").split() if raw else []

  def canonical_page_format(self, name: str) -> Optional[str]:
    """Canonical page-format name matched case-insensitively ("b5" → "B5"), or None
    (never "custom") for anything unknown — port of the CLI's canonicalPageFormat."""
    low = (name or "").strip().lower()
    for fmt in self.page_formats():
      if fmt.lower() == low:
        return fmt
    return None

  def default_blank_size_px(
    self, wcm: float, hcm: float, dpi: float = 96.0
  ) -> Tuple[int, int]:
    """Default blank-image pixel dimensions for a page (cm) rendered at `dpi`."""
    out_w = ctypes.c_int()
    out_h = ctypes.c_int()
    self._lib.stencil_cli_defaultBlankSizePx(
      ctypes.c_double(wcm),
      ctypes.c_double(hcm),
      ctypes.c_double(dpi),
      ctypes.byref(out_w),
      ctypes.byref(out_h),
    )
    return (out_w.value, out_h.value)

  # ── formula (coordinate transform) ──────────────────────────────────────────
  def validate_formula(self, expr: str, var: str = "x") -> bool:
    """True if `expr` is a valid single-variable formula in `var` ('x'/'y'); empty = identity."""
    return bool(self._lib.stencil_cli_validateFormula(_encode(expr), ord(var[:1] or "x")))

  def apply_formula(
    self, expr: str, var: str, value: float, allow: bool = True
  ) -> float:
    """Apply `expr` to `value` (the same FormulaParser the browser uses). Returns `value`
    unchanged when allow is False, expr is empty, or evaluation fails (identity-on-error)."""
    return float(
      self._lib.stencil_cli_applyFormula(
        _encode(expr),
        ord(var[:1] or "x"),
        ctypes.c_double(value),
        ctypes.c_int(1 if allow else 0),
      )
    )

  # ── duration (expiration) ───────────────────────────────────────────────────
  def parse_duration(self, spec: str) -> Optional[int]:
    """Parse a human duration ("days 23", "months 3", "fortnight", "month", "off") to
    milliseconds (0 = keep forever), or None if the spec is invalid — the same
    DurationParser the CLI `/expire` and the browser `stencil.expire` use. Add the
    result to an epoch-ms 'now' and pass to ServerConnection.set_project_expiration
    to expire a server project."""
    out = ctypes.c_longlong(0)
    if not self._lib.stencil_cli_parseDuration(_encode(spec), ctypes.byref(out)):
      return None
    return int(out.value)


# Process-wide singleton so repeated get_core() calls share one library handle.
_CORE: Optional[Core] = None


def get_core() -> Core:
  """Return a cached, lazily-loaded Core singleton."""
  global _CORE
  if _CORE is None:
    _CORE = Core.load()
  return _CORE
