"""A decoder's memory is bounded by the header it accepts: the CLI's 16384-per-side cap, a
stream too short for its rows refused before anything is allocated, and an inflate held to
the plane the header claims — stb refuses past twice it, the fallback reads only it."""

from __future__ import annotations

import struct
import tracemalloc
import unittest
import zlib

from pystencil import codecs
from pystencil.codecs import bmpdecode, pngdecode
from pystencil.codecs.sniff import MAX_SIDE
from tests.helpers.nativecase import require_stb
from tests.image.test_png_filters import _chunk

_MIB = 1 << 20


def _png(width: int, height: int, idat: bytes, color_type: int = 6) -> bytes:
  ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
  return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", idat)
          + _chunk(b"IEND", b""))


def _bmp(width: int, height: int, pixel_bytes: int, bpp: int = 32) -> bytes:
  header = struct.pack("<2sIHHI", b"BM", 54 + pixel_bytes, 0, 0, 54)
  info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, bpp, 0, pixel_bytes, 0, 0, 0, 0)
  return header + info + bytes(pixel_bytes)


class PeakMemoryCase(unittest.TestCase):
  def assert_peak_under(self, limit: int, body) -> None:
    """Run ``body`` under tracemalloc and hold its peak — also when it raises, which it
    then re-raises for the caller's assertRaises."""
    tracemalloc.start()
    error = None
    try:
      body()
    except Exception as exc:  # noqa: BLE001 - re-raised below, after the peak check
      error = exc
    finally:
      peak = tracemalloc.get_traced_memory()[1]
      tracemalloc.stop()
    self.assertLess(peak, limit, "peak %d bytes, limit %d" % (peak, limit))
    if error is not None: raise error


def _bomb() -> bytes:
  """64 MiB of zeros deflates to ~64 KiB: a stream that claims far more than its header."""
  deflate = zlib.compressobj(9)
  chunk = bytes(_MIB)
  return b"".join(deflate.compress(chunk) for _ in range(64)) + deflate.flush()


class PngBoundsTests(PeakMemoryCase):
  """Through ``codecs.decode_png``: stb when the native library has it, else the fallback."""

  decode = staticmethod(codecs.decode_png)

  def test_a_side_past_the_cap_is_refused(self):
    for width, height in ((MAX_SIDE + 1, 1), (1, MAX_SIDE + 1), (100000, 100000)):
      with self.subTest(size=(width, height)):
        with self.assertRaises(codecs.CodecError) as caught:
          self.decode(_png(width, height, zlib.compress(b"")))
        self.assertIn("side cap", str(caught.exception))

  def test_a_capped_header_over_a_short_stream_is_refused_before_allocating(self):
    png = _png(MAX_SIDE, MAX_SIDE, zlib.compress(bytes(1000)))
    with self.assertRaises(codecs.CodecError) as caught:
      self.assert_peak_under(8 * _MIB, lambda: self.decode(png))
    self.assertIn("pixel data is", str(caught.exception))

  def test_a_corrupt_stream_is_a_codec_error(self):
    with self.assertRaises(codecs.CodecError):
      self.decode(_png(2, 2, b"\x78\x9c not deflate"))

  def test_a_malformed_ihdr_is_a_codec_error(self):
    png = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", b"\x00\x00\x00\x01") + _chunk(b"IEND", b"")
    with self.assertRaises(codecs.CodecError):
      self.decode(png)


class FallbackPngBoundsTests(PngBoundsTests):
  decode = staticmethod(pngdecode.decode_png)

  def test_a_bomb_inflates_only_the_plane_its_header_claims(self):
    png = _png(16, 16, _bomb())
    result = list()
    self.assert_peak_under(4 * _MIB, lambda: result.append(self.decode(png)))
    width, height, pixels = result[0]
    self.assertEqual((width, height, bytes(pixels)), (16, 16, bytes(16 * 16 * 4)))


class StbPngBoundsTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    require_stb()

  def test_a_bomb_is_refused_once_it_inflates_past_its_plane(self):
    with self.assertRaises(codecs.CodecError) as caught:
      codecs.decode_png(_png(16, 16, _bomb()))
    self.assertIn("runs past the plane its header claims", str(caught.exception))

  def test_a_stream_near_the_plane_still_decodes(self):
    raw = bytes(16 * (16 * 4 + 1))
    self.assertEqual(codecs.decode_png(_png(16, 16, zlib.compress(raw + bytes(900))))[:2],
                     (16, 16))


class BmpBoundsTests(PeakMemoryCase):
  """Through ``codecs.decode_bmp``: stb when the native library has it, else the fallback."""

  decode = staticmethod(codecs.decode_bmp)

  def test_a_side_past_the_cap_is_refused_before_allocating(self):
    for width, height in ((MAX_SIDE + 1, 1), (1, -(MAX_SIDE + 1)), (100000, 100000)):
      with self.subTest(size=(width, height)):
        with self.assertRaises(codecs.CodecError):
          self.assert_peak_under(_MIB, lambda: self.decode(_bmp(width, height, 16)))

  def test_non_positive_dimensions_are_refused(self):
    for width, height in ((0, 1), (-4, 2), (3, 0)):
      with self.subTest(size=(width, height)):
        with self.assertRaises(codecs.CodecError):
          self.decode(_bmp(width, height, 16))

  def test_pixel_data_shorter_than_the_header_claims_is_refused(self):
    with self.assertRaises(codecs.CodecError) as caught:
      self.assert_peak_under(_MIB, lambda: self.decode(_bmp(4000, 4000, 64)))
    self.assertIn("truncated", str(caught.exception))

  def test_a_truncated_header_is_refused(self):
    with self.assertRaises(codecs.CodecError):
      self.decode(b"BM" + bytes(20))

  def test_the_last_rows_padding_may_be_missing(self):
    # 3 px of 24-bit is 9 bytes of pixels padded to 12; a writer may omit the final 3.
    data = _bmp(3, 2, 12 + 9, bpp=24)
    self.assertEqual(self.decode(data)[:2], (3, 2))


class FallbackBmpBoundsTests(BmpBoundsTests):
  decode = staticmethod(bmpdecode.decode_bmp)

if __name__ == "__main__":
  unittest.main()
