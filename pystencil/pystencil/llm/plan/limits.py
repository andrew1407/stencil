"""Contract limits (§1/§7/§11) — the same numbers in every client — read off the
shared op-registry schema, plus the chat-transcript constants built on them.
"""

from __future__ import annotations

import re

from ..._ffi.types import NoneType
from ..._opschema import schema

# ── contract limits (§1/§7/§11 — the same numbers in every client) ──
# From the checked-in copy of browser/js/config/llm/opRegistry.json, byte-pinned by tests.
SCHEMA = schema("pystencil")
_LIMITS = SCHEMA.limits
MAX_ACTIONS = _LIMITS["MAX_ACTIONS"]              # per plan: top-level and per variant
MAX_VARIANTS = _LIMITS["MAX_VARIANTS"]
MAX_LAYOUT_LINES = _LIMITS["MAX_LAYOUT_LINES"]
MAX_STRING_LENGTH = _LIMITS["MAX_STRING_CHARS"]   # per string field unless its spec caps it
MAX_FRAME_INDICES = _LIMITS["MAX_FRAME_INDICES"]
MAX_SAVE_NAME = _LIMITS["MAX_SAVE_NAME"]          # §2.1: the `save` op's optional project name
MAX_PATH_CHARS = _LIMITS["MAX_PATH_CHARS"]        # §10: the longest local path a `save` op may carry
# §11 interactive replies — the same numbers as every other client.
MIN_ASK_OPTIONS = _LIMITS["ask"]["minOptions"]
MAX_ASK_OPTIONS = _LIMITS["ask"]["maxOptions"]
MAX_ASK_QUESTION = _LIMITS["ask"]["question"]
MAX_ASK_LABEL = _LIMITS["ask"]["label"]
MAX_ASK_ANSWER = _LIMITS["ask"]["answer"]
DEFAULT_CUSTOM_LABEL = SCHEMA.registry["ask"]["defaultCustomLabel"]
MAX_HISTORY = 32        # chat messages replayed per call
# §12 chat persistence — the persisted-chat document version Chat.to_doc writes and
# Chat.from_doc accepts (any other version is treated as "no saved chat").
CHAT_DOC_VERSION = 1
# §7's auto-continuation note: the sentence the console appends to the RESTATED request.
# It sits beside the §12 rules so the one place that writes it and the one that must not agree.
CONTINUATION_NOTE = (
  "[The working image is now the picture that action just made — "
  "continue with it, using its real pixel size.]"
)
_CONTINUATION_OPEN = "[The working image is now"
_PLAN_VERSION_KEY = re.compile(r'"version"\s*:')
_PLAN_FIELD_KEY = re.compile(r'"(?:actions|reply|variants|ask)"\s*:')


def chat_display_text(role: str, text: str) -> (str | NoneType):
  """The §12.1 text to persist/restore for one turn, or None when it is dropped.

  The document is SHARED across surfaces and "a restored transcript must read as a
  conversation", so machinery never enters it. Applied on BOTH sides (``Chat.to_doc``
  and ``Chat.from_doc``, plus the editor's ``chat`` block), so a document from another
  surface or an older build can't be replayed as the user's own words:

  * §7's continuation note — appended to the restated request (stripped, the request
   stays) or standing alone in any bracketed variant (the turn is dropped);
  * an assistant turn that is a raw op-plan — §7 permits that on the WIRE, §12.1 does
   not. Assistant turns only: a user may paste JSON and see it again.
  """
  t = str(text or "").strip()
  if t.endswith("]"):
    at = t.rfind(_CONTINUATION_OPEN)
    if at != -1: t = t[:at].rstrip()
  if not t: return None
  if role == "assistant" and t[0] in "{[" and _PLAN_VERSION_KEY.search(t) and _PLAN_FIELD_KEY.search(t):
    return None
  return t

# How many images ONE message may carry (§7; the browser's MAX_ATTACHMENTS and the
# desktop's kMaxAttachments). The working image rides on top and does not count.
