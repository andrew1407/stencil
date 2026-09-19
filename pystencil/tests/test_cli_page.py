"""The REPL's page-format surface: /format (list, named, custom), the /blank defaults it
drives, and /filter's bare listing.
"""

from __future__ import annotations

import io
import json

from pystencil import cli

from tests.clicase import _PipelineCase


class ReplPageFormatTest(_PipelineCase):
  """/format and the blanks it sizes — the Python twin of the Zig console's
  doFormat/doBlank pair."""

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
    # '/blank b5' makes B5 the session page format (the Zig console's doBlank →
    # session.setPageSize): the next bare /blank is B5 and the layout carries pageSize "B5".
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
    # PINNED (Zig console "blank with explicit dims keeps the /format pick"): a dims-only blank
    # sizes the page but the exported layout still carries pageSize "B5".
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
    # A layout can adopt pageSize "custom" with no (or only one) cm dimension; a bare /blank
    # must fall through to the default A4 blank like the Zig console.
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
    # A layout can carry an unknown pageSize (adopted raw); a bare /blank then quietly creates
    # the default A4 blank instead of erroring (canonicalPageFormat → null).
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
