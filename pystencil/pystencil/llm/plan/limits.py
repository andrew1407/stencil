"""Contract limits (§1/§7/§11) — the same numbers in every client — read off the
shared op registry, plus the chat-transcript constants built on them.
"""

from __future__ import annotations

import importlib.resources
import json
import re

from ..._ffi.types import NoneType

# The build-time copy of common/config/llm/opRegistry.json (byte-pinned by tests): core
# validates against these bytes; Python reads only the limits, the profile and the entries.
REGISTRY: dict = json.loads(
  importlib.resources.files("pystencil").joinpath("_data/opRegistry.json").read_text(encoding="utf-8")
)
PROFILE: str = REGISTRY["$meta"]["surfaceProfiles"]["pystencil"]
_LIMITS = REGISTRY["limits"]
MAX_ACTIONS = _LIMITS["MAX_ACTIONS"]              # per plan: top-level and per variant
MAX_VARIANTS = _LIMITS["MAX_VARIANTS"]
MAX_LAYOUT_LINES = _LIMITS["MAX_LAYOUT_LINES"]
MAX_STRING_LENGTH = _LIMITS["MAX_STRING_CHARS"]   # per string field unless its spec caps it
MAX_FRAME_INDICES = _LIMITS["MAX_FRAME_INDICES"]
MAX_SAVE_NAME = _LIMITS["MAX_SAVE_NAME"]          # §2.1: the `save` op's optional project name
MAX_PATH_CHARS = _LIMITS["MAX_PATH_CHARS"]        # §10: the longest local path a `save` op may carry
MIN_ASK_OPTIONS = _LIMITS["ask"]["minOptions"]
MAX_ASK_OPTIONS = _LIMITS["ask"]["maxOptions"]
MAX_ASK_QUESTION = _LIMITS["ask"]["question"]
MAX_ASK_LABEL = _LIMITS["ask"]["label"]
MAX_ASK_ANSWER = _LIMITS["ask"]["answer"]
DEFAULT_CUSTOM_LABEL = REGISTRY["ask"]["defaultCustomLabel"]
MAX_HISTORY = 32        # chat messages replayed per call
# §12: any other persisted-chat version reads as "no saved chat".
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

  Applied on save and restore alike: §7's continuation note is stripped (a turn that is
  only the note is dropped), and an assistant turn that is a raw op-plan is dropped —
  §7 allows that on the wire, §12.1 not in the shared transcript.
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
