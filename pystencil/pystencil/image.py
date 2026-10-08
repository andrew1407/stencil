"""The ``Image`` value type: interleaved RGBA8, top-to-bottom rows, no stride — the buffer
the core ABI takes as is. Coding goes through :mod:`pystencil.codecs`; the core stays codec-free.
"""

from __future__ import annotations

from ._ffi.types import NoneType
from . import codecs


class Image:
  """An RGBA8 raster: ``width`` x ``height`` pixels in a flat ``bytearray``."""

  def __init__(self, width: int, height: int, data: bytearray):
    expected = width * height * 4
    if len(data) != expected:
      raise ValueError(
        "data length %d != %d (width*height*4)" % (len(data), expected)
      )
    self.width = width
    self.height = height
    # A bytearray, so in-place core ops write through ctypes.from_buffer.
    self.data = data if isinstance(data, bytearray) else bytearray(data)

  @property
  def pixel_count(self) -> int:
    """Number of pixels (width*height) — the unit core ops count in."""
    return self.width * self.height

  @classmethod
  def blank(
    cls,
    width: int,
    height: int,
    rgba: tuple[int, int, int, int] = (255, 255, 255, 255),
  ) -> "Image":
    """A solid-colour image, filled by the core's ``fill_rgba`` or, with no native library,
    in Python."""
    data = bytearray(width * height * 4)
    r, g, b, a = rgba
    try:
      from .core import get_core

      get_core().fill_rgba(data, width * height, r, g, b, a)
    except Exception:
      for i in range(width * height):
        d = i * 4
        data[d] = r
        data[d + 1] = g
        data[d + 2] = b
        data[d + 3] = a
    return cls(width, height, data)

  @classmethod
  def decode(cls, raw: bytes) -> "Image":
    """Build an Image from encoded bytes (PNG/BMP/JPEG) via :mod:`codecs`."""
    width, height, data = codecs.decode(raw)
    return cls(width, height, data)

  @classmethod
  def open(cls, path: str) -> "Image":
    """Read and decode an image file from disk."""
    with open(path, "rb") as fh:
      raw = fh.read()
    return cls.decode(raw)

  def encode(self, fmt: str = "png") -> bytes:
    """Encode this image to bytes in ``fmt`` ("png", "bmp", or "jpeg"/"jpg")."""
    fmt = fmt.lower()
    if fmt == "png": return codecs.encode_png(self.width, self.height, self.data)
    if fmt == "bmp": return codecs.encode_bmp(self.width, self.height, self.data)
    if fmt in ("jpeg", "jpg"): return codecs.encode_jpeg(self.width, self.height, self.data)
    raise codecs.CodecError("unsupported encode format: %s" % fmt)

  def save(self, path: str, fmt: (str | NoneType) = None) -> None:
    """Encode to ``path``; ``fmt`` defaults to the extension's format, else PNG."""
    if fmt is None: fmt = codecs.format_from_ext(path) or "png"
    data = self.encode(fmt)
    with open(path, "wb") as fh:
      fh.write(data)

  def copy(self) -> "Image":
    """Return an independent copy (its own pixel buffer)."""
    return Image(self.width, self.height, bytearray(self.data))

  def __repr__(self) -> str:
    return "Image(%dx%d)" % (self.width, self.height)
