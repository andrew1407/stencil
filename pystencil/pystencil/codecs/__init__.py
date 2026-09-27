"""Image I/O for pystencil (the C++ ``core/`` is codec-free by design), over RGBA8
``bytearray`` buffers:

* PNG  — decode through the CLI's own stb unit in the native library, else the pure-Python
    8-bit ``pngdecode``; encode in pure Python as RGBA (type 6), a None/Sub/Up filter per row.
* BMP  — decode through the same stb unit, else the pure-Python 24/32-bit ``BI_RGB``
    ``bmpdecode``; encode in pure Python as 32-bit ``BI_RGB``, bottom-up.
* JPEG — the CLI's own stb units in the native library (``jpeg``): decode, and encode at
    the CLI's quality 90.

Every decoder refuses a header past ``sniff.MAX_SIDE`` before it allocates. Split across
``sniff`` (detection, the size cap), ``stblib`` (the native library), ``png`` (with
``pngdecode``, ``pngfilter`` and ``pngwave``), ``bmp`` (with ``bmpdecode``) and ``jpeg``;
this module is the façade and holds the one function that spans them.
"""

from __future__ import annotations

from .bmp import decode_bmp, encode_bmp
from .jpeg import JPEG_QUALITY, decode_jpeg, encode_jpeg
from .png import decode_png, encode_png
from .sniff import CodecError, format_from_ext, image_dimensions, sniff

# Module surface kept narrow and explicit so callers (and tests) bind to names,
# not to import order.
__all__ = [
  "CodecError", "sniff", "image_dimensions", "format_from_ext", "decode",
  "decode_png", "encode_png", "decode_bmp", "encode_bmp", "decode_jpeg", "encode_jpeg",
  "JPEG_QUALITY",
]


def decode(data: bytes) -> tuple[int, int, bytearray]:
  """Decode any supported image to ``(width, height, rgba8 bytearray)``.

  Dispatches on the sniffed magic; unrecognized data raises ``CodecError``.
  """
  kind = sniff(data)
  if kind == "png":
    return decode_png(data)
  if kind == "bmp":
    return decode_bmp(data)
  if kind == "jpeg":
    return decode_jpeg(data)
  raise CodecError("unrecognized image data; PNG, BMP and JPEG are supported")
