"""pystencil's JPEG against the built Zig CLI's, which compiles the same stb units: the same
pixels from a decode and the same quality-90 encoder. Skips when ``cli/zig-out/bin/stencil``
is absent, since this suite never builds the CLI.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests import _PKG_ROOT
from tests.helpers.nativecase import require_stb
from tests.image.test_jpeg import FIXTURE, SMOOTH_SIZE, jpeg_header, smooth

from pystencil import codecs

_CLI = Path(_PKG_ROOT).parent / "cli" / "zig-out" / "bin" / (
  "stencil.exe" if sys.platform == "win32" else "stencil")


class CliJpegParityTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    if not _CLI.is_file():
      raise unittest.SkipTest("the CLI is not built at %s" % _CLI)
    require_stb()

  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.dir = Path(tmp.name)

  def _cli(self, source: bytes, source_name: str, out_name: str) -> bytes:
    """Run ``stencil -i <source> <out>`` in the temp dir and return the file it wrote."""
    (self.dir / source_name).write_bytes(source)
    proc = subprocess.run([str(_CLI), "-i", source_name, out_name], cwd=str(self.dir),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
    self.assertEqual(proc.returncode, 0, proc.stderr)
    self.assertIn("wrote %s" % out_name, proc.stderr)
    return (self.dir / out_name).read_bytes()

  def test_a_decode_matches_the_clis_pixel_for_pixel(self):
    data = FIXTURE.read_bytes()
    theirs = codecs.decode(self._cli(data, "in.jpg", "out.png"))
    self.assertEqual(codecs.decode(data), theirs)

  def test_an_encode_carries_the_clis_tables_and_pixels(self):
    png = codecs.encode_png(*SMOOTH_SIZE, smooth())
    theirs = self._cli(png, "in.png", "out.jpg")
    ours = codecs.encode_jpeg(*SMOOTH_SIZE, smooth())
    self.assertEqual(jpeg_header(ours), jpeg_header(theirs))
    # Float DCT: another compiler's FMA contraction may move a coefficient by one step.
    _, _, a = codecs.decode(ours)
    _, _, b = codecs.decode(theirs)
    self.assertLessEqual(max(abs(x - y) for x, y in zip(a, b)), 4)


if __name__ == "__main__":
  unittest.main()
