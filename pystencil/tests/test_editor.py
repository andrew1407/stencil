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


class EditorFetchGuardTests(unittest.TestCase):
    """The URL fetch is http(s)-only. These need no native core (they reject the
    scheme before any urlopen), so they run everywhere."""

    def test_fetch_url_rejects_non_http_schemes(self):
        for bad in (
            "file:///etc/passwd",
            "ftp://host/clip.png",
            "data:text/plain;base64,AAAA",
            "gopher://host/1",
        ):
            with self.assertRaises(ValueError):
                Editor._fetch_url(bad)

    def test_is_url_only_http(self):
        self.assertTrue(Editor._is_url("http://example.com/a.png"))
        self.assertTrue(Editor._is_url("HTTPS://example.com/a.png"))
        self.assertFalse(Editor._is_url("file:///etc/passwd"))
        self.assertFalse(Editor._is_url("/local/path.png"))


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

    def test_filter_invert_negates_channels(self):
        # #3060c0 = (48, 96, 192) -> inverted (207, 159, 63), alpha untouched.
        ed = Editor().blank(4, 4, color="#3060c0")
        ed.set_filter("invert")
        img = ed.result()
        for i in range(img.pixel_count):
            d = i * 4
            self.assertEqual(
                (img.data[d], img.data[d + 1], img.data[d + 2], img.data[d + 3]),
                (207, 159, 63, 255),
            )

    def test_filter_contour_uniform_is_white(self):
        # A uniform page has no edges, so the contour filter renders it all white.
        ed = Editor().blank(6, 5, color="#3060c0")
        ed.set_filter("contour")
        img = ed.result()
        for i in range(img.pixel_count):
            d = i * 4
            self.assertEqual(
                (img.data[d], img.data[d + 1], img.data[d + 2]), (255, 255, 255)
            )

    def test_apply_filter_accepts_new_named_modes(self):
        # invert/contour are named modes (checked before the colour fallback), not tints.
        ed = self._blank()
        ed.apply_filter("Invert")
        self.assertEqual(ed.layout().to_dict()["imageFilter"], "invert")
        ed.apply_filter("contour")
        self.assertEqual(ed.layout().to_dict()["imageFilter"], "contour")

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

    def test_set_formula_validates_applies_and_serializes(self) -> None:
        ed = Editor().blank(200, 100)
        ed.set_formula("x", "x*2 + 1").set_formula("y", "y/3")
        self.assertTrue(ed.allow_formulas)
        # evaluation goes through the shared parser
        self.assertEqual(ed.apply_formula("x", 10.0), 21.0)
        self.assertEqual(ed.apply_formula("y", 9.0), 3.0)
        # the formulas ride the saved layout
        d = ed.layout().to_dict()
        self.assertEqual(d["allowFormulas"], True)
        self.assertEqual(d["formulaX"], "x*2 + 1")
        self.assertEqual(d["formulaY"], "y/3")
        # toggling off keeps the expressions but stops applying them
        ed.set_allow_formulas(False)
        self.assertEqual(ed.apply_formula("x", 10.0), 10.0)
        off = ed.layout().to_dict()
        self.assertNotIn("allowFormulas", off)  # omitted when off
        self.assertEqual(off["formulaX"], "x*2 + 1")  # expression kept
        # an invalid expression is rejected
        with self.assertRaises(ValueError):
            ed.set_formula("x", "foo(x)")

    def test_blank_named_page_b5(self) -> None:
        from pystencil.core import get_core

        # A named page picks its default pixel size from the core's table (@ 96 dpi);
        # the name is matched case-insensitively ("b5" -> "B5").
        expected = get_core().default_blank_size_px(17.6, 25.0)
        self.assertEqual(Editor().blank(page="B5").image_size, expected)
        self.assertEqual(Editor().blank(page="b5").image_size, expected)

    def test_blank_unknown_page_falls_back_to_a4(self) -> None:
        # An unknown page name quietly blanks on A4 (mirror of the Zig console's
        # canonicalPageFormat -> null -> default A4 blank; pinned per-consumer fallback).
        self.assertEqual(
            Editor().blank(page="Z9").image_size, Editor().blank(page="A4").image_size
        )

    def test_page_format_unset_by_default(self) -> None:
        ed = self._blank()
        self.assertEqual(ed.page_format, "")
        self.assertNotIn("pageSize", ed.layout().to_dict())

    def test_set_page_format_rides_the_layout(self) -> None:
        ed = self._blank()
        ed.set_page_format("b5")
        self.assertEqual(ed.page_format, "B5")
        d = ed.layout().to_dict()
        self.assertEqual(d["pageSize"], "B5")
        self.assertNotIn("customPageWidth", d)
        ed.set_page_format("custom", 10.0, 15.0)
        d = ed.layout().to_dict()
        self.assertEqual(d["pageSize"], "custom")
        self.assertEqual(d["customPageWidth"], 10.0)
        self.assertEqual(d["customPageHeight"], 15.0)
        with self.assertRaises(ValueError):
            ed.set_page_format("Z9")
        with self.assertRaises(ValueError):
            ed.set_page_format("custom")  # custom needs positive dims

    def test_set_page_format_custom_pins_cm_range(self) -> None:
        # Custom dims mirror the console's parseCmDim: 0.1–500 cm, NaN/inf rejected
        # (a stored NaN would make the exported layout invalid RFC-8259 JSON).
        ed = self._blank()
        for w, h in (
            (float("nan"), float("nan")),
            (float("inf"), 10.0),
            (1000.0, 1000.0),
            (0.05, 10.0),
            (10.0, 501.0),
        ):
            with self.assertRaises(ValueError):
                ed.set_page_format("custom", w, h)
        # The boundaries themselves are accepted (inclusive range).
        ed.set_page_format("custom", 0.1, 500.0)
        self.assertEqual(ed.custom_page_width, 0.1)
        self.assertEqual(ed.custom_page_height, 500.0)

    def test_apply_layout_adopts_page_format(self) -> None:
        # The page format round-trips: layout() -> apply_layout() on a fresh editor.
        src = self._blank().set_page_format("custom", 10.0, 15.0)
        ed = Editor().blank(8, 8)
        ed.apply_layout(src.layout().to_dict())
        self.assertEqual(ed.page_format, "custom")
        self.assertEqual(ed.custom_page_width, 10.0)
        self.assertEqual(ed.custom_page_height, 15.0)

    def test_project_color_default_is_empty(self) -> None:
        ed = self._blank()
        self.assertEqual(ed.project_color, "")

    def test_set_project_color_normalizes_to_hex(self) -> None:
        ed = self._blank()
        # A CSS name and a #rgb shorthand both land as a lower-case #rrggbb.
        ed.set_project_color("red")
        self.assertEqual(ed.project_color, "#ff0000")
        ed.set_project_color("#0F0")
        self.assertEqual(ed.project_color, "#00ff00")

    def test_set_project_color_empty_clears(self) -> None:
        ed = self._blank().set_project_color("#123456")
        self.assertEqual(ed.project_color, "#123456")
        ed.set_project_color("")
        self.assertEqual(ed.project_color, "")
        ed.set_project_color("#123456").set_project_color("   ")
        self.assertEqual(ed.project_color, "")

    def test_set_project_color_rejects_invalid(self) -> None:
        ed = self._blank().set_project_color("#abcdef")
        with self.assertRaises(ValueError):
            ed.set_project_color("not-a-color")
        # The previous valid colour is kept on rejection.
        self.assertEqual(ed.project_color, "#abcdef")

    def test_load_resets_project_color(self) -> None:
        ed = self._blank().set_project_color("#abcdef")
        ed.blank(8, 8)
        self.assertEqual(ed.project_color, "")


if __name__ == "__main__":
    unittest.main()
