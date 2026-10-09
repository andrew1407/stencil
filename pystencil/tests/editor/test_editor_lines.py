"""The drawn lines follow a rotate or a crop as the browser's do — the same cases, and the same
numbers, as the CLI console's ``rotate_flip_lines_test.zig`` / ``crop_lines_test.zig``."""

from __future__ import annotations

import unittest

from tests.helpers.editorcase import EditorCase

from pystencil.editor import Editor
from pystencil.editor._lines import crop_change, turn_lines
from pystencil.layout import Line, Point


def _line(*pts):
  return Line(points=[Point(x, y) for x, y in pts], color="#00ff00", thickness=2, point_size=3)


def _points(ed):
  return [[(p.x, p.y) for p in ln.points] for ln in ed.layout().lines]


def _green(ed):
  img = ed.result()
  d = img.data
  return sum(1 for i in range(0, len(d), 4) if d[i + 1] > 200 and d[i] < 60)


class LineMathTests(unittest.TestCase):
  """The pure halves, no core needed."""

  def test_turns_right_and_left_inside_the_view_unclamped(self):
    src = [_line((1, 2), (20, -1.5))]
    self.assertEqual([(p.x, p.y) for p in turn_lines(src, 1, 16, 12)[0].points], [(10, 1), (13.5, 20)])
    self.assertEqual([(p.x, p.y) for p in turn_lines(src, -1, 16, 12)[0].points], [(2, 15), (-1.5, -4)])
    self.assertEqual([(p.x, p.y) for p in turn_lines(src, 2, 16, 12)[0].points], [(15, 10), (-4, 13.5)])

  def test_a_left_press_is_one_exact_step(self):
    self.assertEqual(turn_lines([_line((2, 0.1))], -1, 16, 12)[0].points, [Point(0.1, 14.0)])

  def test_turning_never_touches_the_source_lines(self):
    src = [_line((1, 2))]
    turn_lines(src, 1, 16, 12)
    self.assertEqual(src[0].points, [Point(1, 2)])

  def test_crop_change_flips_or_scales_by_the_width_ratio(self):
    self.assertEqual(crop_change((0, 0, 16, 12), (4, 3, 8, 6)), (False, 0.5))
    self.assertEqual(crop_change((0, 0, 16, 12), (0, 0, 8, 12)), (True, 1.0))
    self.assertTrue(crop_change((0, 0, 16, 12), (0, 0, 12, 12))[0])  # a square is portrait


class EditorLinesTests(EditorCase):
  def _drawn(self):
    return Editor().blank(16, 12, color="red").draw([_line((2, 3), (7, 1))])

  def test_rotate_turns_the_lines_and_undo_turns_them_back(self):
    ed = self._drawn()
    ed.rotate_right()  # right in 16x12: (x, y) → (12 − y, x)
    self.assertEqual(_points(ed), [[(9, 2), (11, 7)]])
    self.assertEqual(ed.image_size, (12, 16))
    ed.undo()
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])
    ed.redo()
    ed.rotate_left()
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])

  def test_under_a_crop_the_lines_turn_in_the_cropped_view_and_the_crop_follows_core(self):
    ed = Editor().blank(16, 12, color="red").crop("x1=0% x2=50% y1=0% y2=100%")
    ed.draw([_line((2, 3), (7, 1))])
    crop = ed._current().crop
    ed.rotate(1)
    self.assertEqual((ed._current().crop, ed._current().rotation), self.core.rotate_edit_quarter(crop, 0, 16, 12, True))
    self.assertEqual(_points(ed), [[(9, 2), (11, 7)]])
    ed.rotate(3)
    self.assertEqual(ed._current().crop, crop)
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])

  def test_a_same_orientation_crop_scales_the_lines(self):
    ed = self._drawn().crop("x1=25% x2=75% y1=25% y2=75%")
    self.assertEqual(ed.image_size, (8, 6))
    self.assertEqual(_points(ed), [[(1, 1.5), (3.5, 0.5)]])
    self.assertGreater(_green(ed), 0)
    ed.undo()
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])

  def test_an_album_portrait_flip_clears_the_lines_and_undo_restores_them(self):
    ed = self._drawn().crop("x1=0% x2=50% y1=0% y2=100%")
    self.assertEqual(ed.image_size, (8, 12))
    self.assertEqual(ed.layout().lines, [])
    self.assertEqual(_green(ed), 0)
    ed.undo()
    self.assertEqual(_points(ed), [[(2, 3), (7, 1)]])
    self.assertGreater(_green(ed), 0)


if __name__ == "__main__":
  unittest.main()
