"""Reversing the five PNG row filters, and the palette/grayscale expansion tables.

Split out of ``png.py``: this is the decoder's whole per-byte-cost story, and the one
place that decides which filters get a whole-row form. ``tests/bench_codecs.py`` pins
the cost ratios between them.
"""

from __future__ import annotations


def __paeth(a: int, b: int, c: int) -> int:
  """The PNG Paeth predictor (RFC 2083 sec. 6.6): pick the neighbour closest
  to ``a + b - c`` (left, above, upper-left), ties broken toward ``a``."""
  p = a + b - c
  pa = abs(p - a)
  pb = abs(p - b)
  pc = abs(p - c)
  if pa <= pb and pa <= pc:
    return a
  if pb <= pc:
    return b
  return c


def _swar_add(a: int, b: int, low: int, high: int) -> int:
  """Byte-wise (mod 256) add of two scanlines packed as big ints — the low 7 bits sum
  and bit 7 arrives by XOR, so no byte carries into its neighbour. `low`/`high` are the
  0x7f.../0x80... masks for the row width."""
  return ((a & low) + (b & low)) ^ ((a ^ b) & high)


def _unfilter_sub(row: bytes, bpp: int, low: int, high: int) -> bytes:
  """Filter 1 (Sub): add the byte `bpp` to the left, as a doubling prefix scan."""
  n = len(row)
  acc = int.from_bytes(row, "big")
  step = bpp
  while step < n:
    acc = _swar_add(acc, acc >> (step * 8), low, high)
    step *= 2
  return acc.to_bytes(n, "big")


def _unfilter_seq(ftype: int, row: bytes, prev: bytes, bpp: int) -> bytes:
  """Filters 3 (Average) and 4 (Paeth): each byte needs the RECONSTRUCTED byte to its
  left, so they are genuinely sequential and stay a per-byte loop."""
  out = bytearray(len(row))
  for x in range(len(row)):
    a = out[x - bpp] if x >= bpp else 0
    b = prev[x]
    if ftype == 3:
      out[x] = (row[x] + ((a + b) >> 1)) & 0xFF
    else:
      c = prev[x - bpp] if x >= bpp else 0
      out[x] = (row[x] + __paeth(a, b, c)) & 0xFF
  return bytes(out)


def _plane_table(values: bytes, start: int, step: int, fill: int) -> bytes:
  """A 256-entry `bytes.translate` table: i -> values[start + i * step], else `fill`."""
  n = len(values)
  picks = (start + i * step for i in range(256))
  return bytes(values[i] if i < n else fill for i in picks)
