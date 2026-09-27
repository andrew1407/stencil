"""The fallback decoder's two ways of reversing Average/Paeth rows — per byte and the
anti-diagonal wavefront — agree with a naive reference on every sample model, band split
and mix of filters; and the encoder's filter choice round-trips and stays on the whole-row
filters."""

from __future__ import annotations

import random
import struct
import unittest
import zlib
from unittest import mock

from pystencil import codecs
from pystencil.codecs import pngdecode, pngfilter
from tests.image.test_png_filters import CHANNELS, _chunk, paeth


def _reference_encode(samples: bytes, width: int, bpp: int, ftypes: list) -> bytes:
  """Filter each scanline with its own type, byte by byte (the straightforward form)."""
  stride = width * bpp
  raw = bytearray()
  prev = bytes(stride)
  for y, ftype in enumerate(ftypes):
    cur = samples[y * stride:(y + 1) * stride]
    raw.append(ftype)
    for x in range(stride):
      a = cur[x - bpp] if x >= bpp else 0
      b = prev[x]
      c = prev[x - bpp] if x >= bpp else 0
      pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[ftype]
      raw.append((cur[x] - pred) & 0xFF)
    prev = cur
  return bytes(raw)


def _png(width: int, height: int, color_type: int, raw: bytes) -> bytes:
  ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
  return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
          + _chunk(b"IDAT", zlib.compress(raw, 6)) + _chunk(b"IEND", b""))


class WavefrontParityTests(unittest.TestCase):
  """Each case decodes with the wavefront forced on and forced off, over small bands so
  one image crosses several band seams."""

  def _decode_both_ways(self, png: bytes, band: int) -> list:
    got = list()
    for per_byte in (0, 10 ** 9):
      with mock.patch.object(pngfilter, "_PER_BYTE", per_byte), \
          mock.patch.object(pngfilter, "BAND", band):
        got.append(bytes(pngdecode.decode_png(png)[2]))
    return got

  def test_random_filters_on_every_sample_model(self):
    rnd = random.Random(20260926)
    for color_type in (0, 2, 4, 6):
      bpp = CHANNELS[color_type]
      for width, height, band in ((1, 9, 4), (7, 23, 5), (33, 17, 256), (5, 1, 1)):
        with self.subTest(color_type=color_type, size=(width, height), band=band):
          samples = bytes(rnd.getrandbits(8) for _ in range(width * height * bpp))
          ftypes = [rnd.randint(0, 4) for _ in range(height)]
          png = _png(width, height, color_type, _reference_encode(samples, width, bpp, ftypes))
          want = bytes(pngdecode.decode_png(_png(
            width, height, color_type, _reference_encode(samples, width, bpp, [0] * height)))[2])
          for got in self._decode_both_ways(png, band):
            self.assertEqual(got, want)

  def test_a_paeth_image_large_enough_to_pick_the_wavefront_itself(self):
    width, height = 64, 40
    rnd = random.Random(7)
    samples = bytes(rnd.getrandbits(8) for _ in range(width * height * 4))
    png = _png(width, height, 6, _reference_encode(samples, width, 4, [4] * height))
    with mock.patch.object(pngfilter, "unfilter_band", wraps=pngfilter.unfilter_band) as spy:
      self.assertEqual(bytes(pngdecode.decode_png(png)[2]), samples)
    self.assertTrue(spy.called)


class EncodeFilterTests(unittest.TestCase):
  @staticmethod
  def _filter_bytes(png: bytes, width: int, height: int) -> bytes:
    idat = png.index(b"IDAT")
    length = struct.unpack(">I", png[idat - 4:idat])[0]
    return zlib.decompress(png[idat + 4:idat + 4 + length])[0::width * 4 + 1][:height]

  def _round_trip(self, width: int, height: int, pixels: bytes) -> bytes:
    png = codecs.encode_png(width, height, bytearray(pixels))
    self.assertEqual(bytes(codecs.decode_png(png)[2]), pixels)
    return self._filter_bytes(png, width, height)

  def _adaptive(self):
    patcher = mock.patch.object(pngfilter, "STRATEGIES", ((0, 1, 2),))
    patcher.start()
    self.addCleanup(patcher.stop)

  def test_per_row_a_vertically_constant_image_is_filtered_up(self):
    self._adaptive()
    row = bytes((x * 37) & 0xFF for x in range(40 * 4))
    self.assertEqual(set(self._round_trip(40, 30, row * 30)[1:]), {2})

  def test_per_row_a_horizontal_ramp_is_filtered_sub(self):
    self._adaptive()
    width, height = 64, 32
    pixels = bytes(((x * 3 + y * 101) & 0xFF) for y in range(height) for x in range(width * 4))
    self.assertIn(1, self._round_trip(width, height, pixels))

  def test_the_trial_filters_a_vertically_smooth_image_up(self):
    rnd = random.Random(5)
    base = [rnd.getrandbits(7) for _ in range(48 * 4)]
    pixels = bytes((v + y) & 0xFF for y in range(40) for v in base)
    self.assertEqual(set(self._round_trip(48, 40, pixels)), {2})

  def test_the_trial_leaves_a_two_tone_image_unfiltered(self):
    rnd = random.Random(6)
    pixels = b"".join(
      (b"\x00\x00\x00\xff", b"\xff\xff\xff\xff")[rnd.getrandbits(1)] for _ in range(48 * 40))
    self.assertEqual(set(self._round_trip(48, 40, pixels)), {0})

  def test_the_encoder_only_writes_whole_row_filters(self):
    rnd = random.Random(3)
    pixels = bytes(rnd.getrandbits(8) for _ in range(24 * 18 * 4))
    self.assertLessEqual(set(self._round_trip(24, 18, pixels)), {0, 1, 2})

  def test_every_strategy_round_trips(self):
    rnd = random.Random(4)
    pixels = bytes(rnd.getrandbits(3) for _ in range(20 * 15 * 4))
    for allowed in pngfilter.STRATEGIES:
      with self.subTest(allowed=allowed):
        with mock.patch.object(pngfilter, "STRATEGIES", (allowed,)):
          self.assertLessEqual(set(self._round_trip(20, 15, pixels)), set(allowed))


if __name__ == "__main__":
  unittest.main()
