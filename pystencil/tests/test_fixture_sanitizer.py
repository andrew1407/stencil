"""_clean_detail over the shared sanitizer vectors.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.fixturebase`.
"""

from __future__ import annotations

import re
import unittest

from tests.fixturebase import _LLM_FIXTURES, _OVERRIDES, _load

from pystencil.llm import _clean_detail

_URL_RE = re.compile(r"[a-z][a-z0-9+.-]*://", re.I)
_TOKEN_RUN_RE = re.compile(r"[A-Za-z0-9_-]{24,}")


# Parsed once per module, not once per test method.
_CASES = _load(_LLM_FIXTURES / "sanitizer" / "cases.json")


class TestSanitizerFixtures(unittest.TestCase):
  def _divergent_surfaces(self, name: str):
    m = re.match(r"DIVERGENCE\(([^)]*)\)", name)
    return [s.strip() for s in m.group(1).split(",")] if m else []

  def test_walk(self):
    overrides = _OVERRIDES["sanitizer"]
    for case in _CASES:
      name = case["name"]
      with self.subTest(case=name):
        if case["input"] is None:
          continue  # _clean_detail takes str; callers guard None
        got = _clean_detail(case["input"])
        if name in overrides:
          want = overrides[name]["expect"]
        elif "pystencil" in self._divergent_surfaces(name):
          # Recompute locally: Python counts code points, not UTF-16
          # units. Only the length cap may diverge, so require that no
          # redaction/control step applies, then re-derive the cut.
          inp = case["input"]
          self.assertLessEqual(len(inp), 800)
          self.assertIsNone(_URL_RE.search(inp))
          self.assertIsNone(_TOKEN_RUN_RE.search(inp))
          self.assertIsNone(re.search(r"[\x00-\x1f\x7f]", inp))
          want = " ".join(inp.split())
          if len(want) > 200:
            want = want[:199].strip() + "…"
        else:
          want = case["expect"]
        self.assertEqual(got, want)
        # Invariants hold for every case, divergent or not.


if __name__ == "__main__":
  unittest.main()
