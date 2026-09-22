"""Format detection: magic-byte sniffing and codec-free header dimension reads.

No decode here — just enough of each header to name the format and size it, so
callers can size-filter scraped media without a full decode.
"""

from __future__ import annotations

import struct

from .._ffi.types import NoneType


class CodecError(Exception):
  """Raised for any decode/encode failure (bad magic, unsupported subtype...)."""

  pass


# --- format detection ------------------------------------------------------

# The 8-byte PNG signature (RFC 2083) and the 2-byte BMP / JPEG markers.
_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
_BMP_MAGIC = b"BM"
_JPEG_MAGIC = b"\xff\xd8\xff"

Dimensions = tuple[int, int]


def sniff(data: bytes) -> str:
  """Identify a buffer by its leading magic bytes.

  Returns one of ``'png'``, ``'bmp'``, ``'jpeg'`` or ``'unknown'``. We only
  look at the header, never the extension, because callers may hand us bytes
  fetched over HTTP with no filename attached.
  """
  if data[:8] == _PNG_MAGIC:
    return "png"
  if data[:2] == _BMP_MAGIC:
    return "bmp"
  if data[:3] == _JPEG_MAGIC:
    return "jpeg"
  return "unknown"


def image_dimensions(data: bytes) -> (Dimensions | NoneType):
  """Return ``(width, height)`` sniffed from an image header, or ``None``.

  A codec-free header read covering PNG (IHDR), GIF (logical screen), BMP
  (BITMAPINFOHEADER), WebP (VP8/VP8L/VP8X) and JPEG (first Start-Of-Frame) — enough to
  size-filter scraped media without a full decode. Anything unrecognized or truncated
  returns ``None`` (an "unknown" size that dimension filters let pass). Used by the
  source-site scraper (``sitesource``); the extension does the same live via
  ``naturalWidth``/``naturalHeight``.
  """
  n = len(data)
  if n >= 24 and data[:8] == _PNG_MAGIC and data[12:16] == b"IHDR":
    w, h = struct.unpack(">II", data[16:24])
    return (w, h)
  if n >= 10 and data[:6] in (b"GIF87a", b"GIF89a"):
    w, h = struct.unpack("<HH", data[6:10])
    return (w, h)
  if n >= 26 and data[:2] == _BMP_MAGIC:
    w = struct.unpack("<i", data[18:22])[0]
    h = struct.unpack("<i", data[22:26])[0]
    return (abs(w), abs(h))
  if n >= 30 and data[:4] == b"RIFF" and data[8:12] == b"WEBP":
    return __webp_dimensions(data)
  if n >= 2 and data[:2] == b"\xff\xd8":
    return __jpeg_dimensions(data)
  return None


def __webp_dimensions(data: bytes) -> (Dimensions | NoneType):
  """Dimensions from a WebP header (simple VP8, lossless VP8L, or extended VP8X)."""
  fourcc = data[12:16]
  if fourcc == b"VP8 " and len(data) >= 30:
    # Lossy: 3-byte frame tag, 3-byte start code, then 14-bit width/height.
    w = (data[26] | (data[27] << 8)) & 0x3FFF
    h = (data[28] | (data[29] << 8)) & 0x3FFF
    return (w, h)
  if fourcc == b"VP8L" and len(data) >= 25:
    # Lossless: 1-byte signature (0x2f) then packed 14-bit (w-1) and (h-1).
    b0, b1, b2, b3 = data[21], data[22], data[23], data[24]
    w = 1 + (((b1 & 0x3F) << 8) | b0)
    h = 1 + (((b3 & 0x0F) << 10) | (b2 << 2) | ((b1 & 0xC0) >> 6))
    return (w, h)
  if fourcc == b"VP8X" and len(data) >= 30:
    # Extended: little-endian 24-bit (w-1) and 24-bit (h-1).
    w = 1 + (data[24] | (data[25] << 8) | (data[26] << 16))
    h = 1 + (data[27] | (data[28] << 8) | (data[29] << 16))
    return (w, h)
  return None


def __jpeg_dimensions(data: bytes) -> (Dimensions | NoneType):
  """Dimensions from the first JPEG Start-Of-Frame (SOF0-15, excl. DHT/JPG/DAC)."""
  i = 2
  n = len(data)
  while i + 4 <= n:
    if data[i] != 0xFF:
      i += 1
      continue
    marker = data[i + 1]
    if marker == 0xFF:
      i += 1  # skip fill byte
      continue
    # Standalone markers (SOI/EOI/RSTn/TEM) carry no length payload.
    if marker == 0x01 or 0xD0 <= marker <= 0xD9:
      i += 2
      continue
    seg_len = struct.unpack(">H", data[i + 2 : i + 4])[0]
    if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
      if i + 9 <= n:
        h, w = struct.unpack(">HH", data[i + 5 : i + 9])
        return (w, h)
      return None
    i += 2 + seg_len
  return None


def format_from_ext(path: str) -> (str | NoneType):
  """Map a filename's extension to an encoder name, or ``None`` if unknown.

  Used by ``Image.save`` to pick a format when none is given. Case-insensitive
  so ``"x.PNG"`` -> ``"png"``.
  """
  lower = path.lower()
  if lower.endswith(".png"):
    return "png"
  if lower.endswith(".bmp"):
    return "bmp"
  return None
