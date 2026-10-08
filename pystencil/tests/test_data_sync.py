"""build_data.sync: the _data/ copies land atomically, under a lock, and only when stale.

Every case syncs a temp common/config into a temp _data/, so the package's copies are never
touched.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from tests import _PKG_ROOT
from tests.test_build import build_py

data = build_py._data
_SIZE = 1 << 20

_WRITER = """
import importlib.util, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("bd", sys.argv[1])
bd = importlib.util.module_from_spec(spec); spec.loader.exec_module(bd)
for i in range(int(sys.argv[4])):
  bd.sync(Path(sys.argv[2 + i % 2]), Path(sys.argv[5]))"""


def _config(root: Path, fill: bytes) -> Path:
  for rel in data.FILES:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(fill * _SIZE)
  return root


class DataSyncTests(unittest.TestCase):
  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.dir = Path(tmp.name)
    self.a = _config(self.dir / "a", b"a")
    self.b = _config(self.dir / "b", b"b")
    self.out = self.dir / "_data"

  def _copies(self):
    return [self.out / Path(rel).name for rel in data.FILES]

  def _temps(self):
    return sorted(p.name for p in self.out.glob("*.tmp"))

  def test_equal_bytes_are_never_rewritten(self):
    data.sync(self.a, self.out)
    with mock.patch.object(data.os, "replace") as replace:
      data.sync(self.a, self.out)
    replace.assert_not_called()

  def test_a_stale_copy_is_replaced_by_one_rename(self):
    data.sync(self.a, self.out)
    inodes = [p.stat().st_ino for p in self._copies()]
    data.sync(self.b, self.out)
    self.assertTrue(all(p.read_bytes() == b"b" * _SIZE for p in self._copies()))
    self.assertNotEqual(inodes, [p.stat().st_ino for p in self._copies()])
    self.assertEqual(self._temps(), [])

  def test_a_failed_write_keeps_the_old_copy_and_no_temp(self):
    data.sync(self.a, self.out)
    with mock.patch.object(data.os, "replace", side_effect=OSError("disk")):
      with self.assertRaises(OSError):
        data.sync(self.b, self.out)
    self.assertTrue(all(p.read_bytes() == b"a" * _SIZE for p in self._copies()))
    self.assertEqual(self._temps(), [])

  @unittest.skipIf(sys.platform == "win32", "fcntl lock")
  def test_a_writer_waits_for_the_lock_holder(self):
    import fcntl

    self.out.mkdir()
    lock = open(self.out / data.LOCK_NAME, "a+b")
    self.addCleanup(lock.close)
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    writer = threading.Thread(target=data.sync, args=(self.a, self.out))
    writer.start()
    time.sleep(0.3)
    self.assertTrue(writer.is_alive(), "sync() did not wait for the lock")
    self.assertEqual([p for p in self._copies() if p.exists()], [])
    fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
    writer.join(timeout=10)
    self.assertTrue(all(p.read_bytes() == b"a" * _SIZE for p in self._copies()))

  def test_racing_processes_never_expose_a_torn_copy(self):
    data.sync(self.a, self.out)
    whole = {b"a" * _SIZE, b"b" * _SIZE}
    procs = [subprocess.Popen([sys.executable, "-I", "-c", _WRITER,
                               str(_PKG_ROOT / "build_data.py"),
                               str(self.a if i % 2 else self.b),
                               str(self.b if i % 2 else self.a), "40", str(self.out)])
             for i in range(4)]
    torn = list()
    while any(p.poll() is None for p in procs):
      for path in self._copies():
        body = path.read_bytes()
        if body not in whole:
          torn.append((path.name, len(body)))
    self.assertEqual([p.wait() for p in procs], [0, 0, 0, 0])
    self.assertEqual(torn, [])
    self.assertEqual(self._temps(), [])


if __name__ == "__main__":
  unittest.main()
