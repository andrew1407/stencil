"""ctypes binding over the stencil_cli_* extern "C" ABI (core/cliApi.h) -> class Core.

Memory model (matches the C side): the caller owns every buffer; core never allocates,
frees, or returns heap memory. C strings are const char* UTF-8 inputs. RGBA8 buffers are
interleaved R,G,B,A bytes. In-place ops mutate a Python bytearray directly by casting it
to a ctypes array view via (c_uint8 * len).from_buffer(buf) — so writes land in the
bytearray. Every bound function gets explicit .argtypes/.restype (the rasterize call in
particular FAILS silently without argtypes on the double* parameter on 64-bit).
"""

from __future__ import annotations

import ctypes
from typing import List, Optional, Tuple

from . import _native


# Shorthands for the ctypes pointer types used across the ABI.
_u8p = ctypes.POINTER(ctypes.c_uint8)
_intp = ctypes.POINTER(ctypes.c_int)
_dblp = ctypes.POINTER(ctypes.c_double)
_cstr = ctypes.c_char_p


def _encode(s: str) -> bytes:
    """Encode a Python str to the UTF-8 bytes the C ABI expects for const char*."""
    return s.encode("utf-8")


def _buf_view(buf: bytearray) -> ctypes.Array:
    """Cast a bytearray into a ctypes c_uint8 array that aliases the SAME memory.

    Using from_buffer (not from_buffer_copy) is what makes in-place core ops visible to
    the caller's bytearray.
    """
    return (ctypes.c_uint8 * len(buf)).from_buffer(buf)


def _bytes_arg(src: bytes) -> ctypes.Array:
    """A read-only c_uint8 array for an immutable bytes source (a private copy is fine)."""
    return (ctypes.c_uint8 * len(src)).from_buffer_copy(src)


def _check_pixels(buf, pixel_count: int, name: str) -> None:
    """Guard a flat RGBA8 op: the declared pixel count must be non-negative and fit
    within `buf`. The C kernels only reject non-positive dims, so an oversized
    pixel_count would read/write past the buffer — a memory-safety hole. Raise
    ValueError here before the ctypes call rather than corrupt memory."""
    if pixel_count < 0:
        raise ValueError(f"{name}: pixel_count must be non-negative, got {pixel_count}")
    if pixel_count * 4 > len(buf):
        raise ValueError(
            f"{name}: pixel_count {pixel_count} needs {pixel_count * 4} bytes but "
            f"buffer holds {len(buf)}"
        )


def _check_dims(buf, w: int, h: int, name: str) -> None:
    """Guard a width x height RGBA8 op against oversized/negative dims vs `buf`."""
    if w < 0 or h < 0:
        raise ValueError(f"{name}: dimensions must be non-negative, got {w}x{h}")
    if w * h * 4 > len(buf):
        raise ValueError(
            f"{name}: {w}x{h} needs {w * h * 4} bytes but buffer holds {len(buf)}"
        )


