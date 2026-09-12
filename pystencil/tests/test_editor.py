"""The Editor facade's image pipeline: blank, crop, rotate, filter, draw, undo, save."""

from __future__ import annotations

import os
import tempfile
import unittest

from tests.editorcase import EditorCase, _grayscale_pixels

from pystencil import codecs
from pystencil.editor import Editor
from pystencil.layout import Layout, Line, Point


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


class EditorPipelineTests(EditorCase):
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


if __name__ == "__main__":
  unittest.main()
