"""Op-plan parsing, rejection side: the limits and shapes that fail a plan."""

from __future__ import annotations

import unittest

from pystencil.llm import LlmPlanError, MAX_ACTIONS, MAX_VARIANTS, parse_op_plan
from tests.stubs import _plan_json


class ParseOpPlanRejectionTest(unittest.TestCase):
    def _reject(self, actions=None, **kw) -> None:
        if actions is not None:
            kw["actions"] = actions
        with self.assertRaises(LlmPlanError):
            parse_op_plan(_plan_json(**kw))

    def test_missing_or_empty_reply_is_tolerated(self) -> None:
        # §1 reply tolerance: "Done." + a warning, the plan itself survives
        # (this parser folds warnings into the displayed reply text).
        plan = parse_op_plan('{"actions":[{"op":"rotate","dir":"left"}]}')
        self.assertTrue(plan.reply.startswith("Done."))
        self.assertEqual(len(plan.actions), 1)
        self.assertTrue(any("omitted its reply" in w for w in plan.warnings))
        # An EMPTY plan says so — a bare "Done." would read as a success that
        # never occurred (contract §1).
        plan = parse_op_plan('{"reply":"   "}')
        self.assertIn("empty plan", plan.reply)
        self.assertFalse(any("still ran" in w for w in plan.warnings))

    def test_actions_must_be_an_array(self) -> None:
        self._reject(actions={"op": "rotate"})

    def test_action_must_be_object_with_op(self) -> None:
        self._reject(actions=["rotate"])
        self._reject(actions=[{"dir": "left"}])

    def test_unknown_field_on_known_op(self) -> None:
        self._reject(actions=[{"op": "rotate", "dir": "left", "angle": 45}])

    def test_rotate_invalid_params(self) -> None:
        self._reject(actions=[{"op": "rotate", "dir": "up"}])
        self._reject(actions=[{"op": "rotate", "dir": "left", "times": 0}])
        self._reject(actions=[{"op": "rotate", "dir": "left", "times": 4}])
        self._reject(actions=[{"op": "rotate", "dir": "left", "times": "2"}])
        self._reject(actions=[{"op": "rotate", "dir": "left", "times": True}])

    def test_filter_invalid_params(self) -> None:
        self._reject(actions=[{"op": "filter", "mode": "blur"}])
        self._reject(actions=[{"op": "filter", "mode": "custom"}])  # tint required
        self._reject(actions=[{"op": "filter", "mode": "custom", "tint": "#12g"}])
        self._reject(actions=[{"op": "filter", "mode": "bw", "tint": "#123456"}])

    def test_crop_invalid_params(self) -> None:
        self._reject(actions=[{"op": "crop", "spec": "x1=10%"}])  # not an object
        self._reject(actions=[{"op": "crop", "spec": {}}])  # no edges
        self._reject(actions=[{"op": "crop", "spec": {"left": "10%"}}])  # bad key
        self._reject(actions=[{"op": "crop", "spec": {"x1": "10em"}}])  # bad unit
        self._reject(actions=[{"op": "crop", "spec": {"x1": 10}}])  # not a string

    def test_crop_invalid_aspect(self) -> None:
        # Strict W:H, digits only, both positive — malformed = whole plan fails.
        for aspect in ("0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2",
                       "4", "4:", ":3", "1e2:3", ""):
            self._reject(actions=[{"op": "crop", "spec": {"aspect": aspect}}])
        self._reject(actions=[{"op": "crop", "spec": {"aspect": 43}}])  # not a string

    def test_crop_action_level_aspect_invalid(self) -> None:
        # The folded action-level spelling gets the same W:H validation …
        self._reject(actions=[{"op": "crop", "spec": {"x1": "10%"}, "aspect": "0:3"}])
        self._reject(actions=[{"op": "crop", "spec": {"x1": "10%"}, "aspect": 43}])
        # … conflicting duplicates fail the whole plan …
        self._reject(
            actions=[{"op": "crop", "spec": {"aspect": "3:4"}, "aspect": "4:3"}]
        )
        # … and other stray fields on crop still fail it.
        self._reject(actions=[{"op": "crop", "spec": {"x1": "10%"}, "ratio": "3:4"}])

    def test_layout_invalid_lines(self) -> None:
        self._reject(actions=[{"op": "layout", "lines": {"points": []}}])
        # An empty point list is a valid (per-line defaults apply) line — the
        # registry schema, like the browser reference, puts no floor on "points".
        plan = parse_op_plan(_plan_json(actions=[{"op": "layout", "lines": [{"points": []}]}]))
        self.assertEqual(plan.actions[0]["lines"], [{"points": []}])
        self._reject(
            actions=[{"op": "layout", "lines": [{"points": [{"x": 1}]}]}]
        )  # point missing y
        self._reject(
            actions=[
                {"op": "layout", "lines": [{"points": [{"x": 1, "y": 2}], "style": "wavy"}]}
            ]
        )
        self._reject(
            actions=[
                {"op": "layout", "lines": [{"points": [{"x": 1, "y": 2}], "glow": True}]}
            ]
        )  # unknown line key

    def test_formula_invalid_params(self) -> None:
        self._reject(actions=[{"op": "formula", "axis": "z", "expr": "x"}])
        self._reject(actions=[{"op": "formula", "axis": "x", "expr": "y+1"}])  # wrong var
        self._reject(actions=[{"op": "formula", "axis": "x", "expr": "sin(x)"}])  # charset
        self._reject(actions=[{"op": "formula", "axis": "x"}])  # expr required

    def test_page_and_blank_formats(self) -> None:
        self._reject(actions=[{"op": "page", "format": "A4"}])  # lowercase only
        self._reject(actions=[{"op": "page", "format": "a11"}])
        self._reject(actions=[{"op": "page", "format": "d4"}])
        self._reject(actions=[{"op": "blank", "color": "#ffffff", "format": "letter"}])
        self._reject(actions=[{"op": "blank", "color": "#12345"}])  # short hex
        self._reject(actions=[{"op": "blank", "color": "not a colour!"}])

    def test_frame_invalid_params(self) -> None:
        self._reject(actions=[{"op": "frame"}])  # neither
        self._reject(actions=[{"op": "frame", "index": 0, "indices": [1]}])  # both
        self._reject(actions=[{"op": "frame", "index": -1}])
        self._reject(actions=[{"op": "frame", "indices": []}])
        self._reject(actions=[{"op": "frame", "indices": list(range(33))}])  # > 32

    def test_limits(self) -> None:
        too_many = [{"op": "rotate", "dir": "left"}] * (MAX_ACTIONS + 1)
        self._reject(actions=too_many)
        self._reject(variants=[{"actions": []}] * (MAX_VARIANTS + 1))
        # Per-variant actions get the same cap as top-level actions.
        self._reject(variants=[{"actions": too_many}])
        lines = [{"points": [{"x": 0, "y": 0}]}] * 201
        self._reject(actions=[{"op": "layout", "lines": lines}])
        self._reject(actions=[{"op": "formula", "axis": "x", "expr": "x" * 5001}])

    def test_variant_shape(self) -> None:
        self._reject(variants=["rotated"])
        self._reject(variants=[{"label": 7, "actions": []}])
        # The registry envelope tolerates extra keys on a variant object (allowUnknown);
        # only ops are strict about unknown fields.
        plan = parse_op_plan(_plan_json(variants=[{"label": "x", "actions": [], "seed": 1}]))
        self.assertEqual(plan.variants[0].label, "x")
