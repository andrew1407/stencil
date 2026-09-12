from __future__ import annotations

"""PNG decode (8-bit color types 0/2/3/4/6, all five row filters) and encode.

Encode is deliberately trivial: color type 6, filter 0 on every scanline, zlib.
"""

import struct
import zlib

from .pngfilter import _plane_table, _swar_add, _unfilter_seq, _unfilter_sub
from .sniff import _PNG_MAGIC, CodecError


# Channels per pixel in the raw (pre-expansion) sample stream, by PNG color type.
_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def decode_png(data: bytes) -> tuple[int, int, bytearray]:
  """Decode an 8-bit PNG to RGBA8.

  Supports color types 0 (grayscale), 2 (RGB), 3 (palette), 4 (gray+alpha)
  and 6 (RGBA), bit depth 8 only. Concatenates all IDAT chunks, inflates them,
  then reverses the per-row filter (none/sub/up/average/paeth) before
  expanding each sample model out to RGBA. A short inflate raises rather than
  decoding to a buffer under the ``width*height*4`` every caller sizes its reads by.
  """
  if data[:8] != _PNG_MAGIC:
    raise CodecError("not a PNG (bad signature)")

  pos = 8
  width = 0
  height = 0
  bit_depth = 0
  color_type = 0
  palette = b""
  trns = b""
  idat = bytearray()

  # Walk the chunk stream: each chunk is length(4) + type(4) + data + crc(4).
  total = len(data)
  while pos + 8 <= total:
    length = struct.unpack(">I", data[pos:pos + 4])[0]
    ctype = data[pos + 4:pos + 8]
    cstart = pos + 8
    cend = cstart + length
    if cend > total:
      raise CodecError("truncated PNG chunk")
    chunk = data[cstart:cend]
    if ctype == b"IHDR":
      (width, height, bit_depth, color_type, comp, filt, interlace) = struct.unpack(
        ">IIBBBBB", chunk
      )
      if bit_depth != 8:
        raise CodecError("only 8-bit PNG is supported (got %d)" % bit_depth)
      if interlace != 0:
        raise CodecError("interlaced PNG is not supported")
    elif ctype == b"PLTE":
      palette = chunk
    elif ctype == b"tRNS":
      trns = chunk
    elif ctype == b"IDAT":
      idat += chunk
    elif ctype == b"IEND":
      break
    # advance past data + 4-byte CRC (we trust zlib to catch corruption)
    pos = cend + 4

  if width == 0 or height == 0:
    raise CodecError("PNG has no IHDR / zero dimensions")

  channels = _CHANNELS.get(color_type)
  if channels is None:
    raise CodecError("unsupported PNG color type %d" % color_type)

  raw = zlib.decompress(bytes(idat))

  stride = width * channels
  bpp = channels  # bytes per pixel == channels at 8-bit depth
  # Masks for the whole-row adds, built once — every scanline shares the stride.
  low = int.from_bytes(b"\x7f" * stride, "big")
  high = int.from_bytes(b"\x80" * stride, "big")

  wanted = height * (stride + 1)
  if len(raw) < wanted:
    raise CodecError("PNG pixel data is %d of %d bytes" % (len(raw), wanted))

  # Reverse the per-row filter; row 0's "previous row" is all zeros (RFC 2083).
  rows = list()
  prev = b"\x00" * stride
  src = 0
  for _y in range(height):
    ftype = raw[src]
    src += 1
    row = raw[src:src + stride]
    src += stride
    if ftype == 0:
      cur = row  # no filter — what our own encoder emits
    elif ftype == 1:
      cur = _unfilter_sub(row, bpp, low, high)
    elif ftype == 2:
      # Up: one whole-row add against the previous scanline.
      cur = _swar_add(
        int.from_bytes(row, "big"), int.from_bytes(prev, "big"), low, high
      ).to_bytes(len(row), "big")
    elif ftype == 3 or ftype == 4:
      cur = _unfilter_seq(ftype, row, prev, bpp)
    else:
      raise CodecError("unknown PNG filter type %d" % ftype)
    rows.append(cur)
    prev = cur
  out = b"".join(rows)

  # Expand the sample model out to interleaved RGBA8. Every branch is a strided slice
  # assignment (a C-level copy) rather than a per-pixel loop.
  if color_type == 6:
    return width, height, bytearray(out)
  rgba = bytearray(b"\xff" * (width * height * 4))  # alpha defaults to opaque
  if color_type == 2:
    for c in (0, 1, 2):
      rgba[c::4] = out[c::3]
  elif color_type == 0:
    # Grayscale: replicate the single sample across R/G/B.
    for c in (0, 1, 2):
      rgba[c::4] = out
  elif color_type == 4:
    # Grayscale + alpha.
    gray = out[0::2]
    for c in (0, 1, 2):
      rgba[c::4] = gray
    rgba[3::4] = out[1::2]
  elif color_type == 3:
    # Palette index -> PLTE RGB, plus optional per-index alpha from tRNS: one
    # 256-entry translate table per channel turns each plane in C.
    if not palette:
      raise CodecError("palette PNG missing PLTE chunk")
    if out and max(out) >= len(palette) // 3:
      raise CodecError("PNG palette index out of range")
    for c in (0, 1, 2):
      rgba[c::4] = out.translate(_plane_table(palette, c, 3, 0))
    rgba[3::4] = out.translate(_plane_table(trns, 0, 1, 255))

  return width, height, rgba


def _png_chunk(ctype: bytes, payload: bytes) -> bytes:
  """Assemble one PNG chunk: length, type, payload, CRC32 over type+payload."""
  crc = zlib.crc32(ctype)
  crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
  return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)


def encode_png(width: int, height: int, rgba: bytes | bytearray) -> bytes:
  """Encode an RGBA8 buffer as a PNG (color type 6, 8-bit, filter 0).

  We always prepend filter byte 0 ("none") to each scanline and let zlib do
  the compression; this keeps the encoder trivial while staying a valid,
  widely-readable PNG. CRC32 is computed per chunk via ``zlib.crc32``.
  """
  if len(rgba) != width * height * 4:
    raise CodecError(
      "rgba length %d != %d (w*h*4)" % (len(rgba), width * height * 4)
    )

  stride = width * 4
  # Filter-0 framing: one 0 byte in front of every row of raw RGBA samples.
  raw = b"".join(
    b"\x00" + bytes(rgba[y * stride:(y + 1) * stride]) for y in range(height)
  )

  ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
  idat = zlib.compress(raw, 9)
  return (
    _PNG_MAGIC
    + _png_chunk(b"IHDR", ihdr)
    + _png_chunk(b"IDAT", idat)
    + _png_chunk(b"IEND", b"")
  )

