"""build.py's staleness rules, its three-list sync, and the native test gate.

A stale shared library silently runs every core-parity test against the PREVIOUS edit,
so the artifact is rebuilt whenever any input is newer — and "any input" has to mean
every compiled source AND every header they include. These tests pin that input set
against the tree, so adding a core group dir without extending ``INCLUDE_DIRS`` fails
here instead of going quiet.
"""

from __future__ import annotations

import importlib.util
import os
import re
import tempfile
import unittest
from pathlib import Path

from tests import _PKG_ROOT
from tests.nativecase import SKIP_NATIVE_ENV, require_core


def _load_build_py():
  """Import build.py by path, the way _native.py does (no sys.path mutation)."""
  spec = importlib.util.spec_from_file_location("tests._build", _PKG_ROOT / "build.py")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


build_py = _load_build_py()
CORE = build_py.CORE_DIR
_HEADER_SUFFIXES = (".hpp", ".h", ".inc")
# Core subtrees that are never compiled into the ctypes library.
_NOT_OURS = ("tests", "third_party", "build", "build-wasm")


def _core_header_dirs():
  """Every core dir holding a header we could include, relative to core/ ('.' = root)."""
  found = set()
  for path in CORE.rglob("*"):
    if path.suffix not in _HEADER_SUFFIXES or not path.is_file():
      continue
    rel = path.relative_to(CORE).parent.as_posix()
    if rel.split("/")[0] in _NOT_OURS:
      continue
    found.add(rel)
  return found


class BuildInputsTests(unittest.TestCase):
  def test_every_compiled_source_is_an_input(self):
    inputs = {Path(p).resolve() for p in build_py.build_inputs()}
    for rel in build_py.STENCIL_CORE_SOURCES + [build_py.ABI_SOURCE]:
      self.assertIn((CORE / rel).resolve(), inputs, "%s is compiled but not watched" % rel)

  def test_build_py_is_its_own_input(self):
    # It carries the flags and the source list, so editing it invalidates the artifact.
    inputs = {Path(p).resolve() for p in build_py.build_inputs()}
    self.assertIn((_PKG_ROOT / "build.py").resolve(), inputs)

  def test_every_core_header_directory_is_an_include_dir(self):
    listed = set(build_py.INCLUDE_DIRS)
    self.assertEqual(
      sorted(_core_header_dirs() - listed),
      [],
      "core dir(s) with headers missing from build.py INCLUDE_DIRS",
    )

  def test_every_core_header_is_an_input(self):
    inputs = {Path(p).resolve() for p in build_py.build_inputs()}
    for rel in _core_header_dirs():
      for suffix in _HEADER_SUFFIXES:
        for path in (CORE / rel).glob("*" + suffix):
          self.assertIn(path.resolve(), inputs, "%s is not watched" % path)

  def test_the_source_list_matches_core_cmakelists(self):
    # The parity contract's three-list sync, checked from this side (the other two
    # are core/CMakeLists.txt and cli/build.zig).
    text = (CORE / "CMakeLists.txt").read_text(encoding="utf-8")
    block = re.search(r"set\(STENCIL_CORE_SOURCES\n(.*?)\n\)", text, re.S)
    self.assertIsNotNone(block, "STENCIL_CORE_SOURCES not found in core/CMakeLists.txt")
    cmake = [line.strip() for line in block.group(1).splitlines() if line.strip()]
    self.assertEqual(build_py.STENCIL_CORE_SOURCES, cmake)


class StalenessTests(unittest.TestCase):
  """``is_stale`` over a temp artifact — mtime comparison, not mere existence."""

  def setUp(self):
    self._dir = tempfile.TemporaryDirectory()
    self.addCleanup(self._dir.cleanup)
    self.out = Path(self._dir.name) / "lib.so"
    self.src = Path(self._dir.name) / "src.cpp"
    self.src.write_text("x")
    self.out.write_text("built")
    self._touch(self.out, self._age(self.src) + 10.0)

  @staticmethod
  def _age(path):
    return path.stat().st_mtime

  @staticmethod
  def _touch(path, when):
    os.utime(path, (when, when))

  def test_a_missing_artifact_is_stale(self):
    self.out.unlink()
    self.assertTrue(build_py.is_stale(self.out, [self.src]))

  def test_an_artifact_newer_than_every_input_is_fresh(self):
    self.assertFalse(build_py.is_stale(self.out, [self.src]))

  def test_an_edited_input_makes_it_stale(self):
    self._touch(self.src, self._age(self.out) + 5.0)
    self.assertTrue(build_py.is_stale(self.out, [self.src]))

  def test_one_newer_input_among_many_is_enough(self):
    others = []
    for i in range(3):
      p = Path(self._dir.name) / ("o%d.hpp" % i)
      p.write_text("y")
      self._touch(p, self._age(self.out) - 100.0)
      others.append(p)
    self.assertFalse(build_py.is_stale(self.out, others))
    self._touch(others[-1], self._age(self.out) + 1.0)
    self.assertTrue(build_py.is_stale(self.out, others))

  def test_a_missing_input_is_ignored(self):
    # The compiler reports a vanished source far better than a mtime check can.
    self.assertFalse(build_py.is_stale(self.out, [Path(self._dir.name) / "gone.cpp"]))

  def test_the_real_artifact_is_fresh_after_a_build(self):
    require_core()  # builds on demand
    self.assertFalse(build_py.is_stale(build_py.lib_path()))


class NativeGateTests(unittest.TestCase):
  """The one opt-out every native-backed case goes through."""

  def _with_env(self, value):
    previous = os.environ.get(SKIP_NATIVE_ENV)
    if value is None:
      os.environ.pop(SKIP_NATIVE_ENV, None)
    else:
      os.environ[SKIP_NATIVE_ENV] = value
    self.addCleanup(
      lambda: os.environ.__setitem__(SKIP_NATIVE_ENV, previous)
      if previous is not None
      else os.environ.pop(SKIP_NATIVE_ENV, None)
    )

  def test_the_env_var_skips_instead_of_compiling(self):
    self._with_env("1")
    with self.assertRaises(unittest.SkipTest):
      require_core()

  def test_unset_and_zero_both_mean_run(self):
    for value in (None, "0", ""):
      self._with_env(value)
      self.assertIsNotNone(require_core())


if __name__ == "__main__":
  unittest.main()
