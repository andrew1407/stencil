"""Variant rendering fans out over independent editors (`llm.execute`).

A plan's variants each branch into their own editor over their own pixel buffer, so the
renders run together — the second fan-out in the package, and the only CPU-bound one (the
core releases the GIL). A `threading.Barrier` proves the concurrency: the branches only get
past it if they are genuinely in flight at once, so a regression to a serial loop fails
instead of just being slower. Order and error reporting must stay as the serial code left them.
"""

from __future__ import annotations

import threading
import time
import unittest

from pystencil import _native, core as core_module
from pystencil.editor import Editor
from pystencil.editor import editor as editor_module
from pystencil.llm import LlmExecutionError, execute_op_plan, parse_op_plan
from pystencil.llm.plan.execute import MAX_VARIANT_WORKERS
from tests.helpers.nativecase import NativeCase
from tests.helpers.stubs import _StubEditor, _plan_json

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


class _BarrierEditor(Editor):
  """A REAL editor whose branch renders meet at a barrier before they answer.

  The stub suite above proves the plumbing; this one puts four live cores on four
  threads at once, which is what the singleton guard has to survive.
  """

  barrier = None
  branches: list = list()

  def __init__(self, core=None) -> None:
    super().__init__(core)
    self.is_root = False
    type(self).branches.append(self)

  def result(self, with_lines: bool = True):
    if not self.is_root and type(self).barrier is not None: type(self).barrier.wait()
    return super().result(with_lines)


class RealCoreVariantTests(NativeCase):
  """Variants branch on the compiled core, so the branch editors must not each go
  looking for the process singleton from their own thread."""

  def setUp(self):
    _BarrierEditor.branches = list()
    _BarrierEditor.barrier = None
    self.addCleanup(setattr, _BarrierEditor, "barrier", None)

  def _root(self):
    root = _BarrierEditor(self.core).blank(16, 12, "white")
    root.is_root = True
    return root

  @staticmethod
  def _plan(count):
    return parse_op_plan(_plan_json(
      actions=[{"op": "rotate", "dir": "right"}],
      variants=[
        {"label": "v%d" % i, "actions": [{"op": "filter", "mode": "bw"}]}
        for i in range(count)
      ],
    ))

  def test_branches_render_together_on_the_real_core(self):
    count = MAX_VARIANT_WORKERS
    _BarrierEditor.barrier = threading.Barrier(count, timeout=_TIMEOUT)
    outputs = execute_op_plan(self._plan(count), self._root())
    self.assertEqual(len(outputs), count + 1)
    self.assertEqual([(im.width, im.height) for im in outputs], [(12, 16)] * (count + 1))

  def test_a_branch_never_loads_a_core_of_its_own(self):
    asked = list()

    def borrow():
      asked.append(1)
      return self.core

    editor_module.get_core = borrow
    self.addCleanup(setattr, editor_module, "get_core", core_module.get_core)
    _BarrierEditor.barrier = threading.Barrier(MAX_VARIANT_WORKERS, timeout=_TIMEOUT)
    execute_op_plan(self._plan(MAX_VARIANT_WORKERS), self._root())
    branches = _BarrierEditor.branches[1:]
    self.assertEqual(len(branches), MAX_VARIANT_WORKERS)
    self.assertEqual([b._core for b in branches], [self.core] * MAX_VARIANT_WORKERS)
    self.assertEqual(asked, [])


class CoreSingletonRaceTests(NativeCase):
  """`get_core()` and `load_library()` are check-then-set caches, and a miss on the
  second one compiles the library — so a racing first call must build exactly once."""

  def setUp(self):
    for module, name in ((core_module, "_CORE"), (_native, "_CDLL")):
      self.addCleanup(setattr, module, name, getattr(module, name))
      setattr(module, name, None)
    self.real_find = _native.find_or_build
    self.addCleanup(setattr, _native, "find_or_build", self.real_find)
    self.found = list()
    self.lock = threading.Lock()

  def _counting_find(self, **kw):
    with self.lock: self.found.append(1)
    time.sleep(0.05)  # widens the window a serial check-then-set would lose
    return self.real_find(**kw)

  def _race(self, call):
    ready = threading.Barrier(MAX_VARIANT_WORKERS, timeout=_TIMEOUT)
    _native.find_or_build = self._counting_find
    got = list()
    def run():
      ready.wait()  # every thread arrives at the cache miss together
      loaded = call()
      with self.lock: got.append(loaded)
    threads = [threading.Thread(target=run) for _ in range(MAX_VARIANT_WORKERS)]
    for th in threads: th.start()
    for th in threads: th.join(timeout=_TIMEOUT)
    self.assertEqual(len(got), MAX_VARIANT_WORKERS)
    return got

  def test_racing_load_library_compiles_once_and_shares_one_handle(self):
    got = self._race(_native.load_library)
    self.assertEqual(len(self.found), 1)
    self.assertEqual(got, [got[0]] * MAX_VARIANT_WORKERS)

  def test_racing_get_core_hands_out_one_instance(self):
    got = self._race(core_module.get_core)
    self.assertEqual(len(self.found), 1)
    for instance in got: self.assertIs(instance, got[0])


if __name__ == "__main__":
  unittest.main()
