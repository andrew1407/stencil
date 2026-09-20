from __future__ import annotations

"""§11 interactive replies: validating an ``ask`` card, rendering it for this text
console, and turning the user's numeric pick back into answer text.
"""

import re

from .._types import NoneType
from .limits import MAX_ASK_ANSWER
from .types import AskCard

def ask_answer_text(
  card: (AskCard | NoneType), typed: str
) -> (str | NoneType):
  """Resolve what the user typed at a card into the answer for their next turn.

  A console answers by NUMBER (contract §11.4): ``"2"`` picks one option, ``"1,3"`` (or
  ``"1 3"``) picks several when the card is multi-select. Returns the joined labels, or
  ``None`` when the text is not a selection — which is not an error: it simply goes to the
  model as typed, so an unanswered card never blocks the conversation.
  """
  if card is None or not card.options: return None
  text = (typed or "").strip()
  if not text: return None
  picked: list[int] = list()
  for token in re.split(r"[,\s]+", text):
    if not token: continue
    if not token.isdigit(): return None
    n = int(token)
    if not 1 <= n <= len(card.options): return None
    if n - 1 not in picked:  # a repeat is the user re-stating a pick
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
