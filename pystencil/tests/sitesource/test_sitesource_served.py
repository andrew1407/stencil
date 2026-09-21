"""scan_page + download_media over a real local static HTTP server."""

from __future__ import annotations

import io
import tempfile
from pathlib import Path

from pystencil import codecs
from pystencil.sitesource import download_media, scan_page
from tests.helpers.servedsite import ServedSiteCase


class ServedSiteTests(ServedSiteCase):
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
