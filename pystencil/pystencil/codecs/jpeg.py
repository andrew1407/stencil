"""JPEG through stb: the CLI's own decoder and encoder units, compiled into the native library
beside the core by ``build.py`` and reached through :mod:`pystencil._ffi.stb`.

Decode refuses a header past ``MAX_SIDE`` before stb allocates; encode writes quality 90, the
CLI's (``cli/src/media/image.zig``). Neither reads EXIF orientation, as the CLI does not.
"""

from __future__ import annotations

from .._ffi.stb import encode_jpeg as _encode_jpeg
from . import stblib
from .sniff import CodecError, check_size, image_dimensions

JPEG_QUALITY = 90


def _library():
  """The native library with the shim bound, or ``CodecError`` naming why there is none."""
  return stblib.library("JPEG")


def decode_jpeg(data: bytes) -> tuple[int, int, bytearray]:
  """Decode a JPEG to ``(width, height, rgba8 bytearray)``, alpha 255 throughout."""
  dims = image_dimensions(data) or (0, 0)
  if dims != (0, 0): check_size(dims[0], dims[1], "JPEG")
  return stblib.decode("JPEG", _library(), data, stblib.block_cap(data, *dims))


def encode_jpeg(width: int, height: int, pixels: bytearray) -> bytes:
  """Encode an RGBA8 buffer as a baseline JPEG at :data:`JPEG_QUALITY`, alpha dropped."""
  lib = _library()
  try:
    return _encode_jpeg(lib, width, height, pixels, JPEG_QUALITY)
  except ValueError as exc:
    raise CodecError("JPEG encode failed: %s" % exc) from None
