"""ctypes binding over the stencil_cli_* extern "C" ABI (core/cliApi.h) -> class Core.

The scalar half lives here (colour, page sizing, formula, duration, the op-plan walk); the
pixel-buffer half is :class:`pystencil._raster.ops.RasterOps`, and the marshalling rules —
including the caller-owns-every-buffer memory model — are in :mod:`pystencil._ffi.marshal`.
Every bound function gets explicit .argtypes/.restype (the rasterize call in particular FAILS
silently without argtypes on the double* parameter on 64-bit).
"""

from __future__ import annotations

import ctypes
import importlib.resources
import json

from ._ffi.types import NoneType
from . import _native
from ._ffi.bindings import bind
from ._ffi.formula import FormulaContext, ctx_args
from ._ffi.marshal import _encode
from ._raster.ops import RasterOps


class Core(RasterOps):
  """Thin, typed wrapper around the shared core's CLI ABI: construct via Core.load(),
  and every method maps 1:1 to a stencil_cli_* entry point, handling the ctypes
  marshalling so callers work in plain Python types."""

  def __init__(self, lib: ctypes.CDLL) -> None:
    self._lib = lib
    self.__opplan = 0
    bind(lib)

  # ── construction ──────────────────────────────────────────────────────────
  @classmethod
  def load(cls, lib_path: (str | NoneType) = None, build: bool = True) -> "Core":
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
  def parse_color(self, spec: str) -> (tuple[int, int, int, int] | NoneType):
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
    if not ok: return None
    return (r.value, g.value, b.value, a.value)

  # ── page sizing ───────────────────────────────────────────────────────────
  def named_page_size(self, name: str) -> (tuple[float, float] | NoneType):
    """Return (width_cm, height_cm) for a known page name (e.g. "A4"), else None."""
    wcm = ctypes.c_double()
    hcm = ctypes.c_double()
    ok = self._lib.stencil_cli_namedPageSize(
      _encode(name),
      ctypes.byref(wcm),
      ctypes.byref(hcm),
    )
    if not ok: return None
    return (wcm.value, hcm.value)

  def page_formats(self) -> list[str]:
    """The canonical page-format names ("A0".."C10", no "custom") in canonical order."""
    raw = self._lib.stencil_cli_pageFormats()
    return raw.decode("utf-8").split() if raw else []

  def canonical_page_format(self, name: str) -> (str | NoneType):
    """Canonical page-format name matched case-insensitively ("b5" → "B5"), or None
    (never "custom") for anything unknown — port of the CLI's canonicalPageFormat."""
    low = (name or "").strip().lower()
    for fmt in self.page_formats():
      if fmt.lower() == low: return fmt
    return None

  def default_blank_size_px(
    self, wcm: float, hcm: float, dpi: float = 96.0
  ) -> tuple[int, int]:
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
  def validate_formula(self, expr: str, var: str = "x", ctx: (FormulaContext | NoneType) = None) -> bool:
    """True if `expr` is a valid formula in `var` ('x'/'y'); empty = identity. With `ctx` the
    named values are in reach and `var` no longer binds — both axes probe at 1 instead."""
    if ctx is None:
      return bool(self._lib.stencil_cli_validateFormula(_encode(expr), ord(var[:1] or "x")))
    return bool(self._lib.stencil_cli_validateFormulaCtx(_encode(expr), *ctx_args(ctx)))

  def apply_formula(self, expr: str, var: str, value: float, allow: bool = True,
                    ctx: (FormulaContext | NoneType) = None) -> float:
    """Apply `expr` to `value` (the same FormulaParser the browser uses). Returns `value`
    unchanged when allow is False, expr is empty, or evaluation fails (identity-on-error);
    `ctx` puts the named values — the other axis, PAGE_*, IMAGE_* — in reach."""
    args = (_encode(expr), ord(var[:1] or "x"), ctypes.c_double(value), ctypes.c_int(1 if allow else 0))
    if ctx is None: return float(self._lib.stencil_cli_applyFormula(*args))
    return float(self._lib.stencil_cli_applyFormulaCtx(*args, *ctx_args(ctx)))

  # ── layout caps ─────────────────────────────────────────────────────────────
  def layout_caps(self) -> tuple[int, int, int]:
    """(lines, points per line, points in all) a layout keeps: constants.json LIMITS, from core."""
    lines, line_points, points = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    self._lib.stencil_cli_layoutCaps(ctypes.byref(lines), ctypes.byref(line_points), ctypes.byref(points))
    return (lines.value, line_points.value, points.value)

  # ── duration (expiration) ───────────────────────────────────────────────────
  def parse_duration(self, spec: str) -> (int | NoneType):
    """Parse a human duration ("days 23", "months 3", "fortnight", "month", "off") to
    milliseconds (0 = keep forever), or None if the spec is invalid — the same
    DurationParser the CLI `/expire` and the browser `stencil.expire` use. Add the
    result to an epoch-ms 'now' and pass to ServerConnection.set_project_expiration
    to expire a server project."""
    out = ctypes.c_longlong(0)
    if not self._lib.stencil_cli_parseDuration(_encode(spec), ctypes.byref(out)): return None
    return int(out.value)

  # ── op plan (core/opplan, llm-contract.md §1) ─────────────────────────────────
  def opplan_entries(self) -> dict:
    """The registry as core resolved it for pystencil: entries, forbidden names, limits."""
    return json.loads(self._lib.stencil_cli_opplanSchemaEntries(self.__opplan_schema()))

  def opplan_parse(self, text: str) -> tuple[int, dict]:
    """One model reply walked by core → ``(status, result)``: status 0 valid, 1 chat-only,
    2 invalid; the result is ``{status, reply, actions, variants, ask, warnings, error}``."""
    raw = _reply_bytes(text)
    lib = self._lib
    p = lib.stencil_cli_opplanParse(self.__opplan_schema(), raw, len(raw))
    if not p: raise RuntimeError("the core refused to walk an op plan")
    try:
      return lib.stencil_cli_opplanStatus(p), json.loads(lib.stencil_cli_opplanJson(p))
    finally:
      lib.stencil_cli_opplanDestroy(p)

  def __opplan_schema(self) -> int:
    """The schema handle, created once per Core and kept for its lifetime."""
    if not self.__opplan:
      with _native._LOCK:
        if not self.__opplan: self.__opplan = _opplan_schema(self._lib)
    return self.__opplan


