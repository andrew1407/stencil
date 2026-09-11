"""§1/§2.1: a misplaced top-level-only or console op costs its variant, not the plan."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.llm import LlmPlanError, execute_op_plan, parse_op_plan
from tests.stubs import _StubEditor, _plan_json


class MisplacedVariantOpTest(unittest.TestCase):
    """§1's one exception: a variant holding a top-level-only or console-settings op
    is DROPPED with a warning — the rest of the plan (top-level actions and the
    well-formed variants) still runs. Losing a whole turn to one misplaced op taught
    the user nothing and cost them everything."""

    def setUp(self) -> None:
        _StubEditor.instances = []
        self.editor = _StubEditor()

    def _run(self, text: str):
        plan = parse_op_plan(text)
        return plan, execute_op_plan(plan, self.editor)

    def test_rest_of_the_plan_still_runs(self):
        plan, outputs = self._run(
            _plan_json(
                actions=[{"op": "rotate", "dir": "right"}],
                variants=[
                    {"label": "wiped", "actions": [{"op": "clear"}]},
                    {"label": "tinted", "actions": [{"op": "filter", "mode": "sepia"}]},
                ],
            )
        )
        # The top-level action applied and the good variant produced its image.
        self.assertEqual(self.editor.calls[0], ("rotate", 1))
        self.assertEqual([v.label for v in plan.variants], ["tinted"])
        self.assertEqual(len(outputs), 2)  # working image + the surviving variant
        self.assertEqual(
            plan.warnings,
            ['dropped variant 1 ("wiped") — the "clear" op adjusts the console, not '
             "the image, and cannot appear in a variant"],
        )
        self.assertIn("[warning] dropped variant 1 (\"wiped\")", plan.reply)

    def test_only_a_bad_variant_is_a_reply_plus_warning(self):
        plan, outputs = self._run(
            _plan_json(reply="here you go", variants=[{"label": "wiped", "actions": [{"op": "clear"}]}])
        )
        self.assertEqual(outputs, [])
        self.assertEqual(self.editor.calls, [])  # nothing rendered, nothing failed
        self.assertEqual(plan.variants, [])
        self.assertTrue(plan.reply.startswith("here you go"))
        self.assertEqual(len(plan.warnings), 1)

    def test_unlabelled_variant_is_named_by_index(self):
        plan = parse_op_plan(
            _plan_json(
                variants=[
                    {"actions": [{"op": "rotate", "dir": "left"}]},
                    {"actions": [{"op": "save", "name": "x"}]},
                ]
            )
        )
        self.assertEqual([v.label for v in plan.variants], ["variant 1"])
        self.assertEqual(
            plan.warnings,
            ['dropped variant 2 — the "save" op is top-level only and cannot appear '
             "in a variant"],
        )

    def test_other_strictness_is_untouched(self):
        # Unknown op inside a variant: skip + warn, the variant itself survives.
        plan = parse_op_plan(
            _plan_json(
                variants=[{"label": "v", "actions": [{"op": "accent", "color": "#fff000"},
                                                     {"op": "rotate", "dir": "left"}]}]
            )
        )
        self.assertEqual(len(plan.variants), 1)
        self.assertEqual(plan.warnings, ['unknown op "accent" dropped'])
        # A KNOWN op with bad params still fails the whole plan, in a variant…
        with self.assertRaises(LlmPlanError):
            parse_op_plan(
                _plan_json(variants=[{"actions": [{"op": "rotate", "dir": "sideways"}]}])
            )
        # …and at top level.
        with self.assertRaises(LlmPlanError):
            parse_op_plan(_plan_json(actions=[{"op": "clear", "hard": True}]))

    def test_ask_option_preview_never_fails_the_plan(self):
        # A console has nowhere to show previews: an option's actions are validated
        # (§11) but never rendered — a misplaced op costs nothing but the preview,
        # which was going to be dropped anyway, so ONE card-level note results.
        plan = parse_op_plan(
            _plan_json(
                ask={
                    "question": "which?",
                    "options": [
                        {"label": "wipe", "actions": [{"op": "clear"}]},
                        {"label": "keep"},
                    ],
                }
            )
        )
        self.assertEqual([o.label for o in plan.ask.options], ["wipe", "keep"])
        self.assertEqual(len(plan.warnings), 1)
        self.assertIn("option previews", plan.warnings[0])
