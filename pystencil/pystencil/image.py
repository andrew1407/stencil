from __future__ import annotations

"""The ``Image`` value type: a plain RGBA8 pixel buffer.

This mirrors the raw RGBA8 buffers the C++ ``core/`` operates on — interleaved
R,G,B,A bytes, top-to-bottom rows — so it can be handed straight to the core
ABI (crop/rotate/filter/rasterize) without conversion. Decoding/encoding goes
through :mod:`pystencil.codecs` (pure-Python PNG/BMP); the core stays codec-free
by design, exactly like the browser/wasm and Zig CLI front-ends.
"""

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
        # Always hold a bytearray so in-place core ops (fill/filter/rasterize)
        # can mutate the buffer directly via ctypes.from_buffer.
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
        """Create a solid-color image.

        Prefers the native core's ``fill_rgba`` (so a blank page is filled by the
        exact same code path the CLI/browser use). The import is deferred and
        guarded: codec tests and any environment without a compiled core lib must
        still be able to build blank images, so we fall back to a pure-Python
        fill when the native library can't be loaded.
        """
        data = bytearray(width * height * 4)
        r, g, b, a = rgba
        try:
            from .core import get_core

            get_core().fill_rgba(data, width * height, r, g, b, a)
        except Exception:
            # Pure-Python fallback: write the RGBA pattern across the buffer.
            for i in range(width * height):
                d = i * 4
                data[d] = r
                data[d + 1] = g
                data[d + 2] = b
                data[d + 3] = a
        return cls(width, height, data)

    @classmethod
    def decode(cls, raw: bytes) -> "Image":
        """Build an Image from encoded bytes (PNG/BMP) via :mod:`codecs`."""
        width, height, data = codecs.decode(raw)
        return cls(width, height, data)

    @classmethod
    def open(cls, path: str) -> "Image":
        """Read and decode an image file from disk."""
        with open(path, "rb") as fh:
            raw = fh.read()
        return cls.decode(raw)

    def encode(self, fmt: str = "png") -> bytes:
        """Encode this image to bytes in ``fmt`` ("png" or "bmp")."""
        fmt = fmt.lower()
        if fmt == "png":
            return codecs.encode_png(self.width, self.height, self.data)
        if fmt == "bmp":
            return codecs.encode_bmp(self.width, self.height, self.data)
        raise codecs.CodecError("unsupported encode format: %s" % fmt)

    def save(self, path: str, fmt: str | None = None) -> None:
        """Encode and write this image to ``path``.

        When ``fmt`` is omitted, it's inferred from the extension, defaulting to
        PNG for unknown/missing extensions.
        """
        if fmt is None:
            fmt = codecs.format_from_ext(path) or "png"
        data = self.encode(fmt)
        with open(path, "wb") as fh:
            fh.write(data)

    def copy(self) -> "Image":
        """Return an independent copy (its own pixel buffer)."""
        return Image(self.width, self.height, bytearray(self.data))

    def __repr__(self) -> str:
        return "Image(%dx%d)" % (self.width, self.height)
