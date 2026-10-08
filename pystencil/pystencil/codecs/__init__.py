"""Image I/O over RGBA8 ``bytearray`` buffers (the C++ ``core/`` is codec-free by design).

PNG, BMP and JPEG decode through the CLI's own stb unit in the native library, PNG and BMP
falling back to ``pngdecode`` / ``bmpdecode``; PNG and BMP encode in pure Python, JPEG through
stb at the CLI's quality 90. Every decoder refuses a header past ``sniff.MAX_SIDE`` before it
allocates."""

from __future__ import annotations

from .bmp import decode_bmp, encode_bmp
from .jpeg import JPEG_QUALITY, decode_jpeg, encode_jpeg
from .png import decode_png, encode_png
from .sniff import CodecError, format_from_ext, image_dimensions, sniff

__all__ = [
  "CodecError", "sniff", "image_dimensions", "format_from_ext", "decode",
  "decode_png", "encode_png", "decode_bmp", "encode_bmp", "decode_jpeg", "encode_jpeg",
  "JPEG_QUALITY",
]


def decode(data: bytes) -> tuple[int, int, bytearray]:
  """``(width, height, rgba8)`` by the sniffed magic; unrecognized data raises ``CodecError``."""
  kind = sniff(data)
  if kind == "png":
    return decode_png(data)
  if kind == "bmp":
    return decode_bmp(data)
  if kind == "jpeg":
    return decode_jpeg(data)
  raise CodecError("unrecognized image data; PNG, BMP and JPEG are supported")