class Core:
    """Thin, typed wrapper around the shared core's CLI ABI.

    Construct via Core.load(); each method maps 1:1 to a stencil_cli_* entry point and
    handles the ctypes marshalling so callers work in plain Python types.
    """

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib
        self._bind()

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

    def _bind(self) -> None:
        """Set .argtypes/.restype on every ABI function exactly once (required for safety)."""
        lib = self._lib

        lib.stencil_cli_parseColor.restype = ctypes.c_int
        lib.stencil_cli_parseColor.argtypes = [_cstr, _intp, _intp, _intp, _intp]

        lib.stencil_cli_namedPageSize.restype = ctypes.c_int
        lib.stencil_cli_namedPageSize.argtypes = [_cstr, _dblp, _dblp]

        lib.stencil_cli_pageFormats.restype = _cstr
        lib.stencil_cli_pageFormats.argtypes = []

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

    # ── crop ──────────────────────────────────────────────────────────────────
    def resolve_crop(
        self,
        spec: str,
        image_w: float,
        image_h: float,
        px_per_cm_x: float,
        px_per_cm_y: float,
        page_wcm: float,
        page_hcm: float,
        album: bool,
    ) -> Optional[Tuple[int, int, int, int]]:
        """Resolve a crop spec to a clamped integer pixel rect (x,y,w,h), or None."""
        out_x = ctypes.c_int()
        out_y = ctypes.c_int()
        out_w = ctypes.c_int()
        out_h = ctypes.c_int()
        ok = self._lib.stencil_cli_resolveCrop(
            _encode(spec),
            ctypes.c_double(image_w),
            ctypes.c_double(image_h),
            ctypes.c_double(px_per_cm_x),
            ctypes.c_double(px_per_cm_y),
            ctypes.c_double(page_wcm),
            ctypes.c_double(page_hcm),
            ctypes.c_int(1 if album else 0),
            ctypes.byref(out_x),
            ctypes.byref(out_y),
            ctypes.byref(out_w),
            ctypes.byref(out_h),
        )
        if not ok:
            return None
        return (out_x.value, out_y.value, out_w.value, out_h.value)

    # ── RGBA8 transforms ──────────────────────────────────────────────────────
    def crop_image_rgba(
        self, src: bytes, src_w: int, src_h: int, rx: int, ry: int, rw: int, rh: int
    ) -> bytearray:
        """Copy the (rx,ry,rw,rh) sub-rectangle of src into a fresh rw*rh*4 bytearray."""
        if rw < 0 or rh < 0:
            raise ValueError(f"crop_image_rgba: crop size must be non-negative, got {rw}x{rh}")
        _check_dims(src, src_w, src_h, "crop_image_rgba source")
        dst = bytearray(rw * rh * 4)
        self._lib.stencil_cli_cropImageRGBA(
            ctypes.cast(_bytes_arg(src), _u8p),
            ctypes.c_int(src_w),
            ctypes.c_int(src_h),
            ctypes.c_int(rx),
            ctypes.c_int(ry),
            ctypes.c_int(rw),
            ctypes.c_int(rh),
            ctypes.cast(_buf_view(dst), _u8p),
        )
        return dst

    def normalize_quarters(self, q: int) -> int:
        """Normalize a signed quarter-turn count to 0..3 (clockwise)."""
        return int(self._lib.stencil_cli_normalizeQuarters(ctypes.c_int(q)))

    def rotated_dims(self, w: int, h: int, q: int) -> Tuple[int, int]:
        """Dimensions after rotating w x h by `q` quarter-turns."""
        out_w = ctypes.c_int()
        out_h = ctypes.c_int()
        self._lib.stencil_cli_rotatedDims(
            ctypes.c_int(w),
            ctypes.c_int(h),
            ctypes.c_int(q),
            ctypes.byref(out_w),
            ctypes.byref(out_h),
        )
        return (out_w.value, out_h.value)

    def rotate_image_rgba(self, src: bytes, w: int, h: int, q: int) -> bytearray:
        """Rotate src (w x h) by `q` quarter-turns clockwise into a fresh bytearray."""
        _check_dims(src, w, h, "rotate_image_rgba source")
        ow, oh = self.rotated_dims(w, h, q)
        dst = bytearray(ow * oh * 4)
        self._lib.stencil_cli_rotateImageRGBA(
            ctypes.cast(_bytes_arg(src), _u8p),
            ctypes.c_int(w),
            ctypes.c_int(h),
            ctypes.c_int(q),
            ctypes.cast(_buf_view(dst), _u8p),
        )
        return dst

    def fill_rgba(
        self, dst: bytearray, pixel_count: int, r: int, g: int, b: int, a: int
    ) -> None:
        """Fill `pixel_count` RGBA8 pixels of dst with one colour, in place."""
        _check_pixels(dst, pixel_count, "fill_rgba")
        self._lib.stencil_cli_fillRGBA(
            ctypes.cast(_buf_view(dst), _u8p),
            ctypes.c_int(pixel_count),
            ctypes.c_int(r),
            ctypes.c_int(g),
            ctypes.c_int(b),
            ctypes.c_int(a),
        )

    # ── filter ────────────────────────────────────────────────────────────────
    def apply_filter(
        self,
        mode: str,
        data: bytearray,
        pixel_count: int,
        tint: Tuple[int, int, int] = (0, 0, 0),
    ) -> None:
        """Apply "none"|"bw"|"sepia"|"invert"|<duotone> in place to a pixel_count RGBA8
        buffer. "contour" is a no-op here (it needs dimensions) — use apply_contour."""
        _check_pixels(data, pixel_count, "apply_filter")
        self._lib.stencil_cli_applyFilter(
            _encode(mode),
            ctypes.cast(_buf_view(data), _u8p),
            ctypes.c_int(pixel_count),
            ctypes.c_int(tint[0]),
            ctypes.c_int(tint[1]),
            ctypes.c_int(tint[2]),
        )

    def apply_contour(self, data: bytearray, width: int, height: int) -> None:
        """Sobel edge detection ("contour") in place on a width x height RGBA8 buffer:
        dark edges on a white page, alpha preserved. Degenerate dims are a no-op."""
        _check_dims(data, width, height, "apply_contour")
        self._lib.stencil_cli_applyContour(
            ctypes.cast(_buf_view(data), _u8p),
            ctypes.c_int(width),
            ctypes.c_int(height),
        )

    # ── rasterise a layout line ───────────────────────────────────────────────
    def rasterize_line(
        self,
        buf: bytearray,
        w: int,
        h: int,
        points: List[Tuple[float, float]],
        color: str = "#FFFF00",
        thickness: float = 2.0,
        marker_size: float = 4.0,
        style: str = "solid",
        locked: bool = False,
        fill_color: str = "transparent",
    ) -> None:
        """Burn one polyline into buf (w x h) in place. `points` are (x,y) pairs."""
        _check_dims(buf, w, h, "rasterize_line")
        n = len(points)
        # Flatten to the 2*n contiguous doubles the ABI expects (x0,y0,x1,y1,...).
        flat = (ctypes.c_double * (2 * n))()
        for i, (px, py) in enumerate(points):
            flat[2 * i] = float(px)
            flat[2 * i + 1] = float(py)
        self._lib.stencil_cli_rasterizeLine(
            ctypes.cast(_buf_view(buf), _u8p),
            ctypes.c_int(w),
            ctypes.c_int(h),
            ctypes.cast(flat, _dblp),
            ctypes.c_int(n),
            _encode(color),
            ctypes.c_double(thickness),
            ctypes.c_double(marker_size),
            _encode(style),
            ctypes.c_int(1 if locked else 0),
            _encode(fill_color),
        )

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
