"""Op-plan parsing, acceptance side: what a valid plan may look like (contract §1)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.llm import OpPlan, Variant, parse_op_plan
from tests.stubs import _plan_json


class ParseOpPlanAcceptanceTest(unittest.TestCase):
    def test_bare_json_plan(self) -> None:
        plan = parse_op_plan(
            _plan_json(actions=[{"op": "rotate", "dir": "right"}])
        )
        self.assertIsInstance(plan, OpPlan)
        self.assertEqual(plan.reply, "ok")
        # `times` defaults to 1 when omitted.
        self.assertEqual(plan.actions, [{"op": "rotate", "dir": "right", "times": 1}])
        self.assertEqual(plan.variants, [])
        self.assertEqual(plan.warnings, [])

    def test_markdown_fences_are_stripped(self) -> None:
        text = "```json\n%s\n```" % _plan_json(reply="fenced")
        self.assertEqual(parse_op_plan(text).reply, "fenced")

    def test_first_balanced_object_amid_prose(self) -> None:
        text = "Sure! Here is the plan:\n%s\nHope that helps." % _plan_json(
            reply="embedded", actions=[{"op": "filter", "mode": "bw"}]
        )
        plan = parse_op_plan(text)
        self.assertEqual(plan.reply, "embedded")
        self.assertEqual(plan.actions, [{"op": "filter", "mode": "bw"}])

    def test_braces_inside_strings_do_not_break_extraction(self) -> None:
        plan = parse_op_plan(_plan_json(reply='look: { "not a plan" }'))
        self.assertEqual(plan.reply, 'look: { "not a plan" }')

    def test_chat_only_when_no_json(self) -> None:
        plan = parse_op_plan("  Just chatting, no JSON here.  ")
        self.assertEqual(plan.reply, "Just chatting, no JSON here.")
        self.assertEqual(plan.actions, [])
        self.assertEqual(plan.variants, [])

    def test_unknown_op_dropped_with_warning(self) -> None:
        plan = parse_op_plan(
            _plan_json(
                actions=[{"op": "resize", "w": 100}, {"op": "rotate", "dir": "left"}]
            )
        )
        self.assertEqual(len(plan.actions), 1)
        self.assertEqual(plan.actions[0]["op"], "rotate")
        self.assertEqual(plan.warnings, ['unknown op "resize" dropped'])
        self.assertIn('unknown op "resize" dropped', plan.reply)

    def test_version_other_than_1_is_ignored(self) -> None:
        self.assertEqual(parse_op_plan(_plan_json(version=2)).reply, "ok")
        self.assertEqual(parse_op_plan('{"reply":"no version"}').reply, "no version")

    def test_crop_token_grammar(self) -> None:
        spec = {"x1": "10%", "x2": "-10%", "y1": "0", "y2": "2.5cm"}
        plan = parse_op_plan(_plan_json(actions=[{"op": "crop", "spec": spec}]))
        self.assertEqual(plan.actions[0]["spec"], spec)
        for token in ("5px", "1in", ".5", "-3", "90%"):
            parse_op_plan(_plan_json(actions=[{"op": "crop", "spec": {"x1": token}}]))

    def test_crop_aspect_accepted(self) -> None:
        spec = {"x1": "10%", "aspect": "4:3"}
        plan = parse_op_plan(_plan_json(actions=[{"op": "crop", "spec": spec}]))
        self.assertEqual(plan.actions[0]["spec"], spec)
        # Aspect alone satisfies the at-least-one-key rule.
        plan = parse_op_plan(
            _plan_json(actions=[{"op": "crop", "spec": {"aspect": "16:9"}}])
        )
        self.assertEqual(plan.actions[0]["spec"], {"aspect": "16:9"})

    def test_action_level_aspect_folds_into_the_spec(self) -> None:
        # §3.2 tolerance: "aspect" BESIDE "spec" is accepted and folded in.
        plan = parse_op_plan(
            _plan_json(
                actions=[{"op": "crop", "spec": {"x1": "10%"}, "aspect": "3:4"}]
            )
        )
        self.assertEqual(plan.actions[0]["spec"], {"x1": "10%", "aspect": "3:4"})
        # Beside an EMPTY spec it still satisfies the at-least-one-key rule.
        plan = parse_op_plan(
            _plan_json(actions=[{"op": "crop", "spec": {}, "aspect": "16:9"}])
        )
        self.assertEqual(plan.actions[0]["spec"], {"aspect": "16:9"})
        # An EQUAL duplicate folds silently.
        plan = parse_op_plan(
            _plan_json(
                actions=[{"op": "crop", "spec": {"aspect": "1:1"}, "aspect": "1:1"}]
            )
        )
        self.assertEqual(plan.actions[0]["spec"], {"aspect": "1:1"})

    def test_filter_custom_with_tint(self) -> None:
        plan = parse_op_plan(
            _plan_json(actions=[{"op": "filter", "mode": "custom", "tint": "#A1b2C3"}])
        )
        self.assertEqual(plan.actions[0]["tint"], "#A1b2C3")

    def test_layout_lines_pass_through(self) -> None:
        line = {
            "points": [{"x": 10, "y": 20}, {"x": 120, "y": 40}],
            "color": "#FFFF00",
            "thickness": 2,
            "pointSize": 4,
            "style": "solid",
            "locked": False,
            "fillColor": "transparent",
        }
        plan = parse_op_plan(_plan_json(actions=[{"op": "layout", "lines": [line]}]))
        self.assertEqual(plan.actions[0]["lines"], [line])

    def test_variants_with_default_labels(self) -> None:
        plan = parse_op_plan(
            _plan_json(
                variants=[
                    {"label": "rotated", "actions": [{"op": "rotate", "dir": "right"}]},
                    {"actions": [{"op": "filter", "mode": "sepia"}]},
                ]
            )
        )
        self.assertEqual(len(plan.variants), 2)
        self.assertIsInstance(plan.variants[0], Variant)
        self.assertEqual(plan.variants[0].label, "rotated")
        self.assertEqual(plan.variants[1].label, "variant 2")

    def test_frame_parses_but_is_execution_gated(self) -> None:
        plan = parse_op_plan(_plan_json(actions=[{"op": "frame", "index": 3}]))
        self.assertEqual(plan.actions[0], {"op": "frame", "index": 3})
        plan = parse_op_plan(_plan_json(actions=[{"op": "frame", "indices": [0, 30]}]))
        self.assertEqual(plan.actions[0], {"op": "frame", "indices": [0, 30]})
