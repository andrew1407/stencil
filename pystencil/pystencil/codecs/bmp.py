from __future__ import annotations

"""BMP decode/encode: 24/32-bit ``BI_RGB`` only, bottom-up rows, BGR(A) <-> RGBA."""

import struct

from .sniff import _BMP_MAGIC, CodecError


def decode_bmp(data: bytes) -> tuple[int, int, bytearray]:
    """Decode a 24- or 32-bit ``BI_RGB`` BMP to RGBA8.

    BMP rows are stored bottom-up and padded to 4-byte boundaries, and pixels
    are BGR(A); we flip the rows and swizzle to RGBA. Compression other than
    ``BI_RGB`` (0) is not supported.
    """
    if data[:2] != _BMP_MAGIC:
        raise CodecError("not a BMP (bad signature)")

    # BITMAPFILEHEADER: pixel data offset at byte 10.
    pixel_offset = struct.unpack("<I", data[10:14])[0]
    # BITMAPINFOHEADER (we read the fields we need).
    header_size = struct.unpack("<I", data[14:18])[0]
    width = struct.unpack("<i", data[18:22])[0]
    height_raw = struct.unpack("<i", data[22:26])[0]
    bpp = struct.unpack("<H", data[28:30])[0]
    compression = struct.unpack("<I", data[30:34])[0]

    if compression != 0:
        raise CodecError("only uncompressed BI_RGB BMP is supported")
    if bpp not in (24, 32):
        raise CodecError("only 24/32-bit BMP is supported (got %d)" % bpp)

    # Negative height means a top-down image (rare, but legal).
    top_down = height_raw < 0
    height = abs(height_raw)

    bytes_per_px = bpp // 8
    # Each row is padded up to a multiple of 4 bytes.
    row_size = ((width * bytes_per_px + 3) // 4) * 4

    rgba = bytearray(b"\xff" * (width * height * 4))  # alpha defaults to opaque
    view = memoryview(rgba)
    for row in range(height):
        # Source row index, accounting for bottom-up storage.
        base = pixel_offset + (row if top_down else height - 1 - row) * row_size
        line = data[base:base + width * bytes_per_px]
        dst = view[row * width * 4:(row + 1) * width * 4]
        # BGR(A) -> RGBA, one strided copy per channel.
        dst[0::4] = line[2::bytes_per_px]
        dst[1::4] = line[1::bytes_per_px]
        dst[2::4] = line[0::bytes_per_px]
        if bytes_per_px == 4:
            dst[3::4] = line[3::4]

    return width, height, rgba


def encode_bmp(width: int, height: int, rgba: bytes | bytearray) -> bytes:
    """Encode an RGBA8 buffer as a 32-bit ``BI_RGB`` BMP (BGRA, bottom-up)."""
    if len(rgba) != width * height * 4:
        raise CodecError(
            "rgba length %d != %d (w*h*4)" % (len(rgba), width * height * 4)
        )

    bytes_per_px = 4
    # 32-bit rows are already 4-byte aligned, so no padding is needed.
    row_size = width * bytes_per_px
    pixel_data_size = row_size * height

    # Bottom-up BGRA pixel block: copy the rows in reverse, then swap R and B.
    pixels = bytearray(pixel_data_size)
    for row in range(height):
        src = (height - 1 - row) * row_size
        pixels[row * row_size:(row + 1) * row_size] = rgba[src:src + row_size]
    red = bytes(pixels[0::4])
    pixels[0::4] = pixels[2::4]
    pixels[2::4] = red

    file_header_size = 14
    info_header_size = 40
    pixel_offset = file_header_size + info_header_size
    file_size = pixel_offset + pixel_data_size

    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    info_header = struct.pack(
        "<IiiHHIIiiII",
        info_header_size,
        width,
        height,
        1,            # planes
        32,           # bits per pixel
        0,            # BI_RGB
        pixel_data_size,
        2835,         # ~72 DPI horizontal (pixels/metre)
        2835,         # ~72 DPI vertical
        0,            # colors used
        0,            # important colors
    )
    return bytes(file_header + info_header + pixels)
