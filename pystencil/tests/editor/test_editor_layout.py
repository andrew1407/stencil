"""The Editor facade's layout export, coordinate formulas and page formats."""

from __future__ import annotations

import os
import tempfile
import unittest
from dataclasses import replace

from tests.helpers.editorcase import EditorCase

from pystencil.core import get_core
from pystencil.editor import Editor


class EditorLayoutTests(EditorCase):
  def test_layout_width_matches_result(self):
    ed = self._blank().rotate_right()
    lay = ed.layout()
    self.assertEqual(lay.image_width, ed.image_size[0])
    self.assertEqual(lay.image_height, ed.image_size[1])
    self.assertEqual(lay.rotation_quarters, 1)

  def test_layout_always_names_a_croprect(self):
    # The GUIs auto-crop a freshly loaded image to the page aspect unless the layout names a
    # cropRect, so omitting it shrinks the image and strands lines outside the page rect.
    ed = self._blank()  # 32x48, nothing cropped
    self.assertEqual(
      ed.layout().to_dict()["cropRect"],
      {"x": 0, "y": 0, "w": 32, "h": 48},  # canonical {w,h} keys (Phase 6)
    )
    # Still full-frame after a quarter turn, in the ROTATED original's space.
    self.assertEqual(
      ed.rotate_right().layout().to_dict()["cropRect"],
      {"x": 0, "y": 0, "w": 48, "h": 32},
    )
    # An explicit crop still wins over the full-frame default.
    cropped = Editor().blank(32, 48).crop("x1=0 y1=0 x2=-16 y2=-24")
    self.assertEqual(
      cropped.layout().to_dict()["cropRect"],
      {"x": 0, "y": 0, "w": 16, "h": 24},
    )

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

  def test_formula_names_reach_the_page_the_image_and_the_other_axis(self) -> None:
    ed = Editor().blank(200, 100).set_page_format("A4")  # album blank: A4 lies on its side
    ctx = ed.formula_context()
    self.assertAlmostEqual(ctx.page_width_cm, 29.7)
    self.assertAlmostEqual(ctx.page_height_cm, 21.0)
    self.assertEqual((ctx.image_width, ctx.image_height), (200.0, 100.0))

    ed.set_formula("x", "9")  # a constant needs no variable at all
    self.assertEqual(ed.apply_formula("x", 10.0), 9.0)
    ed.set_formula("x", "IMAGE_WIDTH").set_formula("y", "PAGE_WIDTH - y")
    self.assertEqual(ed.apply_formula("x", 10.0), 200.0)
    self.assertAlmostEqual(ed.apply_formula("y", 6.0), 29.7 - 6.0)
    # f(x) may read y, but only when the caller supplies the other coordinate.
    ed.set_formula("x", "x / y")
    self.assertEqual(ed.apply_formula("x", 10.0, other=4.0), 2.5)
    self.assertEqual(ed.apply_formula("x", 10.0), 10.0)
    # PAGE_* follows the unit a caller names; pystencil itself shows cm.
    self.assertAlmostEqual(
      get_core().apply_formula("PAGE_WIDTH", "x", 0.0, True, ctx=replace(ctx, unit="in")),
      29.7 / 2.54,
    )

  def test_image_names_need_an_image_open(self) -> None:
    ed = Editor()
    ctx = ed.formula_context()
    self.assertNotEqual(ctx.image_width, ctx.image_width)  # NaN: not supplied
    with self.assertRaises(ValueError):
      ed.set_formula("x", "IMAGE_WIDTH")
    # Nothing was committed, so the coordinate keeps its raw value rather than moving to 0.
    self.assertEqual(ed.apply_formula("x", 7.0), 7.0)
    ed.blank(200, 100).set_formula("x", "IMAGE_WIDTH")
    self.assertEqual(ed.apply_formula("x", 7.0), 200.0)

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

  def test_apply_layout_croprect_reads_both_key_forms(self) -> None:
    # Canonical {w,h} and legacy {width,height} both adopt; canonical wins.
    for rect in (
      {"x": 1, "y": 2, "w": 3, "h": 4},
      {"x": 1, "y": 2, "width": 3, "height": 4},
      {"x": 1, "y": 2, "w": 3, "h": 4, "width": 9, "height": 9},
    ):
      ed = Editor().blank(8, 8)
      ed.apply_layout({"imageWidth": 8, "imageHeight": 8, "lines": [], "cropRect": rect})
      self.assertEqual(ed._current().crop, (1, 2, 3, 4))


if __name__ == "__main__":
  unittest.main()
