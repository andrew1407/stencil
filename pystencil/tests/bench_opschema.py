"""validate_action timings: the table-driven op validator every plan goes through.

`Schema.validate_action` runs the op's native rules then walks its declared keys, and it is
the one thing every action of every plan pays for. The ceilings guard that the walk is one
pass over what the action actually carries, with no per-call registry work.
"""

from __future__ import annotations

import json

from tests.benchsupport import BenchCase
from tests.fixturebase import _LLM_FIXTURES, _load

from pystencil._opschema import schema
from pystencil.llm import parse_op_plan

SCHEMA = schema("pystencil")
_CORPUS = [
    fx
    for fx in _load(_LLM_FIXTURES / "opPlan" / "generated" / "cases.json")["cases"]
    if fx["expect"] == "valid" and any(p in ("console", "all") for p in fx["profiles"])
]


def _layout_action(lines: int, points: int) -> dict:
    return {
        "op": "layout",
        "lines": [
            {
                "color": "#FFFF00",
                "thickness": 2,
                "points": [{"x": float(x), "y": float(y)} for x in range(points)],
            }
            for y in range(lines)
        ],
    }


def _cycler(items):
    """A zero-allocation round-robin over `items`, so the timed body is the call alone."""
    box = [0]

    def nxt():
        box[0] = (box[0] + 1) % len(items)
        return items[box[0]]

    return nxt


class ValidateActionBench(BenchCase):
    def _validate(self, action, entry=None):
        entry = entry or SCHEMA.ops[action["op"]]
        return lambda: SCHEMA.validate_action(action, entry)

    def test_the_layout_walk_is_one_pass_over_lines_times_points(self):
        small = self._validate(_layout_action(40, 20))
        wide = self._validate(_layout_action(80, 20))
        deep = self._validate(_layout_action(40, 40))

        base = self.micros("validate_action layout 40x20", 30, small)
        twice_lines = self.micros("validate_action layout 80x20", 15, wide)
        twice_points = self.micros("validate_action layout 40x40", 15, deep)

        # One visit per point, so twice the work either way is twice the time. A ballooning
        # ratio means a nested re-walk (re-resolving the line spec per point, say).
        self.ratio("twice the lines", twice_lines, base, ceiling=3.0)
        self.ratio("twice the points", twice_points, base, ceiling=3.0)

    def test_the_scalar_ops_stay_in_one_band(self):
        rotate = self._validate({"op": "rotate", "dir": "right"})
        crop = self._validate({
            "op": "crop",
            "spec": {"x1": "10%", "x2": "-10%", "y1": "0", "y2": "90px", "aspect": "4:3"},
        })

        cheapest = self.micros("validate_action rotate (1 key)", 5000, rotate)
        dearest = self.micros("validate_action crop (5 sub-keys)", 5000, crop)

        # Both are fixed-key scalar ops, so the spread is the key count and nothing else:
        # a wider gap means a per-call cost crept in (a registry re-resolve, say).
        self.ratio("crop vs rotate", dearest, cheapest, ceiling=10.0)

    def test_rejecting_an_action_is_no_dearer_than_accepting_one(self):
        good = self._validate({"op": "rotate", "dir": "right"})
        entry = SCHEMA.ops["rotate"]

        def bad():
            try:
                SCHEMA.validate_action({"op": "rotate", "dir": "sideways"}, entry)
            except Exception:
                pass

        accepted = self.micros("validate_action rotate (valid)", 5000, good)
        rejected = self.micros("validate_action rotate (invalid)", 5000, bad)

        # The reject path raises on the first bad key — it must never be the dear path,
        # or a malformed plan becomes a way to spend the validator's time.
        self.ratio("invalid vs valid", rejected, accepted, ceiling=4.0)

    def test_parsing_the_corpus_costs_per_case_not_per_corpus(self):
        whole = [json.dumps(fx["input"]) if not isinstance(fx["input"], str) else fx["input"]
                 for fx in _CORPUS]
        half = whole[: len(whole) // 2]
        self.assertGreaterEqual(len(half), 10, "the console corpus collapsed")

        all_next, half_next = _cycler(whole), _cycler(half)
        full_us = self.micros(
            "parse_op_plan over %d cases" % len(whole), len(whole) * 4,
            lambda: parse_op_plan(all_next()))
        half_us = self.micros(
            "parse_op_plan over %d cases" % len(half), len(half) * 4,
            lambda: parse_op_plan(half_next()))

        # Per-case cost, not per-corpus: halving the corpus must not move µs/op much, or
        # something is being re-derived per plan that the registry should own once.
        self.ratio("whole vs half corpus", max(full_us, half_us), min(full_us, half_us), ceiling=2.5)


if __name__ == "__main__":
    import unittest

    unittest.main()
