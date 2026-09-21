"""Round-trip tests for the portable .stencil project file
(Editor.save_project / open_project): the document, its keywords, and delete.
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest

from tests.projectfilecase import ProjectFileCase

from pystencil.editor import Editor


class ProjectFileTests(ProjectFileCase):
  def test_round_trip_file(self):
    ed = self._authored()
    with tempfile.TemporaryDirectory() as d:
      path = os.path.join(d, "p.stencil")
      self.assertEqual(ed.save_project(path), path)
      with open(path) as fh:
        doc = json.load(fh)
      self.assertEqual(doc["format"], "stencil-project")
      self.assertEqual(doc["version"], 1)
      self.assertEqual(doc["name"], "proj")
      self.assertEqual(doc["color"], "#7c3aed")
      self.assertEqual(doc["source"], "https://example.com/a.png")
      self.assertTrue(doc["image"]["dataUrl"].startswith("data:image/png;base64,"))

      ed2 = Editor().open_project(path)
      self.assertEqual(ed2.name, "proj")
      self.assertEqual(ed2._color, "#7c3aed")
      layout = ed2.layout()
      self.assertEqual(layout.rotation_quarters, 1)
      self.assertEqual(len(layout.lines), 1)
      self.assertEqual(ed2.image_size, ed.image_size)  # same rendered dims

  def test_open_from_bytes_and_dict(self):
    ed = self._authored()
    with tempfile.TemporaryDirectory() as d:
      path = os.path.join(d, "p.stencil")
      ed.save_project(path)
      with open(path, "rb") as fh:
        raw = fh.read()
      self.assertTrue(Editor().open_project(raw).has_image())  # raw JSON bytes
      self.assertTrue(Editor().open_project(json.loads(raw)).has_image())  # parsed dict

  def test_keywords_round_trip(self):
    ed = self._authored()
    # set_keywords trims and drops empties/non-strings (mirrors project/file.js cleanKeywords).
    ed.set_keywords(["  alpha ", "beta", "", 5, "alpha"])
    self.assertEqual(ed.keywords, ["alpha", "beta", "alpha"])
    with tempfile.TemporaryDirectory() as d:
      path = os.path.join(d, "p.stencil")
      ed.save_project(path)
      with open(path) as fh:
        doc = json.load(fh)
      self.assertEqual(doc["keywords"], ["alpha", "beta", "alpha"])
      self.assertEqual(Editor().open_project(path).keywords, ["alpha", "beta", "alpha"])

  def test_keywords_omitted_when_empty(self):
    ed = self._authored()  # no keywords set
    with tempfile.TemporaryDirectory() as d:
      path = os.path.join(d, "p.stencil")
      ed.save_project(path)
      with open(path) as fh:
        doc = json.load(fh)
      self.assertNotIn("keywords", doc)
      self.assertEqual(Editor().open_project(path).keywords, [])

  def test_preserves_foreign_keywords_on_reopen(self):
    # A .stencil authored by another surface carries keywords pystencil never set; opening then
    # re-saving must not drop them.
    ed = self._authored()
    with tempfile.TemporaryDirectory() as d:
      src = os.path.join(d, "src.stencil")
      ed.save_project(src)
      with open(src) as fh:
        doc = json.load(fh)
      doc["keywords"] = ["from-browser", "shared"]
      resaved = os.path.join(d, "out.stencil")
      Editor().open_project(doc).save_project(resaved)
      with open(resaved) as fh:
        out = json.load(fh)
      self.assertEqual(out["keywords"], ["from-browser", "shared"])

  def test_delete_project(self):
    # Parity with the browser/desktop trash button + CLI /delete: remove the file from disk,
    # while a loaded editor stays loaded (delete_project is stateless).
    ed = self._authored()
    with tempfile.TemporaryDirectory() as d:
      path = os.path.join(d, "p.stencil")
      ed.save_project(path)
      self.assertTrue(os.path.exists(path))
      self.assertEqual(Editor.delete_project(path), path)
      self.assertFalse(os.path.exists(path))
      self.assertTrue(ed.has_image())  # the open project is untouched

  def test_delete_project_rejects_non_stencil(self):
    with tempfile.TemporaryDirectory() as d:
      other = os.path.join(d, "notes.txt")
      with open(other, "w") as fh:
        fh.write("keep me")
      with self.assertRaises(ValueError):
        Editor.delete_project(other)
      self.assertTrue(os.path.exists(other))  # a non-.stencil file is never removed

  def test_delete_project_missing_raises(self):
    with tempfile.TemporaryDirectory() as d:
      with self.assertRaises(FileNotFoundError):
        Editor.delete_project(os.path.join(d, "gone.stencil"))

  def test_rejects_foreign_or_too_new(self):
    with self.assertRaises(ValueError):
      Editor().open_project('{"version":1}')  # no format sentinel
    with self.assertRaises(ValueError):
      Editor().open_project(
        '{"format":"stencil-project","version":999,'
        '"image":{"dataUrl":"data:image/png;base64,AAAA"}}'
      )  # too new
    with self.assertRaises(ValueError):
      Editor().open_project('{"format":"stencil-project","version":1}')  # no image


if __name__ == "__main__":
  unittest.main()


if __name__ == "__main__":
  unittest.main()
