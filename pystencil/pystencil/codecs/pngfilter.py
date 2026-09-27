"""The PNG row filters: reversing all five for the decoder, choosing among the whole-row
three for the encoder, and the palette/grayscale expansion tables.

None, Sub and Up have whole-row forms over big ints. Average and Paeth run per byte on a
band with few of them, and as ``pngwave``'s anti-diagonal wavefront where that is
cheaper. ``tests/bench/bench_codecs.py`` pins the cost ratios between the paths.
"""

from __future__ import annotations

import zlib

from .pngwave import unfilter_band

# Rows per wavefront band: past this the per-diagonal ints stop getting cheaper per byte.
BAND = 256

# Measured cost model, in units of one 16-bit lane of wavefront arithmetic: a diagonal
# costs a fixed _DIAGONAL plus its lanes; a per-byte Average/Paeth step costs _PER_BYTE.
_DIAGONAL = 90
_PER_BYTE = 12


def _swar_add(a: int, b: int, low: int, high: int) -> int:
  """Byte-wise (mod 256) add of two scanlines packed as big ints — the low 7 bits sum
  and bit 7 arrives by XOR, so no byte carries into its neighbour. `low`/`high` are the
  0x7f.../0x80... masks for the row width."""
  return ((a & low) + (b & low)) ^ ((a ^ b) & high)


def _swar_sub(a: int, b: int, low: int, high: int) -> int:
  """Byte-wise (mod 256) a - b: bit 7 is lent to every byte so none borrows from its
  neighbour, and the XOR puts back the bit 7 the borrow decided."""
  return ((a | high) - (b & low)) ^ (((a ^ b) & high) ^ high)


def _unfilter_sub(row: bytes, bpp: int, low: int, high: int) -> bytes:
  """Filter 1 (Sub): add the byte `bpp` to the left, as a doubling prefix scan."""
  n = len(row)
  acc = int.from_bytes(row, "big")
  step = bpp
  while step < n:
    acc = _swar_add(acc, acc >> (step * 8), low, high)
    step *= 2
  return acc.to_bytes(n, "big")


def _unfilter_avg(row: bytes, prev: bytes, bpp: int) -> bytearray:
  """Filter 3 (Average), one byte at a time."""
  out = bytearray(row)
  for x in range(min(bpp, len(out))):
    out[x] = (out[x] + (prev[x] >> 1)) & 0xFF
  for x in range(bpp, len(out)):
    out[x] = (out[x] + ((out[x - bpp] + prev[x]) >> 1)) & 0xFF
  return out


def _unfilter_paeth(row: bytes, prev: bytes, bpp: int) -> bytearray:
  """Filter 4 (Paeth), one byte at a time; the first pixel's predictor is just `b`."""
  out = bytearray(row)
  for x in range(min(bpp, len(out))):
    out[x] = (out[x] + prev[x]) & 0xFF
  for x in range(bpp, len(out)):
    a, b, c = out[x - bpp], prev[x], prev[x - bpp]
    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - c - c)
    out[x] = (out[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
  return out


def _unfilter_row(plane: bytearray, y: int, stride: int, bpp: int, ftype: int,
                  low: int, high: int) -> None:
  """Reverse row `y` of `plane` in place against the finished row above it."""
  if ftype == 0: return
  at = y * stride
  row = plane[at:at + stride]
  prev = plane[at - stride:at] if y else bytes(stride)
  if ftype == 1:
    cur = _unfilter_sub(row, bpp, low, high)
  elif ftype == 2:
    both = _swar_add(int.from_bytes(row, "big"), int.from_bytes(prev, "big"), low, high)
    cur = both.to_bytes(stride, "big")
  elif ftype == 3:
    cur = _unfilter_avg(row, prev, bpp)
  else:
    cur = _unfilter_paeth(row, prev, bpp)
  plane[at:at + stride] = cur


def unfilter(plane: bytearray, width: int, height: int, bpp: int, ftypes: bytes) -> None:
  """Reverse every row filter of `plane` — the scanlines with their filter bytes
  stripped, `ftypes` holding those bytes — in place, top to bottom."""
  stride = width * bpp
  low = int.from_bytes(b"\x7f" * stride, "big")
  high = int.from_bytes(b"\x80" * stride, "big")
  for y0 in range(0, height, BAND):
    y1 = min(height, y0 + BAND)
    band = ftypes[y0:y1]
    per_byte = (band.count(3) + band.count(4)) * stride * _PER_BYTE
    lanes = (y1 - y0 + 1) * bpp
    if per_byte > (width + y1 - y0) * (_DIAGONAL + lanes):
      unfilter_band(plane, width, bpp, y0, y1, band)
      continue
    for y in range(y0, y1):
      _unfilter_row(plane, y, stride, bpp, ftypes[y], low, high)


# byte -> the bit length of its distance from zero mod 256: summed over every 7th byte
# of a filtered row (co-prime with any pixel size), an estimate of its deflated size.
_MAGNITUDE = bytes(min(v, 256 - v).bit_length() for v in range(256))

# The filter sets a trial picks among: none, Up, or per row the cheapest of None/Sub/Up.
STRATEGIES = ((0,), (2,), (0, 1, 2))
# The trial deflates this many windows of this many consecutive rows.
_WINDOWS, _WINDOW_ROWS = 16, 4


def _cost(line: bytes) -> int:
  sample = line[::7].translate(_MAGNITUDE)
  return sum(k * sample.count(k) for k in range(1, 9))


def filter_row(row: bytes, prev: int, bpp: int, low: int, high: int,
               allowed: tuple = (0, 1, 2)) -> tuple:
  """``(filter byte, filtered row, row as int)`` for whichever ``allowed`` filter leaves
  the smallest magnitudes; all three reverse whole-row, so a decode stays cheap."""
  cur = int.from_bytes(row, "big")
  best = None
  for ftype in allowed:
    out = row
    if ftype:
      base = cur >> (bpp * 8) if ftype == 1 else prev
      out = _swar_sub(cur, base, low, high).to_bytes(len(row), "big")
    if len(allowed) == 1: return ftype, out, cur
    cost = _cost(out)
    if best is None or cost < best[0]: best = (cost, ftype, out)
  return best[1], best[2], cur


def pick_strategy(rgba: bytes, width: int, height: int, low: int, high: int) -> tuple:
  """The :data:`STRATEGIES` entry whose rows deflate smallest over row windows spread down
  the image: flat text and line art want None, photographs Up or Sub."""
  stride = width * 4
  rows = sorted({
    y for w in range(_WINDOWS)
    for y in range(w * height // _WINDOWS, min(height, w * height // _WINDOWS + _WINDOW_ROWS))
  })
  best = None
  for allowed in STRATEGIES:
    sample = bytearray()
    for y in rows:
      prev = int.from_bytes(rgba[(y - 1) * stride:y * stride], "big") if y else 0
      ftype, line, _ = filter_row(rgba[y * stride:(y + 1) * stride], prev, 4, low, high, allowed)
      sample.append(ftype)
      sample += line
    size = len(zlib.compress(sample, 6))
    if best is None or size < best[0]: best = (size, allowed)
  return best[1]


def _plane_table(values: bytes, start: int, step: int, fill: int) -> bytes:
  """A 256-entry `bytes.translate` table: i -> values[start + i * step], else `fill`."""
  n = len(values)
  picks = (start + i * step for i in range(256))
  return bytes(values[i] if i < n else fill for i in picks)
