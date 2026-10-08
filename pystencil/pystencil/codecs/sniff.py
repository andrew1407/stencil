"""Format detection: magic-byte sniffing and codec-free header dimension reads."""

from __future__ import annotations

import struct

from .._ffi.types import NoneType


class CodecError(Exception):
  """Raised for any decode/encode failure (bad magic, unsupported subtype...)."""

  pass


# Per-side decode cap, the CLI's STBI_MAX_DIMENSIONS (cli/src/media/image.zig max_pixels):
# a header can never make a decoder allocate more than 16384² × 4 bytes.
MAX_SIDE = 16384


def check_size(width: int, height: int, fmt: str) -> None:
  """Refuse a header whose dimensions are non-positive or past :data:`MAX_SIDE`."""
  if width <= 0 or height <= 0:
    raise CodecError("%s has zero or negative dimensions (%dx%d)" % (fmt, width, height))
  if width > MAX_SIDE or height > MAX_SIDE:
    raise CodecError(
      "%s is %dx%d, past the %d-pixel side cap" % (fmt, width, height, MAX_SIDE))


# The 8-byte PNG signature (RFC 2083) and the 2-byte BMP / JPEG markers.
_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
_BMP_MAGIC = b"BM"
_JPEG_MAGIC = b"\xff\xd8\xff"
# The PNG spec caps a side at 2^31 - 1.
_PNG_MAX_SIDE = 0x7FFFFFFF

Dimensions = tuple[int, int]


def sniff(data: bytes) -> str:
  """``'png'``, ``'bmp'``, ``'jpeg'`` or ``'unknown'`` by the magic bytes, never an extension:
  fetched bytes carry no filename."""
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
  size-filter scraped media without a full decode. Anything unrecognized, truncated or
  zero-sized returns ``None`` (an "unknown" size that dimension filters let pass). Held to
  the shared corpus ``common/fixtures/imageHeader/cases.json``.
  """
  n = len(data)
  if n >= 24 and data[:8] == _PNG_MAGIC and data[12:16] == b"IHDR":
    w, h = struct.unpack(">II", data[16:24])
    return __checked(w, h) if max(w, h) <= _PNG_MAX_SIDE else None
  if n >= 10 and data[:6] in (b"GIF87a", b"GIF89a"):
    return __checked(*struct.unpack("<HH", data[6:10]))
  if n >= 26 and data[:2] == _BMP_MAGIC:
    w, h = struct.unpack("<ii", data[18:26])
    return __checked(abs(w), abs(h))
  if n >= 30 and data[:4] == b"RIFF" and data[8:12] == b"WEBP":
    return __webp_dimensions(data)
  if n >= 4 and data[:2] == b"\xff\xd8":
    return __jpeg_dimensions(data)
  return None


def __checked(w: int, h: int) -> (Dimensions | NoneType):
  """A size is only an answer when both sides are non-zero."""
  return (w, h) if w > 0 and h > 0 else None


def __webp_dimensions(data: bytes) -> (Dimensions | NoneType):
  """Dimensions from a WebP header (simple VP8, lossless VP8L, or extended VP8X)."""
  fourcc = data[12:16]
  if fourcc == b"VP8 " and data[23:26] == b"\x9d\x01\x2a":
    # Lossy: 3-byte frame tag, the start code, then 14-bit width/height past 2 scale bits.
    w = (data[26] | (data[27] << 8)) & 0x3FFF
    h = (data[28] | (data[29] << 8)) & 0x3FFF
    return __checked(w, h)
  if fourcc == b"VP8L" and data[20] == 0x2F:
    # Lossless: the 0x2f signature, then packed 14-bit (w-1) and (h-1).
    b0, b1, b2, b3 = data[21], data[22], data[23], data[24]
    w = 1 + (((b1 & 0x3F) << 8) | b0)
    h = 1 + (((b3 & 0x0F) << 10) | (b2 << 2) | ((b1 & 0xC0) >> 6))
    return (w, h)
  if fourcc == b"VP8X":
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
      i += 1  # bytes that are no marker are skipped, as the decoder skips them
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
        return __checked(w, h)
      return None
    if seg_len < 2: return None
    i += 2 + seg_len
  return None


def format_from_ext(path: str) -> (str | NoneType):
  """The encoder a filename's extension names, case-insensitively, or ``None`` if unknown."""
  lower = path.lower()
  if lower.endswith(".png"):
    return "png"
  if lower.endswith(".bmp"):
    return "bmp"
  if lower.endswith((".jpg", ".jpeg")):
    return "jpeg"
  return None
