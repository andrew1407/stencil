"""The layout caps ``Layout.from_dict`` cuts a document to, as ``sanitizeLines`` in
``browser/js/core/layout.js`` cuts it: core hands the caps out, which must equal constants.json
LIMITS; the real per-line and line caps through the public parse, the total budget under small ones.
"""

from __future__ import annotations

import unittest
from unittest import mock

from tests.helpers.fixturebase import _CONFIG, _load
from tests.helpers.nativecase import NativeCase

from pystencil import layout as layout_mod
from pystencil.layout import Layout


def _line(n: int, start: int = 0) -> dict:
  return {"points": [{"x": start + i, "y": 1} for i in range(n)]}


class LayoutCapsTests(NativeCase):
  def test_core_caps_are_constants_json_limits(self):
    limits = _load(_CONFIG / "constants.json")["LIMITS"]
    want = (limits["layoutLinesMax"], limits["layoutLinePointsMax"], limits["layoutPointsMax"])
    self.assertEqual(self.core.layout_caps(), want)

  def test_a_line_past_the_per_line_cap_is_cut_and_lines_past_the_line_cap_dropped(self):
    lines, line_points, _ = self.core.layout_caps()
    one = Layout.from_dict({"lines": [_line(line_points + 10)]})
    self.assertEqual(len(one.lines[0].points), line_points)
    self.assertEqual(one.lines[0].points[-1].x, line_points - 1)
    many = Layout.from_dict({"lines": [_line(1)] * (lines + 10)})
    self.assertEqual(len(many.lines), lines)

  def test_the_line_spending_the_last_of_the_budget_is_cut_there_and_the_rest_dropped(self):
    doc = {"lines": [_line(5), {}, _line(2, 5), _line(3, 7), _line(1, 10)]}
    with mock.patch.object(layout_mod, "_caps", return_value=(5, 3, 7)):
      got = Layout.from_dict(doc)
    # Budget 7: five cut to three (4 left), a pointless line kept (4), two (2), three cut to two.
    self.assertEqual([[p.x for p in ln.points] for ln in got.lines], [[0, 1, 2], [], [5, 6], [7, 8]])
    with mock.patch.object(layout_mod, "_caps", return_value=(2, 9, 99)):
      self.assertEqual(len(Layout.from_dict(doc).lines), 2)

  def test_a_non_object_line_or_point_is_skipped_before_it_counts(self):
    points = [7, {"x": 0, "y": 0}, None, {"x": 1, "y": 0}, "p", {"x": 2, "y": 0}, {"x": 3, "y": 0}]
    doc = {"lines": [5, None, {"points": points}, "junk", _line(3, 4), _line(1, 9)]}
    with mock.patch.object(layout_mod, "_caps", return_value=(2, 3, 4)):
      got = Layout.from_dict(doc)
    # Budget 4: the junk spends nothing, the first line is cut to three (1 left), the next to one.
    self.assertEqual([[p.x for p in ln.points] for ln in got.lines], [[0, 1, 2], [4]])
    with mock.patch.object(layout_mod, "_caps", return_value=(2, 9, 99)):
      self.assertEqual([[p.x for p in ln.points] for ln in Layout.from_dict(doc).lines], [[0, 1, 2, 3], [4, 5, 6]])


if __name__ == "__main__":
  unittest.main()
