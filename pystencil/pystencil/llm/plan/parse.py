"""Op-plan parsing (contract §1/§2/§3): core/opplan's result typed into :class:`OpPlan`, in
core's §1 words. Twin of the cli's ``src/llm/opplan/validate.zig``.
"""

from __future__ import annotations

import itertools

from ..._ffi.types import NoneType
from ...core import get_core
from ..errors import LlmPlanError
from ..types import AskCard, AskOption, OpPlan, Variant
from .registry import OP_REGISTRY

# opplanStatus: 0 valid, 1 chat-only, 2 invalid.
_INVALID = 2

# A terminal shows no option picture or preview: one note for the whole card, after core's own.
PREVIEW_NOTE = "the console can't show option previews — the choices are listed by name"


def __typed(actions: list) -> list[dict]:
  out: list[dict] = list()
  for a in actions:
    normalizer = OP_REGISTRY[a["op"]].normalizer
    out.append(normalizer(a) if normalizer is not None else a)
  return out


def __variants(doc: dict) -> list[Variant]:
  """The kept variants, an unlabelled one named by its place in the model's list — a
  dropped variant still holds its number."""
  dropped = {w["index"] for w in doc["warnings"] if w["code"] == "W_VARIANT_DROPPED"}
  places = (i for i in itertools.count(1) if i not in dropped)
  out: list[Variant] = list()
  for v, place in zip(doc["variants"], places):
    label = (v.get("label") or "").strip()
    out.append(Variant(label=label or "variant %d" % place, actions=__typed(v["actions"])))
  return out


def __has_previews(doc: dict) -> bool:
  """Whether the card carried any picture or preview — which a terminal drops with one note."""
  options = (doc["ask"] or {}).get("options") or []
  return any("actions" in o or "image" in o for o in options) or any(
    w["code"] == "W_PREVIEW_DROPPED" for w in doc["warnings"])


def __notes(doc: dict) -> list[str]:
  out = [w["message"] for w in doc["warnings"]]
  if __has_previews(doc): out.append(PREVIEW_NOTE)
  return out


def __ask(card: (dict | NoneType)) -> (AskCard | NoneType):
  """The §11 card; a console shows no picture, so each option keeps only its label."""
  if card is None: return None
  return AskCard(
    question=card["question"],
    multi=card["mode"] == "multi",
    allow_custom=card["allowCustom"],
    custom_label=card["customLabel"],
    options=[AskOption(label=o["label"]) for o in card["options"]],
  )


def parse_op_plan(text: str) -> OpPlan:
  """Raw LLM reply text as a validated :class:`OpPlan` (contract §1).

  Text with no JSON object is a chat-only turn (zero actions, not an error). Unknown ops
  and a variant holding a top-level-only or console op drop with a warning; a known op with
  invalid params raises :class:`LlmPlanError`.
  """
  raw = text if isinstance(text, str) else str(text)
  status, doc = get_core().opplan_parse(raw)
  if status == _INVALID: raise LlmPlanError(doc["error"]["message"])
  notes = __notes(doc)
  reply = doc["reply"]
  if notes: reply = reply + "\n" + "\n".join("[warning] " + w for w in notes)
  return OpPlan(
    reply=reply, actions=__typed(doc["actions"]), variants=__variants(doc), warnings=notes,
    ask=__ask(doc["ask"]),
  )
