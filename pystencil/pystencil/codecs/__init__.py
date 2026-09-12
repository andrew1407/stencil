from __future__ import annotations

"""Pure-Python image I/O for pystencil (the C++ ``core/`` is codec-free by design).

Stdlib-only (``zlib`` + ``struct``), so it implements just the two formats it can do
correctly without a third-party codec, over RGBA8 ``bytearray`` buffers:

* PNG  — decode 8-bit color types 0/2/3/4/6 (all five row filters); encode as RGBA
    (type 6), 8-bit, filter 0.
* BMP  — 24/32-bit ``BI_RGB``, bottom-up rows, BGR(A) <-> RGBA.

JPEG needs a real DCT codec we don't ship: ``decode`` raises ``CodecError`` pointing
callers at the Zig CLI, which owns codec-heavy work.

Split across ``sniff`` (detection), ``png`` and ``bmp``; this module is the façade and
holds the one function that spans them.
"""

from .bmp import decode_bmp, encode_bmp
from .png import decode_png, encode_png
from .sniff import CodecError, format_from_ext, image_dimensions, sniff

# Module surface kept narrow and explicit so callers (and tests) bind to names,
# not to import order.
__all__ = [
  "CodecError", "sniff", "image_dimensions", "format_from_ext", "decode",
  "decode_png", "encode_png", "decode_bmp", "encode_bmp",
]


def decode(data: bytes) -> tuple[int, int, bytearray]:
  """Decode any supported image to ``(width, height, rgba8 bytearray)``.

  Dispatches on the sniffed magic. JPEG and unrecognized data raise
  ``CodecError`` with a message that points at the Zig CLI, which owns codecs.
  """
  kind = sniff(data)
  if kind == "png":
    return decode_png(data)
  if kind == "bmp":
    return decode_bmp(data)
  # JPEG (and anything else) needs a real codec we deliberately don't ship.
  raise CodecError(
    "JPEG decode needs the Zig CLI; PNG/BMP are supported natively"
  )
