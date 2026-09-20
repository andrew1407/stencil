from __future__ import annotations

"""Stateful chat (contract §7): a client-side conversation whose bounded history is
replayed in full on every call, since every provider is stateless.
"""

import json
import time
from typing import Any, Iterable

from .._types import NoneType
from .client import LlmClient
from .config import ACCEPTED_MEDIA_TYPES, MAX_ATTACHMENTS
from .errors import LlmError
from .limits import CHAT_DOC_VERSION, MAX_HISTORY, chat_display_text
from .plan import parse_op_plan
from .prompt import LLM_SYSTEM_PROMPT
from .run import wire_images
from .types import OpPlan

Messages = list[dict]


# ── stateful chat (contract §7) ───────────────────────────────────────────────
class Chat:
  """A client-side conversation: bounded history replayed in full on every call.

  All providers are stateless, so :meth:`send` replays the most recent
  ``MAX_HISTORY`` (32) messages. The image replay rule keeps payloads bounded:
  only the current turn's images plus the single most recent prior image are sent;
  older turns are replayed text-only. Attached images are ``(media_type, bytes)``
  tuples (see the module docstring for the no-downscale deviation).
  """

  def __init__(self, client: (LlmClient | NoneType) = None) -> None:
    self.client = client if client is not None else LlmClient()
    # Retained history: dicts {"role", "text", "images"} (newest last). Images the replay rule
    # can never send again are blanked by send().
    self.history: Messages = list()

  @staticmethod
  def _check_images(images: (Iterable | NoneType), cap: (int | NoneType) = MAX_ATTACHMENTS) -> list:
    """Validate/normalize attachments into [(media_type, bytes), ...].

    Bounded by ``MAX_ATTACHMENTS`` (contract §7): a turn's images are re-encoded,
    replayed and paid for on every call. Over the cap is an error rather than a
    silent trim — a caller that passed ten images should hear about it. ``cap=None``
    keeps the media-type validation but lifts the count bound — the §7 cap binds the
    USER attachment queue, not surface-internal transients (the edge map, the
    console's §2.1 /upload set, which has its own 8-image bound).
    """
    out: list = list()
    for media_type, data in wire_images(images):
      if media_type not in ACCEPTED_MEDIA_TYPES:
        raise ValueError(
          "unsupported media type %r — accepted: %s"
          % (media_type, ", ".join(ACCEPTED_MEDIA_TYPES))
        )
      out.append((media_type, data))
    if cap is not None and len(out) > cap:
      raise ValueError(
        "up to %d images per message (got %d)" % (cap, len(out))
      )
    return out

  def __trim(self) -> None:
    """Bound the retained history to the most recent MAX_HISTORY messages."""
    if len(self.history) > MAX_HISTORY:
      del self.history[: len(self.history) - MAX_HISTORY]

  def _wire_messages(self) -> Messages:
    """History with the image replay rule applied (current turn's images plus
    the single most recent prior image; everything older text-only)."""
    last = len(self.history) - 1
    prior: (int | NoneType) = None  # index of the newest earlier image-bearing message
    for i in range(last - 1, -1, -1):
      if self.history[i]["images"]:
        prior = i
        break
    out: Messages = list()
    for i, m in enumerate(self.history):
      if i == last:
        images = list(m["images"])
      elif i == prior:
        images = [m["images"][-1]]  # only its most recent image replays
      else:
        images = list()
      out.append({"role": m["role"], "text": m["text"], "images": images})
    return out

  def send(
    self,
    text: str,
    images: (Iterable | NoneType) = None,
    *,
    system: (str | NoneType) = None,
    transient_images: (Iterable | NoneType) = None,
  ) -> tuple[str, OpPlan]:
    """Send one user turn; returns ``(reply, plan)``.

    The user message (with its validated attachments) joins the history, the
    bounded history is replayed through the client, and the raw reply is parsed
    into an :class:`OpPlan` (the raw text — not the extracted reply — is what the
    assistant turn stores, so the model sees its own JSON on later turns).
    Execution is the caller's choice via :func:`execute_op_plan`.

    ``system`` overrides the canonical system prompt for THIS call only (a
    suffixed prompt is never remembered). ``transient_images`` ride the wire
    directly after ``images`` on this turn only — never stored, so never
    replayed (§7's edge map).

    Memory: once an image-bearing turn is on the wire, the replay rule can never
    send any EARLIER turn's images again (the new turn supersedes them as the
    most recent prior), so those attachments are blanked from the retained
    history rather than held for the life of the chat. The wire payloads are
    identical to keeping them.
    """
    self.history.append(
      {"role": "user", "text": str(text), "images": self._check_images(images)}
    )
    self.__trim()
    wire = self._wire_messages()
    transient = self._check_images(transient_images, cap=None)
    if transient:
      wire[-1]["images"] = wire[-1]["images"] + transient
    if self.history[-1]["images"]:
      for m in self.history[:-1]:
        m["images"] = list()
    raw = self.client.chat(wire, system if system is not None else LLM_SYSTEM_PROMPT)
    plan = parse_op_plan(raw)
    self.history.append({"role": "assistant", "text": raw, "images": []})
    self.__trim()
    return plan.reply, plan

  # ── §12 chat persistence (per-project, opt-in) ──
  @staticmethod
  def _display_text(raw: str) -> str:
    """The DISPLAYED form of an assistant turn: the plan's extracted ``reply``.

    History stores the raw model text (see :meth:`send`), but the persisted
    document is shared across surfaces and must read as a conversation, so the
    raw JSON is re-parsed here; text that fails to parse as a plan (it was
    displayed as-is when it arrived) falls back to itself.
    """
    try:
      return parse_op_plan(raw).reply
    except LlmError:
      return raw

  def to_doc(self, now_ms: (int | NoneType) = None) -> dict:
    """Serialize the conversation as the §12.1 persisted-chat document.

    Text-only by contract: images are NEVER persisted, assistant turns store
    their displayed reply (never the raw JSON plan), empty-text turns are
    dropped, and the result is trimmed to the most recent ``MAX_HISTORY`` (32)
    messages. §7's internal machinery is dropped too, via
    :func:`chat_display_text`. ``now_ms`` pins the informational ``savedAt``
    stamp (tests); the default is the current epoch ms.
    """
    messages: Messages = list()
    for m in self.history:
      role = m.get("role")
      if role not in ("user", "assistant"):
        continue
      text = m.get("text")
      if not isinstance(text, str):
        continue
      if role == "assistant":
        text = self._display_text(text)
      shown = chat_display_text(role, text)
      if shown is None:
        continue
      messages.append({"role": role, "text": shown})
    return {
      "version": CHAT_DOC_VERSION,
      "savedAt": int(time.time() * 1000) if now_ms is None else int(now_ms),
      "messages": messages[-MAX_HISTORY:],
    }

  # Contract §12.3 documents these under the to_dict/from_dict spelling.
  to_dict = to_doc

  @classmethod
  def from_doc(cls, doc: Any, client: (LlmClient | NoneType) = None) -> "Chat":
    """Rebuild a Chat from a §12.1 document (a dict or its JSON string).

    Restore is forgiving by contract — a malformed document, a ``version``
    other than 1, non-``user``/``assistant`` roles, and missing/non-string
    ``text`` all degrade to dropped messages or an empty Chat, never an
    exception. Stray ``images`` fields are ignored (persisted chats are
    text-only); anything past ``MAX_HISTORY`` messages is truncated to the
    most recent. Restoring never triggers a model call. The §12.1 gate
    (:func:`chat_display_text`) runs here too: another surface's — or an older
    build's — internal text is never restored into the conversation.
    """
    chat = cls(client)
    if isinstance(doc, str):
      try:
        doc = json.loads(doc)
      except ValueError:
        return chat
    if not isinstance(doc, dict) or doc.get("version") != CHAT_DOC_VERSION:
      return chat
    messages = doc.get("messages")
    if not isinstance(messages, list):
      return chat
    for m in messages:
      if not isinstance(m, dict):
        continue
      role = m.get("role")
      text = m.get("text")
      if role not in ("user", "assistant") or not isinstance(text, str):
        continue
      shown = chat_display_text(role, text)
      if shown is None:
        continue
      chat.history.append({"role": role, "text": shown, "images": []})
    chat.__trim()
    return chat

  from_dict = from_doc

  def clear(self) -> None:
    """Forget the whole conversation (the client/config is kept)."""
    del self.history[:]
