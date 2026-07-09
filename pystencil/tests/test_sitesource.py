from __future__ import annotations

"""Tests for the source-site scraper (``pystencil.sitesource``) + ``codecs.image_dimensions``.

Two layers:

* **Pure unit tests** (no network, no core): ``format_of``, ``image_dimensions`` header
  sniffing, and ``scan_html`` parsing / ordering / dedupe / poster-tagging.
* **Integration tests** over a real local static HTTP server: a fixture HTML page (imgs,
  lazy-loaded img, ``<picture>``, ``<video>`` + poster, CSS ``background-image``) plus tiny
  real PNGs are served from ``127.0.0.1:0``; we assert ``scan_page`` finds and filters the
  right subset (category / format / dimension / count / group) and ``download_media`` writes
  exactly the matching files. The one core-backed path (loading a scraped still into an
  :class:`Editor` via ``/source-upload``) self-skips when the native core is unavailable.
"""

import io
import struct
import sys
import tempfile
import unittest
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import ipaddress

from pystencil import codecs
from pystencil.sitesource import download_media, format_of, scan_html, scan_page
from pystencil.sitesource import _assert_fetchable, _is_blocked_ip, _sub_strict


# ── SSRF guard (parity with the Zig CLI's net.zig) ──────────────────────────────
class SsrfGuardTests(unittest.TestCase):
    def _blocked(self, host, strict):
        return _is_blocked_ip(ipaddress.ip_address(host), strict)

    def test_internal_ranges_blocked_in_both_modes(self):
        for host in (
            "169.254.169.254",  # cloud metadata
            "10.0.0.5",
            "172.16.4.4",
            "192.168.1.1",
            "100.64.0.1",  # CGNAT
            "0.0.0.0",
            "fe80::1",  # link-local
            "fc00::1",  # ULA
            "::ffff:169.254.169.254",  # IPv4-mapped metadata
            "::ffff:10.0.0.1",  # IPv4-mapped private
        ):
            self.assertTrue(self._blocked(host, False), host)
            self.assertTrue(self._blocked(host, True), host)

    def test_loopback_allowed_only_when_not_strict(self):
        for host in ("127.0.0.1", "127.9.9.9", "::1", "::ffff:127.0.0.1"):
            self.assertFalse(self._blocked(host, False), host)  # user-named URL
            self.assertTrue(self._blocked(host, True), host)  # harvested sub-resource

    def test_public_hosts_allowed(self):
        for host in ("8.8.8.8", "93.184.216.34", "2606:2800:220:1:248:1893:25c8:1946"):
            self.assertFalse(self._blocked(host, False), host)
            self.assertFalse(self._blocked(host, True), host)

    def test_assert_fetchable_rejects_ip_literals_and_localhost(self):
        for url in (
            "http://169.254.169.254/latest/meta-data/",
            "http://10.0.0.1/x",
            "http://[::1]/x",  # strict → loopback blocked
        ):
            with self.assertRaises(ValueError):
                _assert_fetchable(url, strict=True)
        with self.assertRaises(ValueError):
            _assert_fetchable("http://localhost/x", strict=True)
        # Non-strict tolerates loopback for a user-named URL.
        _assert_fetchable("http://127.0.0.1:8080/x", strict=False)
        _assert_fetchable("http://localhost/x", strict=False)

    def test_assert_fetchable_catches_alternate_numeric_encodings(self):
        # getaddrinfo canonicalizes these to 127.0.0.1 → blocked in strict mode.
        for url in ("http://2130706433/x", "http://0x7f000001/x"):
            with self.assertRaises(ValueError):
                _assert_fetchable(url, strict=True)

    def test_sub_strict_same_host_exception(self):
        # Same host as the page → loopback tolerated (own localhost gallery).
        self.assertFalse(_sub_strict("http://127.0.0.1:8080/a.png", "127.0.0.1"))
        self.assertFalse(_sub_strict("http://cdn.example.com/x.png", "cdn.example.com"))
        # Different host → strict (the SSRF pivot).
        self.assertTrue(_sub_strict("http://127.0.0.1/admin", "evil.com"))
        self.assertTrue(_sub_strict("http://169.254.169.254/meta", "site.com"))


