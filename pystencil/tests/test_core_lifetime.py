"""A Core's op-plan schema handle: destroyed once by close() or collection, never the shared one's."""

from __future__ import annotations

import gc
import sys
import unittest

from pystencil import core as core_mod
from pystencil.core import Core, get_core
from tests.helpers.nativecase import NativeCase

_PLAN = '{"reply": "ok", "actions": []}'


class _SpyLib:
  """The real CDLL with every schema destroy recorded."""

  def __init__(self, lib) -> None:
    self.lib = lib
    self.destroyed = list()

  def __getattr__(self, name):
    real = getattr(self.lib, name)
    if name != "stencil_cli_opplanSchemaDestroy": return real
    return lambda handle: (self.destroyed.append(handle), real(handle))[1]


class CoreLifetimeTests(NativeCase):
  def _spy_core(self):
    spy = _SpyLib(self.core._lib)
    return Core(spy), spy

  def test_close_destroys_the_schema_once(self):
    core, spy = self._spy_core()
    self.assertEqual(core.opplan_parse(_PLAN)[0], 0)
    core.close()
    core.close()
    self.assertEqual(len(spy.destroyed), 1)
    self.assertNotEqual(spy.destroyed[0], 0)

  def test_a_closed_core_walks_again_on_a_fresh_handle(self):
    core, spy = self._spy_core()
    with core:
      core.opplan_parse(_PLAN)
    self.assertEqual(core.opplan_parse(_PLAN)[0], 0)
    core.close()
    self.assertEqual(len(spy.destroyed), 2)

  def test_a_core_that_never_walked_destroys_nothing(self):
    core, spy = self._spy_core()
    core.close()
    self.assertEqual(spy.destroyed, [])

  def test_collection_destroys_the_schema(self):
    core, spy = self._spy_core()
    core.opplan_parse(_PLAN)
    del core
    gc.collect()
    self.assertEqual(len(spy.destroyed), 1)

  def test_the_shared_core_keeps_its_handle(self):
    shared = get_core()
    shared.opplan_parse(_PLAN)
    handle = shared._Core__opplan
    shared.close()
    self.assertEqual(shared._Core__opplan, handle)
    self.assertEqual(shared.opplan_parse(_PLAN)[0], 0)

  def test_collection_after_the_library_is_gone_raises_nothing(self):
    core, _ = self._spy_core()
    core.opplan_parse(_PLAN)
    seen = list()
    hook, sys.unraisablehook = sys.unraisablehook, seen.append
    try:
      core._lib = None
      del core
      gc.collect()
    finally:
      sys.unraisablehook = hook
    self.assertEqual(seen, [])
    self.assertIs(core_mod._CORE, get_core())


if __name__ == "__main__":
  unittest.main()
