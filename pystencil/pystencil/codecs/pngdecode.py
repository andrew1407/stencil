"""The pure-Python PNG decoder, for 8-bit color types 0/2/3/4/6 with all five row filters,
not interlaced: ``png.decode_png`` falls back to it when the native library has no stb. On
what it accepts it gives stb's pixels, a tRNS colour key included; it never inflates past
the pixel plane its header claims.
"""

from __future__ import annotations

import struct
import zlib

from .pngfilter import _plane_table, unfilter
from .sniff import _PNG_MAGIC, CodecError, check_size


# Channels per pixel in the raw (pre-expansion) sample stream, by PNG color type.
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def _inflate(idat: (bytes | bytearray), wanted: int) -> bytes:
  """Inflate at most ``wanted`` bytes: trailing data is ignored, so a small stream that
  expands without end (a decompression bomb) costs no more than the plane it claims."""
  try:
    raw = zlib.decompressobj().decompress(idat, wanted)
  except zlib.error as exc:
    raise CodecError("corrupt PNG pixel data: %s" % exc) from None
  if len(raw) < wanted:
    raise CodecError("PNG pixel data is %d of %d bytes" % (len(raw), wanted))
  return raw


def _key_alpha(samples: bytearray, channels: int, key: bytes) -> bytes:
  """255 per pixel, 0 where every sample equals the tRNS key's low byte, as stb reads it."""
  hit = -1
  for c in range(channels):
    table = bytes(1 if v == key[c * 2 + 1] else 0 for v in range(256))
    hit &= int.from_bytes(samples[c::channels].translate(table), "big")
  return hit.to_bytes(len(samples) // channels, "big").translate(b"\xff\x00" + bytes(254))


def decode_png(data: bytes) -> tuple[int, int, bytearray]:
  """An 8-bit, non-interlaced PNG as RGBA8. A short inflate raises rather than decoding to a
  buffer under the ``width*height*4`` every caller sizes its reads by."""
  if data[:8] != _PNG_MAGIC:
    raise CodecError("not a PNG (bad signature)")

  pos = 8
  width = height = color_type = 0
  palette = trns = b""
  idat = bytearray()

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
      if length != 13:
        raise CodecError("PNG IHDR is %d bytes, not 13" % length)
      (width, height, bit_depth, color_type, _comp, _filt, interlace) = struct.unpack(
        ">IIBBBBB", chunk
      )
      check_size(width, height, "PNG")
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
    pos = cend + 4

  if width == 0 or height == 0:
    raise CodecError("PNG has no IHDR / zero dimensions")

  channels = CHANNELS.get(color_type)
  if channels is None:
    raise CodecError("unsupported PNG color type %d" % color_type)

  stride = width * channels  # bytes per scanline == channels per pixel at 8-bit depth
  raw = _inflate(idat, height * (stride + 1))
  del idat
  ftypes = raw[0::stride + 1]
  if max(ftypes) > 4:
    raise CodecError("unknown PNG filter type %d" % max(ftypes))
  # The scanlines with their filter bytes dropped, reversed in place top to bottom.
  out = bytearray(raw)
  del raw
  del out[0::stride + 1]
  unfilter(out, width, height, channels, ftypes)

  if color_type == 6:
    return width, height, out
  rgba = bytearray(b"\xff" * (width * height * 4))  # alpha defaults to opaque
  if color_type == 2:
    for c in (0, 1, 2):
      rgba[c::4] = out[c::3]
    if len(trns) == 6: rgba[3::4] = _key_alpha(out, 3, trns)
  elif color_type == 0:
    for c in (0, 1, 2):
      rgba[c::4] = out
    if len(trns) == 2: rgba[3::4] = _key_alpha(out, 1, trns)
  elif color_type == 4:
    gray = out[0::2]
    for c in (0, 1, 2):
      rgba[c::4] = gray
    rgba[3::4] = out[1::2]
  elif color_type == 3:
    # One 256-entry translate table per channel; an index past PLTE is opaque black.
    if not palette:
      raise CodecError("palette PNG missing PLTE chunk")
    for c in (0, 1, 2):
      rgba[c::4] = out.translate(_plane_table(palette, c, 3, 0))
    rgba[3::4] = out.translate(_plane_table(trns, 0, 1, 255))

  return width, height, rgba
