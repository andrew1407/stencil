"""§11 interactive replies: validating an ``ask`` card, rendering it for this text
console, and turning the user's numeric pick back into answer text.
"""

from __future__ import annotations

import re

from .._ffi.types import NoneType
from .plan.limits import MAX_ASK_ANSWER
from .types import AskCard

def ask_answer_text(
  card: (AskCard | NoneType), typed: str
) -> (str | NoneType):
  """The joined labels the user picked by number (§11.4: ``"2"``, or ``"1,3"`` on a multi
  card), or ``None`` when the text is no selection and goes to the model as typed."""
  if card is None or not card.options: return None
  text = (typed or "").strip()
  if not text: return None
  picked: list[int] = list()
  for token in re.split(r"[,\s]+", text):
    if not token: continue
    if not token.isdigit(): return None
    n = int(token)
    if not 1 <= n <= len(card.options): return None
    if n - 1 not in picked:
      picked.append(n - 1)
  if not picked: return None
  if len(picked) > 1 and not card.multi: return None
  return ", ".join(card.options[i].label for i in picked)[:MAX_ASK_ANSWER]


def format_ask(card: AskCard) -> str:
  """The card as console text: the question, its numbered options, and how to answer."""
  lines = ["", card.question]
  for i, option in enumerate(card.options): lines.append("  %d. %s" % (i + 1, option.label))
  if card.allow_custom: lines.append("  or type your own: %s" % card.custom_label)
  lines.append(
    "answer with the number%s, or just say what you want"
    % ("s (e.g. 1,3)" if card.multi else " (e.g. 2)")
  )
  return "\n".join(lines)
