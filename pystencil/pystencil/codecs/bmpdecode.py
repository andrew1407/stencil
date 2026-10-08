"""The pure-Python BMP decoder, for 24/32-bit ``BI_RGB`` only: ``bmp.decode_bmp`` falls back to
it when the native library has no stb. On what it accepts it gives stb's pixels.
"""

from __future__ import annotations

import struct

from .sniff import _BMP_MAGIC, CodecError, check_size

# BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40): the fields decode_bmp reads.
_HEADER_BYTES = 54


def decode_bmp(data: bytes) -> tuple[int, int, bytearray]:
  """A 24- or 32-bit ``BI_RGB`` BMP as RGBA8; a 32-bit alpha that is 0 throughout reads as
  opaque, as stb reads it."""
  if data[:2] != _BMP_MAGIC:
    raise CodecError("not a BMP (bad signature)")
  if len(data) < _HEADER_BYTES:
    raise CodecError("truncated BMP header (%d bytes)" % len(data))

  pixel_offset = struct.unpack("<I", data[10:14])[0]
  header_size = struct.unpack("<I", data[14:18])[0]
  width = struct.unpack("<i", data[18:22])[0]
  height_raw = struct.unpack("<i", data[22:26])[0]
  bpp = struct.unpack("<H", data[28:30])[0]
  compression = struct.unpack("<I", data[30:34])[0]

  if compression != 0:
    raise CodecError("only uncompressed BI_RGB BMP is supported")
  if bpp not in (24, 32):
    raise CodecError("only 24/32-bit BMP is supported (got %d)" % bpp)

  # Negative height means a top-down image (rare, but legal).
  top_down = height_raw < 0
  height = abs(height_raw)
  check_size(width, height, "BMP")

  bytes_per_px = bpp // 8
  # Each row is padded up to a multiple of 4 bytes.
  row_size = ((width * bytes_per_px + 3) // 4) * 4
  # The last row's padding may be left off, so only its pixels have to be present.
  if pixel_offset + row_size * (height - 1) + width * bytes_per_px > len(data):
    raise CodecError("truncated BMP pixel data")

  rgba = bytearray(b"\xff" * (width * height * 4))  # alpha defaults to opaque
  view = memoryview(rgba)
  for row in range(height):
    base = pixel_offset + (row if top_down else height - 1 - row) * row_size
    line = data[base:base + width * bytes_per_px]
    dst = view[row * width * 4:(row + 1) * width * 4]
    dst[0::4] = line[2::bytes_per_px]
    dst[1::4] = line[1::bytes_per_px]
    dst[2::4] = line[0::bytes_per_px]
    if bytes_per_px == 4:
      dst[3::4] = line[3::4]

  if bytes_per_px == 4 and rgba and max(view[3::4]) == 0:
    view[3::4] = b"\xff" * (width * height)
  return width, height, rgba
