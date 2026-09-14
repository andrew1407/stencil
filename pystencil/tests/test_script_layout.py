"""``@layout`` through ``Editor.script`` — the one op the other script suites never run.

A layout op hands its path straight to ``Editor.draw``, so ``combine`` (the default) keeps
the lines already drawn and ``replace`` drops them.
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest

from tests.nativecase import NativeCase

from pystencil.editor import Editor


def _layout_file(directory: str, name: str, x: float) -> str:
  """A one-line layout on disk, at the given x, sized for the blank below."""
  path = os.path.join(directory, name)
  with open(path, "w", encoding="utf-8") as handle:
    json.dump({
      "imageWidth": 400, "imageHeight": 300,
      "lines": [{"points": [{"x": x, "y": 10}, {"x": x + 20, "y": 40}], "color": "#ff0000"}],
    }, handle)
  return path


class EditorScriptLayoutTests(NativeCase):
  """``@layout <path> [combine|replace]`` on the single-image door."""

  def _blank(self):
    return Editor().blank(400, 300)

  def test_a_layout_op_draws_the_files_lines(self):
    with tempfile.TemporaryDirectory() as directory:
      path = _layout_file(directory, "notes.json", 10)
      editor = self._blank()
      result = editor.script("@layout %s\n" % path)
      self.assertFalse(result.has_errors)
      self.assertEqual(result.applied, 1)
      self.assertEqual(len(editor.layout().lines), 1)

  def test_combine_is_the_default_and_keeps_what_was_drawn(self):
    with tempfile.TemporaryDirectory() as directory:
      path = _layout_file(directory, "notes.json", 10)
      editor = self._blank()
      editor.script("@rect (1,1) (2,2)\n@layout %s\n" % path)
      self.assertEqual(len(editor.layout().lines), 2)

  def test_replace_drops_the_lines_already_drawn(self):
    with tempfile.TemporaryDirectory() as directory:
      path = _layout_file(directory, "notes.json", 10)
      editor = self._blank()
      editor.script("@rect (1,1) (2,2)\n@layout %s replace\n" % path)
      lines = editor.layout().lines
      self.assertEqual(len(lines), 1)
      self.assertEqual(lines[0].points[0].x, 10)

  def test_two_layouts_in_one_script_stack(self):
    with tempfile.TemporaryDirectory() as directory:
      first = _layout_file(directory, "one.json", 10)
      second = _layout_file(directory, "two.json", 100)
      editor = self._blank()
      result = editor.script("@layout %s\n@layout %s\n" % (first, second))
      self.assertEqual(result.applied, 2)
      self.assertEqual(len(editor.layout().lines), 2)

  def test_an_undo_after_a_layout_takes_its_lines_back(self):
    with tempfile.TemporaryDirectory() as directory:
      path = _layout_file(directory, "notes.json", 10)
      editor = self._blank()
      editor.script("@layout %s\n@undo\n" % path)
      self.assertEqual(len(editor.layout().lines), 0)


if __name__ == "__main__":
  unittest.main()
