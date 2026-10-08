"""A flip mirrors the picture, the drawn lines and the crop as the browser's does — the same
numbers as the CLI console's ``flip_lines_test.zig`` — and rides the layout as ``mirrored``."""

from __future__ import annotations

from tests.helpers.editorcase import EditorCase

from pystencil.editor import Editor
from pystencil.layout import Layout, Line, Point


def _line(*pts):
  return Line(points=[Point(x, y) for x, y in pts], color="#00ff00", thickness=2, point_size=3)


def _points(ed):
  return [[(p.x, p.y) for p in ln.points] for ln in ed.layout().lines]


class EditorFlipTests(EditorCase):
  def _drawn(self):
    return Editor().blank(16, 12, color="red").draw([_line((2, 3), (7, 1))])

  def test_flip_mirrors_the_lines_and_undo_brings_them_back(self):
    ed = self._drawn()
    ed.flip()  # in a 16-wide view: (x, y) → (16 − x, y)
    self.assertEqual(_points(ed), [[(14, 3), (9, 1)]])
    self.assertEqual(ed.image_size, (16, 12))
    self.assertIs(ed.layout().mirrored, True)
    self.assertTrue(ed.layout().to_json().endswith('"mirrored": true}'))
    ed.undo()
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])
    self.assertIsNone(ed.layout().mirrored)

  def test_the_picture_itself_is_mirrored(self):
    ed = Editor().blank(4, 2, color="white").draw([_line((0, 0), (0, 2))])
    before = ed.result().data
    after = ed.flip().result().data
    row = lambda d, y, x: d[(y * 4 + x) * 4:(y * 4 + x) * 4 + 4]
    self.assertEqual(row(after, 0, 3), row(before, 0, 0))

  def test_a_flip_negates_the_turn(self):
    ed = self._drawn().rotate_right().flip()
    self.assertEqual(ed.layout().rotation_quarters, 3)
    ed.flip()
    self.assertEqual(ed.layout().rotation_quarters, 1)
    self.assertIsNone(ed.layout().mirrored)

  def test_apply_layout_adopts_the_mirror(self):
    ed = Editor().blank(16, 12, color="red")
    ed.apply_layout(Layout.from_dict({"imageWidth": 16, "imageHeight": 12, "lines": [], "mirrored": True}))
    self.assertIs(ed.layout().mirrored, True)
    self.assertNotIn("mirrored", Layout.from_dict({"imageWidth": 1, "imageHeight": 1, "mirrored": "yes"}).to_dict())
