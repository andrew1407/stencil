"""Op-plan parsing (contract §1/§2/§3): tolerant extraction of the first balanced
JSON object from the reply text, then strict validation against the registry.
"""

from __future__ import annotations

import json
import re

from ..._ffi.types import NoneType
from ..errors import LlmPlanError
from .limits import SCHEMA
from ..types import OpPlan, Variant
from .validate import _MisplacedOp, _plan_check, _validate_actions, _validate_ask

# ── op-plan parsing (contract §1/§2/§3) ──
# Field schemas, token grammars and the cross-field rules are the registry's (SCHEMA).

# _ACTION_FIELDS / _ACTION_VALIDATORS / _ACTION_APPLIERS / _TOP_LEVEL_ONLY_OPS /
# _CONSOLE_SETTINGS_OPS are DERIVED from OP_REGISTRY (§13), defined after the appliers.


def __strip_fences(text: str) -> str:
  """Drop Markdown code-fence lines (``` / ```json) before JSON extraction."""
  return "\n".join(
    line for line in text.split("\n") if not line.strip().startswith("```")
  )


def __balanced_end(text: str, start: int) -> (int | NoneType):
  """Index of the ``}`` closing the ``{`` at ``start`` (string/escape aware)."""
  depth = 0
  in_string = False
  escape = False
  for i in range(start, len(text)):
    ch = text[i]
    if in_string:
      if escape:
        escape = False
      elif ch == "\\":
        escape = True
      elif ch == '"': in_string = False
      continue
    if ch == '"':
      in_string = True
    elif ch == "{":
      depth += 1
    elif ch == "}":
      depth -= 1
      if depth == 0: return i
  return None


def __first_json_object(text: str) -> (dict | NoneType):
  """The first balanced ``{…}`` in ``text`` that parses as a JSON object, or None."""
  i = text.find("{")
  while i != -1:
    end = __balanced_end(text, i)
    if end is not None:
      try:
        obj = json.loads(text[i : end + 1])
      except ValueError:
        obj = None
      if isinstance(obj, dict): return obj
    i = text.find("{", i + 1)
  return None


def parse_op_plan(text: str) -> OpPlan:
  """Parse raw LLM reply text into a validated :class:`OpPlan` (contract §1).

  Extraction is tolerant: Markdown code fences are stripped and the first balanced
  ``{…}`` JSON object is taken. Text with no JSON object at all is a *chat-only*
  turn — the raw text becomes ``reply`` with zero actions (not an error). Once an
  object is found, validation is strict: ``reply`` must be a non-empty string, every
  action must validate per §2 (unknown ops are dropped with a warning appended to
  the reply; a known op with invalid params raises :class:`LlmPlanError`), and the
  shared limits apply. ``version`` other than 1 (or absent) is accepted but ignored.

  §1's one exception to that strictness: a variant holding a top-level-only or
  console-settings op is dropped with a warning naming it, and the rest of the plan
  still runs — one misplaced op must not cost the user the whole turn.
  """
  raw = text if isinstance(text, str) else str(text)
  obj = __first_json_object(__strip_fences(raw))
  if obj is None: return OpPlan(reply=raw.strip())
  reply = obj.get("reply")
  warnings: list[str] = list()
  # §1 reply tolerance: models routinely omit the reply while planning valid actions —
  # substitute rather than lose the plan to a missing pleasantry.
  reply_omitted = not isinstance(reply, str) or not reply.strip()
  actions = _validate_actions(obj.get("actions"), warnings, "actions")
  variants: list[Variant] = list()
  raw_variants = obj.get("variants")
  if raw_variants is not None:
    # The registry envelope: ≤ MAX_VARIANTS objects of {label: string, actions}.
    _plan_check(lambda: SCHEMA.check_envelope(raw_variants, "variants"))
    for i, rv in enumerate(raw_variants):
      label = (rv.get("label") or "").strip()
      try:
        v_actions = _validate_actions(
          rv.get("actions"), warnings, "variants[%d]" % i
        )
      except _MisplacedOp as e:
        # §1: drop THIS variant with a warning naming it; the rest still runs.
        named = 'variant %d ("%s")' % (i + 1, label) if label else "variant %d" % (i + 1)
        warnings.append("dropped %s — %s" % (named, e.reason))
        continue
      variants.append(
        Variant(label=label or "variant %d" % (i + 1), actions=v_actions)
      )
  ask = _validate_ask(obj.get("ask"), warnings)
  # "Done." only when the plan actually carries work — a bare "Done." on an
  # empty plan reads as a success that never occurred (contract §1).
  if reply_omitted:
    if actions or variants or ask is not None:
      reply = "Done."
      warnings.append("The model omitted its reply — the plan still ran")
    else:
      reply = "The model returned an empty plan — nothing was changed."
  if warnings:
    reply = reply + "\n" + "\n".join("[warning] " + w for w in warnings)
  return OpPlan(
    reply=reply, actions=actions, variants=variants, warnings=warnings, ask=ask
  )
