"""The op-plan walk over the core ABI: what reaches core/opplan, and what comes back.

The verdicts and the result documents are the fixture walker's; these pin the binding —
the registry read Python keeps for prompt assembly against core's own resolution, and
the reply text marshalled by length with a lone surrogate replaced.
"""

from __future__ import annotations

import json

from tests.helpers.nativecase import NativeCase

from pystencil.core import get_core
from pystencil.llm import DEFAULT_CUSTOM_LABEL, FORBIDDEN_OPS, LlmPlanError, parse_op_plan
from pystencil.llm.plan.limits import PROFILE, REGISTRY
from pystencil.llm.plan.registry import ENTRIES


class OpPlanCoreTest(NativeCase):
  def test_the_registry_read_matches_cores_resolution(self):
    resolved = get_core().opplan_entries()
    self.assertEqual(resolved["surface"], "pystencil")
    self.assertEqual(resolved["profile"], PROFILE)
    self.assertEqual([e["name"] for e in resolved["entries"]], list(ENTRIES))
    for e in resolved["entries"]:
      with self.subTest(op=e["name"]):
        self.assertEqual({"bullet": e["bullet"], "flags": e["flags"]}, ENTRIES[e["name"]])
    self.assertEqual(tuple(resolved["forbidden"]), FORBIDDEN_OPS)
    self.assertFalse(resolved["hardFail"])
    self.assertEqual(resolved["limits"], REGISTRY["limits"])
    self.assertEqual(resolved["defaultCustomLabel"], DEFAULT_CUSTOM_LABEL)

  def test_the_reply_travels_by_length_past_a_nul(self):
    plan = parse_op_plan('chat\x00 {"reply":"after the nul","actions":[{"op":"rotate","dir":"left"}]}')
    self.assertEqual(plan.reply, "after the nul")
    self.assertEqual(plan.actions, [{"op": "rotate", "dir": "left", "times": 1}])

  def test_a_lone_surrogate_arrives_as_one_replacement_char(self):
    plan = parse_op_plan('{"reply":"a\ud800b","actions":[{"op":"save","name":"%s"}]}' % ("\udc00" * 120))
    self.assertEqual(plan.reply, "a�b")
    self.assertEqual(plan.actions, [{"op": "save", "name": "�" * 120}])

  def test_an_empty_op_name_is_an_unknown_op(self):
    plan = parse_op_plan(json.dumps({"reply": "x", "actions": [{"op": ""}]}))
    self.assertEqual(plan.warnings, ['Skipped unknown operation ""'])

  def test_a_hostile_nest_is_refused_never_raised(self):
    deep = '{"reply":"x","z":' + "[" * 10000 + "]" * 10000 + "}"
    with self.assertRaises(LlmPlanError) as cm:
      parse_op_plan(deep)
    self.assertEqual(str(cm.exception), "Invalid plan: the plan's JSON nests deeper than 64 levels")

  def test_a_dropped_variant_keeps_its_place_in_the_names(self):
    plan = parse_op_plan(json.dumps({"reply": "x", "variants": [
      {"actions": [{"op": "undo"}]},
      {"label": "  ", "actions": [{"op": "filter", "mode": "bw"}]},
    ]}))
    self.assertEqual([v.label for v in plan.variants], ["variant 2"])
    self.assertEqual(plan.warnings, [
      'Dropped variant 1 ("variant 1") — "undo" steps the live edit history — a top-level '
      "action only, not allowed inside variants or previews; the rest of the plan ran"])
