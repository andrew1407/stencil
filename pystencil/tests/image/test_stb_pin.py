"""The stb header pin: stb/pin.json names the commit cli/build.zig.zon fetches, and every
header a build compiles hashes to its recorded SHA-256 — the cached copy, and the CLI's
zig-cache copy when one is present. The fetch order and refusals run over stubbed sources,
so nothing here touches the network.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.test_build import build_py

stb = build_py._stb
PIN = stb.read_pin(build_py.STB_PIN)
ZON = stb.zon_stb(build_py.CLI_DIR / "build.zig.zon")
HEADERS = ("stb_image.h", "stb_image_write.h")


class PinTests(unittest.TestCase):
  def test_the_pinned_commit_is_the_one_the_cli_fetches(self):
    self.assertIsNotNone(ZON, "no stb dependency found in cli/build.zig.zon")
    self.assertEqual(PIN["commit"], ZON[0])

  def test_the_pin_covers_both_headers_the_codec_units_include(self):
    self.assertEqual(sorted(PIN["sha256"]), list(HEADERS))
    for name in HEADERS:
      self.assertRegex(PIN["sha256"][name], r"^[0-9a-f]{64}$")

  def test_every_cached_header_hashes_to_its_pin(self):
    cached = [name for name in HEADERS if (build_py.STB_CACHE / name).is_file()]
    if not cached:
      self.skipTest("no stb header fetched into %s yet" % build_py.STB_CACHE)
    for name in cached:
      data = (build_py.STB_CACHE / name).read_bytes()
      self.assertEqual(stb.sha256(data), PIN["sha256"][name], name)

  def test_the_zig_cache_copy_is_byte_equal(self):
    copies = {name: stb.zig_copy(ZON[1], name) for name in HEADERS}
    if any(data is None for data in copies.values()):
      self.skipTest("the CLI's stb package is not in %s" % stb.zig_cache_dir())
    for name, data in copies.items():
      self.assertEqual(stb.sha256(data), PIN["sha256"][name], name)
      cached = build_py.STB_CACHE / name
      if cached.is_file():
        self.assertEqual(cached.read_bytes(), data, name)

  def test_the_codec_units_the_pin_and_the_fetch_are_build_inputs(self):
    inputs = {Path(p).resolve() for p in build_py.build_inputs()}
    for path in build_py.CODEC_SOURCES + [build_py.STB_PIN, build_py._HERE / "build_stb.py"]:
      self.assertIn(Path(path).resolve(), inputs, "%s is not watched" % path)


class EnsureTests(unittest.TestCase):
  """``ensure`` over a temp cache: verified sources only, and a miss means no JPEG."""

  GOOD = b"/* the pinned header */\n"

  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.dir = Path(tmp.name)
    self.cache = self.dir / "cache"
    self.pin = self.dir / "pin.json"
    self.pin.write_text('{"commit": "%s", "sha256": {"stb_image.h": "%s"}}'
                        % (ZON[0], stb.sha256(self.GOOD)), encoding="utf-8")
    self.zon = build_py.CLI_DIR / "build.zig.zon"
    self.downloads = list()

  def _sources(self, zig=None, download=None):
    def fetch(commit, name):
      self.downloads.append((commit, name))
      if isinstance(download, Exception):
        raise download
      return download
    mock.patch.object(stb, "zig_copy", lambda package, name: zig).start()
    mock.patch.object(stb, "download", fetch).start()
    self.addCleanup(mock.patch.stopall)

  def _ensure(self):
    return stb.ensure(self.cache, self.pin, self.zon)

  def test_a_verified_cache_is_used_without_any_source(self):
    self._sources(zig=b"never", download=AssertionError("fetched"))
    self.cache.mkdir()
    (self.cache / "stb_image.h").write_bytes(self.GOOD)
    self.assertEqual(self._ensure(), self.cache)
    self.assertEqual(self.downloads, [])

  def test_the_zig_copy_comes_before_the_download(self):
    self._sources(zig=self.GOOD, download=AssertionError("fetched"))
    self.assertEqual(self._ensure(), self.cache)
    self.assertEqual((self.cache / "stb_image.h").read_bytes(), self.GOOD)
    self.assertEqual(self.downloads, [])

  def test_the_download_names_the_pinned_commit(self):
    self._sources(download=self.GOOD)
    self.assertEqual(self._ensure(), self.cache)
    self.assertEqual(self.downloads, [(ZON[0], "stb_image.h")])

  def test_a_cached_header_that_fails_its_hash_is_deleted_and_replaced(self):
    self._sources(download=self.GOOD)
    self.cache.mkdir()
    (self.cache / "stb_image.h").write_bytes(b"tampered")
    self.assertEqual(self._ensure(), self.cache)
    self.assertEqual((self.cache / "stb_image.h").read_bytes(), self.GOOD)

  def test_a_download_that_fails_its_hash_is_refused_and_never_kept(self):
    self._sources(zig=b"also wrong", download=b"not the pinned bytes")
    self.assertIsNone(self._ensure())
    self.assertEqual(sorted(p.name for p in self.cache.iterdir()), [])

  def test_offline_leaves_jpeg_out(self):
    self._sources(download=OSError("no network"))
    self.assertIsNone(self._ensure())
    self.assertFalse((self.cache / "stb_image.h").exists())

  def test_a_missing_pin_leaves_jpeg_out_instead_of_failing_the_build(self):
    self._sources(zig=self.GOOD, download=self.GOOD)
    self.pin.unlink()
    self.assertIsNone(self._ensure())
    self.assertEqual(self.downloads, [])

  def test_another_commit_in_the_zon_never_reads_the_zig_cache(self):
    self.pin.write_text('{"commit": "%s", "sha256": {"stb_image.h": "%s"}}'
                        % ("0" * 40, stb.sha256(self.GOOD)), encoding="utf-8")
    self._sources(zig=self.GOOD, download=OSError("no network"))
    self.assertIsNone(self._ensure())
    self.assertEqual(self.downloads, [("0" * 40, "stb_image.h")])


if __name__ == "__main__":
  unittest.main()
