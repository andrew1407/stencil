from __future__ import annotations

"""Decoder coverage for every PNG color type x row filter combination.

``test_codecs.py`` round-trips what our own encoder writes (color type 6, filter 0).
Foreign PNGs use the other four filters and the narrower sample models, and the decoder
takes a vectorized path for Sub/Up and the channel expansions — so this suite builds
PNGs itself, one per (color type, filter) pair, and checks the decoded RGBA against a
straightforward per-pixel expectation.
"""

import struct
import unittest
import zlib

from pystencil import codecs

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
FILTERS = (0, 1, 2, 3, 4)


def paeth(a, b, c):
    """The reference predictor, kept separate from the one under test."""
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _chunk(ctype, payload):
    crc = zlib.crc32(payload, zlib.crc32(ctype)) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)


def build_png(width, height, color_type, samples, ftype, palette=b"", trns=b""):
    """Encode ``samples`` (raw, pre-expansion bytes) with ``ftype`` on every scanline."""
    bpp = CHANNELS[color_type]
    stride = width * bpp
    raw = bytearray()
    prev = bytes(stride)
    for y in range(height):
        cur = samples[y * stride:(y + 1) * stride]
        raw.append(ftype)
        line = bytearray(stride)
        for x in range(stride):
            a = cur[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 0:
                pred = 0
            elif ftype == 1:
                pred = a
            elif ftype == 2:
                pred = b
            elif ftype == 3:
                pred = (a + b) >> 1
            else:
                pred = paeth(a, b, c)
            line[x] = (cur[x] - pred) & 0xFF
        raw += line
        prev = cur
    out = b"\x89PNG\r\n\x1a\n" + _chunk(
        b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    )
    if color_type == 3:
        out += _chunk(b"PLTE", palette)
        if trns:
            out += _chunk(b"tRNS", trns)
    return out + _chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + _chunk(b"IEND", b"")


def expected_rgba(width, height, color_type, samples, palette=b"", trns=b""):
    """The obvious per-pixel expansion the decoder's slice arithmetic must match."""
    out = bytearray()
    for i in range(width * height):
        if color_type == 6:
            out += samples[i * 4:i * 4 + 4]
        elif color_type == 2:
            out += samples[i * 3:i * 3 + 3] + b"\xff"
        elif color_type == 0:
            out += bytes([samples[i]] * 3) + b"\xff"
        elif color_type == 4:
            out += bytes([samples[i * 2]] * 3) + bytes([samples[i * 2 + 1]])
        else:
            idx = samples[i]
            out += palette[idx * 3:idx * 3 + 3]
            out += bytes([trns[idx] if idx < len(trns) else 255])
    return bytes(out)


def sample_bytes(width, height, color_type, entries=0):
    """A deterministic, non-uniform raw sample plane for the given color type."""
    n = width * height * CHANNELS[color_type]
    if color_type == 3:
        return bytes((i * 5 + 3) % entries for i in range(n))
    return bytes((i * 37 + (i // 7) * 11) & 0xFF for i in range(n))


class TestFilterAndColorTypeMatrix(unittest.TestCase):
    """Every (color type, filter) pair decodes to the same pixels as the naive expansion."""

    WIDTH, HEIGHT = 9, 5
    PALETTE = bytes(((i * 17) & 0xFF) for i in range(6 * 3))
    TRNS = bytes([0, 40, 255])  # shorter than the palette: the rest default to opaque

    def test_matrix(self):
        for color_type in sorted(CHANNELS):
            entries = len(self.PALETTE) // 3
            samples = sample_bytes(self.WIDTH, self.HEIGHT, color_type, entries)
            want = expected_rgba(
                self.WIDTH, self.HEIGHT, color_type, samples, self.PALETTE, self.TRNS
            )
            for ftype in FILTERS:
                with self.subTest(color_type=color_type, filter=ftype):
                    png = build_png(
                        self.WIDTH, self.HEIGHT, color_type, samples, ftype,
                        self.PALETTE, self.TRNS,
                    )
                    w, h, got = codecs.decode_png(png)
                    self.assertEqual((w, h), (self.WIDTH, self.HEIGHT))
                    self.assertEqual(bytes(got), want)

    def test_mixed_filters_across_rows(self):
        # Real encoders pick a filter per scanline; the row loop must carry the
        # reconstructed previous row across a change of filter.
        width, height, bpp = 6, len(FILTERS), 4
        samples = sample_bytes(width, height, 6)
        stride = width * bpp
        raw = bytearray()
        prev = bytes(stride)
        for y, ftype in enumerate(FILTERS):
            cur = samples[y * stride:(y + 1) * stride]
            raw += bytes([ftype])
            for x in range(stride):
                a = cur[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[ftype]
                raw.append((cur[x] - pred) & 0xFF)
            prev = cur
        png = b"\x89PNG\r\n\x1a\n" + _chunk(
            b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        ) + _chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + _chunk(b"IEND", b"")
        w, h, got = codecs.decode_png(png)
        self.assertEqual((w, h), (width, height))
        self.assertEqual(bytes(got), bytes(samples))

    def test_single_pixel_each_color_type(self):
        for color_type in sorted(CHANNELS):
            samples = sample_bytes(1, 1, color_type, 6)
            for ftype in FILTERS:
                with self.subTest(color_type=color_type, filter=ftype):
                    png = build_png(1, 1, color_type, samples, ftype, self.PALETTE, self.TRNS)
                    _, _, got = codecs.decode_png(png)
                    self.assertEqual(
                        bytes(got),
                        expected_rgba(1, 1, color_type, samples, self.PALETTE, self.TRNS),
                    )

    def test_unknown_filter_type_raises(self):
        png = bytearray(build_png(2, 1, 6, sample_bytes(2, 1, 6), 0))
        # Rewrite the single scanline's filter byte to an illegal value.
        idat = png.index(b"IDAT") + 4
        length = struct.unpack(">I", bytes(png[idat - 8:idat - 4]))[0]
        raw = bytearray(zlib.decompress(bytes(png[idat:idat + length])))
        raw[0] = 9
        rebuilt = _chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        png = png[:idat - 8] + bytearray(rebuilt) + png[idat + length + 4:]
        with self.assertRaises(codecs.CodecError):
            codecs.decode_png(bytes(png))

    def test_palette_index_out_of_range_raises(self):
        png = build_png(2, 1, 3, bytes([0, 5]), 0, palette=bytes(3 * 2))
        with self.assertRaises(codecs.CodecError):
            codecs.decode_png(png)

    def test_palette_without_plte_raises(self):
        png = build_png(2, 1, 3, bytes([0, 0]), 0, palette=b"")
        with self.assertRaises(codecs.CodecError):
            codecs.decode_png(png)

    def test_a_truncated_scanline_raises_instead_of_decoding_short(self):
        # A short inflate used to unfilter what was there and return a buffer smaller
        # than width*height*4, which every caller sizes its reads by.
        for ftype in range(5):
            with self.subTest(filter=ftype):
                raw = bytearray([0]) + bytes(16) + bytearray([ftype]) + bytes(5)
                png = (
                    b"\x89PNG\r\n\x1a\n"
                    + _chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 2, 8, 6, 0, 0, 0))
                    + _chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                    + _chunk(b"IEND", b"")
                )
                with self.assertRaises(codecs.CodecError):
                    codecs.decode_png(png)

    def test_a_missing_scanline_raises(self):
        png = bytearray(build_png(4, 3, 6, sample_bytes(4, 3, 6), 0))
        idat = png.index(b"IDAT") + 4
        length = struct.unpack(">I", bytes(png[idat - 8:idat - 4]))[0]
        raw = bytearray(zlib.decompress(bytes(png[idat:idat + length])))
        rebuilt = _chunk(b"IDAT", zlib.compress(bytes(raw[: len(raw) - 17]), 6))
        png = png[:idat - 8] + bytearray(rebuilt) + png[idat + length + 4:]
        with self.assertRaises(codecs.CodecError):
            codecs.decode_png(bytes(png))


if __name__ == "__main__":
    unittest.main()
