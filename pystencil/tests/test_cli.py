from __future__ import annotations

# Tests for the pystencil command-line front-end (pystencil/cli.py).
#
# These drive cli.main() directly with argument lists (no subprocess), capturing
# stderr to assert the canonical `wrote {path} ({w}x{h})` contract and verifying
# the written artifacts decode/parse correctly. The whole suite self-skips when
# the native core library cannot be loaded, since every pipeline needs it.

import contextlib
import io
import json
import os
import tempfile
import unittest

from pystencil import cli
from pystencil import codecs


class CliPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # The one-shot pipeline always touches the core (blank/crop/filter), so
        # skip the whole suite if the shared library is unavailable.
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as e:  # pragma: no cover - environment-dependent
            raise unittest.SkipTest("native core unavailable: %s" % e)

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.tmp = self._dir.name

    def tearDown(self) -> None:
        self._dir.cleanup()

    def _path(self, name: str) -> str:
        """Absolute path inside this test's temp directory."""
        return os.path.join(self.tmp, name)

    def _run(self, args: list) -> str:
        """Invoke cli.main(args), asserting success, and return captured stderr."""
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.main(args)
        self.assertEqual(code, 0, "cli.main exited non-zero; stderr=%r" % err.getvalue())
        return err.getvalue()

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
        page, width, height, color, leftover = cli._consume_blank(["b5", "pink", "out.png"])
        self.assertEqual(page, "B5")
        self.assertIsNone(width)
        self.assertIsNone(height)
        self.assertEqual(color, "pink")
        self.assertEqual(leftover, ["out.png"])

    def test_consume_blank_dims_still_parse(self) -> None:
        # The pre-format grammar is unchanged: `w h [color]`.
        page, width, height, color, leftover = cli._consume_blank(["800", "600", "white"])
        self.assertIsNone(page)
        self.assertEqual((width, height), (800, 600))
        self.assertEqual(color, "white")
        self.assertEqual(leftover, [])

    def test_consume_blank_format_and_dims_are_exclusive(self) -> None:
        # PINNED: a format token and an explicit w h pair cannot be combined.
        with self.assertRaises(ValueError):
            cli._consume_blank(["a5", "800", "600"])

    def test_blank_named_format_writes_that_page(self) -> None:
        # `--blank b5 out.png` sizes the page from the B5 table entry (17.6×25cm @96dpi).
        from pystencil.core import get_core

        out = self._path("b5.png")
        stderr = self._run(["--blank", "b5", out])
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertIn("wrote %s (%dx%d)" % (out, w, h), stderr)

    def test_repl_format_lists_sets_and_drives_blank(self) -> None:
        # /format bare lists the formats; /format b5 sets the session format, which
        # then drives the /blank default page.
        from pystencil.core import get_core

        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/format\n/format b5\n/blank\n"))
        text = out.getvalue()
        self.assertIn("A0", text)
        self.assertIn("C10", text)
        self.assertIn("custom <w> <h>", text)
        self.assertIn("page format B5 (17.6×25cm)", text)
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertIn("blank %dx%d (white)" % (w, h), text)

    def test_repl_format_unknown_name_hints(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/format z9\n"))
        self.assertIn("type '/format' to list formats", out.getvalue())

    def test_repl_blank_format_token_adopts_session_format(self) -> None:
        # '/blank b5' makes B5 the session page format (mirror of the Zig console's
        # doBlank -> session.setPageSize): the next bare /blank is B5 again and the
        # exported layout carries pageSize "B5".
        from pystencil.core import get_core

        lay = self._path("blank-b5.json")
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/blank b5\n/blank\n/layout %s\n" % lay))
        text = out.getvalue()
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertEqual(text.count("blank %dx%d (white)" % (w, h)), 2)
        with open(lay, "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["pageSize"], "B5")

    def test_repl_blank_explicit_dims_keep_session_format(self) -> None:
        # PINNED (Zig console "blank with explicit dims keeps the /format pick"):
        # a dims-only blank sizes the page but preserves the picked format, so the
        # exported layout still carries pageSize "B5".
        lay = self._path("blank-dims.json")
        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format b5\n/blank 40 30\n/layout %s\n" % lay))
        self.assertEqual(repl._editor.page_format, "B5")
        with open(lay, "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["pageSize"], "B5")

    def test_repl_blank_explicit_dims_keep_custom_pick(self) -> None:
        # Same for a custom pick: the format and its cm dims survive an explicit-dims
        # blank (mirror of the Zig console test's '/format custom 10 15' + '/blank 64 48').
        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format custom 10 15\n/blank 64 48 red\n"))
        self.assertEqual(repl._editor.page_format, "custom")
        self.assertEqual(repl._editor.custom_page_width, 10.0)
        self.assertEqual(repl._editor.custom_page_height, 15.0)

    def test_repl_blank_explicit_dims_without_pick_stay_unset(self) -> None:
        # With no prior /format pick a dims-only blank leaves the format unset, so the
        # exported layout omits pageSize entirely.
        lay = self._path("blank-dims-unset.json")
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/blank 40 30\n/layout %s\n" % lay))
        with open(lay, "r", encoding="utf-8") as f:
            self.assertNotIn("pageSize", json.load(f))

    def test_repl_bare_blank_uses_custom_pick_dims(self) -> None:
        # A bare /blank on a custom pick renders the custom cm dims at the default DPI
        # and keeps the pick (Zig console: 10×15cm @96dpi -> 378x567 px).
        from pystencil.core import get_core

        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format custom 10 15\n/blank\n"))
        w, h = get_core().default_blank_size_px(10.0, 15.0)
        self.assertIn("blank %dx%d (white)" % (w, h), out.getvalue())
        self.assertEqual(repl._editor.page_format, "custom")
        self.assertEqual(repl._editor.custom_page_width, 10.0)
        self.assertEqual(repl._editor.custom_page_height, 15.0)

    def test_repl_bare_blank_custom_without_dims_falls_back_to_a4(self) -> None:
        # A layout can adopt pageSize "custom" with no (or only one) cm dimension
        # (adoptLayoutMeta keeps it raw); a bare /blank must fall through to the
        # default A4 blank like the Zig console — not error or blank a 1px page.
        from pystencil.core import get_core

        a4_w, a4_h = get_core().default_blank_size_px(21.0, 29.7)
        for meta in ({"pageSize": "custom"}, {"pageSize": "custom", "customPageWidth": 10.0}):
            out = io.StringIO()
            repl = cli._Repl(out)
            repl._editor.blank(8, 8)
            repl._editor.apply_layout(meta)
            repl.run(io.StringIO("/blank\n"))
            text = out.getvalue()
            self.assertNotIn("error", text, meta)
            self.assertIn("blank %dx%d (white)" % (a4_w, a4_h), text, meta)
            # The unusable pick does not survive the blank (nothing to restore).
            self.assertEqual(repl._editor.page_format, "", meta)

    def test_repl_bare_blank_with_unknown_adopted_format_falls_back_to_a4(self) -> None:
        # A layout can carry an unknown pageSize (adopted raw, like adoptLayoutMeta);
        # a bare /blank then quietly creates the default A4 blank instead of erroring
        # (the Zig console maps it through canonicalPageFormat -> null).
        from pystencil.core import get_core

        out = io.StringIO()
        repl = cli._Repl(out)
        repl._editor.blank(8, 8)
        repl._editor.apply_layout({"pageSize": "Z9"})
        repl.run(io.StringIO("/blank\n"))
        text = out.getvalue()
        self.assertNotIn("error", text)
        w, h = get_core().default_blank_size_px(21.0, 29.7)
        self.assertIn("blank %dx%d (white)" % (w, h), text)

    def test_repl_format_custom_rejects_nan_and_out_of_range(self) -> None:
        # NaN/inf and out-of-range cm dims are rejected (parseCmDim's 0.1–500 pin) so
        # the exported layout can never contain a non-RFC-8259 `NaN` constant.
        for spec in ("nan nan", "inf 10", "1000 1000", "0.05 10"):
            out = io.StringIO()
            repl = cli._Repl(out)
            repl.run(io.StringIO("/format custom %s\n" % spec))
            text = out.getvalue()
            self.assertIn("error: custom takes width + height in cm (0.1-500)", text)
            self.assertEqual(repl._editor.page_format, "", spec)

    def test_repl_bare_filter_lists_variants(self) -> None:
        # A bare /filter lists the possible modes instead of erroring out.
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/filter\n"))
        text = out.getvalue()
        self.assertNotIn("error:", text)
        for variant in ("bw", "sepia", "invert", "contour", "none"):
            self.assertIn(variant, text)


if __name__ == "__main__":
    unittest.main()
