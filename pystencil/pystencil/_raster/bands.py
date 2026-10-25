"""The RGBA8 kernels banded over threads, the CLI's src/media/imageRows.zig on pystencil's pool.

Each band writes only its own rows (or, for a per-pixel filter, its own pixel range), so the
bytes are the whole-image call's; a picture under one band runs in a single call. ctypes
releases the GIL around every call.
"""

from __future__ import annotations

import ctypes

from .._ffi.marshal import _buf_view, _bytes_arg, _encode
from .parallel import MAX_BANDS, map_parallel, row_bands


def crop(lib, src, src_w: int, src_h: int, rx: int, ry: int, rw: int, rh: int, dst: bytearray) -> None:
  s, d = _bytes_arg(src), _buf_view(dst)
  map_parallel(row_bands(rw, rh), lambda b: lib.stencil_cli_cropImageRows(s, src_w, src_h, rx, ry, rw, rh, d, b[0], b[1]), MAX_BANDS)


def rotate(lib, src, w: int, h: int, q: int, out_w: int, out_h: int, dst: bytearray) -> None:
  """Banded over the rotated (destination) rows."""
  s, d = _bytes_arg(src), _buf_view(dst)
  map_parallel(row_bands(out_w, out_h), lambda b: lib.stencil_cli_rotateImageRows(s, w, h, q, d, b[0], b[1]), MAX_BANDS)


def mirror(lib, src, w: int, h: int, dst: bytearray) -> None:
  s, d = _bytes_arg(src), _buf_view(dst)
  map_parallel(row_bands(w, h), lambda b: lib.stencil_cli_mirrorImageRows(s, w, h, d, b[0], b[1]), MAX_BANDS)


def filter_pixels(lib, mode: str, data: bytearray, pixel_count: int, tint: tuple[int, int, int]) -> None:
  """A per-pixel filter needs no rows: each band is the whole-buffer call over its own pixel range."""
  m = _encode(mode)
  def band(b):
    n = b[1] - b[0]
    if n > 0: lib.stencil_cli_applyFilter(m, (ctypes.c_uint8 * (n * 4)).from_buffer(data, b[0] * 4), n, *tint)
  map_parallel(row_bands(1, pixel_count), band, MAX_BANDS)


def contour(lib, data: bytearray, width: int, height: int) -> None:
  """Two phases over one luma plane: every luma row is built before any Sobel row reads its neighbours."""
  bands = row_bands(width, height)
  if len(bands) < 2:
    lib.stencil_cli_applyContour(_buf_view(data), ctypes.c_int(width), ctypes.c_int(height))
    return
  src, plane = _buf_view(data), _buf_view(bytearray(width * height))
  map_parallel(bands, lambda b: lib.stencil_cli_buildLumaRows(src, width, height, b[0], b[1], plane), MAX_BANDS)
  map_parallel(bands, lambda b: lib.stencil_cli_sobelRows(plane, src, width, height, b[0], b[1]), MAX_BANDS)
