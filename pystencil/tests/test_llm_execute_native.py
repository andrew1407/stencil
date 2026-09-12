"""Executing a plan against a REAL Editor; self-skips without a C++ compiler."""

from __future__ import annotations

from tests.nativecase import NativeCase

from pystencil.editor import Editor
from pystencil.llm import execute_op_plan, parse_op_plan
from tests.stubs import _StubClient, _plan_json


class ExecuteOpPlanNativeTest(NativeCase):
  """Integration: a validated plan drives a REAL Editor over the native core.

  Builds the shared library on demand (build.py, via get_core) and self-skips
  when no C++ compiler is available — the test_core.py pattern.
  """

  def test_plan_executes_end_to_end(self) -> None:
    editor = Editor().blank(32, 48, color="#3060c0")
    plan = parse_op_plan(
      _plan_json(
        reply="rotated + two variants",
        actions=[{"op": "rotate", "dir": "right"}],
        variants=[
          {"label": "bw", "actions": [{"op": "filter", "mode": "bw"}]},
          {
            "label": "cropped",
            "actions": [{"op": "crop", "spec": {"x1": "25%", "x2": "-25%"}}],
          },
        ],
      )
    )
    outputs = execute_op_plan(plan, editor)
    self.assertEqual(len(outputs), 3)  # working + 2 variants
    working, bw, cropped = outputs
    self.assertEqual((working.width, working.height), (48, 32))  # quarter turn
    self.assertEqual(editor.image_size, (48, 32))  # editor mutated in place
    # The bw variant collapsed the channels to grayscale.
    self.assertEqual((bw.width, bw.height), (48, 32))
    for i in range(bw.pixel_count):
      d = i * 4
      self.assertTrue(bw.data[d] == bw.data[d + 1] == bw.data[d + 2])
    # The cropped variant lost width but kept height.
    self.assertLess(cropped.width, 48)
    self.assertEqual(cropped.height, 32)

  def test_formula_page_and_layout_ops(self) -> None:
    editor = Editor().blank(32, 32)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "formula", "axis": "x", "expr": "x*2+10"},
          {"op": "page", "format": "b5"},
          {
            "op": "layout",
            "lines": [{"points": [{"x": 2, "y": 2}, {"x": 20, "y": 20}]}],
          },
        ]
      )
    )
    outputs = execute_op_plan(plan, editor)
    self.assertEqual(len(outputs), 1)
    self.assertTrue(editor.allow_formulas)
    self.assertEqual(editor.apply_formula("x", 2.0), 14.0)
    self.assertEqual(editor.page_format, "B5")  # canonicalized by the editor
    self.assertEqual(len(editor.layout().lines), 1)

  def test_prompt_delegate_executes_against_editor(self) -> None:
    editor = Editor().blank(32, 48)
    reply, outputs = editor.prompt(
      "rotate it", llm=_StubClient(_plan_json(
        reply="done", actions=[{"op": "rotate", "dir": "right"}]
      ))
    )
    self.assertEqual(reply, "done")
    self.assertEqual(len(outputs), 1)
    self.assertEqual(editor.image_size, (48, 32))
