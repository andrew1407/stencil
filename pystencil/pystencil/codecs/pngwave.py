"""Reversing a band of PNG scanlines along anti-diagonals — the whole-int form of Average
and Paeth, which need the reconstructed byte to their left and so cannot go row-wise.

Pixel (x, y) reads only (x-1, y), (x, y-1) and (x-1, y-1): every pixel of anti-diagonal
x + y = k depends on diagonals k-1 and k-2 alone. The band is skewed into diagonal-major
order, each diagonal becomes one int of 16-bit lanes, and all five predictors run on it.
"""

from __future__ import annotations

# filter type -> a translate table turning a band's filter bytes into its 0xFF row selector.
_SELECT = {f: bytes(0xFF if i == f else 0 for i in range(256)) for f in (1, 2, 3, 4)}


def _lanes(count: int, value: int) -> int:
  """``count`` little-endian 16-bit lanes, each holding ``value``."""
  return int.from_bytes(value.to_bytes(2, "little") * count, "little")


def _ge(x: int, y: int, k10: int, one: int) -> int:
  """0xFFFF in every lane where x >= y (lanes < 1024): bit 10 of x + 1024 - y."""
  g = (((x | k10) - y) >> 10) & one
  return (g << 16) - g


def _absdiff(x: int, y: int, k10: int, one: int) -> int:
  """|x - y| per lane, as max - min so no lane borrows from its neighbour."""
  d = (x ^ y) & _ge(x, y, k10, one)
  return (y ^ d) - (x ^ d)


def _paeth(a: int, b: int, c: int, k10: int, one: int) -> int:
  """The RFC 2083 predictor per lane: a when pa <= pb, else b; then c when pc is smaller
  still — the same tie order as picking a, then b, then c."""
  pa = _absdiff(b, c, k10, one)
  pb = _absdiff(a, c, k10, one)
  pc = _absdiff(a + b, c << 1, k10, one)
  m = _ge(pb, pa, k10, one)
  best = b ^ ((a ^ b) & m)
  keep = _ge(pc, pb ^ ((pa ^ pb) & m), k10, one)
  return c ^ ((best ^ c) & keep)


def _row_mask(sel: bytes, bpp: int, n: int) -> int:
  """0x00FF in the lanes of every band row whose selector byte is set (lane row 0 never)."""
  lanes = bytearray(2 * n * bpp)
  for c in range(bpp):
    lanes[2 * (bpp + c)::2 * bpp] = sel
  return int.from_bytes(lanes, "little")


def unfilter_band(plane: bytearray, width: int, bpp: int, y0: int, y1: int,
                  ftypes: bytes) -> None:
  """Reverse rows ``y0``..``y1``-1 of ``plane`` in place; ``ftypes`` are their filter
  bytes and row ``y0``-1 (zeros above the image) is already reconstructed."""
  stride = width * bpp
  n = y1 - y0 + 1  # lane row 0 carries the finished row above the band
  step = n * bpp
  skew = bytearray((width + n - 1) * step)
  for r in range(0 if y0 else 1, n):
    at, src = r * (n + 1) * bpp, (y0 - 1 + r) * stride
    row = plane[src:src + stride]
    for c in range(bpp):
      skew[at + c:at + c + width * step:step] = row[c::bpp]
  wide = bytearray(2 * len(skew))
  wide[0::2] = skew

  ff, one, k10 = _lanes(n * bpp, 0xFF), _lanes(n * bpp, 1), _lanes(n * bpp, 0x400)
  m_sub, m_up, m_avg, m_paeth = (
    _row_mask(ftypes.translate(_SELECT[f]), bpp, n) for f in (1, 2, 3, 4))
  shift = 16 * bpp  # one lane row: moving a diagonal down a row reads the pixel above
  size = 2 * step
  last = prior = 0
  for off in range(0, len(wide), size):
    up = (last << shift) & ff
    pred = (last & m_sub) | (up & m_up)
    if m_avg: pred |= ((last + up) >> 1) & m_avg
    if m_paeth: pred |= _paeth(last, up, (prior << shift) & ff, k10, one) & m_paeth
    prior, last = last, (int.from_bytes(wide[off:off + size], "little") + pred) & ff
    wide[off:off + size] = last.to_bytes(size, "little")

  skew = wide[0::2]
  for r in range(1, n):
    at, dst = r * (n + 1) * bpp, (y0 - 1 + r) * stride
    for c in range(bpp):
      plane[dst + c:dst + stride:bpp] = skew[at + c:at + c + width * step:step]