# ── tiny image builders (stdlib-only, no core) ──────────────────────────────────
def _solid_png(w: int, h: int, rgb=(200, 30, 30)) -> bytes:
    data = bytearray(w * h * 4)
    for i in range(w * h):
        d = i * 4
        data[d], data[d + 1], data[d + 2], data[d + 3] = rgb[0], rgb[1], rgb[2], 255
    return codecs.encode_png(w, h, data)


# ── pure unit tests ─────────────────────────────────────────────────────────────
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


# ── integration over a real local static HTTP server ────────────────────────────
_FIXTURE_HTML = """<!doctype html>
<html><head>
<style>.hero { background-image: url("bg.png"); }</style>
</head><body>
  <img src="logo.png" alt="logo">
  <img src="hero.png" alt="hero">
  <img data-src="tiny.png" src="data:image/gif;base64,AAAA">
  <picture><source src="pic-source.png"><img src="pic-fallback.png"></picture>
  <video src="clip.mp4" poster="poster.png"></video>
  <svg><image href="vector.png"/></svg>
  <div style="background-image: url('bg.png')"></div>
</body></html>
"""

# name -> (width, height) for the served PNGs.
_IMAGES = {
    "logo.png": (200, 80),
    "hero.png": (120, 90),
    "tiny.png": (8, 8),
    "pic-fallback.png": (30, 20),
    "pic-source.png": (40, 40),
    "vector.png": (24, 24),
    "poster.png": (100, 50),
    "bg.png": (64, 48),
}


class _QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):  # keep the test output clean
        pass


class ServedSiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "index.html").write_text(_FIXTURE_HTML, encoding="utf-8")
        for name, (w, h) in _IMAGES.items():
            (root / name).write_bytes(_solid_png(w, h))
        # A non-image "video" file: downloads fine, sniffs no dimensions (unmeasured).
        (root / "clip.mp4").write_bytes(b"\x00\x00\x00\x18ftypmp42" + b"\x00" * 32)

        handler = lambda *a, **k: _QuietHandler(*a, directory=str(root), **k)  # noqa: E731
        cls._server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls._thread = Thread(target=cls._server.serve_forever, daemon=True)
        cls._thread.start()
        host, port = cls._server.server_address
        cls.base = "http://127.0.0.1:%d/" % port
        cls.page = cls.base + "index.html"

    @classmethod
    def tearDownClass(cls):
        cls._server.shutdown()
        cls._server.server_close()
        cls._thread.join(timeout=5)
        cls._tmp.cleanup()

    def _names(self, items):
        return [it.url.rsplit("/", 1)[-1] for it in items]

    def test_scan_all_finds_every_category(self):
        items = scan_page(self.page)
        self.assertEqual(
            [(self._names([it])[0], it.kind) for it in items],
            [
                ("logo.png", "img"),
                ("hero.png", "img"),
                ("tiny.png", "img"),
                ("pic-fallback.png", "img"),
                ("vector.png", "img"),
                ("clip.mp4", "video"),
                ("poster.png", "poster"),
                ("pic-source.png", "img"),
                ("bg.png", "bg"),
            ],
        )

    def test_name_filter_substring_and_regex(self):
        # Plain token: matches every URL containing it (like the CLI/extension default).
        self.assertEqual(
            self._names(scan_page(self.page, name="pic")),
            ["pic-fallback.png", "pic-source.png"],
        )
        # Case-insensitive (parity with regex.h REG_ICASE / RegExp 'i').
        self.assertEqual(self._names(scan_page(self.page, name="LOGO")), ["logo.png"])
        # Regex metacharacters — anchored alternation and an end-anchored extension.
        self.assertEqual(
            self._names(scan_page(self.page, name=r"(logo|hero)\.png$")),
            ["logo.png", "hero.png"],
        )
        self.assertEqual(self._names(scan_page(self.page, name=r"\.mp4$")), ["clip.mp4"])

    def test_category_filters(self):
        self.assertEqual(
            self._names(scan_page(self.page, category="img")),
            ["logo.png", "hero.png", "tiny.png", "pic-fallback.png", "vector.png", "pic-source.png"],
        )
        self.assertEqual(self._names(scan_page(self.page, category="background")), ["bg.png"])
        self.assertEqual(self._names(scan_page(self.page, category="video")), ["clip.mp4"])
        self.assertEqual(self._names(scan_page(self.page, category="poster")), ["poster.png"])

    def test_format_filter(self):
        self.assertEqual(self._names(scan_page(self.page, formats="mp4")), ["clip.mp4"])
        # png only -> everything except the mp4.
        pngs = self._names(scan_page(self.page, formats="png"))
        self.assertNotIn("clip.mp4", pngs)
        self.assertEqual(len(pngs), 8)

    def test_dimension_filter_measures_images(self):
        # Only logo (200) and hero (120) reach width >= 100 among img-category items.
        self.assertEqual(
            self._names(scan_page(self.page, category="img", min_width=100)),
            ["logo.png", "hero.png"],
        )

    def test_dimension_filter_passes_unmeasured_video(self):
        # Across all categories a >=100 width keeps logo/hero/poster AND the unmeasured video.
        got = self._names(scan_page(self.page, min_width=100))
        self.assertIn("clip.mp4", got)
        self.assertEqual(set(got), {"logo.png", "hero.png", "poster.png", "clip.mp4"})

    def test_count_and_group_windowing(self):
        g0 = self._names(scan_page(self.page, category="img", count=2, group=0))
        g1 = self._names(scan_page(self.page, category="img", count=2, group=1))
        g2 = self._names(scan_page(self.page, category="img", count=2, group=2))
        self.assertEqual(g0, ["logo.png", "hero.png"])
        self.assertEqual(g1, ["tiny.png", "pic-fallback.png"])
        self.assertEqual(g2, ["vector.png", "pic-source.png"])

    def test_download_media_writes_subset_and_stderr_lines(self):
        items = scan_page(self.page, category="img", formats="png")
        with tempfile.TemporaryDirectory() as out:
            err = io.StringIO()
            paths = download_media(items, out, host="127.0.0.1", err=err)
            self.assertEqual(len(paths), 6)
            written = sorted(Path(p).name for p in paths)
            self.assertEqual(
                written,
                ["hero.png", "logo.png", "pic-fallback.png", "pic-source.png", "tiny.png", "vector.png"],
            )
            # Files exist and are the real PNG bytes we served.
            for p in paths:
                self.assertGreater(Path(p).stat().st_size, 0)
                self.assertEqual(codecs.sniff(Path(p).read_bytes()), "png")
            lines = err.getvalue().splitlines()
            self.assertEqual(len(lines), 6)
            self.assertTrue(all(ln.startswith("wrote ") for ln in lines))
            # Measured images carry the "WxH px · source host" tail.
            self.assertTrue(any("(200x80 px · source 127.0.0.1)" in ln for ln in lines))

    def test_download_media_video_line_has_no_dims(self):
        items = scan_page(self.page, category="video")
        with tempfile.TemporaryDirectory() as out:
            err = io.StringIO()
            # host must be the page's real host: the SSRF guard only tolerates a loopback
            # sub-resource (clip.mp4 is served on 127.0.0.1) when it is on that same host.
            paths = download_media(items, out, host="127.0.0.1", err=err)
            self.assertEqual(len(paths), 1)
            self.assertEqual(err.getvalue().strip(), "wrote %s (source 127.0.0.1)" % paths[0])

    def test_download_media_custom_name_multiple_gets_index_suffix(self):
        items = scan_page(self.page, category="img", formats="png")
        self.assertGreater(len(items), 1)
        with tempfile.TemporaryDirectory() as out:
            paths = download_media(items, out, host="127.0.0.1", name="photo")
            names = [Path(p).name for p in paths]
            # A batch keeps the custom stem but stays distinct via -{index}.
            self.assertEqual(names, ["photo-%d.png" % i for i in range(len(paths))])

    def test_download_media_custom_name_single_is_bare_stem(self):
        # One video item → the custom stem with no index suffix.
        items = scan_page(self.page, category="video")
        self.assertEqual(len(items), 1)
        with tempfile.TemporaryDirectory() as out:
            paths = download_media(items, out, host="127.0.0.1", name="clip")
            self.assertEqual([Path(p).name for p in paths], ["clip.mp4"])

    def test_download_media_custom_name_is_sanitized(self):
        items = scan_page(self.page, category="video")
        with tempfile.TemporaryDirectory() as out:
            paths = download_media(items, out, host="127.0.0.1", name="../a b")
            # Path separators / spaces collapse to '_'; leading dots are stripped.
            self.assertEqual([Path(p).name for p in paths], ["_a_b.mp4"])


