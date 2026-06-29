"""Unit tests for the ctypes core binding (pystencil.core.Core).

These exercise the real compiled shared library, so they self-skip when no C++ compiler
is available to build it (e.g. a minimal CI image without a toolchain).
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path


# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.core import Core, get_core


class CoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        try:
            cls.core = Core.load()
        except Exception as exc:  # no compiler / build failure -> skip, don't fail
            raise unittest.SkipTest(
                "stencil core library unavailable (need a C++ compiler): %s" % exc
            )

    def test_parse_color_named(self) -> None:
        self.assertEqual(self.core.parse_color("red"), (255, 0, 0, 255))

    def test_parse_color_invalid(self) -> None:
        self.assertIsNone(self.core.parse_color("nonsense"))

    def test_named_page_size_a4(self) -> None:
        size = self.core.named_page_size("A4")
        self.assertIsNotNone(size)
        wcm, hcm = size
        self.assertAlmostEqual(wcm, 21.0, places=4)
        self.assertAlmostEqual(hcm, 29.7, places=4)

    def test_fill_and_bw_filter(self) -> None:
        # One pixel filled (200,100,50,255); "bw" collapses it to luma 117 on all channels.
        buf = bytearray(4)
        self.core.fill_rgba(buf, 1, 200, 100, 50, 255)
        self.assertEqual(list(buf), [200, 100, 50, 255])
        self.core.apply_filter("bw", buf, 1)
        self.assertEqual(buf[0], 117)
        self.assertEqual(buf[1], 117)
        self.assertEqual(buf[2], 117)
        self.assertEqual(buf[3], 255)

    def test_rotated_dims(self) -> None:
        self.assertEqual(self.core.rotated_dims(4, 2, 1), (2, 4))

    def test_crop_image_rgba(self) -> None:
        # 2x2 image, extract the top-left 1x1 -> 4 bytes.
        src = bytes(2 * 2 * 4)
        dst = self.core.crop_image_rgba(src, 2, 2, 0, 0, 1, 1)
        self.assertEqual(len(dst), 4)

    def test_rasterize_line_marks_pixels(self) -> None:
        # A diagonal line across a 4x4 transparent buffer should touch some pixels.
        buf = bytearray(4 * 4 * 4)
        self.core.rasterize_line(
            buf,
            4,
            4,
            [(0.0, 0.0), (3.0, 3.0)],
            color="#FFFF00",
            thickness=2.0,
        )
        self.assertTrue(any(b != 0 for b in buf))

    def test_resolve_crop(self) -> None:
        rect = self.core.resolve_crop(
            "x1=0 x2=2 y1=0 y2=2",
            image_w=100.0,
            image_h=100.0,
            px_per_cm_x=10.0,
            px_per_cm_y=10.0,
            page_wcm=21.0,
            page_hcm=29.7,
            album=False,
        )
        self.assertIsNotNone(rect)
        self.assertEqual(len(rect), 4)

    def test_get_core_singleton(self) -> None:
        self.assertIs(get_core(), get_core())

    def test_formula_validate_and_apply(self) -> None:
        c = get_core()
        self.assertTrue(c.validate_formula("x*2 + 1", "x"))
        self.assertTrue(c.validate_formula("", "x"))  # empty = identity = valid
        self.assertFalse(c.validate_formula("foo(x)", "x"))  # unknown ident = invalid
        self.assertEqual(c.apply_formula("x*2", "x", 10.0, True), 20.0)
        self.assertEqual(c.apply_formula("x*2", "x", 10.0, False), 10.0)  # disabled = identity
        self.assertEqual(c.apply_formula("y/3", "y", 9.0, True), 3.0)
        self.assertEqual(c.apply_formula("bad(", "x", 10.0, True), 10.0)  # invalid = identity


if __name__ == "__main__":
    unittest.main()