def _opplan_schema(lib: ctypes.CDLL) -> int:
  """``_data/opRegistry.json`` resolved by core for the pystencil surface, every capability wired."""
  registry = importlib.resources.files("pystencil").joinpath("_data/opRegistry.json").read_bytes()
  handle = lib.stencil_cli_opplanSchemaCreate(registry, len(registry), b"pystencil", None)
  why = lib.stencil_cli_opplanSchemaError(handle) if handle else b"no handle"
  if why:
    lib.stencil_cli_opplanSchemaDestroy(handle)
    raise RuntimeError("opRegistry.json refused by core: %s" % why.decode("utf-8", "replace"))
  return handle


def _reply_bytes(text: str) -> bytes:
  """A reply's UTF-8. A lone surrogate (a JSON-escaped provider body can carry one) becomes
  U+FFFD, as the JS reference writes it: one code unit either way, so every length holds."""
  try:
    return text.encode("utf-8")
  except UnicodeEncodeError:
    return text.encode("utf-16-le", "surrogatepass").decode("utf-16-le", "replace").encode("utf-8")


# Process-wide singleton so repeated get_core() calls share one library handle.
_CORE: (Core | NoneType) = None


def get_core() -> Core:
  """Return a cached, lazily-loaded Core singleton; racing callers share the winner."""
  global _CORE
  if _CORE is None:
    with _native._LOCK:  # double-checked: the hot path never takes the lock
      if _CORE is None: _CORE = Core.load()
  return _CORE
