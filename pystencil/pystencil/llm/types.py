from __future__ import annotations

"""Op-plan value types: the variant, the §11 ask card and the parsed plan itself."""

import re
from dataclasses import dataclass, field
from typing import Any, Sequence

from .._types import NoneType
from .limits import DEFAULT_CUSTOM_LABEL

# ── op-plan value types ───────────────────────────────────────────────────────
@dataclass
class Variant:
  """One alternative branch: a label (used for file/project naming) + its actions.

  Each variant starts from the image state AFTER the plan's top-level actions and
  yields one extra output image.
  """

  label: str
  actions: list[dict] = field(default_factory=list)


def variant_slug(label: str) -> str:
  """Sanitize a variant label into a filename slug (``[a-z0-9-]``, runs of other
  characters collapsed to a dash, "variant" fallback) — the Python counterpart of
  the Zig CLI's ``sanitizeLabel`` and mcp's ``sanitize_label``."""
  s = re.sub(r"[^a-z0-9-]+", "-", (label or "").lower()).strip("-")
  return s or "variant"


def variant_slugs(variants: Sequence["Variant"]) -> list[str]:
  """Unique file slugs for a plan's variants, in order.

  Each label goes through :func:`variant_slug`; a slug already taken gains the first
  free ``-2``/``-3``/… suffix so same-slug labels ("Rotated!" vs "rotated") don't
  silently overwrite each other's output files — the same dedupe the Zig CLI's
  ``variantStem`` and mcp's ``to_edit_requests`` apply.
  """
  taken: set = set()
  out: list[str] = list()
  for v in variants:
    slug = variant_slug(v.label)
    if slug in taken:
      n = 2
      while "%s-%d" % (slug, n) in taken: n += 1
      slug = "%s-%d" % (slug, n)
    taken.add(slug)
    out.append(slug)
  return out


@dataclass
class AskOption:
  """One choice on an :class:`AskCard` (contract §11).

  A console cannot show a picture, so an option's preview — a render spec or an image
  reference — is dropped at parse time and only ``label`` survives (§11.4). The option
  itself is never dropped.
  """

  label: str


@dataclass
class AskCard:
  """A question put back to the user (contract §11).

  Rendered as a numbered list and answered by number on the next prompt; ``multi`` marks a
  card that takes several picks, ``allow_custom`` one that also accepts free text.
  """

  question: str
  multi: bool = False
  allow_custom: bool = False
  custom_label: str = DEFAULT_CUSTOM_LABEL
  options: list[AskOption] = field(default_factory=list)


@dataclass
class OpPlan:
  """A validated op-plan (contract §1): the chat reply plus whitelisted actions.

  ``actions``/``variants[i].actions`` hold normalized action dicts that passed the
  strict per-op validation; ``warnings`` lists any unknown ops that were dropped
  (they are also appended to ``reply``). A chat-only turn is a plan with the raw
  text as ``reply`` and no actions/variants. ``saved`` is filled in by
  :func:`execute_op_plan` with the ``.stencil`` paths the plan's §2.1 ``save``
  actions wrote.
  """

  reply: str
  actions: list[dict] = field(default_factory=list)
  variants: list[Variant] = field(default_factory=list)
  warnings: list[str] = field(default_factory=list)
  ask: ("AskCard" | NoneType) = None
  saved: list[str] = field(default_factory=list)
