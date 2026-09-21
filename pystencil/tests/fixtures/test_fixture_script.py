"""The shared ``.stc`` corpus, walked through pystencil's own parser binding.

Replays ``browser/js/config/script/fixtures/cases.txt`` — the same file
``core/tests/scriptFixtures.test.cpp`` and ``browser/tests/scriptFixtures.test.js``
read, with the same splitting rules. A case named ``err-*`` must produce at least one
error; every other case must produce none.
"""

from __future__ import annotations

import unittest
from pathlib import Path

from tests import _PKG_ROOT
from tests.helpers.nativecase import NativeCase

from pystencil.script import parse_script

CASES_PATH = (
  _PKG_ROOT.parent / "browser" / "js" / "config" / "script" / "fixtures" / "cases.txt"
)
_SECTIONS = ("script", "dump", "diagnostics")


def _join(body):
  """A section's text. The blank line before the next marker belongs to the file."""
  while body and not body[-1]:
    body.pop()
  return "".join(line + "\n" for line in body)


def read_cases(path: Path):
  """Every ``=== <name>`` section as ``{name, script, dump, diagnostics}``."""
  cases, body, section, current = list(), list(), "", None
  for raw in path.read_text(encoding="utf-8").split("\n"):
    line = raw[:-1] if raw.endswith("\r") else raw
    if line.startswith("=== "):
      if current is not None:
        if section: current[section] = _join(body)
        cases.append(current)
      body, section = list(), ""
      current = {"name": line[4:], "script": "", "dump": "", "diagnostics": ""}
      continue
    if current is None: continue
    if line[4:] in _SECTIONS and line.startswith("--- "):
      if section: current[section] = _join(body)
      body, section = list(), line[4:]
      continue
    if section: body.append(line)
  if current is not None:
    if section: current[section] = _join(body)
    cases.append(current)
  return cases


class ScriptCorpusTests(NativeCase):
  """One assertion per case, run as a band so a whole-corpus break reads at a glance."""

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    cls.cases = read_cases(CASES_PATH)

  def test_the_corpus_is_present(self):
    self.assertGreaterEqual(len(self.cases), 40, "the corpus shrank — a case was deleted?")

  def test_every_case_matches_its_recorded_dump(self):
    wrong = list()
    for case in self.cases:
      with parse_script(case["script"]) as program:
        if program.dump != case["dump"]: wrong.append(case["name"])
    self.assertEqual(wrong, [], "dump mismatch")

  def test_every_case_matches_its_recorded_diagnostics(self):
    wrong = list()
    for case in self.cases:
      with parse_script(case["script"]) as program:
        if program.diagnostics_text() != case["diagnostics"]: wrong.append(case["name"])
    self.assertEqual(wrong, [], "diagnostic mismatch")

  def test_the_err_naming_convention_holds(self):
    wrong = list()
    for case in self.cases:
      with parse_script(case["script"]) as program:
        if program.has_errors != case["name"].startswith("err-"): wrong.append(case["name"])
    self.assertEqual(wrong, [], "error expectation")

  def test_a_parse_never_throws_on_truncated_input(self):
    src = next(c["script"] for c in self.cases if c["name"] == "tour-crop")
    for cut in range(0, len(src), 7):
      with parse_script(src[:cut]) as program:
        self.assertIsInstance(program.ops, tuple)


if __name__ == "__main__":
  unittest.main()
