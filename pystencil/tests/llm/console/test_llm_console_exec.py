"""Executing history and §10 console ops through the attached hooks."""

from __future__ import annotations

import unittest

from pystencil.llm import execute_op_plan, parse_op_plan
from tests.helpers.stubs import _StubConsole, _StubEditor, _plan_json


class HistoryOpExecutionTest(unittest.TestCase):
  """§2 undo/redo/reset + the new §2 forms, dispatched over the stub editor."""

  def setUp(self) -> None:
    _StubEditor.instances = list()
    self.editor = _StubEditor()

  def _run(self, actions) -> "OpPlan":
    plan = parse_op_plan(_plan_json(actions=actions))
    execute_op_plan(plan, self.editor)
    return plan

  def test_undo_steps_and_the_run_out_note(self):
    self.editor.undo_budget = 2
    plan = self._run([{"op": "undo", "steps": 4}])
    # Walked twice, failed the third probe, stopped — with the §2 note.
    self.assertEqual(self.editor.calls.count(("undo",)), 3)
    self.assertIn("undo stopped after 2 step(s) — no more history entries", plan.warnings)

  def test_redo_within_budget_is_silent(self):
    self.editor.redo_budget = 3
    plan = self._run([{"op": "redo", "steps": 2}])
    self.assertEqual(self.editor.calls.count(("redo",)), 2)
    self.assertEqual(plan.warnings, [])

  def test_reset_dispatch(self):
    self._run([{"op": "reset"}])
    self.assertIn(("reset",), self.editor.calls)

  def test_formula_enabled_dispatches_to_set_allow_formulas(self):
    self._run([{"op": "formula", "enabled": False}])
    self.assertIn(("set_allow_formulas", False), self.editor.calls)
    self._run([{"op": "formula", "axis": "x", "expr": ""}])
    self.assertIn(("set_formula", "x", ""), self.editor.calls)

  def test_page_custom_dims_dispatch(self):
    self._run([{"op": "page", "width": 21.0, "height": 29.7}])
    self.assertIn(("set_page_format", "custom", 21.0, 29.7), self.editor.calls)

  def test_crop_album_dispatch(self):
    self._run([{"op": "crop", "spec": {"x1": "10%", "album": True}}])
    self.assertIn(("crop", "x1=10%", "album"), self.editor.calls)
    self._run([{"op": "crop", "spec": {"x1": "10%"}}])
    self.assertIn(("crop", "x1=10%"), self.editor.calls)


class ConsoleOpExecutionTest(unittest.TestCase):
  """Console-profile ops execute through the attached console hooks, in plan
  order; without a console they are skipped with a note (the library API)."""

  def setUp(self) -> None:
    _StubEditor.instances = list()
    self.editor = _StubEditor()

  def test_without_a_console_ops_are_skipped_with_a_note(self):
    plan = parse_op_plan(
      _plan_json(actions=[{"op": "connect", "server": "a.example"}, {"op": "clear"}])
    )
    execute_op_plan(plan, self.editor)
    self.assertEqual(len(plan.warnings), 2)
    for w in plan.warnings:
      self.assertIn("no console session is attached", w)
    self.assertEqual(self.editor.calls, [("result",)])  # nothing console-y ran

  def test_hooks_run_in_plan_order_between_edits(self):
    console = _StubConsole()
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "openUrl", "url": "https://a.example/x.png"},
          {"op": "rotate", "dir": "right"},
          {"op": "delete", "path": "old.stencil"},
        ]
      )
    )
    execute_op_plan(plan, self.editor, console=console)
    self.assertEqual([c[0] for c in console.calls], ["openUrl", "delete"])
    self.assertEqual(console.calls[0][1]["url"], "https://a.example/x.png")
    self.assertIn(("rotate", 1), self.editor.calls)
    self.assertEqual(plan.warnings, [])

  def test_clear_chat_reaches_the_hook_and_needs_a_console(self):
    # With a console the hook is only a RECORDER (the REPL defers the confirm to end of turn);
    # without one the op is a skip note — a one-shot Editor.prompt has nothing to clear.
    console = _StubConsole()
    plan = parse_op_plan(_plan_json(actions=[{"op": "clearChat"}]))
    execute_op_plan(plan, self.editor, console=console)
    self.assertEqual(console.calls, [("clearChat", {"op": "clearChat"})])
    self.assertEqual(plan.warnings, [])
    bare = parse_op_plan(_plan_json(actions=[{"op": "clearChat"}]))
    execute_op_plan(bare, _StubEditor())
    self.assertEqual(len(bare.warnings), 1)
    self.assertIn("no console session is attached", bare.warnings[0])

  def test_a_hook_miss_becomes_a_plan_warning(self):
    console = _StubConsole(notes={"connect": 'skipped connect — "x" is not yours'})
    plan = parse_op_plan(_plan_json(actions=[{"op": "connect", "server": "x"}]))
    execute_op_plan(plan, self.editor, console=console)
    self.assertEqual(plan.warnings, ['skipped connect — "x" is not yours'])
    self.assertIn("[warning]", plan.reply)
