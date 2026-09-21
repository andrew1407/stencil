"""Line/Layout parse and export over the shared layout vectors.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.helpers.fixturebase`.
"""

from __future__ import annotations

import unittest

from tests.helpers.fixturebase import _FIXTURES, _OVERRIDES, _filled_line_dict, _load, _norm

from pystencil.layout import (
  DEFAULT_COLOR,
  DEFAULT_FILL_COLOR,
  DEFAULT_LOCKED,
  DEFAULT_POINT_SIZE,
  DEFAULT_STYLE,
  DEFAULT_THICKNESS,
  Layout,
  Line,
)

_LAYOUT_DIR = _FIXTURES / "fixtures" / "layout"
# Parsed once per module, not once per test method.
_SPARSE = _load(_LAYOUT_DIR / "sparse.json")
_PAYLOAD = _load(_LAYOUT_DIR / "payload.json")


class TestLayoutFixtures(unittest.TestCase):
  def test_defaults_align_with_corpus(self):
    # The cross-surface per-line defaults pinned by sparse.json/_schema.md.
    self.assertEqual(DEFAULT_COLOR, "#FFFF00")
    self.assertEqual(DEFAULT_THICKNESS, 2.0)
    self.assertEqual(DEFAULT_POINT_SIZE, 4.0)
    self.assertEqual(DEFAULT_STYLE, "solid")
    self.assertEqual(DEFAULT_LOCKED, False)
    self.assertEqual(DEFAULT_FILL_COLOR, "transparent")
    self.assertEqual(Line().point_color, "")  # '' = inherit stroke

  def test_sparse_filled(self):
    # pystencil is a tolerant parser: its parse of the sparse lines must
    # yield the corpus's expectFilled shape (empty-points lines kept).
    overrides = _OVERRIDES["layout/sparse"]
    for case in _SPARSE:
      with self.subTest(case=case["name"]):
        raw = case["sparse"]
        lines = [Line.from_dict(x) for x in raw] if isinstance(raw, list) else []
        got = [_filled_line_dict(ln) for ln in lines]
        want = overrides.get(case["name"], {}).get("expectFilled", case["expectFilled"])
        self.assertEqual(_norm(got), _norm(want))

  def test_payload_export(self):
    # Layout.from_dict → to_dict is pystencil's export path; uses imageFilter
    # (aligned with the browser) and pins the top-level key order.
    overrides = _OVERRIDES["layout/payload"]
    for case in _PAYLOAD:
      with self.subTest(case=case["name"]):
        out = Layout.from_dict(case["layout"]).to_dict()
        over = overrides.get(case["name"])
        want = over["expectPayload"] if over else case["expectPayload"]
        self.assertEqual(_norm(out), _norm(want))
        order = case.get("expectKeyOrder")
        if order and not over:
          self.assertEqual(list(out.keys()), order)
        for key in case.get("forcedKeys", []):
          self.assertIn(key, out)


if __name__ == "__main__":
  unittest.main()
