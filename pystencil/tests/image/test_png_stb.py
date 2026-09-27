"""PNG through the CLI's stb unit: the sample models the fallback refuses (16-bit, 1/2/4-bit,
Adam7), the tRNS colour key both decoders honour, the fallback taking over when the library
has no stb, and the CLI's own decode of the same bytes.
"""

from __future__ import annotations

import random
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

from tests.helpers.nativecase import require_stb
from tests.image.test_jpeg_cli import _CLI
from tests.image.test_png_filters import _chunk

from pystencil import codecs
from pystencil.codecs import pngdecode, stblib

_ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2),
          (0, 1, 1, 2))


def _row(pixels: list, depth: int) -> bytes:
  """One unfiltered scanline: samples packed MSB first below 8 bits, big-endian at 16."""
  if depth >= 8: return b"".join(s.to_bytes(depth // 8, "big") for px in pixels for s in px)
  bits = "".join(format(s, "0%db" % depth) for px in pixels for s in px)
  bits += "0" * (-len(bits) % 8)
  return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def _encode(rows: list, color: int, depth: int, interlace: int = 0, extra: bytes = b"") -> bytes:
  """``rows`` of sample tuples as a PNG, filter 0 on every scanline of every pass."""
  height, width = len(rows), len(rows[0])
  passes = _ADAM7 if interlace else ((0, 0, 1, 1),)
  raw = bytearray()
  for x0, y0, dx, dy in passes:
    for y in range(y0, height, dy):
      line = rows[y][x0::dx]
      if line: raw += b"\x00" + _row(line, depth)
  ihdr = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace)
  return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + extra
          + _chunk(b"IDAT", zlib.compress(bytes(raw))) + _chunk(b"IEND", b""))


def _random_rows(width: int, height: int, channels: int, top: int, seed: int) -> list:
  rnd = random.Random(seed)
  return [[tuple(rnd.randint(0, top) for _ in range(channels)) for _ in range(width)]
          for _ in range(height)]


class StbPngTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    require_stb()

  def test_a_sixteen_bit_sample_keeps_its_high_byte(self):
    rows = _random_rows(5, 3, 4, 0xFFFF, 1)
    want = bytes(s >> 8 for row in rows for px in row for s in px)
    self.assertEqual(codecs.decode_png(_encode(rows, 6, 16)), (5, 3, bytearray(want)))

  def test_a_sub_byte_grey_scales_to_the_full_range(self):
    for depth in (1, 2, 4):
      with self.subTest(depth=depth):
        rows = _random_rows(11, 4, 1, (1 << depth) - 1, depth)
        scale = 255 // ((1 << depth) - 1)
        want = b"".join(bytes([px[0] * scale] * 3 + [255]) for row in rows for px in row)
        self.assertEqual(bytes(codecs.decode_png(_encode(rows, 0, depth))[2]), want)

  def test_an_interlaced_png_decodes_as_its_plain_twin(self):
    for color, channels in ((2, 3), (6, 4), (0, 1)):
      with self.subTest(color=color):
        rows = _random_rows(13, 9, channels, 255, color)
        plain = codecs.decode_png(_encode(rows, color, 8))
        self.assertEqual(codecs.decode_png(_encode(rows, color, 8, interlace=1)), plain)

  def test_a_colour_key_clears_the_alpha_of_the_pixels_it_names_in_both_decoders(self):
    for color, channels in ((0, 1), (2, 3)):
      with self.subTest(color=color):
        rows = _random_rows(7, 5, channels, 3, 10 + color)
        key = rows[2][3]
        png = _encode(rows, color, 8, extra=_chunk(b"tRNS", struct.pack(">%dH" % channels, *key)))
        _, _, got = codecs.decode_png(png)
        want = [0 if px == key else 255 for row in rows for px in row]
        self.assertEqual(list(got[3::4]), want)
        self.assertEqual(pngdecode.decode_png(png)[2], got)


class FallbackTests(unittest.TestCase):
  def test_a_library_without_stb_decodes_through_the_fallback(self):
    png = codecs.encode_png(2, 1, bytearray(range(8)))
    with mock.patch.object(stblib, "loaded", return_value=None), \
        mock.patch.object(pngdecode, "decode_png", wraps=pngdecode.decode_png) as spy:
      self.assertEqual(bytes(codecs.decode_png(png)[2]), bytes(range(8)))
    self.assertTrue(spy.called)

  def test_the_fallback_names_the_sample_models_it_cannot_read(self):
    rows = _random_rows(3, 3, 4, 255, 3)
    for png, reason in ((_encode(rows, 6, 16), "only 8-bit"),
                        (_encode(rows, 6, 8, interlace=1), "interlaced")):
      with self.subTest(reason=reason):
        with self.assertRaises(codecs.CodecError) as caught:
          pngdecode.decode_png(png)
        self.assertIn(reason, str(caught.exception))


class CliPngParityTests(unittest.TestCase):
  """The built CLI compiles the same stb unit: its decode of each sample model is ours."""

  @classmethod
  def setUpClass(cls):
    if not _CLI.is_file():
      raise unittest.SkipTest("the CLI is not built at %s" % _CLI)
    require_stb()

  def test_a_decode_matches_the_clis_pixel_for_pixel(self):
    palette = _chunk(b"PLTE", bytes(range(0, 120, 10))) + _chunk(b"tRNS", bytes([0, 90, 180]))
    cases = {"rgba16-interlaced": _encode(_random_rows(9, 7, 4, 0xFFFF, 4), 6, 16, 1),
             "palette2": _encode(_random_rows(10, 6, 1, 3, 5), 3, 2, 0, palette),
             "grey-alpha": _encode(_random_rows(6, 5, 2, 255, 6), 4, 8, 1)}
    with tempfile.TemporaryDirectory() as tmp:
      for name, png in cases.items():
        with self.subTest(case=name):
          (Path(tmp) / "in.png").write_bytes(png)
          proc = subprocess.run([str(_CLI), "-i", "in.png", "out.png"], cwd=tmp,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                timeout=120)
          self.assertEqual(proc.returncode, 0, proc.stderr)
          theirs = codecs.decode((Path(tmp) / "out.png").read_bytes())
          self.assertEqual(codecs.decode_png(png), theirs)


if __name__ == "__main__":
  unittest.main()
