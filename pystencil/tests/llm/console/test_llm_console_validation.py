"""§10 console-settings op validation: keys, grammars and the surface's own limits."""

from __future__ import annotations

import unittest

from pystencil.llm import LlmPlanError, parse_op_plan
from tests.helpers.stubs import _plan_json


class ConsoleOpValidationTest(unittest.TestCase):
  """The §10 console-profile ops' validators (shape only — resolution and the
  /delete guards run at execution) and their variant ban."""

  def test_connect_disconnect_shapes(self):
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "connect", "server": " http://a.example:8090 "},
          {"op": "disconnect", "server": "a.example"},
        ]
      )
    )
    self.assertEqual(plan.actions[0], {"op": "connect", "server": "http://a.example:8090"})
    self.assertEqual(plan.actions[1], {"op": "disconnect", "server": "a.example"})
    for bad in ({"op": "connect"}, {"op": "disconnect", "server": "  "},
          {"op": "connect", "server": 3}):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[bad]))

  def test_delete_shape(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "delete", "path": "old.stencil"}]))
    self.assertEqual(plan.actions, [{"op": "delete", "path": "old.stencil"}])
    for bad in ({"op": "delete"}, {"op": "delete", "path": ""},
          {"op": "delete", "path": "x.stencil", "force": True}):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[bad]))

  def test_open_url_shape(self):
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "openUrl", "url": " https://a.example/cat.png ", "incognito": True},
          {"op": "openUrl", "url": "HTTP://b.example/x.jpg"},
        ]
      )
    )
    self.assertEqual(
      plan.actions[0],
      {"op": "openUrl", "url": "https://a.example/cat.png", "incognito": True},
    )
    self.assertEqual(
      plan.actions[1],
      {"op": "openUrl", "url": "HTTP://b.example/x.jpg", "incognito": False},
    )
    for bad in (
      {"op": "openUrl", "url": "ftp://a.example/x"},
      {"op": "openUrl", "url": "https://a.example/a b"},
      {"op": "openUrl", "url": "https://"},
      {"op": "openUrl"},
      {"op": "openUrl", "url": "https://a.example/x", "incognito": "yes"},
      {"op": "openUrl", "url": "https://a.example/x", "tab": 1},
    ):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[bad]))

  def test_clear_takes_no_fields(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "clear"}]))
    self.assertEqual(plan.actions, [{"op": "clear"}])
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "clear", "what": "image"}]))

  def test_clear_chat_takes_no_fields(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "clearChat"}]))
    self.assertEqual(plan.actions, [{"op": "clearChat"}])
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "clearChat", "keep": 2}]))

  def test_accent_reconnect_copy_stay_unknown_ops(self):
    # The three cli-console ops pystencil deliberately does NOT carry (§10:
    # no theme, no reconnect command, no clipboard) — skipped with a warning.
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "accent", "color": "#7c3aed"},
          {"op": "reconnect", "server": "a.example"},
          {"op": "copy"},
        ]
      )
    )
    self.assertEqual(plan.actions, [])
    self.assertEqual(
      plan.warnings,
      ['unknown op "accent" dropped', 'unknown op "reconnect" dropped',
      'unknown op "copy" dropped'],
    )

  def test_console_ops_cannot_appear_in_variants(self):
    # §1: the variant is dropped with a warning; the plan itself survives.
    for action in (
      {"op": "connect", "server": "a"},
      {"op": "disconnect", "server": "a"},
      {"op": "delete", "path": "x.stencil"},
      {"op": "openUrl", "url": "https://a.example/x"},
      {"op": "clear"},
      {"op": "clearChat"},
    ):
      plan = parse_op_plan(
        _plan_json(variants=[{"label": "v", "actions": [action]}])
      )
      self.assertEqual(plan.variants, [])
      self.assertEqual(
        plan.warnings,
        ['dropped variant 1 ("v") — the "%s" op adjusts the console, not the '
        "image, and cannot appear in a variant" % action["op"]],
      )