class CliScrapeModeTests(unittest.TestCase):
    """The one-shot ``--source-site`` argparse path, driven end-to-end over the server."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "index.html").write_text(_FIXTURE_HTML, encoding="utf-8")
        for name, (w, h) in _IMAGES.items():
            (root / name).write_bytes(_solid_png(w, h))
        (root / "clip.mp4").write_bytes(b"\x00\x00\x00\x18ftypmp42" + b"\x00" * 32)
        handler = lambda *a, **k: _QuietHandler(*a, directory=str(root), **k)  # noqa: E731
        cls._server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls._thread = Thread(target=cls._server.serve_forever, daemon=True)
        cls._thread.start()
        _, port = cls._server.server_address
        cls.page = "http://127.0.0.1:%d/index.html" % port

    @classmethod
    def tearDownClass(cls):
        cls._server.shutdown()
        cls._server.server_close()
        cls._thread.join(timeout=5)
        cls._tmp.cleanup()

    def _run_scrape(self, argv):
        """Run `main(argv)` capturing stderr; return (exit_code, stderr_text)."""
        from pystencil.cli import main

        err = io.StringIO()
        _real = sys.stderr
        sys.stderr = err
        try:
            code = main(argv)
        finally:
            sys.stderr = _real
        return code, err.getvalue()

    def test_scrape_mode_name_filter(self):
        # --source-name is a case-insensitive regex on each media URL.
        with tempfile.TemporaryDirectory() as out:
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-filter", "img",
                 "--source-name", r"(logo|hero)\.png$", "--source-count", "0", out]
            )
            self.assertEqual(code, 0)
            self.assertEqual(
                sorted(p.name for p in Path(out).glob("*.png")),
                ["hero.png", "logo.png"],
            )
            self.assertIn("scraped 2 file(s)", err)

    def test_scrape_mode_invalid_name_regex_is_error(self):
        # An unbalanced group is a hard error (exit 1), mirroring the Zig CLI's regcomp failure.
        with tempfile.TemporaryDirectory() as out:
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-name", "cat(", out]
            )
            self.assertEqual(code, 1)
            self.assertIn("invalid --source-name regex", err)

    def test_scrape_mode_downloads_and_prints_summary(self):
        with tempfile.TemporaryDirectory() as out:
            # The 6 img-category PNGs exceed the default window of 5, so pin count=0 (= all)
            # to download every match and assert the full summary.
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-filter", "img",
                 "--source-format", "png", "--source-count", "0", out]
            )
            self.assertEqual(code, 0)
            self.assertEqual(len(list(Path(out).glob("*.png"))), 6)
            self.assertIn("scraped 6 file(s) from 127.0.0.1 into %s" % out, err)

    def test_scrape_mode_default_count_is_five(self):
        # No --source-count: the entry-layer default of 5 windows the 6 img matches down to 5.
        with tempfile.TemporaryDirectory() as out:
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-filter", "img", out]
            )
            self.assertEqual(code, 0)
            got = sorted(p.name for p in Path(out).glob("*.png"))
            self.assertEqual(
                got, ["hero.png", "logo.png", "pic-fallback.png", "tiny.png", "vector.png"]
            )
            self.assertIn("scraped 5 file(s) from 127.0.0.1 into %s" % out, err)

    def test_scrape_mode_count_zero_is_all(self):
        # --source-count 0 means "all": every one of the 6 img matches is downloaded.
        with tempfile.TemporaryDirectory() as out:
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-filter", "img",
                 "--source-count", "0", out]
            )
            self.assertEqual(code, 0)
            self.assertEqual(len(list(Path(out).glob("*.png"))), 6)

    def test_scrape_mode_count_and_group_paging(self):
        # count=2 group=1 selects the second page: img items 3-4 (tiny, pic-fallback).
        with tempfile.TemporaryDirectory() as out:
            code, err = self._run_scrape(
                ["--source-site", self.page, "--source-filter", "img",
                 "--source-count", "2", "--group", "1", out]
            )
            self.assertEqual(code, 0)
            got = sorted(p.name for p in Path(out).glob("*.png"))
            self.assertEqual(got, ["pic-fallback.png", "tiny.png"])

    def test_scrape_mode_rejects_conflicting_source(self):
        from pystencil.cli import main

        err = io.StringIO()
        _real = sys.stderr
        sys.stderr = err
        try:
            code = main(["--source-site", self.page, "--input", "x.png", "out"])
        finally:
            sys.stderr = _real
        self.assertEqual(code, 2)
        self.assertIn("cannot be combined", err.getvalue())

    def test_scrape_mode_no_media_matched_is_error(self):
        from pystencil.cli import main

        with tempfile.TemporaryDirectory() as out:
            err = io.StringIO()
            _real = sys.stderr
            sys.stderr = err
            try:
                # A format present on no item -> zero matches -> hard error, exit 1.
                code = main(["--source-site", self.page, "--source-format", "tiff", out])
            finally:
                sys.stderr = _real
            self.assertEqual(code, 1)
            self.assertIn("no media matched", err.getvalue())


class SourceUploadReplTests(unittest.TestCase):
    """The REPL ``/source-upload`` command loads a scraped still into the Editor."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "index.html").write_text(_FIXTURE_HTML, encoding="utf-8")
        for name, (w, h) in _IMAGES.items():
            (root / name).write_bytes(_solid_png(w, h))
        (root / "clip.mp4").write_bytes(b"\x00\x00\x00\x18ftypmp42" + b"\x00" * 32)
        handler = lambda *a, **k: _QuietHandler(*a, directory=str(root), **k)  # noqa: E731
        cls._server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls._thread = Thread(target=cls._server.serve_forever, daemon=True)
        cls._thread.start()
        _, port = cls._server.server_address
        cls.page = "http://127.0.0.1:%d/index.html" % port

    @classmethod
    def tearDownClass(cls):
        cls._server.shutdown()
        cls._server.server_close()
        cls._thread.join(timeout=5)
        cls._tmp.cleanup()

    def test_source_upload_loads_indexed_still(self):
        # PNG decode + image_size need no native core, but guard defensively per the
        # skip-pattern in case an Image path reaches for it.
        try:
            from pystencil.core import Core

            Core.load()
        except Exception as exc:  # pragma: no cover - toolchain-dependent
            # The load path here is codec-only; only skip if the core genuinely can't
            # be probed AND a later assertion needs it. It doesn't, so continue.
            _ = exc
        from pystencil.cli import _Repl

        out = io.StringIO()
        repl = _Repl(out)
        # index=1 over image-category (img|bg|poster) stills, png -> hero.png (120x90).
        repl.run(io.StringIO("/source-upload %s index=1 format=png\n/exit\n" % self.page))
        self.assertTrue(repl._editor.has_image())
        self.assertEqual(repl._editor.image_size, (120, 90))
        self.assertIn("loaded", out.getvalue())

    def test_source_upload_index_out_of_range(self):
        from pystencil.cli import _Repl

        out = io.StringIO()
        repl = _Repl(out)
        repl.run(io.StringIO("/source-upload %s index=999\n" % self.page))
        self.assertIn("out of range", out.getvalue())
        self.assertFalse(repl._editor.has_image())

    def test_source_upload_custom_name_overrides_derived(self):
        from pystencil.cli import _Repl

        out = io.StringIO()
        repl = _Repl(out)
        # name= overrides the URL-derived project label.
        repl.run(io.StringIO("/source-upload %s index=1 format=png name=my-hero\n" % self.page))
        self.assertTrue(repl._editor.has_image())
        self.assertEqual(repl._editor.name, "my-hero")


if __name__ == "__main__":
    unittest.main()
