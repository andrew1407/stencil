"""parse_op_plan over the shared op-plan corpus.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.fixturebase`.
"""

from __future__ import annotations

import json
import re
import unittest

from tests.fixturebase import _LLM_FIXTURES, _OVERRIDES, _load

from pystencil.llm import LlmError, parse_op_plan

_OPPLAN_DIR = _LLM_FIXTURES / "opPlan"
_PROFILES = {"editor", "console", "bot", "mcp", "extension", "all"}
_SURFACES = {"browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"}
# The pystencil console shares the cli's "console" profile.
_MY_PROFILES = ("console", "all")


# The hand-written bundle (each case carrying its stable "file" label) and the
# registry-generated one (browser/tools/genOpPlanFixtures.mjs), which walks as "<name>.json".
_HAND = _load(_OPPLAN_DIR / "cases.json")["cases"]
_GENERATED = _load(_OPPLAN_DIR / "generated" / "cases.json")["cases"]
_FIXTURE_FILES = [(fx["file"], fx) for fx in _HAND] + [
  (fx["name"] + ".json", fx) for fx in _GENERATED
]


class TestOpPlanFixtures(unittest.TestCase):
  def test_corpus_is_well_formed(self):
    # Port of the reference walker's corpus-shape check. Floors per bundle, not on the total:
    # the generated cases alone would clear a combined floor.
    self.assertGreaterEqual(len(_HAND), 180, "hand-written cases.json collapsed")
    self.assertGreaterEqual(len(_GENERATED), 400, "generated/cases.json collapsed")
    for fname, fx in _FIXTURE_FILES:
      with self.subTest(fixture=fname):
        self.assertEqual(fx["name"] + ".json", re.sub(r"^\d+-", "", fname))
        self.assertIsInstance(fx["profiles"], list)
        self.assertTrue(fx["profiles"])
        for p in fx["profiles"]:
          self.assertIn(p, _PROFILES)
        self.assertIn(fx["expect"], ("valid", "invalid"))
        self.assertIsNotNone(fx["input"])
        if fx["expect"] == "invalid":
          self.assertTrue(fx.get("reason"), "invalid cases need a reason")
        for surface, verdict in (fx.get("knownDivergence") or {}).items():
          self.assertIn(surface, _SURFACES)
          self.assertIn(verdict, ("valid", "invalid"))

  def test_walk(self):
    overrides = _OVERRIDES["opPlan"]
    walked = 0
    for fname, fx in _FIXTURE_FILES:
      if not any(p in _MY_PROFILES for p in fx["profiles"]):
        continue
      walked += 1
      local = overrides.get(fx["name"], {}).get("verdict")
      want = local or (fx.get("knownDivergence") or {}).get("pystencil") or fx["expect"]
      text = fx["input"] if isinstance(fx["input"], str) else json.dumps(fx["input"])
      with self.subTest(fixture=fname, want=want):
        if want == "valid":
          plan = parse_op_plan(text)  # must not raise (chat-only counts)
          self.assertIsInstance(plan.reply, str)
          self.assertIsInstance(plan.actions, list)
        else:
          with self.assertRaises(LlmError):
            parse_op_plan(text)
    self.assertGreaterEqual(walked, 150, "console/all coverage collapsed")


if __name__ == "__main__":
  unittest.main()
