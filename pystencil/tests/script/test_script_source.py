"""What a ``@source`` spec names once the adapter opens it (``scriptpaths``)."""

from __future__ import annotations

import os
import tempfile
import unittest

from pystencil import scriptpaths
from pystencil.script import ScriptError


class SourceExpansionTests(unittest.TestCase):
  """What a ``@source`` spec names once the adapter opens it."""

  def setUp(self):
    self._dir = tempfile.TemporaryDirectory()
    self.addCleanup(self._dir.cleanup)
    for name in ("b.png", "a.png", "note.txt", ".hidden.png"):
      open(os.path.join(self._dir.name, name), "wb").close()

  def test_a_file_or_url_spec_expands_to_itself(self):
    self.assertEqual(scriptpaths.expand_source("https://e.example/a.png", "url"),
                     ["https://e.example/a.png"])
    self.assertEqual(scriptpaths.expand_source("a.png", "file"), ["a.png"])

  def test_a_foreign_scheme_is_refused_before_anything_opens_it(self):
    with self.assertRaises(ScriptError):
      scriptpaths.expand_source("ftp://h/a.png", "file")

  def test_a_directory_lists_sorted_media_only(self):
    found = scriptpaths.expand_source(self._dir.name + "/", "dir")
    self.assertEqual([os.path.basename(p) for p in found], ["a.png", "b.png"])

  def test_a_glob_matches_one_segment(self):
    found = scriptpaths.expand_source(self._dir.name + "/[ab].png", "glob")
    self.assertEqual([os.path.basename(p) for p in found], ["a.png", "b.png"])
    self.assertEqual(scriptpaths.expand_source(self._dir.name + "/c*.png", "glob"), list())

  def test_a_missing_directory_is_an_error(self):
    with self.assertRaises(ScriptError):
      scriptpaths.expand_source(self._dir.name + "/nope/", "dir")

  def test_more_matches_than_the_cap_are_refused(self):
    # MAX_INPUTS bounds one @source block, so a run cannot be aimed at a whole disk.
    for i in range(scriptpaths.MAX_INPUTS + 1):
      open(os.path.join(self._dir.name, "m%04d.png" % i), "wb").close()
    with self.assertRaises(ScriptError) as caught:
      scriptpaths.expand_source(self._dir.name + "/", "dir")
    self.assertIn("more than %d files" % scriptpaths.MAX_INPUTS, str(caught.exception))
    # The same overflow through a glob, which walks the same counter.
    with self.assertRaises(ScriptError):
      scriptpaths.expand_source(self._dir.name + "/m*.png", "glob")


if __name__ == "__main__":
  unittest.main()
