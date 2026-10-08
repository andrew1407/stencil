"""PNG decode through the CLI's stb unit in the native library (else ``pngdecode``), and an
encoder writing color type 6 with a per-row None/Sub/Up filter at zlib level 6. Decode refuses
a side past ``MAX_SIDE``, or a stream too short for its rows, before stb allocates; stb then
takes no block past ``stblib.block_cap``, so a stream cannot inflate far past its rows.
"""

from __future__ import annotations

import struct
import zlib

from . import pngdecode, stblib
from .pngfilter import filter_row, pick_strategy
from .sniff import _PNG_MAGIC, CodecError, check_size

# Deflate emits at most 1032 bytes per input byte.
_DEFLATE_RATIO = 1032
_PALETTE_BYTES = 256 * 3


def _chunks(data: bytes):
  """``(type, payload start, length)`` per chunk header, through IEND or the last byte."""
  pos = 8
  while pos + 8 <= len(data):
    length = struct.unpack_from(">I", data, pos)[0]
    kind = bytes(data[pos + 4:pos + 8])
    yield kind, pos + 8, length
    if kind == b"IEND": return
    pos += 12 + length


def _padded_palette(data: bytes) -> bytes:
  """``data`` with a short PLTE grown to 256 black entries: stb looks an index past PLTE up
  in a stack array it never set, so an out-of-range pixel would carry old stack bytes."""
  if data[25:26] != b"\x03": return data
  for kind, start, length in _chunks(data):
    if kind == b"PLTE":
      if not 0 < length < _PALETTE_BYTES or length % 3: return data
      grown = bytes(data[start:start + length]) + bytes(_PALETTE_BYTES - length)
      return data[:start - 8] + __png_chunk(b"PLTE", grown) + data[start + length + 4:]
    if kind == b"IDAT": return data
  return data


def _block_cap(data: bytes) -> int:
  """stb's block cap for ``data``, after refusing a side past the cap or a stream that could
  not inflate to the rows its IHDR claims."""
  if data[8:16] != b"\x00\x00\x00\x0dIHDR" or len(data) < 29:
    return stblib.block_cap(data, 0, 0)
  width, height, depth, color = struct.unpack_from(">IIBB", data, 16)
  check_size(width, height, "PNG")
  rows = height * ((width * pngdecode.CHANNELS.get(color, 4) * depth + 7) // 8 + 1)
  idat = sum(length for kind, _, length in _chunks(data) if kind == b"IDAT")
  if idat * _DEFLATE_RATIO + stblib.SLACK < rows:
    raise CodecError("PNG pixel data is %d bytes, too few to inflate to %d" % (idat, rows))
  return stblib.block_cap(data, width, height, rows)


def decode_png(data: bytes) -> tuple[int, int, bytearray]:
  """Decode a PNG — 1 to 16 bits, interlaced or not — to RGBA8, as the CLI does: a 16-bit
  sample keeps its high byte, and a tRNS colour key clears the alpha of the pixels it names."""
  if data[:8] != _PNG_MAGIC:
    raise CodecError("not a PNG (bad signature)")
  cap = _block_cap(data)
  lib = stblib.loaded()
  if lib is None: return pngdecode.decode_png(data)
  return stblib.decode("PNG", lib, _padded_palette(data), cap)


def __png_chunk(ctype: bytes, payload: bytes) -> bytes:
  """Assemble one PNG chunk: length, type, payload, CRC32 over type+payload."""
  crc = zlib.crc32(ctype)
  crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
  return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)


def encode_png(width: int, height: int, rgba: bytes | bytearray) -> bytes:
  """An RGBA8 buffer as an 8-bit type-6 PNG at zlib level 6, its filters picked by a deflate
  trial among the whole-row ones a decode reverses fastest."""
  if len(rgba) != width * height * 4:
    raise CodecError(
      "rgba length %d != %d (w*h*4)" % (len(rgba), width * height * 4)
    )

  stride = width * 4
  low = int.from_bytes(b"\x7f" * stride, "big")
  high = int.from_bytes(b"\x80" * stride, "big")
  allowed = pick_strategy(rgba, width, height, low, high)
  raw = bytearray(height * (stride + 1))
  prev = 0
  for y in range(height):
    ftype, line, prev = filter_row(
      rgba[y * stride:(y + 1) * stride], prev, 4, low, high, allowed)
    at = y * (stride + 1)
    raw[at] = ftype
    raw[at + 1:at + 1 + stride] = line

  ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
  idat = zlib.compress(raw, 6)
  return (
    _PNG_MAGIC
    + __png_chunk(b"IHDR", ihdr)
    + __png_chunk(b"IDAT", idat)
    + __png_chunk(b"IEND", b"")
  )
