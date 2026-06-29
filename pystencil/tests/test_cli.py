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


if __name__ == "__main__":
    unittest.main()
