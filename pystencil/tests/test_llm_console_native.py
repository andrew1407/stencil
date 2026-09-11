"""§10 console ops against a REAL Editor; self-skips without a C++ compiler."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.editor import Editor
from pystencil.layout import Line, Point
from pystencil.llm import execute_op_plan, parse_op_plan
from tests.stubs import _plan_json


class ConsoleOpNativeTest(unittest.TestCase):
    """The new §2 forms against the REAL editor + core."""

    @classmethod
    def setUpClass(cls) -> None:
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as exc:  # noqa: BLE001 - any failure means "no native lib"
            raise unittest.SkipTest("native core unavailable: %s" % exc)

    def _run(self, editor, actions):
        plan = parse_op_plan(_plan_json(actions=actions))
        execute_op_plan(plan, editor)
        return plan

    def test_undo_redo_reset_walk_the_editor_history(self):
        editor = Editor().blank(40, 30)
        editor.rotate(1)
        self._run(editor, [{"op": "undo"}])
        self.assertEqual(editor.image_size, (40, 30))
        self._run(editor, [{"op": "redo"}])
        self.assertEqual(editor.image_size, (30, 40))
        self._run(editor, [{"op": "reset"}])
        self.assertEqual(editor.image_size, (40, 30))

    def test_undo_past_the_history_notes_and_stops(self):
        editor = Editor().blank(40, 30)
        editor.rotate(1)
        plan = self._run(editor, [{"op": "undo", "steps": 5}])
        self.assertEqual(editor.image_size, (40, 30))
        self.assertTrue(any("undo stopped after 1 step(s)" in w for w in plan.warnings))

    def test_formula_enabled_false_switches_formulas_off(self):
        editor = Editor().blank(10, 10)
        editor.set_formula("x", "x*2")
        self.assertTrue(editor.allow_formulas)
        self._run(editor, [{"op": "formula", "enabled": False}])
        self.assertFalse(editor.allow_formulas)
        # The expression survived: re-enabling restores it (§2's "restoring identity"
        # is about application, not erasure).
        self._run(editor, [{"op": "formula", "enabled": True}])
        self.assertTrue(editor.allow_formulas)
        self.assertEqual(editor.apply_formula("x", 3.0), 6.0)

    def test_formula_empty_expr_clears_that_axis(self):
        editor = Editor().blank(10, 10)
        editor.set_formula("x", "x*2")
        self._run(editor, [{"op": "formula", "axis": "x", "expr": ""}])
        self.assertEqual(editor.apply_formula("x", 3.0), 3.0)  # identity again

    def test_page_custom_dims_set_the_custom_page(self):
        editor = Editor().blank(10, 10)
        self._run(editor, [{"op": "page", "width": 21.0, "height": 29.7}])
        self.assertEqual(editor.page_format, "custom")
        self.assertEqual((editor.custom_page_width, editor.custom_page_height), (21.0, 29.7))

    def test_blank_dims_render_at_the_default_dpi(self):
        from pystencil.core import get_core

        editor = Editor()
        self._run(editor, [{"op": "blank", "color": "#ffffff", "width": 20, "height": 30}])
        self.assertEqual(editor.image_size, get_core().default_blank_size_px(20.0, 30.0))

    def test_clear_via_hook_leaves_the_editor_empty(self):
        # The Editor.clear() the REPL's plan_clear hook drives: in place, same object.
        editor = Editor().blank(10, 10)
        editor.draw([Line(points=[Point(1, 1), Point(2, 2)])])

        class _Hook:
            def plan_clear(self, action):
                editor.clear()
                return None

        plan = parse_op_plan(_plan_json(actions=[{"op": "clear"}]))
        outputs = execute_op_plan(plan, editor, console=_Hook())
        self.assertFalse(editor.has_image())
        self.assertEqual(outputs, [])  # nothing left to render — and no crash
