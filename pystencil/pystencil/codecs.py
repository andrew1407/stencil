from __future__ import annotations

"""Pure-Python image I/O for pystencil (the C++ ``core/`` is codec-free by design).

Stdlib-only (``zlib`` + ``struct``), so it implements just the two formats it can do
correctly without a third-party codec, over RGBA8 ``bytearray`` buffers:

* PNG  — decode 8-bit color types 0/2/3/4/6 (all five row filters); encode as RGBA
         (type 6), 8-bit, filter 0.
* BMP  — 24/32-bit ``BI_RGB``, bottom-up rows, BGR(A) <-> RGBA.

JPEG needs a real DCT codec we don't ship: ``decode`` raises ``CodecError`` pointing
callers at the Zig CLI, which owns codec-heavy work.
"""

import struct
import zlib

# Module surface kept narrow and explicit so callers (and tests) bind to names,
# not to import order.
__all__ = [
    "CodecError",
    "sniff",
    "format_from_ext",
    "decode",
    "decode_png",
    "encode_png",
    "decode_bmp",
    "encode_bmp",
]


class CodecError(Exception):
    """Raised for any decode/encode failure (bad magic, unsupported subtype...)."""

    pass


# --- format detection ------------------------------------------------------

# The 8-byte PNG signature (RFC 2083) and the 2-byte BMP / JPEG markers.
_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
_BMP_MAGIC = b"BM"
_JPEG_MAGIC = b"\xff\xd8\xff"


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


def format_from_ext(path: str) -> str | None:
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


# --- top-level dispatch ----------------------------------------------------

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


# --- PNG -------------------------------------------------------------------

def _paeth(a: int, b: int, c: int) -> int:
    """The PNG Paeth predictor (RFC 2083 sec. 6.6): pick the neighbour closest
    to ``a + b - c`` (left, above, upper-left), ties broken toward ``a``."""
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data: bytes) -> tuple[int, int, bytearray]:
    """Decode an 8-bit PNG to RGBA8.

    Supports color types 0 (grayscale), 2 (RGB), 3 (palette), 4 (gray+alpha)
    and 6 (RGBA), bit depth 8 only. Concatenates all IDAT chunks, inflates them,
    then reverses the per-row filter (none/sub/up/average/paeth) before
    expanding each sample model out to RGBA.
    """
    if data[:8] != _PNG_MAGIC:
        raise CodecError("not a PNG (bad signature)")

    pos = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    palette = b""
    trns = b""
    idat = bytearray()

    # Walk the chunk stream: each chunk is length(4) + type(4) + data + crc(4).
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
            (width, height, bit_depth, color_type, comp, filt, interlace) = struct.unpack(
                ">IIBBBBB", chunk
            )
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
        # advance past data + 4-byte CRC (we trust zlib to catch corruption)
        pos = cend + 4

    if width == 0 or height == 0:
        raise CodecError("PNG has no IHDR / zero dimensions")

    # Channels per pixel in the raw (pre-expansion) sample stream.
    if color_type == 0:
        channels = 1
    elif color_type == 2:
        channels = 3
    elif color_type == 3:
        channels = 1
    elif color_type == 4:
        channels = 2
    elif color_type == 6:
        channels = 4
    else:
        raise CodecError("unsupported PNG color type %d" % color_type)

    raw = zlib.decompress(bytes(idat))

    stride = width * channels
    # Output of the unfilter step: raw sample bytes, one filtered byte per row.
    out = bytearray(height * stride)
    bpp = channels  # bytes per pixel == channels at 8-bit depth
    src = 0
    for y in range(height):
        ftype = raw[src]
        src += 1
        row_start = y * stride
        if ftype == 0:
            # No filter (what our own encoder emits): copy the row wholesale.
            out[row_start:row_start + stride] = raw[src:src + stride]
            src += stride
            continue
        prev_start = row_start - stride
        for x in range(stride):
            value = raw[src + x]
            a = out[row_start + x - bpp] if x >= bpp else 0
            b = out[prev_start + x] if y > 0 else 0
            c = out[prev_start + x - bpp] if (y > 0 and x >= bpp) else 0
            if ftype == 1:
                recon = value + a
            elif ftype == 2:
                recon = value + b
            elif ftype == 3:
                recon = value + ((a + b) >> 1)
            elif ftype == 4:
                recon = value + _paeth(a, b, c)
            else:
                raise CodecError("unknown PNG filter type %d" % ftype)
            out[row_start + x] = recon & 0xFF
        src += stride

    # Expand whatever sample model we have out to interleaved RGBA8.
    rgba = bytearray(width * height * 4)
    npix = width * height

    if color_type == 6:
        # Already RGBA — straight copy.
        rgba[:] = out
    elif color_type == 2:
        for i in range(npix):
            s = i * 3
            d = i * 4
            rgba[d] = out[s]
            rgba[d + 1] = out[s + 1]
            rgba[d + 2] = out[s + 2]
            rgba[d + 3] = 255
    elif color_type == 0:
        # Grayscale: replicate the single sample across R/G/B.
        for i in range(npix):
            g = out[i]
            d = i * 4
            rgba[d] = g
            rgba[d + 1] = g
            rgba[d + 2] = g
            rgba[d + 3] = 255
    elif color_type == 4:
        # Grayscale + alpha.
        for i in range(npix):
            s = i * 2
            g = out[s]
            d = i * 4
            rgba[d] = g
            rgba[d + 1] = g
            rgba[d + 2] = g
            rgba[d + 3] = out[s + 1]
    elif color_type == 3:
        # Palette index -> PLTE RGB, with optional per-index alpha from tRNS.
        if not palette:
            raise CodecError("palette PNG missing PLTE chunk")
        for i in range(npix):
            idx = out[i]
            p = idx * 3
            d = i * 4
            rgba[d] = palette[p]
            rgba[d + 1] = palette[p + 1]
            rgba[d + 2] = palette[p + 2]
            rgba[d + 3] = trns[idx] if idx < len(trns) else 255

    return width, height, rgba


