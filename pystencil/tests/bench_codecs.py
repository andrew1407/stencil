"""decode_png timings: the per-scanline unfilter and the per-channel expansion.

The decoder is pure Python, so its cost model is "how much of the work happens inside a C
builtin". Up/Sub reverse a whole scanline with bigint SWAR arithmetic; Average and Paeth
need the reconstructed byte to their left and stay per-byte loops. These ceilings are what
fails if a whole-row path regresses to a per-byte one.
"""

from __future__ import annotations

import struct
import zlib

from tests.benchsupport import BenchCase

from pystencil.codecs import decode_png, encode_png

_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def _gradient(width: int, height: int) -> bytearray:
  """A non-flat RGBA8 image, so no filter degenerates into a run of zeros."""
  rows = bytearray()
  for y in range(height):
    for x in range(width):
      rows += bytes(((x * 7) & 0xFF, (y * 13) & 0xFF, (x ^ y) & 0xFF, 255))
  return rows


# PNG colour type -> samples per pixel at 8-bit depth (codecs/png.py's _CHANNELS).
_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def _chunk(ctype: bytes, payload: bytes) -> bytes:
  crc = zlib.crc32(payload, zlib.crc32(ctype)) & 0xFFFFFFFF
  return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)


def _filtered_png(width: int, height: int, ftype: int, color_type: int = 6,
         extra: bytes = b"") -> bytes:
  """A PNG whose every scanline carries filter ``ftype`` over non-flat sample bytes, so
  the decoder runs that filter's real reconstruction path on every row."""
  stride = width * _CHANNELS[color_type]
  raw = bytearray()
  for y in range(height):
    raw.append(ftype)
    raw += bytes(((x * 31 + y * 17) & 0xFF) for x in range(stride))
  ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
  return (
    _PNG_MAGIC
    + _chunk(b"IHDR", ihdr)
    + extra
    + _chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    + _chunk(b"IEND", b"")
  )


def _palette_png(width: int, height: int) -> bytes:
  """A filter-0 palette PNG — the 256-entry translate-table expansion path."""
  plte = bytes(b for i in range(256) for b in ((i * 5) & 0xFF, (i * 11) & 0xFF, (i * 23) & 0xFF))
  return _filtered_png(width, height, 0, color_type=3, extra=_chunk(b"PLTE", plte))


class DecodePngBench(BenchCase):
  def test_cost_is_linear_in_the_pixel_count(self):
    small = encode_png(200, 150, _gradient(200, 150))
    wide = encode_png(400, 150, _gradient(400, 150))
    tall = encode_png(200, 300, _gradient(200, 300))

    base = self.micros("decode_png 200x150 (filter 0)", 40, lambda: decode_png(small))
    twice_wide = self.micros("decode_png 400x150", 20, lambda: decode_png(wide))
    twice_tall = self.micros("decode_png 200x300", 20, lambda: decode_png(tall))

    # One inflate + one whole-row step per scanline + one strided copy per channel:
    # twice the pixels is twice the work whichever axis grew.
    self.ratio("twice the width", twice_wide, base, ceiling=3.0)
    self.ratio("twice the height", twice_tall, base, ceiling=3.0)

  def test_the_whole_row_filters_stay_off_the_per_byte_path(self):
    none = _filtered_png(300, 200, 0)
    up = _filtered_png(300, 200, 2)
    sub = _filtered_png(300, 200, 1)
    paeth = _filtered_png(300, 200, 4)

    flat = self.micros("decode_png filter 0 (none)", 20, lambda: decode_png(none))
    up_us = self.micros("decode_png filter 2 (Up)", 20, lambda: decode_png(up))
    sub_us = self.micros("decode_png filter 1 (Sub)", 20, lambda: decode_png(sub))
    paeth_us = self.micros("decode_png filter 4 (Paeth)", 5, lambda: decode_png(paeth))

    # Up is a fixed handful of whole-row C passes; Sub repeats its add log2(stride/bpp) times.
    # Both must stay inside a small multiple of no unfilter at all.
    self.ratio("Up vs none", up_us, flat, ceiling=16.0)
    self.ratio("Sub vs none", sub_us, flat, ceiling=50.0)
    # Paeth is per-byte BY DESIGN: every byte needs the reconstructed byte to its
    # left, so there is no whole-row form. Bounded only against getting dearer still.
    self.ratio("Paeth vs Up", paeth_us, up_us, ceiling=250.0)

  def test_sub_beats_its_own_per_byte_twin(self):
    # Average has Sub's left-neighbour dependency and stays a per-byte loop, so it is
    # the control: Sub costing a FRACTION of it is the evidence Sub reverses rows in C.
    sub = _filtered_png(300, 200, 1)
    average = _filtered_png(300, 200, 3)

    sub_us = self.micros("decode_png filter 1 (Sub, whole-row)", 20, lambda: decode_png(sub))
    avg_us = self.micros("decode_png filter 3 (Average, per-byte)", 5, lambda: decode_png(average))

    # Collapsing toward 1.0 means Sub regressed to a per-byte scan.
    self.ratio("Sub vs Average", sub_us, avg_us, ceiling=0.5)

  def test_expansion_of_a_narrower_sample_model_is_a_strided_copy(self):
    gray = _filtered_png(300, 200, 0, color_type=0)
    palette = _palette_png(300, 200)
    rgba = _filtered_png(300, 200, 0)

    direct = self.micros("decode_png RGBA (no expansion)", 20, lambda: decode_png(rgba))
    gray_us = self.micros("decode_png grayscale (3 strided copies)", 20, lambda: decode_png(gray))
    pal_us = self.micros("decode_png palette (4 translate tables)", 20, lambda: decode_png(palette))

    # Grayscale replicates one plane with three strided assignments — C slicing, so
    # barely more than handing back the RGBA bytes unchanged.
    self.ratio("grayscale vs RGBA", gray_us, direct, ceiling=5.0)
    # Palette adds the index bounds check and four translate tables over the same
    # plane: a fixed number of C passes. A per-pixel Python lookup is ~500x.
    self.ratio("palette vs grayscale", pal_us, gray_us, ceiling=20.0)


if __name__ == "__main__":
  import unittest

  unittest.main()
