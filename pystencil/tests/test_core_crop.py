"""The ctypes core binding's crop and quarter-turn geometry (pystencil.core.Core over
pystencil/_raster/ops.py): crop specs, crop windows and rotated dims.

These exercise the real compiled shared library, so they self-skip when no C++ compiler
is available to build it (e.g. a minimal CI image without a toolchain).
"""

from __future__ import annotations

import unittest

from pystencil.core import Core


class CoreCropTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls) -> None:
    try:
      cls.core = Core.load()
    except Exception as exc:  # no compiler / build failure -> skip, don't fail
      raise unittest.SkipTest(
        "stencil core library unavailable (need a C++ compiler): %s" % exc
      )

  def test_rotated_dims(self) -> None:
    self.assertEqual(self.core.rotated_dims(4, 2, 1), (2, 4))

  def test_snap_crop_rect_keeps_the_window_whole_inside(self) -> None:
    self.assertEqual(self.core.snap_crop_rect((95, -3, 20, 400), 100, 50), (80, 0, 20, 50))

  def test_rotate_edit_quarter_turns_the_window_and_wraps_the_count(self) -> None:
    # A 10x4 window at (1,2) of a 100x50 original, right → x = 50 − 2 − 4 in the 50x100 turn.
    self.assertEqual(self.core.rotate_edit_quarter((1, 2, 10, 4), 0, 100, 50, True), ((44, 1, 4, 10), 1))
    self.assertEqual(self.core.rotate_edit_quarter((44, 1, 4, 10), 1, 100, 50, False), ((1, 2, 10, 4), 0))

  def test_crop_image_rgba(self) -> None:
    # 2x2 image, extract the top-left 1x1 -> 4 bytes.
    src = bytes(2 * 2 * 4)
    dst = self.core.crop_image_rgba(src, 2, 2, 0, 0, 1, 1)
    self.assertEqual(len(dst), 4)

  def test_resolve_crop(self) -> None:
    rect = self.core.resolve_crop(
      "x1=0px x2=2px y1=0px y2=2px",
      image_w=100.0,
      image_h=100.0,
      px_per_cm_x=10.0,
      px_per_cm_y=10.0,
      page_wcm=21.0,
      page_hcm=29.7,
      album=False,
    )
    self.assertEqual(rect, (0, 0, 2, 2))
    # A bare number is a delta from the edge's default, so x2=2 lands past the far edge: refused.
    for spec in ("x1=0 x2=2 y1=0 y2=2", "y2=-200%", "x1=50px x2=50px"):
      self.assertIsNone(self.core.resolve_crop(spec, 100.0, 100.0, 10.0, 10.0, 21.0, 29.7, False))


if __name__ == "__main__":
  unittest.main()
