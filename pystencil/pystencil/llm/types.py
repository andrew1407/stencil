"""Op-plan value types: the variant, the §11 ask card and the parsed plan itself."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Sequence

from .._ffi.types import NoneType
from .plan.limits import DEFAULT_CUSTOM_LABEL

@dataclass
class Variant:
  """One branch from the post-actions state: a label (names its output) and its actions."""

  label: str
  actions: list[dict] = field(default_factory=list)


def variant_slug(label: str) -> str:
  """A label as a ``[a-z0-9-]`` slug, "variant" when empty — twin of the CLI's
  ``sanitizeLabel`` and mcp's ``sanitize_label``."""
  s = re.sub(r"[^a-z0-9-]+", "-", (label or "").lower()).strip("-")
  return s or "variant"


def variant_slugs(variants: Sequence["Variant"]) -> list[str]:
  """Unique slugs in order, a taken one gaining the first free ``-2``/``-3``… (the CLI's
  ``variantStem`` and mcp's ``to_edit_requests`` dedupe)."""
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
  """One §11 choice; a console keeps only its ``label`` (§11.4)."""

  label: str


@dataclass
class AskCard:
  """A §11 question put back to the user, answered by number on the next prompt."""

  question: str
  multi: bool = False
  allow_custom: bool = False
  custom_label: str = DEFAULT_CUSTOM_LABEL
  options: list[AskOption] = field(default_factory=list)


@dataclass
class OpPlan:
  """A validated op-plan (§1). A chat-only turn has no actions; ``warnings`` also ride
  ``reply``; ``saved`` gets the paths §2.1 ``save`` actions wrote."""

  reply: str
  actions: list[dict] = field(default_factory=list)
  variants: list[Variant] = field(default_factory=list)
  warnings: list[str] = field(default_factory=list)
  ask: ("AskCard" | NoneType) = None
  saved: list[str] = field(default_factory=list)
