from __future__ import annotations

"""The canonical system prompt (contract §4 + §13) and the console profile spliced
into it.

The PROSE CORE is the checked-in canonical asset, byte-pinned by
tests/test_canonical_drift.py; the "Available ops" bullets are GENERATED from
OP_REGISTRY, so the prompt can never promise an op this surface cannot run.
"""

import importlib.resources
import json
import re

from .registry import OP_REGISTRY, OpSpec

# ── canonical system prompt (contract §4 + §13) ──
# The prose core is the checked-in canonical asset; "Available ops" is generated from OP_REGISTRY.
_PROMPT_ASSET = json.loads(
  importlib.resources.files("pystencil")
  .joinpath("_data/systemPrompt.json")
  .read_text(encoding="utf-8")
)

_PROMPT_CORE_HEAD = _PROMPT_ASSET["head"]

# This text console diverges from the canonical tail's "ask" paragraph (no previews, no
# image options); the shared prose from "Outlining" on is the asset's, verbatim.
_CONSOLE_ASK = """When a choice is genuinely the user's to make — which tint, which of several images —
add an "ask" object instead of guessing:
{"ask":{"question":"Which tint?","mode":"single"|"multi","allowCustom":true,
  "options":[{"label":"Sepia"},{"label":"B&W"}]}}
2 to 5 options; the pick comes back as the user's next message. This console shows the
options as a numbered list and cannot display pictures, so make each label stand alone.
Never write your own "Something else" / "Other" option: set "allowCustom": true and the client appends that free-text row itself."""

_TAIL_SHARED_ANCHOR = "\n\nOutlining ("
_shared_at = _PROMPT_ASSET["tail"].find(_TAIL_SHARED_ANCHOR)
if _shared_at < 0:  # pragma: no cover - guards asset rewording
  raise AssertionError("systemPrompt.json tail no longer contains the Outlining anchor")
_PROMPT_CORE_TAIL = _CONSOLE_ASK + _PROMPT_ASSET["tail"][_shared_at:]


# ── the console settings-op profile (the §10 cli-console analog) ──
# The cli console's profile minus accent/reconnect and copy — capabilities not wired here.
_CONSOLE_BLOCK_TAIL = (
  'These console ops are not image edits and cannot appear inside "variants".'
)

# The op list's end = the `ask` paragraph's start (the same splice anchor the cli's
# console_system_prompt and the browser's EDITOR_SYSTEM_PROMPT verify).
CONSOLE_SPLICE_ANCHOR = "\n\nWhen a choice is genuinely"


# §7 edge map: appended (verbatim) to the system-prompt suffix when — and only
# when — an edge-map image is actually attached after the working snapshot.
EDGE_MAP_SUFFIX = (
  "The second attached image is an edge-map render of the working image at the "
  "same pixel coordinates: use it to place outline points on real edges."
)


# The capabilities actually wired here. Nothing optional is, so a capability-carrying entry
# would be EXCLUDED from generation, falling to §1's unknown-op skip.
_SURFACE_CAPABILITIES: frozenset[str] = frozenset()

# §13 prompt censor: the generator refuses any bullet matching api keys, bearer tokens or
# endpoint-setting instructions — a registry mistake fails loudly at import.
_PROMPT_CENSOR_PATTERNS = tuple(
  re.compile(p, re.IGNORECASE)
  for p in (
    r"api[\s_-]?key",
    r"\bbearer\b",
    r"\bauthorization\b",
    r"(?:access|auth|session|secret)[\s_-]?token",
    r"\bendpoint\b",
    r"base[\s_-]?url",
  )
)


def _assemble_ops_bullets(
  registry: dict[str, OpSpec],
  scope: str,
  capabilities: frozenset[str] = _SURFACE_CAPABILITIES,
) -> str:
  """Concatenate a prompt block's op bullets from the registry (contract §13).

  Registry order is emission order; a bullet shared by two ops (undo/redo,
  connect/disconnect) sits on the first entry and the partner's is empty, so it is
  emitted once. An entry whose ``capability`` is not in ``capabilities`` is excluded —
  the op is then never promised to the model and falls to §1's unknown-op skip. A
  bullet matching a censor pattern raises."""
  bullets: list[str] = list()
  for name, spec in registry.items():
    if spec.scope != scope or not spec.bullet: continue
    if spec.capability and spec.capability not in capabilities: continue
    for pattern in _PROMPT_CENSOR_PATTERNS:
      if pattern.search(spec.bullet):
        raise AssertionError(
          'the "%s" op\'s prompt bullet matches the sensitive pattern %r '
          "and may not be emitted (contract §13)" % (name, pattern.pattern)
        )
    if spec.bullet not in bullets: bullets.append(spec.bullet)
  return "\n".join(bullets)


# The §4 embed: the verbatim prose core around the GENERATED core ops section.
LLM_SYSTEM_PROMPT = (
  _PROMPT_CORE_HEAD
  + _assemble_ops_bullets(OP_REGISTRY, "core")
  + "\n\n"
  + _PROMPT_CORE_TAIL
)

# The §10 console-profile block: the GENERATED console bullets + the variant-ban
# closing sentence.
CONSOLE_SETTINGS_PROMPT = (
  _assemble_ops_bullets(OP_REGISTRY, "console") + "\n" + _CONSOLE_BLOCK_TAIL
)

if CONSOLE_SPLICE_ANCHOR not in LLM_SYSTEM_PROMPT:  # pragma: no cover - guards rewording
  raise AssertionError("LLM_SYSTEM_PROMPT no longer contains the console-settings splice anchor")

# §4 + the console block at the end of its op list — what the pystencil console's
# /prompt sends as its system prompt; LLM_SYSTEM_PROMPT itself stays the §4 embed.
CONSOLE_SYSTEM_PROMPT = LLM_SYSTEM_PROMPT.replace(
  CONSOLE_SPLICE_ANCHOR, "\n" + CONSOLE_SETTINGS_PROMPT + CONSOLE_SPLICE_ANCHOR, 1
)
