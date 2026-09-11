"""Pure unit tests (no network, no core): format derivation, header sniffing, HTML scan."""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil import codecs
from pystencil.sitesource import format_of, scan_html
from tests.servedsite import _solid_png

class FormatOfTests(unittest.TestCase):
    def test_extension_lowercased(self):
        self.assertEqual(format_of("http://h/a.PNG"), "png")

    def test_query_and_fragment_stripped(self):
        self.assertEqual(format_of("http://h/pic.jpg?v=2#frag"), "jpg")

    def test_jpeg_normalized_to_jpg(self):
        self.assertEqual(format_of("http://h/photo.jpeg"), "jpg")

    def test_quicktime_and_mov(self):
        self.assertEqual(format_of("http://h/clip.MOV"), "mov")
        self.assertEqual(format_of("data:video/quicktime;base64,AA"), "mov")

    def test_data_uri_image(self):
        self.assertEqual(format_of("data:image/jpeg;base64,AAAA"), "jpg")

    def test_svg_xml_normalized(self):
        self.assertEqual(format_of("data:image/svg+xml,<svg/>"), "svg")

    def test_double_extension_takes_last(self):
        self.assertEqual(format_of("http://h/archive.tar.gz"), "gz")

    def test_no_extension(self):
        self.assertEqual(format_of("http://h/noext"), "")

    def test_empty(self):
        self.assertEqual(format_of(""), "")


class ImageDimensionsTests(unittest.TestCase):
    def test_png(self):
        self.assertEqual(codecs.image_dimensions(_solid_png(13, 7)), (13, 7))

    def test_bmp(self):
        bmp = codecs.encode_bmp(9, 4, bytearray(9 * 4 * 4))
        self.assertEqual(codecs.image_dimensions(bmp), (9, 4))

    def test_gif(self):
        gif = b"GIF89a" + struct.pack("<HH", 320, 240) + b"\x00" * 8
        self.assertEqual(codecs.image_dimensions(gif), (320, 240))

    def test_jpeg_sof0(self):
        jpeg = (
            b"\xff\xd8"
            + b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
            + b"\xff\xc0" + struct.pack(">H", 17) + b"\x08" + struct.pack(">HH", 480, 640)
            + b"\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01"
        )
        self.assertEqual(codecs.image_dimensions(jpeg), (640, 480))

    def test_webp_vp8x(self):
        webp = (
            b"RIFF" + struct.pack("<I", 0) + b"WEBP"
            + b"VP8X" + struct.pack("<I", 10)
            + b"\x00" + b"\x00\x00\x00"
            + struct.pack("<I", 800 - 1)[:3]
            + struct.pack("<I", 600 - 1)[:3]
        )
        self.assertEqual(codecs.image_dimensions(webp), (800, 600))

    def test_unrecognized_returns_none(self):
        self.assertIsNone(codecs.image_dimensions(b"not an image at all"))

    def test_truncated_png_returns_none(self):
        self.assertIsNone(codecs.image_dimensions(b"\x89PNG\r\n\x1a\n"))


class ScanHtmlTests(unittest.TestCase):
    def test_ordering_kinds_and_base_resolution(self):
        html = (
            '<base href="http://example.com/media/">'
            '<img src="a.png" alt="A">'
            '<img src="data:image/gif;base64,ZZ" data-src="lazy.png">'
            '<img src="data:image/png;base64,ONLY">'
            '<video src="v.mp4" poster="p.png"></video>'
            "<div style=\"background-image:url(bg.png)\"></div>"
        )
        items = scan_html(html, "http://example.com/page/")
        urls = [(it.url, it.kind) for it in items]
        self.assertEqual(
            urls,
            [
                ("http://example.com/media/a.png", "img"),
                ("http://example.com/media/lazy.png", "img"),
                ("http://example.com/media/v.mp4", "video"),
                ("http://example.com/media/p.png", "poster"),
                ("http://example.com/media/bg.png", "bg"),
            ],
        )
        # The data:-only <img> (no lazy fallback) is dropped as a non-http URL.
        self.assertTrue(all(not it.url.startswith("data:") for it in items))
        self.assertEqual(items[0].alt, "A")
        self.assertEqual(items[2].ext, "mp4")

    def test_poster_matching_img_is_retagged_not_duplicated(self):
        html = (
            '<img src="shared.png">'
            '<video src="v.mp4" poster="shared.png"></video>'
        )
        items = scan_html(html, "http://h/")
        self.assertEqual(
            [(it.url, it.kind) for it in items],
            [("http://h/shared.png", "poster"), ("http://h/v.mp4", "video")],
        )

    def test_picture_source_and_style_block(self):
        html = (
            "<style>.hero{background-image:url('bg.png')}</style>"
            "<picture><source src=\"big.png\"><img src=\"small.png\"></picture>"
        )
        items = scan_html(html, "http://h/")
        kinds = {it.url: it.kind for it in items}
        # imgs first, then the picture <source>, then the <style> background.
        self.assertEqual([it.url for it in items], [
            "http://h/small.png", "http://h/big.png", "http://h/bg.png",
        ])
        self.assertEqual(kinds["http://h/big.png"], "img")
        self.assertEqual(kinds["http://h/bg.png"], "bg")

    def test_dedupe_first_wins(self):
        html = '<img src="dup.png"><img src="dup.png" alt="second">'
        items = scan_html(html, "http://h/")
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0].alt, "")
