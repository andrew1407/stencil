"""Validation of the history ops and the newer action forms."""

from __future__ import annotations

import unittest

from pystencil.llm import LlmPlanError, parse_op_plan
from tests.stubs import _plan_json


class HistoryOpValidationTest(unittest.TestCase):
  """§2 undo/redo/reset: shapes, bounds, and the top-level-only rule."""

  def test_undo_redo_shapes(self):
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "undo"}, {"op": "redo", "steps": 20}])
    )
    self.assertEqual(plan.actions[0], {"op": "undo", "steps": 1})  # default 1
    self.assertEqual(plan.actions[1], {"op": "redo", "steps": 20})

  def test_undo_redo_steps_bounds(self):
    for bad in (0, 21, "2", 1.5, True):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[{"op": "undo", "steps": bad}]))
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "redo", "count": 2}]))

  def test_reset_takes_no_fields(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "reset"}]))
    self.assertEqual(plan.actions, [{"op": "reset"}])
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "reset", "hard": True}]))

  def test_undo_redo_are_top_level_only(self):
    # §1: the misplaced op costs that VARIANT its place, never the whole plan.
    for op in ("undo", "redo"):
      plan = parse_op_plan(
        _plan_json(variants=[{"label": "v", "actions": [{"op": op}]}])
      )
      self.assertEqual(plan.variants, [])
      self.assertEqual(
        plan.warnings,
        ['dropped variant 1 ("v") — the "%s" op is top-level only and cannot '
        "appear in a variant" % op],
      )


class NewActionFormsValidationTest(unittest.TestCase):
  """The §2 forms this change adds: formula enabled/empty, page custom dims,
  blank width/height, and crop's console-only album spec key."""

  def test_formula_enabled_alone(self):
    for flag in (True, False):
      plan = parse_op_plan(_plan_json(actions=[{"op": "formula", "enabled": flag}]))
      self.assertEqual(plan.actions, [{"op": "formula", "enabled": flag}])
    with self.assertRaises(LlmPlanError):  # never beside axis/expr
      parse_op_plan(
        _plan_json(actions=[{"op": "formula", "enabled": False, "axis": "x", "expr": ""}])
      )
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "formula", "enabled": "off"}]))

  def test_formula_empty_expr_clears_that_axis(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "formula", "axis": "y", "expr": ""}]))
    self.assertEqual(plan.actions, [{"op": "formula", "axis": "y", "expr": ""}])

  def test_page_custom_dims(self):
    plan = parse_op_plan(_plan_json(actions=[{"op": "page", "width": 20, "height": 30.5}]))
    self.assertEqual(plan.actions, [{"op": "page", "width": 20.0, "height": 30.5}])
    # Exactly one of the two forms; dims come in pairs; cm range is 0.1..500.
    for bad in (
      {"op": "page", "format": "a4", "width": 20, "height": 30},
      {"op": "page", "width": 20},
      {"op": "page", "height": 30},
      {"op": "page", "width": 0.05, "height": 30},
      {"op": "page", "width": 20, "height": 501},
      {"op": "page", "width": "20", "height": 30},
    ):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[bad]))

  def test_blank_dims_ride_as_width_height(self):
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "blank", "color": "#ffffff", "width": 20, "height": 30}])
    )
    self.assertEqual(
      plan.actions,
      [{"op": "blank", "color": "#ffffff", "width": 20.0, "height": 30.0}],
    )
    for bad in (
      {"op": "blank", "color": "white", "width": 20},
      {"op": "blank", "color": "white", "width": 20, "height": 600},
    ):
      with self.assertRaises(LlmPlanError):
        parse_op_plan(_plan_json(actions=[bad]))

  def test_crop_album_spec_key(self):
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "crop", "spec": {"x1": "10%", "album": True}}])
    )
    self.assertEqual(plan.actions[0]["spec"], {"x1": "10%", "album": True})
    # album: false is dropped from the normalized spec (it means "no derivation").
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "crop", "spec": {"x1": "10%", "album": False}}])
    )
    self.assertEqual(plan.actions[0]["spec"], {"x1": "10%"})
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "crop", "spec": {"album": "yes"}}]))
