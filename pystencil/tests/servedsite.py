"""A real local static HTTP server serving one fixture page, for the scraper tests.

Three suites drive the scraper end-to-end over http(s); they share this fixture — the
page, the tiny real PNGs behind it, and the thread serving them on 127.0.0.1:0 — rather
than each standing up its own copy.
"""

from __future__ import annotations

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

from pystencil import codecs


def _solid_png(w: int, h: int, rgb=(200, 30, 30)) -> bytes:
    data = bytearray(w * h * 4)
    for i in range(w * h):
        d = i * 4
        data[d], data[d + 1], data[d + 2], data[d + 3] = rgb[0], rgb[1], rgb[2], 255
    return codecs.encode_png(w, h, data)


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


class ServedSiteCase(unittest.TestCase):
    """Serves the fixture page + images for the life of the test class.

    Subclasses read ``cls.base`` (the site root) and ``cls.page`` (the fixture page).
    """

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
        _host, port = cls._server.server_address
        cls.base = "http://127.0.0.1:%d/" % port
        cls.page = cls.base + "index.html"

    @classmethod
    def tearDownClass(cls):
        cls._server.shutdown()
        cls._server.server_close()
        cls._thread.join(timeout=5)
        cls._tmp.cleanup()
