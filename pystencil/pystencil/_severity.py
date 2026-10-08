"""Severity prefixes for the console channel — the Python twin of ``cli/src/app/logo.zig``.

``error: `` (the command did not do what was asked) and ``note: `` (it went ahead, with
something worth saying), which grep and the adapters parse. Bold red / amber only on a
terminal with ``NO_COLOR`` unset; anywhere else the text is byte-for-byte the plain prefix.
"""

from __future__ import annotations

import os
from typing import TextIO

from ._ffi.types import NoneType

_RESET = "\x1b[0m"
_RED = "\x1b[1;38;2;239;68;68m"  # #ef4444 `error:` prefix
_AMBER = "\x1b[1;38;2;245;158;11m"  # #f59e0b `note:` prefix

ERROR = "error: "
NOTE = "note: "


def color_enabled(stream: (TextIO | NoneType)) -> bool:
  """True when `stream` is a terminal that accepts colour (NO_COLOR wins)."""
  if stream is None or os.environ.get("NO_COLOR") is not None: return False
  try:
    return bool(stream.isatty())
  except (AttributeError, ValueError):
    return False


def error_line(msg: str, stream: (TextIO | NoneType) = None) -> str:
  """``error: <msg>`` — the prefix coloured when `stream` is a colour terminal."""
  if color_enabled(stream): return "%s%s%s%s" % (_RED, ERROR, _RESET, msg)
  return ERROR + msg


def note_line(msg: str, stream: (TextIO | NoneType) = None) -> str:
  """``note: <msg>`` — the prefix coloured when `stream` is a colour terminal."""
  if color_enabled(stream): return "%s%s%s%s" % (_AMBER, NOTE, _RESET, msg)
  return NOTE + msg


def emit_error(stream: TextIO, msg: str) -> None:
  """Write one ``error: `` line (newline included) to the human channel."""
  stream.write(error_line(msg, stream) + "\n")


def emit_note(stream: TextIO, msg: str) -> None:
  """Write one ``note: `` line (newline included) to the human channel."""
  stream.write(note_line(msg, stream) + "\n")
