"""BMP through the CLI's stb unit: the ``BI_BITFIELDS`` top-down files macOS ``sips`` writes,
palettes and 16-bit pixels the fallback refuses, stb's reading of an all-zero alpha in both
decoders, the header refusals made before stb, and the CLI's own decode of the same bytes.
"""

from __future__ import annotations

import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.helpers.nativecase import require_stb
from tests.image.test_jpeg_cli import _CLI

from pystencil import codecs
from pystencil.codecs import bmpdecode, stblib

_RGBA_MASKS = (0xFF0000, 0xFF00, 0xFF, 0xFF000000)


def _bmp(width: int, rows: list, bpp: int, top_down: bool = False, compression: int = 0,
         info: int = 40, masks: tuple = (0, 0, 0, 0), palette: bytes = b"") -> bytes:
  """``rows`` of raw pixel bytes, top row first, as a BMP with a 40-byte or V5 header."""
  body = b"".join(r + bytes(-len(r) % 4) for r in (rows if top_down else rows[::-1]))
  header = struct.pack("<IiiHHIIiiII", info, width, -len(rows) if top_down else len(rows), 1,
                       bpp, compression, len(body), 2835, 2835, len(palette) // 4, 0)
  if info == 124: header += struct.pack("<IIII", *masks) + b"BGRs" + bytes(64)
  offset = 14 + len(header) + len(palette)
  return struct.pack("<2sIHHI", b"BM", offset + len(body), 0, 0, offset) + header + palette + body


def _bgra_rows(width: int, height: int, seed: int, alpha=None) -> list:
  rnd = random.Random(seed)
  return [bytes(b for _ in range(width) for b in (rnd.randrange(256), rnd.randrange(256),
                                                  rnd.randrange(256),
                                                  rnd.randrange(256) if alpha is None else alpha))
          for _ in range(height)]


def _rgba(rows: list) -> bytes:
  return b"".join(bytes((r[i + 2], r[i + 1], r[i], r[i + 3])) for r in rows
                  for i in range(0, len(r), 4))


class StbBmpTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    require_stb()

  def test_a_bitfields_32_bit_top_down_bmp_keeps_its_rows_and_alpha(self):
    rows = _bgra_rows(5, 3, 1)
    data = _bmp(5, rows, 32, top_down=True, compression=3, info=124, masks=_RGBA_MASKS)
    self.assertEqual(codecs.decode_bmp(data), (5, 3, bytearray(_rgba(rows))))

  def test_a_palette_index_past_the_palette_is_opaque_black(self):
    palette = bytes((10, 20, 30, 0, 40, 50, 60, 0))
    data = _bmp(4, [bytes((0, 1, 9, 1))], 8, palette=palette)
    self.assertEqual(bytes(codecs.decode_bmp(data)[2]),
                     bytes((30, 20, 10, 255, 60, 50, 40, 255, 0, 0, 0, 255, 60, 50, 40, 255)))

  def test_an_all_zero_alpha_reads_as_opaque_in_both_decoders(self):
    rows = _bgra_rows(3, 2, 2, alpha=0)
    for decode in (codecs.decode_bmp, bmpdecode.decode_bmp):
      with self.subTest(decode=decode.__module__):
        self.assertEqual(bytes(decode(_bmp(3, rows, 32))[2])[3::4], b"\xff" * 6)

  @unittest.skipUnless(shutil.which("sips"), "macOS sips is not on PATH")
  def test_a_bmp_sips_writes_decodes_to_the_png_it_was_made_from(self):
    rows = _bgra_rows(7, 5, 3)
    png = codecs.encode_png(7, 5, bytearray(_rgba(rows)))
    with tempfile.TemporaryDirectory() as tmp:
      (Path(tmp) / "in.png").write_bytes(png)
      subprocess.run(["sips", "-s", "format", "bmp", "in.png", "--out", "out.bmp"], cwd=tmp,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60, check=True)
      data = (Path(tmp) / "out.bmp").read_bytes()
    self.assertEqual(codecs.decode_bmp(data), codecs.decode_png(png))


class BmpGateTests(unittest.TestCase):
  def test_a_library_without_stb_decodes_through_the_fallback(self):
    data = codecs.encode_bmp(2, 1, bytearray(range(1, 9)))
    with mock.patch.object(stblib, "loaded", return_value=None), \
        mock.patch.object(bmpdecode, "decode_bmp", wraps=bmpdecode.decode_bmp) as spy:
      self.assertEqual(bytes(codecs.decode_bmp(data)[2]), bytes(range(1, 9)))
    self.assertTrue(spy.called)

  def test_the_fallback_names_what_it_cannot_read(self):
    for data, reason in ((_bmp(1, [bytes(4)], 32, compression=3, info=124, masks=_RGBA_MASKS),
                          "BI_RGB"), (_bmp(1, [b"\x00"], 8, palette=bytes(4)), "24/32-bit")):
      with self.subTest(reason=reason):
        with self.assertRaises(codecs.CodecError) as caught:
          bmpdecode.decode_bmp(data)
        self.assertIn(reason, str(caught.exception))

  def test_a_header_past_the_cap_or_the_bytes_is_refused_before_stb(self):
    core = struct.pack("<2sIHHIIHHHH", b"BM", 0, 0, 0, 26, 12, 20000, 1, 1, 24) + bytes(8)
    cases = ((core, "side cap"), (_bmp(9, [bytes(27)], 24, top_down=True)[:-8], "truncated"))
    with mock.patch.object(stblib, "loaded", side_effect=AssertionError("reached stb")):
      for data, reason in cases:
        with self.subTest(reason=reason):
          with self.assertRaises(codecs.CodecError) as caught:
            codecs.decode_bmp(data)
          self.assertIn(reason, str(caught.exception))


class CliBmpParityTests(unittest.TestCase):
  """The built CLI compiles the same stb unit: its decode of each BMP kind is ours."""

  @classmethod
  def setUpClass(cls):
    if not _CLI.is_file():
      raise unittest.SkipTest("the CLI is not built at %s" % _CLI)
    require_stb()

  def test_a_decode_matches_the_clis_pixel_for_pixel(self):
    rnd = random.Random(9)
    cases = {
      "bitfields-top-down": _bmp(5, _bgra_rows(5, 4, 4), 32, True, 3, 124, _RGBA_MASKS),
      "rgb-alpha": _bmp(3, _bgra_rows(3, 2, 5), 32),
      "bgr-odd-width": _bmp(5, [bytes(rnd.randrange(256) for _ in range(15)) for _ in range(3)], 24),
      "palette8": _bmp(6, [bytes(rnd.randrange(4) for _ in range(6)) for _ in range(2)], 8,
                       palette=bytes(rnd.randrange(256) for _ in range(16))),
      "rgb555": _bmp(3, [bytes(rnd.randrange(128) for _ in range(6)) for _ in range(2)], 16),
    }
    with tempfile.TemporaryDirectory() as tmp:
      for name, data in cases.items():
        with self.subTest(case=name):
          (Path(tmp) / "in.bmp").write_bytes(data)
          proc = subprocess.run([str(_CLI), "-i", "in.bmp", "out.png"], cwd=tmp,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                timeout=120)
          self.assertEqual(proc.returncode, 0, proc.stderr)
          theirs = codecs.decode((Path(tmp) / "out.png").read_bytes())
          self.assertEqual(codecs.decode_bmp(data), theirs)


if __name__ == "__main__":
  unittest.main()
