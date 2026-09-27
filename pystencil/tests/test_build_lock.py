"""build.py's compile is atomic, locked against a racing process, and time-bounded.

Every case points ``NATIVE_DIR`` at a temp dir and ``$CXX`` at a stub compiler, so the
real library is never touched, nothing is compiled and no stb header is fetched.
"""

from __future__ import annotations

import os
import stat
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from tests.test_build import build_py

_STUB = """#!%s
import sys, time
out = sys.argv[sys.argv.index("-o") + 1]
open(%r, "a").write("compiled " + " ".join(sys.argv[1:]) + "\\n")
mode = %r
if mode == "slow": time.sleep(30)
open(out, "w").write("partial" if mode == "fail" else "new")
sys.exit(1 if mode == "fail" else 0)
"""


class AtomicBuildTests(unittest.TestCase):
  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.dir = Path(tmp.name)
    self.log = self.dir / "calls.log"
    patcher = mock.patch.object(build_py, "NATIVE_DIR", self.dir / "_native")
    patcher.start()
    self.addCleanup(patcher.stop)
    # Headers "present", so the stub also runs for every C unit, and nothing is fetched.
    headers = mock.patch.object(build_py, "stb_headers", lambda verbose=False: self.dir)
    headers.start()
    self.addCleanup(headers.stop)
    self.out = build_py.lib_path()

  def _compiler(self, mode):
    script = self.dir / ("cxx-" + mode)
    script.write_text(_STUB % (sys.executable, str(self.log), mode))
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    patcher = mock.patch.dict(os.environ, {"CXX": str(script)})
    patcher.start()
    self.addCleanup(patcher.stop)

  def _calls(self):
    return self.log.read_text().count("compiled") if self.log.exists() else 0

  def _leftovers(self):
    return sorted(p.name for p in self.out.parent.glob("*.tmp"))

  def test_a_build_replaces_the_artifact_and_leaves_no_temp_file(self):
    self._compiler("ok")
    self.assertEqual(build_py.build(force=True), self.out)
    self.assertEqual(self.out.read_text(), "new")
    self.assertEqual(self._leftovers(), [])

  def test_each_c_unit_compiles_on_its_own_before_the_one_link(self):
    self._compiler("ok")
    build_py.build(force=True)
    calls = self.log.read_text().splitlines()
    self.assertEqual(len(calls), len(build_py.CODEC_SOURCES) + 1)
    self.assertTrue(all(" -x c " in call for call in calls[:-1]))
    self.assertIn("-shared", calls[-1])

  def test_without_the_stb_headers_the_core_still_links_and_no_c_unit_compiles(self):
    self._compiler("ok")
    with mock.patch.object(build_py, "stb_headers", lambda verbose=False: None):
      self.assertEqual(build_py.build(force=True), self.out)
    calls = self.log.read_text().splitlines()
    self.assertEqual(len(calls), 1)
    self.assertNotIn(" -x c ", calls[0])

  def test_a_failed_build_keeps_the_old_artifact_intact(self):
    self._compiler("fail")
    self.out.parent.mkdir(parents=True)
    self.out.write_text("old")
    with self.assertRaises(RuntimeError):
      build_py.build(force=True)
    self.assertEqual(self.out.read_text(), "old")
    self.assertEqual(self._leftovers(), [])

  def test_a_hung_compiler_times_out(self):
    self._compiler("slow")
    with mock.patch.object(build_py, "BUILD_TIMEOUT", 0.5):
      with self.assertRaises(RuntimeError) as caught:
        build_py.build(force=True)
    self.assertIn("timed out", str(caught.exception))
    self.assertFalse(self.out.exists())

  @unittest.skipIf(sys.platform == "win32", "fcntl lock")
  def test_a_waiter_reuses_the_library_the_lock_holder_built(self):
    import fcntl

    self._compiler("ok")
    self.out.parent.mkdir(parents=True)
    lock = open(self.out.with_name(self.out.name + ".lock"), "a+b")
    self.addCleanup(lock.close)
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    result = list()
    waiter = threading.Thread(target=lambda: result.append(build_py.build()))
    waiter.start()
    time.sleep(0.3)
    self.assertTrue(waiter.is_alive(), "build() did not wait for the lock")
    # The holder finishes: a library newer than every input appears, then the lock drops.
    self.out.write_text("theirs")
    future = time.time() + 3600
    os.utime(self.out, (future, future))
    fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
    waiter.join(timeout=10)
    self.assertEqual(result, [self.out])
    self.assertEqual(self.out.read_text(), "theirs")
    self.assertEqual(self._calls(), 0)


class ChildEnvTests(unittest.TestCase):
  """The compiler is the one process pystencil spawns; it inherits no credential."""

  def test_the_compiler_runs_without_the_llm_keys_or_the_server_tokens(self):
    compile_mod = build_py._compile
    seen = dict()

    def run(cmd, **kw):
      seen.update(kw["env"])
      return mock.Mock(returncode=0, stderr="")

    env = {
      "PATH": "/usr/bin", "STENCIL_LLM_API_KEY": "sk-ant-secret", "stencil_llm_base_url": "x",
      "STENCIL_SERVER_TOKEN": "srv", "STENCIL_SERVER_TOKENS": "http://h=srv",
      "STENCIL_SERVER_URL": "http://h",
    }
    with mock.patch.dict(os.environ, env, clear=True):
      with mock.patch.object(compile_mod.subprocess, "run", run):
        compile_mod._run(["cc"], Path("."), False, time.monotonic() + 5, 5)
    self.assertEqual(seen, {"PATH": "/usr/bin", "STENCIL_SERVER_URL": "http://h"})


if __name__ == "__main__":
  unittest.main()
