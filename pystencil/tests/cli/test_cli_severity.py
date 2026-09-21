"""The console's severity vocabulary (pystencil/_severity.py) — the Python twin of the Zig
CLI's logo.err()/logo.note(), including the guard against re-typing the prefixes.
"""

from __future__ import annotations

from pathlib import Path

import contextlib
import io
import os
import unittest

from pystencil import _severity
from pystencil import cli
from pystencil import sitesource


class SeverityPrefixTest(unittest.TestCase):
  """The console's severity vocabulary (pystencil/_severity.py) — the Python twin of
  the Zig CLI's logo.err()/logo.note(). Needs no core: nothing here decodes an image."""

  class _Tty(io.StringIO):
    """A capture buffer that claims to be a terminal, so colour turns on."""

    def isatty(self) -> bool:
      return True

  def setUp(self) -> None:
    self._no_color = os.environ.pop("NO_COLOR", None)

  def tearDown(self) -> None:
    if self._no_color is None:
      os.environ.pop("NO_COLOR", None)
    else:
      os.environ["NO_COLOR"] = self._no_color

  def test_plain_when_the_stream_is_not_a_terminal(self) -> None:
    out = io.StringIO()
    self.assertEqual(
      _severity.error_line("cannot read 'a.png'", out), "error: cannot read 'a.png'"
    )
    self.assertEqual(_severity.note_line("skipped save", out), "note: skipped save")

  def test_colours_only_the_prefix_on_a_terminal(self) -> None:
    tty = self._Tty()
    self.assertEqual(
      _severity.error_line("boom", tty), "\x1b[1;38;2;239;68;68merror: \x1b[0mboom"
    )
    self.assertEqual(
      _severity.note_line("hm", tty), "\x1b[1;38;2;245;158;11mnote: \x1b[0mhm"
    )

  def test_no_color_wins_over_the_terminal(self) -> None:
    os.environ["NO_COLOR"] = "1"
    self.assertEqual(_severity.error_line("boom", self._Tty()), "error: boom")

  def test_repl_lines_stay_byte_for_byte_plain_off_a_terminal(self) -> None:
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO("/upload\n/nope\n"))
    self.assertEqual(
      out.getvalue(),
      "error: /upload needs a path or URL\n"
      "error: unknown command '/nope' (try /help)\n",
    )

  def test_repl_colours_the_prefix_on_a_terminal(self) -> None:
    tty = self._Tty()
    cli._Repl(tty).run(io.StringIO("/upload\n"))
    self.assertEqual(
      tty.getvalue(),
      "\x1b[1;38;2;239;68;68merror: \x1b[0m/upload needs a path or URL\n",
    )

  def test_one_shot_pipeline_errors_go_through_the_helper(self) -> None:
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
      code = cli.main(["--input", "a.png", "--blank", "10", "10", "out.png"])
    self.assertEqual(code, 2)
    self.assertEqual(err.getvalue(), "error: --input and --blank are mutually exclusive\n")

  def test_listing_answers_plain_and_actions_refuse_with_error(self) -> None:
    # Same condition, same wording as the Zig console (cli/src/console/handlers.zig):
    # /connections truthfully answering "there are none" is not a refusal, /disconnect is.
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO("/connections\n/disconnect\n"))
    self.assertEqual(
      out.getvalue(),
      "no server connections — use '/connect <url>'\n"
      "error: no server connections — use '/connect <url>'\n",
    )

  def test_console_sources_use_the_helper_not_the_literal(self) -> None:
    # Single-sourcing guard: a new call site must go through _severity, not re-type the prefix
    # (which would silently opt out of the colouring).
    for mod in (cli, sitesource):
      for path in sorted(Path(mod.__file__).parent.rglob("*.py")):
        src = path.read_text(encoding="utf-8")
        for literal in ('"error: ', "'error: ", '"note: ', "'note: ", '"warning: '):
          self.assertNotIn(literal, src, "%s re-types %r" % (path.name, literal))
