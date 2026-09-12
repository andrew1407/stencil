"""The one-shot argv pipeline: blank/filter/layout writes and the `[format] [w h] [color]`
blank grammar. Drives cli.main() directly (no subprocess), asserting the canonical
`wrote {path} ({w}x{h})` contract and that the written artifacts decode.
"""

from __future__ import annotations

import contextlib
import io
import json
import os

from pystencil import cli
from pystencil import codecs

from tests.clicase import _PipelineCase


class CliPipelineTest(_PipelineCase):
  def test_blank_writes_decodable_png(self) -> None:
    # `--blank 40 30 white out.png` writes a PNG decodable at 40x30.
    out = self._path("out.png")
    stderr = self._run(["--blank", "40", "30", "white", out])
    self.assertIn("wrote %s (40x30)" % out, stderr)
    with open(out, "rb") as fh:
      raw = fh.read()
    self.assertEqual(codecs.sniff(raw), "png")
    w, h, _ = codecs.decode(raw)
    self.assertEqual((w, h), (40, 30))

  def test_blank_with_bw_filter(self) -> None:
    # `--blank 40 30 --filter bw out.png` runs the filter and writes the image.
    out = self._path("bw.png")
    self._run(["--blank", "40", "30", "--filter", "bw", out])
    with open(out, "rb") as fh:
      w, h, _ = codecs.decode(fh.read())
    self.assertEqual((w, h), (40, 30))

  def test_save_layout_json_contains_image_width(self) -> None:
    # `--blank 40 30 --save-layout lay.json` writes a JSON layout file.
    lay = self._path("lay.json")
    self._run(["--blank", "40", "30", "--save-layout", lay])
    self.assertTrue(os.path.exists(lay))
    with open(lay, "r", encoding="utf-8") as fh:
      text = fh.read()
    self.assertIn("imageWidth", text)
    parsed = json.loads(text)
    self.assertEqual(parsed["imageWidth"], 40)
    self.assertEqual(parsed["imageHeight"], 30)

  def test_save_layout_dot_json_passthrough(self) -> None:
    # A path ending in .json is written verbatim (path semantics, /layout parity).
    lay = self._path("exact.json")
    self._run(["--blank", "20", "20", "--save-layout", lay])
    self.assertTrue(os.path.exists(lay))

  def test_save_layout_directory_prefix(self) -> None:
    # A non-.json path is a directory/prefix → "<dir>/<project>.json"; the
    # project name for a blank source is "blank".
    subdir = self._path("layouts")
    os.makedirs(subdir, exist_ok=True)
    self._run(["--blank", "20", "20", "--save-layout", subdir])
    expected = os.path.join(subdir, "blank.json")
    self.assertTrue(os.path.exists(expected), "expected %s" % expected)
    with open(expected, "r", encoding="utf-8") as fh:
      self.assertIn("imageWidth", fh.read())

  def test_no_source_is_an_error(self) -> None:
    # With neither --input nor --blank the pipeline reports an error.
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
      code = cli.main([self._path("nope.png")])
    self.assertNotEqual(code, 0)
    self.assertIn("error:", err.getvalue())

  def test_consume_blank_leading_format_token(self) -> None:
    # `[format] [w h] [color]`: a case-insensitive named format leads; a colour
    # and a leftover output path still parse behind it.
    spec = cli.BlankSpec.parse(["b5", "pink", "out.png"])
    self.assertEqual(spec.page, "B5")
    self.assertIsNone(spec.width)
    self.assertIsNone(spec.height)
    self.assertEqual(spec.color, "pink")
    self.assertEqual(spec.leftover, ["out.png"])

  def test_consume_blank_dims_still_parse(self) -> None:
    # The pre-format grammar is unchanged: `w h [color]`.
    spec = cli.BlankSpec.parse(["800", "600", "white"])
    self.assertIsNone(spec.page)
    self.assertEqual((spec.width, spec.height), (800, 600))
    self.assertEqual(spec.color, "white")
    self.assertEqual(spec.leftover, [])

  def test_consume_blank_format_and_dims_are_exclusive(self) -> None:
    # PINNED: a format token and an explicit w h pair cannot be combined.
    with self.assertRaises(ValueError):
      cli.BlankSpec.parse(["a5", "800", "600"])

  def test_blank_grammar_has_one_parser(self) -> None:
    # `--blank` and `/blank` read the same tokens through the same type.
    self.assertEqual(cli.BlankSpec.from_arg("b5 pink"), cli.BlankSpec.parse(["b5", "pink"]))

  def test_blank_named_format_writes_that_page(self) -> None:
    # `--blank b5 out.png` sizes the page from the B5 table entry (17.6×25cm @96dpi).
    from pystencil.core import get_core

    out = self._path("b5.png")
    stderr = self._run(["--blank", "b5", out])
    w, h = get_core().default_blank_size_px(17.6, 25.0)
    self.assertIn("wrote %s (%dx%d)" % (out, w, h), stderr)
