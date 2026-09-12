from __future__ import annotations

"""Plan validation primitives shared by the parser and the §11 ask card.

Unknown ops are dropped with a warning (forward compatibility); a known op with
invalid params fails the whole plan; a misplaced top-level-only/console op costs only
the variant it appeared in.
"""

from typing import Any, Callable

from .._types import NoneType
from .._opschema import SchemaError
from .errors import LlmPlanError
from .limits import DEFAULT_CUSTOM_LABEL, MAX_ASK_LABEL, SCHEMA
from .registry import (
  OP_REGISTRY,
  _ACTION_VALIDATORS,
  _CONSOLE_SETTINGS_OPS,
  _TOP_LEVEL_ONLY_OPS,
)
from .types import AskCard, AskOption

class _MisplacedOp(Exception):
  """§1's one exception: a top-level-only or console-settings op inside a variant.
  Costs that variant its place (the caller turns this into a warning), never the plan."""

  def __init__(self, reason: str) -> None:
    super().__init__(reason)
    self.reason = reason


def _plan_check(check: Callable[[], None]) -> None:
  """Run a plan-level registry check; a failure is the uniform plan error."""
  try:
    check()
  except SchemaError as e:
    raise LlmPlanError(str(e)) from None


def _validate_actions(raw: Any, warnings: list[str], where: str) -> list[dict]:
  """Validate an actions array: unknown ops are dropped with a warning (forward
  compatibility); a known op with invalid params fails the whole plan. A misplaced
  top-level-only/console op raises :class:`_MisplacedOp` — the variant goes, not the plan."""
  if raw is None:
    return []
  _plan_check(lambda: SCHEMA.check_envelope(raw, "actions"))
  out: list[dict] = list()
  for a in raw:
    op = a.get("op")
    if not isinstance(op, str) or not op:
      raise LlmPlanError('an action in "%s" is missing its "op"' % where)
    if op not in OP_REGISTRY:
      warnings.append('unknown op "%s" dropped' % op)
      continue
    if op in _TOP_LEVEL_ONLY_OPS and where != "actions":
      raise _MisplacedOp(
        'the "%s" op is top-level only and cannot appear in a variant' % op
      )
    if op in _CONSOLE_SETTINGS_OPS and where != "actions":
      raise _MisplacedOp(
        'the "%s" op adjusts the console, not the image, and cannot appear '
        "in a variant" % op
      )
    out.append(_ACTION_VALIDATORS[op](a))
  return out


# ── §11 interactive replies (`ask`) ───────────────────────────────────────────
def _validate_ask(raw: Any, warnings: list[str]) -> (AskCard | NoneType):
  """Validate the optional ``ask`` object (contract §11) → the card, or ``None``.

  The card's structure — keys, caps, 2..5 options, an image reference's exactly-one-of
  url / projectId / scanIndex, http(s)-only urls — is the registry's ask schema, checked
  on every surface even where nothing renders. Option previews are ordinary §2 actions,
  validated as "inside variants or previews": a misplaced op costs the option its
  preview (a warning), an invalid one the plan. A console has nowhere to show a preview,
  so it is dropped after validation — with ONE note for the card, never one per option —
  and only ``label`` survives.
  """
  if raw is None:
    return None
  _plan_check(lambda: SCHEMA.validate_ask(raw))
  card = SCHEMA.normalize_ask(raw)
  options: list[AskOption] = list()
  dropped_preview = False
  for i, (ro, opt) in enumerate(zip(raw["options"], card["options"])):
    if ro.get("actions") is not None:
      try:
        _validate_actions(ro["actions"], warnings, "ask option %d" % (i + 1))
      except _MisplacedOp:
        pass  # the preview is dropped below anyway — one card-level note
    if ro.get("actions") is not None or ro.get("image") is not None:
      dropped_preview = True
    options.append(AskOption(label=opt["label"]))
  if dropped_preview:
    warnings.append(
      "the console can't show option previews — the choices are listed by name"
    )
  return AskCard(
    question=card["question"],
    multi=card["mode"] == "multi",
    allow_custom=card.get("allowCustom", False),
    custom_label=card.get("customLabel", DEFAULT_CUSTOM_LABEL),
    options=options,
  )
