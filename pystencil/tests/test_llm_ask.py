"""§11 interactive replies: validating an ask card, rendering it, answering by number."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.llm import (
    LLM_SYSTEM_PROMPT,
    LlmPlanError,
    MAX_ASK_ANSWER,
    MAX_ASK_OPTIONS,
    MAX_ASK_QUESTION,
    OpPlan,
    ask_answer_text,
    format_ask,
    parse_op_plan,
)


class AskCardTests(unittest.TestCase):
    """The §11 interactive-reply card: strict parsing, the console's label-only rendering,
    and answering by number."""

    @staticmethod
    def _plan(ask_json: str) -> OpPlan:
        return parse_op_plan('{"version":1,"reply":"pick","ask":%s}' % ask_json)

    def test_card_parses_with_defaults(self):
        plan = self._plan('{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}')
        self.assertIsNotNone(plan.ask)
        self.assertEqual(plan.ask.question, "Which tint?")
        self.assertFalse(plan.ask.multi)
        self.assertFalse(plan.ask.allow_custom)
        self.assertEqual([o.label for o in plan.ask.options], ["Sepia", "B&W"])

    def test_multi_custom_row_and_trimming(self):
        plan = self._plan(
            '{"question":"  Which?  ","mode":"multi","allowCustom":true,'
            '"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}'
        )
        self.assertTrue(plan.ask.multi)
        self.assertTrue(plan.ask.allow_custom)
        self.assertEqual(plan.ask.question, "Which?")
        self.assertEqual(plan.ask.custom_label, "Other")
        self.assertEqual(plan.ask.options[0].label, "A")

    def test_no_card_on_ordinary_or_chat_only_turns(self):
        self.assertIsNone(parse_op_plan('{"version":1,"reply":"hi","actions":[]}').ask)
        self.assertIsNone(parse_op_plan("just chatting").ask)

    def test_previews_are_dropped_with_one_note_never_the_option(self):
        plan = self._plan(
            '{"question":"Which?","options":['
            '{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},'
            '{"label":"Web","image":{"url":"https://e/x.png"}}]}'
        )
        self.assertEqual([o.label for o in plan.ask.options], ["Sepia", "Web"])
        notes = [w for w in plan.warnings if "option previews" in w]
        self.assertEqual(len(notes), 1)   # one per CARD, not per option

    def test_malformed_cards_reject_the_whole_plan(self):
        bad = [
            '"hello"',
            '{"options":[{"label":"A"},{"label":"B"}]}',
            '{"question":"   ","options":[{"label":"A"},{"label":"B"}]}',
            '{"question":"%s","options":[{"label":"A"},{"label":"B"}]}' % ("x" * (MAX_ASK_QUESTION + 1)),
            '{"question":"Q","options":[]}',
            '{"question":"Q","options":[{"label":"only"}]}',
            '{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}',
            '{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}',
            '{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}',
            '{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}',
            '{"question":"Q","options":[{"label":""},{"label":"B"}]}',
            '{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}',
            '{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}',
        ]
        for ask in bad:
            with self.subTest(ask=ask[:48]):
                with self.assertRaises(LlmPlanError):
                    self._plan(ask)

    def test_five_options_is_the_cap(self):
        plan = self._plan(
            '{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"}]}'
        )
        self.assertEqual(len(plan.ask.options), MAX_ASK_OPTIONS)

    def test_answer_by_number(self):
        card = self._plan('{"question":"Q","mode":"multi","options":[{"label":"Sepia"},{"label":"B&W"},{"label":"Blue"}]}').ask
        self.assertEqual(ask_answer_text(card, " 2 "), "B&W")
        self.assertEqual(ask_answer_text(card, "1, 3"), "Sepia, Blue")
        self.assertEqual(ask_answer_text(card, "3 1"), "Blue, Sepia")   # the user's order is kept
        self.assertEqual(ask_answer_text(card, "2,2"), "B&W")           # a repeat is one pick

    def test_anything_that_is_not_a_selection_stays_plain_text(self):
        single = self._plan('{"question":"Q","options":[{"label":"A"},{"label":"B"}]}').ask
        for typed in ["0", "3", "make it warmer", "", "   ", "1,2"]:
            with self.subTest(typed=typed):
                self.assertIsNone(ask_answer_text(single, typed))   # 1,2 = several picks at a pick-one card
        self.assertIsNone(ask_answer_text(None, "1"))

    def test_the_largest_possible_answer_still_fits_under_the_cap(self):
        # 5 options x 80-char labels + separators is the most a card can produce (408 chars),
        # so a real answer is never truncated — the cap is a backstop, not a normal path.
        label = "x" * 80
        options = ",".join('{"label":"%s"}' % label for _ in range(MAX_ASK_OPTIONS))
        card = self._plan('{"question":"Q","mode":"multi","options":[%s]}' % options).ask
        answer = ask_answer_text(card, "1,2,3,4,5")
        self.assertEqual(len(answer), MAX_ASK_OPTIONS * 80 + (MAX_ASK_OPTIONS - 1) * 2)
        self.assertLessEqual(len(answer), MAX_ASK_ANSWER)

    def test_format_ask_is_a_numbered_list(self):
        card = self._plan('{"question":"Which?","allowCustom":true,"options":[{"label":"A"},{"label":"B"}]}').ask
        text = format_ask(card)
        self.assertIn("Which?", text)
        self.assertIn("1. A", text)
        self.assertIn("2. B", text)
        self.assertIn("or type your own", text)

    def test_the_system_prompt_teaches_ask(self):
        self.assertIn('"ask"', LLM_SYSTEM_PROMPT)

    def test_the_system_prompt_teaches_layout_tracing_quality(self):
        self.assertIn("The attached image is the ground truth", LLM_SYSTEM_PROMPT)
        self.assertIn("trace ONLY what the user", LLM_SYSTEM_PROMPT)
        # §4 lean rewrite: point budget, no templates, edge-map sentence,
        # and the per-feature stroke rules.
        self.assertIn("about 8-16 for an organic shape, 4-8 for a small feature", LLM_SYSTEM_PROMPT)
        self.assertIn("never draw a remembered template", LLM_SYSTEM_PROMPT)
        self.assertIn("edge-map attachment, when present, shows the true edges", LLM_SYSTEM_PROMPT)
        self.assertIn("two separate CLOSED lines", LLM_SYSTEM_PROMPT)
        self.assertIn("a closed almond", LLM_SYSTEM_PROMPT)
        self.assertIn("outline every ear the hair leaves visible", LLM_SYSTEM_PROMPT)
        for stale in ("remembered template of the thing", "up to 40", "artist drafts",
                      "landmark mask", "extreme points first", "an ear hidden under hair"):
            self.assertNotIn(stale, LLM_SYSTEM_PROMPT)


if __name__ == "__main__":
    unittest.main()
