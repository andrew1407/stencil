"""Variant rendering fans out over independent editors (`llm.execute`).

A plan's variants each branch into their own editor over their own pixel buffer, so the
renders run together — the second fan-out in the package, and the only CPU-bound one (the
core releases the GIL). A `threading.Barrier` proves the concurrency: the branches only get
past it if they are genuinely in flight at once, so a regression to a serial loop fails
instead of just being slower. Order and error reporting must stay as the serial code left them.
"""

from __future__ import annotations

import threading
import unittest

from pystencil.llm import LlmExecutionError, execute_op_plan, parse_op_plan
from pystencil.llm.execute import MAX_VARIANT_WORKERS
from tests.stubs import _StubEditor, _plan_json

_TIMEOUT = 10.0  # generous: the barrier only has to be reached, not raced


class _BranchEditor(_StubEditor):
  """A stub whose BRANCH render blocks until every sibling branch is rendering too.

  A branch is recognisable by its first call: ``execute_op_plan`` loads the base
  snapshot into it, which the root editor never does to itself.
  """

  barrier = None

  def result(self):
    if type(self).barrier is not None and self.calls and self.calls[0][0] == "load":
      type(self).barrier.wait()
    return super().result()


class VariantRenderConcurrencyTests(unittest.TestCase):
  """Variants branch into independent editors, so their renders run together while
  their outputs stay in plan order."""

  def setUp(self):
    _StubEditor.instances = list()
    _BranchEditor.barrier = None
    self.addCleanup(setattr, _BranchEditor, "barrier", None)

  @staticmethod
  def _plan(count):
    return parse_op_plan(_plan_json(
      actions=[{"op": "rotate", "dir": "right"}],
      variants=[
        {"label": "v%d" % i, "actions": [{"op": "filter", "mode": "bw"}]}
        for i in range(count)
      ],
    ))

  def test_branches_render_at_the_same_time(self):
    count = MAX_VARIANT_WORKERS
    _BranchEditor.barrier = threading.Barrier(count, timeout=_TIMEOUT)
    outputs = execute_op_plan(self._plan(count), _BranchEditor())
    self.assertEqual(len(outputs), count + 1)

  def test_outputs_and_branch_identity_follow_plan_order(self):
    execute_op_plan(self._plan(3), _BranchEditor())
    branches = _StubEditor.instances[1:]
    self.assertEqual([b.calls[0][2] for b in branches], ["v0", "v1", "v2"])

  def test_a_failing_variant_still_raises(self):
    plan = self._plan(2)
    plan.variants[1].actions = [{"op": "frame", "index": 0}]
    with self.assertRaises(LlmExecutionError):
      execute_op_plan(plan, _BranchEditor())

  def test_one_variant_needs_no_thread(self):
    seen = list()
    editor = _BranchEditor()
    original = _BranchEditor.result

    def spy(self):
      seen.append(threading.current_thread().name)
      return original(self)

    _BranchEditor.result = spy
    self.addCleanup(setattr, _BranchEditor, "result", original)
    execute_op_plan(self._plan(1), editor)
    self.assertEqual(set(seen), {threading.current_thread().name})


if __name__ == "__main__":
  unittest.main()
