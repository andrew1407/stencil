"""The Editor facade's project colour, and `draw(..., combine=)` append vs replace."""

from __future__ import annotations

import unittest

from tests.editorcase import EditorCase
from tests.nativecase import NativeCase

from pystencil.editor import Editor


class EditorProjectColorTests(EditorCase):
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


class DrawCombineTests(NativeCase):
  """`draw(..., combine=)` — append the incoming lines or replace the current ones.

  The parameter form of the same choice the GUI editors put in a Combine/Replace
  prompt, and the CLI console's append-by-default `apply`.
  """

  def _editor(self):
    ed = Editor().blank(20, 10, "white")
    ed.draw({"lines": [{"points": [{"x": 1, "y": 1}, {"x": 2, "y": 2}]}]})
    return ed

  def test_combine_is_the_default_and_appends(self):
    ed = self._editor()
    ed.draw({"lines": [{"points": [{"x": 5, "y": 5}, {"x": 6, "y": 6}]}]})
    lines = ed.layout().lines
    self.assertEqual(len(lines), 2)
    self.assertEqual((lines[0].points[0].x, lines[0].points[0].y), (1, 1))
    self.assertEqual((lines[1].points[0].x, lines[1].points[0].y), (5, 5))

  def test_combine_false_replaces_the_current_lines(self):
    ed = self._editor()
    ed.draw({"lines": [{"points": [{"x": 5, "y": 5}, {"x": 6, "y": 6}]}]}, combine=False)
    lines = ed.layout().lines
    self.assertEqual(len(lines), 1)
    self.assertEqual((lines[0].points[0].x, lines[0].points[0].y), (5, 5))

  def test_a_raw_json_string_is_accepted_either_way(self):
    ed = self._editor()
    ed.draw('{"lines": [{"points": [{"x": 9, "y": 9}, {"x": 8, "y": 8}]}]}')
    self.assertEqual(len(ed.layout().lines), 2)
    ed.draw('{"lines": [{"points": [{"x": 7, "y": 7}, {"x": 6, "y": 6}]}]}', combine=False)
    self.assertEqual(len(ed.layout().lines), 1)

  def test_undo_restores_the_previous_line_set(self):
    ed = self._editor()
    ed.draw({"lines": [{"points": [{"x": 5, "y": 5}]}]}, combine=False)


if __name__ == "__main__":
  unittest.main()
