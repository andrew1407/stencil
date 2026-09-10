"""Byte-exact goldens for pystencil's user-facing text.

The console ``/help`` listing and the ``--help`` wording are slated to move into JSON
assets, so these pin the current bytes to prove the move is verbatim. Rewrite with
``PYSTENCIL_UPDATE_GOLDENS=1 python3 -m unittest discover -s tests``.
"""

from __future__ import annotations

import argparse
import os
import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil import cli

_GOLDENS_DIR = Path(__file__).resolve().parent / "goldens"

# argparse wraps to the terminal width, so pin one; the prog name is already fixed.
_HELP_COLUMNS = "80"


def _diff(expected, actual):
    """The first few differing lines, so a failure names the wording that moved."""
    want = expected.split("\n")
    got = actual.split("\n")
    lines = []
    for i in range(max(len(want), len(got))):
        w = want[i] if i < len(want) else "<eof>"
        g = got[i] if i < len(got) else "<eof>"
        if w == g or len(lines) >= 12:
            continue
        lines.append("  line %d:\n    want %s\n    got  %s" % (i + 1, w, g))
    if not lines:
        lines.append("  identical line-wise; lengths %d vs %d" % (len(expected), len(actual)))
    return "\n".join(lines)


def _format_help():
    """``--help`` at a pinned width, with the stdlib's 3.10 section rename undone."""
    previous = os.environ.get("COLUMNS")
    os.environ["COLUMNS"] = _HELP_COLUMNS
    try:
        text = cli._build_parser().format_help()
    finally:
        if previous is None:
            del os.environ["COLUMNS"]
        else:
            os.environ["COLUMNS"] = previous
    return text.replace("\noptional arguments:\n", "\noptions:\n")


def _renders_short_and_long_paired():
    """False on argparse 3.13+, which prints ``-x, --xx XX`` for ``-x XX, --xx XX``."""
    probe = argparse.ArgumentParser(prog="probe", add_help=False)
    probe.add_argument("-x", "--xx")
    return "-x XX, --xx XX" in probe.format_help()


class TextGoldenTest(unittest.TestCase):
    def check(self, name, actual):
        path = _GOLDENS_DIR / name
        if os.environ.get("PYSTENCIL_UPDATE_GOLDENS") == "1":
            _GOLDENS_DIR.mkdir(parents=True, exist_ok=True)
            path.write_text(actual, encoding="utf-8")
            return
        self.assertTrue(path.is_file(), "missing golden %s — rerun with PYSTENCIL_UPDATE_GOLDENS=1" % path)
        expected = path.read_text(encoding="utf-8")
        if expected != actual:
            self.fail("golden %s differs (PYSTENCIL_UPDATE_GOLDENS=1 to rewrite):\n%s"
                      % (name, _diff(expected, actual)))

    def test_console_help_matches_the_golden(self):
        self.check("console_help.txt", cli._HELP)

    def test_argparse_help_matches_the_golden(self):
        if not _renders_short_and_long_paired():
            self.skipTest("argparse renders short/long options compactly here; "
                          "argparse_strings.txt still pins the wording")
        self.check("argparse_help.txt", _format_help())

    def test_argparse_strings_match_the_golden(self):
        """The parser's own wording, independent of how argparse lays it out."""
        parser = cli._build_parser()
        out = ["prog: %s" % parser.prog, "description: %s" % parser.description, ""]
        for action in parser._actions:
            flags = " ".join(action.option_strings) or action.dest
            out.append("%s [metavar=%s default=%r]\n  %s"
                       % (flags, action.metavar or "", action.default, action.help))
        self.check("argparse_strings.txt", "\n".join(out) + "\n")


if __name__ == "__main__":
    unittest.main()
