"""The one-shot ``--source-site`` argparse path, driven end-to-end over the server."""

from __future__ import annotations

import io
import sys
import tempfile
from pathlib import Path

from tests.helpers.servedsite import ServedSiteCase


class CliScrapeModeTests(ServedSiteCase):
  """The one-shot ``--source-site`` argparse path, driven end-to-end over the server."""

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
