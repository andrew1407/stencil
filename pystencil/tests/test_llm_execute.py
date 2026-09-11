"""Executing a plan against a recording stub editor (no native core needed)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.layout import Line
from pystencil.llm import LlmExecutionError, execute_op_plan, parse_op_plan
from tests.stubs import _StubEditor, _plan_json


class ExecuteOpPlanStubTest(unittest.TestCase):
    def setUp(self) -> None:
        _StubEditor.instances = []
        self.editor = _StubEditor()

    def _run(self, text: str) -> list:
        return execute_op_plan(parse_op_plan(text), self.editor)

    def test_chat_only_plan_is_a_no_op(self) -> None:
        outputs = self._run("just chatting")
        self.assertEqual(outputs, [])
        self.assertEqual(self.editor.calls, [])

    def test_action_dispatch(self) -> None:
        self._run(
            _plan_json(
                actions=[
                    {"op": "crop", "spec": {"x2": "-5px", "x1": "10%"}},
                    {"op": "rotate", "dir": "left", "times": 2},
                    {"op": "rotate", "dir": "right"},
                    {"op": "filter", "mode": "sepia"},
                    {"op": "filter", "mode": "custom", "tint": "#123456"},
                    {"op": "formula", "axis": "y", "expr": "y/2"},
                    {"op": "page", "format": "b5"},
                    {"op": "blank", "color": "seashell", "format": "a5"},
                ]
            )
        )
        calls = self.editor.calls
        # The crop dict-spec is joined "k=v" with spaces in x1 y1 x2 y2 order.
        self.assertEqual(calls[0], ("crop", "x1=10% x2=-5px"))
        self.assertEqual(calls[1], ("rotate", -2))  # left = counter-clockwise
        self.assertEqual(calls[2], ("rotate", 1))  # times defaults to 1
        self.assertEqual(calls[3], ("set_filter", "sepia"))
        self.assertEqual(calls[4], ("set_filter_color", "#123456"))
        self.assertEqual(calls[5], ("set_formula", "y", "y/2"))
        self.assertEqual(calls[6], ("set_page_format", "b5"))
        self.assertEqual(calls[7], ("blank", "seashell", "a5"))
        self.assertEqual(calls[8], ("result",))  # the one updated working image

    def test_crop_aspect_rides_the_spec_string(self) -> None:
        # Editor.crop resolves aspect itself (core cropSpec) — the applier only
        # appends the token after the edge tokens.
        self._run(
            _plan_json(
                actions=[{"op": "crop", "spec": {"x1": "10%", "aspect": "4:3"}}]
            )
        )
        self.assertEqual(self.editor.calls[0], ("crop", "x1=10% aspect=4:3"))

    def test_layout_action_draws_line_objects(self) -> None:
        self._run(
            _plan_json(
                actions=[
                    {
                        "op": "layout",
                        "lines": [
                            {"points": [{"x": 1, "y": 2}, {"x": 3, "y": 4}], "style": "dashed"}
                        ],
                    }
                ]
            )
        )
        name, lines = self.editor.calls[0]
        self.assertEqual(name, "draw")
        self.assertIsInstance(lines[0], Line)
        self.assertEqual(lines[0].style, "dashed")
        self.assertEqual([(p.x, p.y) for p in lines[0].points], [(1, 2), (3, 4)])

    def test_no_working_output_without_actions(self) -> None:
        # "4 variants, empty actions => 4 result images" (contract §1).
        outputs = self._run(
            _plan_json(
                variants=[
                    {"label": "rotated", "actions": [{"op": "rotate", "dir": "right"}]},
                    {"label": "tinted", "actions": [{"op": "filter", "mode": "sepia"}]},
                ]
            )
        )
        self.assertEqual(len(outputs), 2)
        # The main editor only rendered the branch-base snapshot, no edits.
        self.assertEqual(self.editor.calls, [("result",)])
        # Each variant branched into a fresh editor loaded with the snapshot + label.
        branches = _StubEditor.instances[1:]
        self.assertEqual(len(branches), 2)
        self.assertEqual(branches[0].calls[0][0], "load")
        self.assertEqual(branches[0].calls[0][2], "rotated")
        self.assertEqual(branches[0].calls[1], ("rotate", 1))
        self.assertEqual(branches[1].calls[1], ("set_filter", "sepia"))

    def test_actions_plus_variants_yield_one_plus_n_outputs(self) -> None:
        outputs = self._run(
            _plan_json(
                actions=[{"op": "rotate", "dir": "right"}],
                variants=[{"actions": [{"op": "filter", "mode": "bw"}]}],
            )
        )
        self.assertEqual(len(outputs), 2)  # working image + 1 variant
        # The post-actions snapshot doubles as the variant base: rendered ONCE.
        self.assertEqual(self.editor.calls.count(("result",)), 1)

    def test_frame_raises_execution_error(self) -> None:
        with self.assertRaises(LlmExecutionError) as ctx:
            self._run(_plan_json(actions=[{"op": "frame", "index": 0}]))
        self.assertIn("video", str(ctx.exception))