def _png_chunk(ctype: bytes, payload: bytes) -> bytes:
    """Assemble one PNG chunk: length, type, payload, CRC32 over type+payload."""
    crc = zlib.crc32(ctype)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)


def encode_png(width: int, height: int, rgba: bytes | bytearray) -> bytes:
    """Encode an RGBA8 buffer as a PNG (color type 6, 8-bit, filter 0).

    We always prepend filter byte 0 ("none") to each scanline and let zlib do
    the compression; this keeps the encoder trivial while staying a valid,
    widely-readable PNG. CRC32 is computed per chunk via ``zlib.crc32``.
    """
    if len(rgba) != width * height * 4:
        raise CodecError(
            "rgba length %d != %d (w*h*4)" % (len(rgba), width * height * 4)
        )

    stride = width * 4
    # Filter-0 framing: one 0 byte in front of every row of raw RGBA samples.
    raw = bytearray()
    for y in range(height):
        start = y * stride
        raw.append(0)
        raw += rgba[start:start + stride]

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    return (
        _PNG_MAGIC
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", idat)
        + _png_chunk(b"IEND", b"")
    )


# --- BMP -------------------------------------------------------------------

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

    rgba = bytearray(width * height * 4)
    for row in range(height):
        # Source row index, accounting for bottom-up storage.
        src_row = row if top_down else (height - 1 - row)
        base = pixel_offset + src_row * row_size
        for x in range(width):
            s = base + x * bytes_per_px
            b = data[s]
            g = data[s + 1]
            r = data[s + 2]
            a = data[s + 3] if bytes_per_px == 4 else 255
            d = (row * width + x) * 4
            rgba[d] = r
            rgba[d + 1] = g
            rgba[d + 2] = b
            rgba[d + 3] = a

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

    # Bottom-up BGRA pixel block.
    pixels = bytearray(pixel_data_size)
    for row in range(height):
        src_row = height - 1 - row
        for x in range(width):
            s = (src_row * width + x) * 4
            d = (row * width + x) * 4
            pixels[d] = rgba[s + 2]      # B
            pixels[d + 1] = rgba[s + 1]  # G
            pixels[d + 2] = rgba[s]      # R
            pixels[d + 3] = rgba[s + 3]  # A

    file_header_size = 14
    info_header_size = 40
    pixel_offset = file_header_size + info_header_size
    file_size = pixel_offset + pixel_data_size

    # BITMAPFILEHEADER + BITMAPINFOHEADER, then the pixel block.
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
