"""``.stc`` handles parsed, read and closed from many threads at once.

The core serializes its script handle table; ``Script`` holds one lock over its handle, so a
double close destroys once and a read racing a close either finishes or refuses.
"""

from __future__ import annotations

import threading
from concurrent.futures import ThreadPoolExecutor

from tests.helpers.nativecase import NativeCase

from pystencil.core import Core
from pystencil.script import ScriptError, parse_script

_SOURCES = (
  "@source a.png:\n    @crop 10%\n    @filter bw\n",
  "@stencil s:\n  @line (0,0) (10,10)\n\n@source b.png:\n  @use stencil s\n  @undo\n",
  "@source c.png:\n  @crp 1\n",
  "@source d.png:\n    @crop x1=10% aspect=3:2\n    @rotate 1\n",
)
_TIMEOUT = 10.0


class _CountingLib:
  def __init__(self, lib) -> None:
    self.lib = lib
    self.destroyed = list()

  def __getattr__(self, name):
    real = getattr(self.lib, name)
    if name != "stencil_cli_scriptDestroy": return real
    return lambda handle: (self.destroyed.append(handle), real(handle))[1]


def _snapshot(program):
  return (program.dump, program.diagnostics_text(), program.ops, program.blocks)


class ScriptThreadTests(NativeCase):
  def test_a_pool_parses_and_closes_to_the_serial_result(self):
    serial = list()
    for text in _SOURCES:
      with parse_script(text, self.core) as program:
        serial.append(_snapshot(program))

    def job(i):
      program = parse_script(_SOURCES[i % len(_SOURCES)], self.core)
      try:
        return _snapshot(program)
      finally:
        program.close()

    with ThreadPoolExecutor(max_workers=8) as pool:
      got = list(pool.map(job, range(200)))
    self.assertEqual(got, [serial[i % len(_SOURCES)] for i in range(200)])

  def test_two_threads_closing_one_script_destroy_it_once(self):
    spy = _CountingLib(self.core._lib)
    core = Core(spy)
    programs = [parse_script(_SOURCES[i % len(_SOURCES)], core) for i in range(40)]
    barrier = threading.Barrier(2, timeout=_TIMEOUT)

    def closer():
      for program in programs:
        barrier.wait()
        program.close()

    threads = [threading.Thread(target=closer) for _ in range(2)]
    for thread in threads: thread.start()
    for thread in threads: thread.join(_TIMEOUT)
    self.assertEqual(len(spy.destroyed), len(programs))
    self.assertEqual(len(set(spy.destroyed)), len(programs))

  def test_a_read_racing_a_close_finishes_or_refuses(self):
    for _ in range(40):
      program = parse_script("@source a.png:\n    @crop 10%\n", self.core)
      barrier = threading.Barrier(2, timeout=_TIMEOUT)
      outcomes = list()

      def reader():
        barrier.wait()
        try:
          outcomes.append(program.resolve(1, 100, 200))
        except ScriptError:
          outcomes.append("closed")

      thread = threading.Thread(target=reader)
      thread.start()
      barrier.wait()
      program.close()
      thread.join(_TIMEOUT)
      self.assertIn(outcomes[0], ([10.0, 20.0, 80.0, 160.0], "closed"))
