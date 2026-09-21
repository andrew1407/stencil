"""§2.1 multi-image ops: `image` switches to a turn attachment, `save` persists."""

from __future__ import annotations

import os
import shutil
import tempfile
import unittest

from pystencil.llm import LlmPlanError, execute_op_plan, parse_op_plan
from tests.helpers.stubs import _SavingStubEditor, _StubEditor, _plan_json

# ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──


class MultiImageOpValidationTest(unittest.TestCase):
  def test_image_and_save_shapes(self) -> None:
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "image", "index": 2},
          {"op": "save", "name": "portrait 1"},
          {"op": "save"},
        ]
      )
    )
    self.assertEqual(
      plan.actions,
      [
        {"op": "image", "index": 2},
        {"op": "save", "name": "portrait 1"},
        {"op": "save"},
      ],
    )

  def test_image_index_must_be_an_integer_from_one(self) -> None:
    for index in (0, -1, 1.5, "1", True, None):
      with self.assertRaises(LlmPlanError) as cm:
        parse_op_plan(_plan_json(actions=[{"op": "image", "index": index}]))
      self.assertIn("index", str(cm.exception))

  def test_save_name_is_bounded(self) -> None:
    with self.assertRaises(LlmPlanError) as cm:
      parse_op_plan(_plan_json(actions=[{"op": "save", "name": "x" * 121}]))
    self.assertIn("120", str(cm.exception))
    with self.assertRaises(LlmPlanError):
      parse_op_plan(_plan_json(actions=[{"op": "save", "name": 7}]))

  def test_save_path_is_accepted_and_bounded(self) -> None:
    # §2.1: "path" is a valid save field everywhere (the executor here
    # notes+skips it); shape mirrors desktop/cli — string ≤ 1024, no URL.
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "save", "path": " x.stencil "}])
    )
    self.assertEqual(plan.actions, [{"op": "save", "path": "x.stencil"}])
    with self.assertRaises(LlmPlanError) as cm:
      parse_op_plan(_plan_json(actions=[{"op": "save", "path": "x" * 1025}]))
    self.assertIn("1024", str(cm.exception))
    with self.assertRaises(LlmPlanError) as cm:
      parse_op_plan(
        _plan_json(actions=[{"op": "save", "path": "https://x.example/out.png"}])
      )
    self.assertIn("not a URL", str(cm.exception))
    # An empty (or all-space) path is the same as no path at all.
    plan = parse_op_plan(_plan_json(actions=[{"op": "save", "path": "  "}]))
    self.assertEqual(plan.actions, [{"op": "save"}])

  def test_both_ops_are_top_level_only(self) -> None:
    # Misplaced inside a variant they drop THAT variant with a warning (§1).
    for action in ({"op": "image", "index": 1}, {"op": "save"}):
      plan = parse_op_plan(
        _plan_json(variants=[{"label": "v", "actions": [action]}])
      )
      self.assertEqual(plan.variants, [])
      self.assertEqual(len(plan.warnings), 1)
      self.assertIn("top-level only", plan.warnings[0])
      self.assertIn('dropped variant 1 ("v")', plan.warnings[0])


class MultiImageOpExecutionTest(unittest.TestCase):
  """Executor half: attachments, per-action skip warnings, and .stencil naming."""

  def setUp(self) -> None:
    _StubEditor.instances = list()
    self.editor = _SavingStubEditor()
    self.tmp = tempfile.mkdtemp(prefix="stencil_save_")
    self.addCleanup(shutil.rmtree, self.tmp, True)

  ATTACHMENTS = [
    ("image/png", b"CAT-BYTES", "cat.jpg"),
    ("image/png", b"DOG-BYTES", "photos/dog.png"),
  ]

  def test_image_switches_to_that_attachment_and_save_names_it(self) -> None:
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "image", "index": 2},
          {"op": "filter", "mode": "bw"},
          {"op": "save"},
        ]
      )
    )
    execute_op_plan(plan, self.editor, self.ATTACHMENTS, self.tmp)
    loads = [c for c in self.editor.calls if c[0] == "load"]
    self.assertEqual(loads, [("load", b"DOG-BYTES", "dog")])
    # The unnamed save took the ACTIVE attachment's file name, extension stripped.
    self.assertEqual(plan.saved, [os.path.join(self.tmp, "dog.stencil")])
    self.assertEqual(plan.warnings, [])
    self.assertTrue(os.path.exists(plan.saved[0]))

  def test_one_plan_saves_one_project_per_image(self) -> None:
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "image", "index": 1},
          {"op": "save"},
          {"op": "image", "index": 2},
          {"op": "save", "name": "second"},
        ]
      )
    )
    execute_op_plan(plan, self.editor, self.ATTACHMENTS, self.tmp)
    self.assertEqual(
      [os.path.basename(p) for p in plan.saved], ["cat.stencil", "second.stencil"]
    )

  def test_a_name_already_on_disk_gains_a_numbered_suffix(self) -> None:
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "image", "index": 1},
          {"op": "save"},
          {"op": "image", "index": 1},
          {"op": "save"},
        ]
      )
    )
    execute_op_plan(plan, self.editor, self.ATTACHMENTS, self.tmp)
    self.assertEqual(
      [os.path.basename(p) for p in plan.saved], ["cat.stencil", "cat 2.stencil"]
    )

  def test_save_without_a_name_or_attachment_falls_back_to_the_image_name(self) -> None:
    plan = parse_op_plan(_plan_json(actions=[{"op": "save"}]))
    execute_op_plan(plan, self.editor, save_dir=self.tmp)
    self.assertEqual([os.path.basename(p) for p in plan.saved], ["current.stencil"])

  def test_an_index_the_turn_cannot_satisfy_costs_that_action_only(self) -> None:
    plan = parse_op_plan(
      _plan_json(
        actions=[{"op": "image", "index": 3}, {"op": "filter", "mode": "sepia"}]
      )
    )
    execute_op_plan(plan, self.editor, self.ATTACHMENTS, self.tmp)
    self.assertEqual(len(plan.warnings), 1)
    self.assertIn("attached image 3", plan.warnings[0])
    self.assertIn("2 image(s)", plan.warnings[0])
    self.assertIn("[warning] Skipped switching", plan.reply)
    # The rest of the plan still ran, and nothing was loaded.
    self.assertIn(("set_filter", "sepia"), self.editor.calls)
    self.assertEqual([c for c in self.editor.calls if c[0] == "load"], [])

  def test_a_save_path_costs_the_destination_not_the_save(self) -> None:
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "save", "name": "x", "path": "~/Downloads"}])
    )
    execute_op_plan(plan, self.editor, save_dir=self.tmp)
    self.assertEqual([os.path.basename(p) for p in plan.saved], ["x.stencil"])
    self.assertEqual(len(plan.warnings), 1)
    self.assertIn("Saved to the usual place", plan.warnings[0])

  def test_save_with_nothing_loaded_is_skipped_with_a_warning(self) -> None:
    self.editor.image = False
    plan = parse_op_plan(_plan_json(actions=[{"op": "save", "name": "x"}]))
    execute_op_plan(plan, self.editor, save_dir=self.tmp)
    self.assertEqual(plan.saved, [])
    self.assertEqual(len(plan.warnings), 1)
    self.assertIn("no working image", plan.warnings[0])
