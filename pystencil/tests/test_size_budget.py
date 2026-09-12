"""Size + comment ratchet for pystencil's own Python sources.

New files must stay under the budget's ``maxNewFileLines``; files already over it are
listed with their current length and may only shrink. Per-directory comment share may
not rise. The recorded numbers live in ``tests/size_budget.json`` — lower them as the
refactor lands.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

_BUDGET_PATH = Path(__file__).resolve().parent / "size_budget.json"


def _repo_root():
  """Walk up from this file to the checkout root so paths read the same everywhere."""
  for candidate in Path(__file__).resolve().parents:
    if (candidate / ".git").exists():
      return candidate
  raise RuntimeError("repo root not found above " + str(__file__))


REPO_ROOT = _repo_root()

# Scope: pystencil's own Python — the package, its tests, and the core build script.
_SCOPE_DIRS = ("pystencil/pystencil", "pystencil/tests")
_SCOPE_FILES = ("pystencil/build.py",)


def scope_files():
  """Repo-relative .py paths in scope, sorted."""
  found = set()
  for rel in _SCOPE_DIRS:
    for path in (REPO_ROOT / rel).rglob("*.py"):
      found.add(path.resolve().relative_to(REPO_ROOT).as_posix())
  for rel in _SCOPE_FILES:
    if (REPO_ROOT / rel).is_file():
      found.add(rel)
  return sorted(found)


def count_lines(path):
  """(total lines, comment lines). A comment is a ``#`` line outside any string."""
  text = path.read_text(encoding="utf-8")
  lines = text.splitlines()
  total = len(lines)
  comments = 0
  delim = ""  # the triple-quote delimiter we are inside of, if any
  for line in lines:
    if not delim and line.lstrip().startswith("#"):
      comments += 1
    delim = _scan_strings(line, delim)
  return total, comments


def _scan_strings(line, delim):
  """Track triple-quoted string state across a line; returns the open delimiter."""
  i = 0
  while i < len(line):
    rest = line[i:]
    if delim:
      if rest.startswith(delim):
        delim = ""
        i += 3
        continue
      i += 2 if rest.startswith("\\") else 1
      continue
    if rest.startswith("#"):
      return delim
    for quote in ('"""', "'''"):
      if rest.startswith(quote):
        delim = quote
        break
    if delim:
      i += 3
      continue
    if rest[0] in "\"'":
      i = _skip_short_string(line, i)
      continue
    i += 1
  return delim


def _skip_short_string(line, i):
  """Advance past a single-line '...' or "..." literal (unterminated = end of line)."""
  quote = line[i]
  i += 1
  while i < len(line):
    if line[i] == "\\":
      i += 2
      continue
    if line[i] == quote:
      return i + 1
    i += 1
  return i


class SizeBudgetTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    with open(_BUDGET_PATH, encoding="utf-8") as fh:
      cls.budget = json.load(fh)
    cls.files = scope_files()
    cls.counts = {rel: count_lines(REPO_ROOT / rel) for rel in cls.files}

  def test_every_oversized_file_is_recorded(self):
    cap = self.budget["maxNewFileLines"]
    listed = self.budget["files"]
    exceptions = self.budget["exceptions"]
    missing = [
      rel
      for rel in self.files
      if self.counts[rel][0] > cap and rel not in listed and rel not in exceptions
    ]
    self.assertEqual(
      missing,
      [],
      "new file(s) over %d lines — split them, or record the length in %s"
      % (cap, _BUDGET_PATH.name),
    )

  def test_recorded_files_do_not_grow(self):
    notes = []
    for rel, recorded in sorted(self.budget["files"].items()):
      if rel in self.budget["exceptions"]:
        continue
      path = REPO_ROOT / rel
      self.assertTrue(path.is_file(), "%s is recorded but gone — drop the entry" % rel)
      total = self.counts.get(rel, count_lines(path))[0]
      self.assertLessEqual(total, recorded, "%s grew: %d > %d recorded" % (rel, total, recorded))
      if total < recorded * 0.9:
        notes.append("  %s: %d -> %d, ratchet it down" % (rel, recorded, total))
    if notes:
      print("\nsize_budget.json can shrink:\n" + "\n".join(notes))

  def test_unlisted_files_stay_small(self):
    cap = self.budget["maxNewFileLines"]
    over = [
      "%s (%d)" % (rel, self.counts[rel][0])
      for rel in self.files
      if rel not in self.budget["files"]
      and rel not in self.budget["exceptions"]
      and self.counts[rel][0] > cap
    ]
    self.assertEqual(over, [], "file(s) over the %d-line cap for new files" % cap)

  def test_comment_share_does_not_rise(self):
    totals = {}
    for rel in self.files:
      parent = str(Path(rel).parent.as_posix())
      total, comments = self.counts[rel]
      acc = totals.setdefault(parent, [0, 0])
      acc[0] += total
      acc[1] += comments
    for directory, recorded in sorted(self.budget["commentPct"].items()):
      self.assertIn(directory, totals, "%s has no files in scope" % directory)
      total, comments = totals[directory]
      pct = comments * 100 // total
      self.assertLessEqual(pct, recorded, "%s comment share rose: %d%% > %d%%" % (directory, pct, recorded))

  def test_budget_shape(self):
    self.assertEqual(
      sorted(self.budget),
      ["_doc", "commentPct", "exceptions", "files", "maxNewFileLines"],
    )
    for rel, why in self.budget["exceptions"].items():
      self.assertTrue(why, "%s needs a reason" % rel)


if __name__ == "__main__":
  unittest.main()
