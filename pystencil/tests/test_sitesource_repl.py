"""The REPL ``/source-upload`` command loading a scraped still into the Editor."""

from __future__ import annotations

import io
import unittest

from tests.servedsite import ServedSiteCase


class SourceUploadReplTests(ServedSiteCase):
    """The REPL ``/source-upload`` command loads a scraped still into the Editor."""

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
