"""The banded kernels (pystencil/_raster/bands.py over parallel.row_bands): crop, rotate, mirror,
filter and the contour's two phases give, over threads, the bytes each whole-image call gives."""

from __future__ import annotations

import random
import unittest

from pystencil._ffi.marshal import _buf_view
from pystencil._raster import parallel
from tests.helpers.nativecase import require_core


class RasterBandsTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls) -> None:
    cls.core = require_core()

  def test_banded_contour_matches_the_whole_image_call(self) -> None:
    w, h = 1500, 900   # past BAND_MIN_PIXELS, so it bands
    self.assertGreater(len(parallel.row_bands(w, h)), 1)
    data = bytearray(random.Random(11).randbytes(w * h * 4))
    whole, banded = bytearray(data), bytearray(data)
    self.core._lib.stencil_cli_applyContour(_buf_view(whole), w, h)
    self.core.apply_contour(banded, w, h)
    self.assertEqual(banded, whole)

  def test_banded_crop_rotate_mirror_and_filter_match_the_whole_image_calls(self) -> None:
    w, h = 1500, 900
    src = bytes(random.Random(7).randbytes(w * h * 4))
    lib = self.core._lib
    crop = bytearray(1100 * 800 * 4)
    lib.stencil_cli_cropImageRGBA(src, w, h, 13, 17, 1100, 800, _buf_view(crop))
    self.assertEqual(self.core.crop_image_rgba(src, w, h, 13, 17, 1100, 800), crop)
    turned = bytearray(w * h * 4)
    lib.stencil_cli_rotateImageRGBA(src, w, h, 3, _buf_view(turned))
    self.assertEqual(self.core.rotate_image_rgba(src, w, h, 3), turned)
    flipped = bytearray(w * h * 4)
    lib.stencil_cli_mirrorImageRows(src, w, h, _buf_view(flipped), 0, h)
    self.assertEqual(self.core.mirror_image_rgba(src, w, h), flipped)
    whole, banded = bytearray(src), bytearray(src)
    lib.stencil_cli_applyFilter(b"sepia", _buf_view(whole), w * h, 0, 0, 0)
    self.core.apply_filter("sepia", banded, w * h)
    self.assertEqual(banded, whole)

  def test_bands_cover_every_row_once(self) -> None:
    for w, h in ((2000, 1000), (5000, 7), (10, 10)):
      bands = parallel.row_bands(w, h)
      rows = [y for a, b in bands for y in range(a, b)]
      self.assertEqual(rows, list(range(h)))

  def test_an_empty_picture_is_a_no_op_not_a_raise(self) -> None:
    self.assertEqual(parallel.row_bands(5, 0), [])
    self.assertEqual(self.core.crop_image_rgba(bytes(16), 2, 2, 0, 0, 2, 0), bytearray())
    self.assertEqual(self.core.crop_image_rgba(bytes(16), 2, 2, 0, 0, 0, 2), bytearray())
    self.assertEqual(self.core.rotate_image_rgba(b"", 2, 0, 2), bytearray())
    self.assertEqual(self.core.mirror_image_rgba(b"", 2, 0), bytearray())
    empty = bytearray()
    self.core.apply_filter("sepia", empty, 0)
    self.assertEqual(empty, bytearray())


if __name__ == "__main__":
  unittest.main()
