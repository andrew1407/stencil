from __future__ import annotations

"""Tests for the pure-Python PNG/BMP codecs.

These must run WITHOUT the native core lib: they exercise only zlib/struct-based
encode/decode, so they stay green in a codec-only checkout. Round-trip fidelity
(encode then decode == original pixels) is the contract that lets the editor
trust ``Image.save`` / ``Image.open``.
"""

import unittest

from pystencil import codecs


class TestSniff(unittest.TestCase):
    """Magic-byte format detection."""

    def test_png_magic(self):
        self.assertEqual(codecs.sniff(b"\x89PNG\r\n\x1a\n" + b"rest"), "png")

    def test_bmp_magic(self):
        self.assertEqual(codecs.sniff(b"BM" + b"\x00" * 30), "bmp")

    def test_jpeg_magic(self):
        self.assertEqual(codecs.sniff(b"\xff\xd8\xff\xe0" + b"junk"), "jpeg")

    def test_unknown(self):
        self.assertEqual(codecs.sniff(b"not an image"), "unknown")

    def test_empty(self):
        self.assertEqual(codecs.sniff(b""), "unknown")


class TestFormatFromExt(unittest.TestCase):
    """Extension -> encoder-name mapping (case-insensitive)."""

    def test_png_uppercase(self):
        self.assertEqual(codecs.format_from_ext("x.PNG"), "png")

    def test_png_lowercase(self):
        self.assertEqual(codecs.format_from_ext("path/to/file.png"), "png")

    def test_bmp(self):
        self.assertEqual(codecs.format_from_ext("y.BmP"), "bmp")

    def test_unknown_ext(self):
        self.assertIsNone(codecs.format_from_ext("z.jpeg"))
        self.assertIsNone(codecs.format_from_ext("noext"))


class TestPngRoundTrip(unittest.TestCase):
    """encode_png -> decode_png must reproduce pixels exactly."""

    def test_known_3x2_buffer(self):
        # A 3x2 image with distinct, fully-opaque/translucent pixels.
        width, height = 3, 2
        pixels = bytearray(
            [
                255, 0, 0, 255,      # red
                0, 255, 0, 255,      # green
                0, 0, 255, 255,      # blue
                255, 255, 0, 128,    # semi-transparent yellow
                0, 255, 255, 64,     # semi-transparent cyan
                255, 0, 255, 0,      # fully-transparent magenta
            ]
        )
        encoded = codecs.encode_png(width, height, pixels)
        self.assertEqual(codecs.sniff(encoded), "png")
        dw, dh, decoded = codecs.decode_png(encoded)
        self.assertEqual((dw, dh), (width, height))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_gradient_round_trip(self):
        # A wider gradient exercises non-trivial scanlines through zlib.
        width, height = 16, 8
        pixels = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                d = (y * width + x) * 4
                pixels[d] = (x * 16) & 0xFF
                pixels[d + 1] = (y * 32) & 0xFF
                pixels[d + 2] = (x * y) & 0xFF
                pixels[d + 3] = 255
        encoded = codecs.encode_png(width, height, pixels)
        dw, dh, decoded = codecs.decode_png(encoded)
        self.assertEqual((dw, dh), (width, height))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_single_pixel(self):
        pixels = bytearray([12, 34, 56, 200])
        encoded = codecs.encode_png(1, 1, pixels)
        dw, dh, decoded = codecs.decode_png(encoded)
        self.assertEqual((dw, dh), (1, 1))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_length_mismatch_raises(self):
        with self.assertRaises(codecs.CodecError):
            codecs.encode_png(2, 2, bytearray(3))


class TestBmpRoundTrip(unittest.TestCase):
    """encode_bmp -> decode_bmp must reproduce pixels exactly (incl. alpha)."""

    def test_round_trip(self):
        width, height = 3, 2
        pixels = bytearray(
            [
                255, 0, 0, 255,
                0, 255, 0, 255,
                0, 0, 255, 255,
                10, 20, 30, 40,
                50, 60, 70, 80,
                90, 100, 110, 120,
            ]
        )
        encoded = codecs.encode_bmp(width, height, pixels)
        self.assertEqual(codecs.sniff(encoded), "bmp")
        dw, dh, decoded = codecs.decode_bmp(encoded)
        self.assertEqual((dw, dh), (width, height))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_non_multiple_width(self):
        # Width whose 24-bit row would need padding still survives our 32-bit
        # encode/decode round trip.
        width, height = 5, 4
        pixels = bytearray(width * height * 4)
        for i in range(width * height):
            d = i * 4
            pixels[d] = i & 0xFF
            pixels[d + 1] = (i * 2) & 0xFF
            pixels[d + 2] = (i * 3) & 0xFF
            pixels[d + 3] = 255
        encoded = codecs.encode_bmp(width, height, pixels)
        dw, dh, decoded = codecs.decode_bmp(encoded)
        self.assertEqual((dw, dh), (width, height))
        self.assertEqual(bytes(decoded), bytes(pixels))


class TestDecodeDispatch(unittest.TestCase):
    """Top-level ``decode`` dispatches and reports unsupported inputs clearly."""

    def test_decode_png_via_dispatch(self):
        pixels = bytearray([1, 2, 3, 4, 5, 6, 7, 8])  # 2x1
        encoded = codecs.encode_png(2, 1, pixels)
        dw, dh, decoded = codecs.decode(encoded)
        self.assertEqual((dw, dh), (2, 1))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_decode_bmp_via_dispatch(self):
        pixels = bytearray([1, 2, 3, 4, 5, 6, 7, 8])
        encoded = codecs.encode_bmp(2, 1, pixels)
        dw, dh, decoded = codecs.decode(encoded)
        self.assertEqual((dw, dh), (2, 1))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_jpeg_raises_helpful(self):
        with self.assertRaises(codecs.CodecError) as ctx:
            codecs.decode(b"\xff\xd8\xff\xe0" + b"\x00" * 20)
        self.assertIn("Zig CLI", str(ctx.exception))

    def test_garbage_raises(self):
        with self.assertRaises(codecs.CodecError):
            codecs.decode(b"definitely not an image file")


if __name__ == "__main__":
    unittest.main()
