"""Tests for the chainable Editor facade.

These exercise the derived-view pipeline (rotate → crop → filter → rasterize) and the
history/cursor model against the REAL native core, so they need the shared library built.
If the core can't be loaded (no compiler / artifact), the whole module self-skips — the
editor is a thin orchestration layer over the core ABI, so there's nothing meaningful to
test without it.
"""

from __future__ import annotations

import os
import tempfile
import unittest

from pystencil import codecs
from pystencil.editor import Editor
from pystencil.layout import Layout, Line, Point


def _grayscale_pixels(data, count):
    """True when every pixel's R==G==B (the b&w filter collapses the channels)."""
    for i in range(count):
        d = i * 4
        if not (data[d] == data[d + 1] == data[d + 2]):
            return False
    return True


class EditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Skip the whole suite if the native core isn't available in this environment.
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as exc:  # noqa: BLE001 - any failure means "no native lib"
            raise unittest.SkipTest("native core unavailable: %s" % exc)

    def _blank(self):
        """A fresh 32x48 blank editor used by most cases."""
        return Editor().blank(32, 48)

    def test_blank_size(self):
        ed = self._blank()
        self.assertEqual(ed.image_size, (32, 48))
        self.assertTrue(ed.has_image())

    def test_crop_shrinks_dims(self):
        ed = self._blank()
        before = ed.image_size
        ed.crop("x1=0.5 y1=0.5 x2=2 y2=2")
        after = ed.image_size
        # A bounded crop must not be larger than the original view in either axis.
        self.assertLessEqual(after[0], before[0])
        self.assertLessEqual(after[1], before[1])
        self.assertLess(after[0] * after[1], before[0] * before[1])

    def test_rotate_swaps_then_restores(self):
        ed = self._blank()
        self.assertEqual(ed.image_size, (32, 48))
        ed.rotate_right()
        # One quarter-turn swaps the axes.
        self.assertEqual(ed.image_size, (48, 32))
        ed.rotate_right()
        # A second (180° total) restores the original dimensions.
        self.assertEqual(ed.image_size, (32, 48))

    def test_filter_bw_is_grayscale(self):
        ed = self._blank().blank(8, 8, color="#3060c0")
        ed.set_filter("bw")
        img = ed.result()
        self.assertTrue(_grayscale_pixels(img.data, img.pixel_count))

    def test_draw_adds_line_and_pixels(self):
        ed = self._blank()
        layout = Layout(
            image_width=32,
            image_height=48,
            lines=[Line(points=[Point(2, 2), Point(30, 46)], color="#ff0000")],
        )
        ed.draw(layout)
        self.assertEqual(len(ed.layout().lines), 1)
        img = ed.result()
        # Some pixel must carry the drawn red stroke (blank base is white).
        found_red = False
        for i in range(img.pixel_count):
            d = i * 4
            if img.data[d] > 200 and img.data[d + 1] < 80 and img.data[d + 2] < 80:
                found_red = True
                break
        self.assertTrue(found_red, "expected drawn red pixels in the result")

    def test_undo_redo_restore_dims(self):
        ed = self._blank()
        self.assertEqual(ed.image_size, (32, 48))
        ed.rotate_right()
        self.assertEqual(ed.image_size, (48, 32))
        self.assertTrue(ed.undo())
        self.assertEqual(ed.image_size, (32, 48))
        self.assertTrue(ed.redo())
        self.assertEqual(ed.image_size, (48, 32))
        # No more redo states.
        self.assertFalse(ed.redo())

    def test_save_writes_png_at_result_dims(self):
        ed = self._blank().rotate_right()
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "out.png")
            ed.save(path)
            with open(path, "rb") as fh:
                raw = fh.read()
            w, h, _ = codecs.decode(raw)
            self.assertEqual((w, h), ed.image_size)

    def test_layout_width_matches_result(self):
        ed = self._blank().rotate_right()
        lay = ed.layout()
        self.assertEqual(lay.image_width, ed.image_size[0])
        self.assertEqual(lay.image_height, ed.image_size[1])
        self.assertEqual(lay.rotation_quarters, 1)

    def test_save_layout_path_semantics(self):
        ed = Editor().blank(16, 16)  # project name -> "blank"
        with tempfile.TemporaryDirectory() as tmp:
            # 1. explicit .json path -> exact path
            exact = os.path.join(tmp, "custom.json")
            out = ed.save_layout(exact)
            self.assertEqual(out, exact)
            self.assertTrue(os.path.exists(exact))

            # 2. directory/prefix -> "<dir>/<name>.json"
            sub = os.path.join(tmp, "sub")
            os.makedirs(sub)
            out2 = ed.save_layout(sub)
            self.assertEqual(out2, os.path.join(sub, "blank.json"))
            self.assertTrue(os.path.exists(out2))

            # 3. bare/None -> "<name>.json" in the cwd
            cwd = os.getcwd()
            try:
                os.chdir(tmp)
                out3 = ed.save_layout()
                self.assertEqual(out3, "blank.json")
                self.assertTrue(os.path.exists(os.path.join(tmp, "blank.json")))
            finally:
                os.chdir(cwd)


if __name__ == "__main__":
    unittest.main()
