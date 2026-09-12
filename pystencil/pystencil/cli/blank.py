from __future__ import annotations

"""The ``[format] [w h] [color]`` blank grammar — one parser for both callers.

``--blank`` (one-shot) and ``/blank`` (console) accept the same tokens, so they share
this type instead of each destructuring an anonymous tuple. Port of the Zig CLI's
``args.parseBlank``.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Sequence


def _is_int(tok: str) -> bool:
  """True when ``tok`` is a base-10 integer (used to spot --blank dimensions)."""
  try:
    int(tok)
    return True
  except ValueError:
    return False


@dataclass
class BlankSpec:
  """A parsed blank request. ``leftover`` is whatever followed the grammar — the
  one-shot entry recovers an output path argparse greedily swallowed from it."""

  page: Optional[str] = None
  width: Optional[int] = None
  height: Optional[int] = None
  color: str = "white"
  leftover: List[str] = field(default_factory=list)

  @classmethod
  def parse(cls, tokens: Sequence[str]) -> "BlankSpec":
    """Parse ``[format] [w h] [color]`` from a token list.

    An optional leading page-format token names the page (case-insensitive, e.g.
    "b5"); it is mutually exclusive with an explicit ``w h`` pair. A leading integer
    requires a matching height (else it is malformed). A colour is consumed only
    when the core recognizes it.
    """
    from ..core import get_core

    core = get_core()
    spec = cls()
    i = 0
    toks = list(tokens)
    if toks and not _is_int(toks[0]):
      spec.page = core.canonical_page_format(toks[0])
      if spec.page is not None:
        i = 1
    if i < len(toks) and _is_int(toks[i]):
      # A format token and explicit dimensions are mutually exclusive (pinned).
      if spec.page is not None:
        raise ValueError("--blank takes a page format or explicit w h, not both")
      # A width is only meaningful paired with a height.
      if i + 1 >= len(toks) or not _is_int(toks[i + 1]):
        raise ValueError("--blank width needs a matching height")
      spec.width = int(toks[i])
      spec.height = int(toks[i + 1])
      i += 2
    if i < len(toks):
      tok = toks[i]
      # Only swallow the colour when it is one (and not a flag), like the Zig parser.
      if not tok.startswith("-") and core.parse_color(tok) is not None:
        spec.color = tok
        i += 1
    spec.leftover = toks[i:]
    return spec

  @classmethod
  def from_arg(cls, arg: str) -> "BlankSpec":
    """Parse the console's single ``/blank`` argument string."""
    return cls.parse(arg.split() if arg else [])
