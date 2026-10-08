"""The pixel-buffer half of the core ABI, mixed into :class:`pystencil.core.Core`.

Crop resolution, a crop window's snap, quarter-turn and flip, and every RGBA8 kernel (crop / rotate / mirror /
fill / filter / contour / rasterize), each marshalled through :mod:`pystencil._ffi.marshal` into one stencil_cli_* call.
"""

from __future__ import annotations

import ctypes

from .._ffi.types import NoneType
from .._ffi.bindings import _dblp
from .._ffi.marshal import _buf_view, _bytes_arg, _check_dims, _check_pixels, _encode

Rect = tuple[int, int, int, int]


def _rect(out) -> Rect:
  """The snapped window the core wrote into ``out[0..3]``; integral by construction."""
  return (int(out[0]), int(out[1]), int(out[2]), int(out[3]))


class RasterOps:
  """Crop + RGBA8 kernels. Requires ``self._lib``, the bound CDLL."""

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
  ) -> (tuple[int, int, int, int] | NoneType):
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
    if not ok: return None
    return (out_x.value, out_y.value, out_w.value, out_h.value)

  def snap_crop_rect(self, rect: Rect, image_w: int, image_h: int) -> Rect:
    """A crop window committed to integer pixels inside ``image_w`` x ``image_h``: each side
    kept in [1, the image's side], then the origin moved inside (core::snapCropRect)."""
    out = (ctypes.c_double * 4)()
    self._lib.stencil_cli_snapCropRect(*map(float, rect), float(image_w), float(image_h), out)
    return _rect(out)

  def rotate_edit_quarter(
    self, rect: Rect, quarters: int, orig_w: int, orig_h: int, clockwise: bool
  ) -> tuple[Rect, int]:
    """One quarter-turn of an edit shown at ``quarters`` over the unturned original: the crop
    follows into the turned space, snapped, and the count wraps to 0..3 (core::rotateEditQuarter)."""
    out = (ctypes.c_double * 5)()
    self._lib.stencil_cli_rotateEditQuarter(
      *map(float, rect), int(quarters), float(orig_w), float(orig_h), 1 if clockwise else 0, out,
    )
    return _rect(out), int(out[4])

  def mirror_edit(self, rect: Rect, quarters: int, orig_w: int, orig_h: int) -> tuple[Rect, int]:
    """A left-right flip of an edit at ``quarters``: the crop reflected, the count negated (core::mirrorEdit)."""
    out = (ctypes.c_double * 5)()
    self._lib.stencil_cli_mirrorEdit(*map(float, rect), int(quarters), float(orig_w), float(orig_h), out)
    return _rect(out), int(out[4])

  # ── RGBA8 transforms ──────────────────────────────────────────────────────
  def crop_image_rgba(
    self, src: (bytes | bytearray), src_w: int, src_h: int, rx: int, ry: int, rw: int,
    rh: int,
  ) -> bytearray:
    """Copy the (rx,ry,rw,rh) sub-rectangle of src into a fresh rw*rh*4 bytearray."""
    if rw < 0 or rh < 0:
      raise ValueError(f"crop_image_rgba: crop size must be non-negative, got {rw}x{rh}")
    _check_dims(src, src_w, src_h, "crop_image_rgba source")
    dst = bytearray(rw * rh * 4)
    self._lib.stencil_cli_cropImageRGBA(
      _bytes_arg(src),
      ctypes.c_int(src_w),
      ctypes.c_int(src_h),
      ctypes.c_int(rx),
      ctypes.c_int(ry),
      ctypes.c_int(rw),
      ctypes.c_int(rh),
      _buf_view(dst),
    )
    return dst

  def normalize_quarters(self, q: int) -> int:
    """Normalize a signed quarter-turn count to 0..3 (clockwise)."""
    return int(self._lib.stencil_cli_normalizeQuarters(ctypes.c_int(q)))

  def rotated_dims(self, w: int, h: int, q: int) -> tuple[int, int]:
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

  def rotate_image_rgba(
    self, src: (bytes | bytearray), w: int, h: int, q: int
  ) -> bytearray:
    """Rotate src (w x h) by `q` quarter-turns clockwise into a fresh bytearray."""
    _check_dims(src, w, h, "rotate_image_rgba source")
    ow, oh = self.rotated_dims(w, h, q)
    dst = bytearray(ow * oh * 4)
    self._lib.stencil_cli_rotateImageRGBA(
      _bytes_arg(src),
      ctypes.c_int(w),
      ctypes.c_int(h),
      ctypes.c_int(q),
      _buf_view(dst),
    )
    return dst

  def mirror_image_rgba(self, src: (bytes | bytearray), w: int, h: int) -> bytearray:
    """Mirror src (w x h) left-right into a fresh bytearray."""
    _check_dims(src, w, h, "mirror_image_rgba source")
    self._lib.stencil_cli_mirrorImageRows(_bytes_arg(src), w, h, _buf_view(dst := bytearray(w * h * 4)), 0, h)
    return dst

  def fill_rgba(
    self, dst: bytearray, pixel_count: int, r: int, g: int, b: int, a: int
  ) -> None:
    """Fill `pixel_count` RGBA8 pixels of dst with one colour, in place."""
    _check_pixels(dst, pixel_count, "fill_rgba")
    self._lib.stencil_cli_fillRGBA(
      _buf_view(dst),
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
    tint: tuple[int, int, int] = (0, 0, 0),
  ) -> None:
    """Apply "none"|"bw"|"sepia"|"invert"|<duotone> in place to a pixel_count RGBA8
    buffer. "contour" is a no-op here (it needs dimensions) — use apply_contour."""
    _check_pixels(data, pixel_count, "apply_filter")
    self._lib.stencil_cli_applyFilter(
      _encode(mode),
      _buf_view(data),
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
      _buf_view(data),
      ctypes.c_int(width),
      ctypes.c_int(height),
    )

  # ── rasterise a layout line ───────────────────────────────────────────────
  def rasterize_line(
    self,
    buf: bytearray,
    w: int,
    h: int,
    points: list[tuple[float, float]],
    color: str = "#FFFF00",
    thickness: float = 2.0,
    point_size: float = 4.0,
    style: str = "solid",
    locked: bool = False,
    fill_color: str = "transparent",
    point_color: str = "",
  ) -> None:
    """Burn one polyline into buf (w x h) in place. `points` are (x,y) pairs.

    `point_color` colours the points independently of the stroke; "" (the
    default) inherits `color`, which is how the line drew before the field existed.
    """
    _check_dims(buf, w, h, "rasterize_line")
    n = len(points)
    # Flatten to the 2*n contiguous doubles the ABI expects (x0,y0,x1,y1,...).
    flat = (ctypes.c_double * (2 * n))()
    for i, (px, py) in enumerate(points):
      flat[2 * i] = float(px)
      flat[2 * i + 1] = float(py)
    self._lib.stencil_cli_rasterizeLine(
      _buf_view(buf),
      ctypes.c_int(w),
      ctypes.c_int(h),
      ctypes.cast(flat, _dblp),
      ctypes.c_int(n),
      _encode(color),
      ctypes.c_double(thickness),
      ctypes.c_double(point_size),
      _encode(style),
      ctypes.c_int(1 if locked else 0),
      _encode(fill_color),
      _encode(point_color),
    )
