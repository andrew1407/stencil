"""BMP decode through the CLI's stb unit in the native library (else ``bmpdecode``) — top-down
rows, 1/4/8-bit palettes, 16-bit and ``BI_BITFIELDS`` included — and a 32-bit ``BI_RGB``
encoder. Decode refuses a side past ``MAX_SIDE``, or pixel data shorter than the header
claims, before stb allocates.
"""

from __future__ import annotations

import struct

from . import bmpdecode, stblib
from .sniff import _BMP_MAGIC, CodecError, check_size

_PALETTE_ENTRIES = 256


def _header(data: bytes) -> tuple[int, int, int, int, int]:
  """``(pixel offset, info size, width, height, bits per pixel)``, stb's reading of the header:
  an OS/2 core header's 16-bit sides, and a negative (top-down) height as its magnitude."""
  if len(data) < 30:
    raise CodecError("truncated BMP header (%d bytes)" % len(data))
  offset, info = struct.unpack_from("<II", data, 10)
  if info == 12:
    width, height, _planes, bpp = struct.unpack_from("<HHHH", data, 18)
    return offset, info, width, height, bpp
  width, height = struct.unpack_from("<ii", data, 18)
  return offset, info, width, abs(height), struct.unpack_from("<H", data, 28)[0]


def _padded_palette(data: bytes, offset: int, info: int, bpp: int) -> bytes:
  """``data`` with a short palette grown to 256 black entries: stb looks an index past it up
  in a stack array it never set, so an out-of-range pixel would carry old stack bytes."""
  if bpp >= 16: return data
  entry = 3 if info == 12 else 4
  # stb's own count of the entries between the header and the pixels.
  used = (offset - 38) // 3 if info == 12 else (offset - 14 - info) // 4
  if used <= 0:
    raise CodecError("BMP has no palette for its %d-bit pixels" % bpp)
  if used >= _PALETTE_ENTRIES: return data
  grow = (_PALETTE_ENTRIES - used) * entry
  return (data[:10] + struct.pack("<I", offset + grow) + data[14:offset] + bytes(grow)
          + data[offset:])


def decode_bmp(data: bytes) -> tuple[int, int, bytearray]:
  """Decode a BMP to RGBA8 as the CLI does: a 32-bit alpha that is 0 throughout reads as
  opaque; RLE and embedded JPEG/PNG are refused."""
  if data[:2] != _BMP_MAGIC:
    raise CodecError("not a BMP (bad signature)")
  offset, info, width, height, bpp = _header(data)
  check_size(width, height, "BMP")
  row_bytes = (width * bpp + 7) // 8
  # The last row's padding may be left off, so only its pixels have to be present.
  if offset + (row_bytes + 3) // 4 * 4 * (height - 1) + row_bytes > len(data):
    raise CodecError("truncated BMP pixel data")
  lib = stblib.loaded()
  if lib is None: return bmpdecode.decode_bmp(data)
  padded = _padded_palette(data, offset, info, bpp)
  return stblib.decode("BMP", lib, padded, stblib.block_cap(data, width, height))


def encode_bmp(width: int, height: int, rgba: bytes | bytearray) -> bytes:
  """Encode an RGBA8 buffer as a 32-bit ``BI_RGB`` BMP (BGRA, bottom-up)."""
  if len(rgba) != width * height * 4:
    raise CodecError(
      "rgba length %d != %d (w*h*4)" % (len(rgba), width * height * 4)
    )

  bytes_per_px = 4
  # 32-bit rows are already 4-byte aligned, so no padding is needed.
  row_size = width * bytes_per_px
  pixel_data_size = row_size * height

  # Bottom-up BGRA pixel block: copy the rows in reverse, then swap R and B.
  pixels = bytearray(pixel_data_size)
  for row in range(height):
    src = (height - 1 - row) * row_size
    pixels[row * row_size:(row + 1) * row_size] = rgba[src:src + row_size]
  red = bytes(pixels[0::4])
  pixels[0::4] = pixels[2::4]
  pixels[2::4] = red

  file_header_size = 14
  info_header_size = 40
  pixel_offset = file_header_size + info_header_size
  file_size = pixel_offset + pixel_data_size

  file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
  info_header = struct.pack(
    "<IiiHHIIiiII",
    info_header_size,
    width,
    height,
    1,            # planes
    32,           # bits per pixel
    0,            # BI_RGB
    pixel_data_size,
    2835,         # ~72 DPI horizontal (pixels/metre)
    2835,         # ~72 DPI vertical
    0,            # colors used
    0,            # important colors
  )
  return bytes(file_header + info_header + pixels)
