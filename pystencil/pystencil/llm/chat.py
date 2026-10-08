"""Stateful chat (contract §7): a client-side conversation whose bounded history is
replayed in full on every call, since every provider is stateless.
"""

from __future__ import annotations

import json
import time
from typing import Any, Iterable

from .._ffi.types import NoneType
from .client import LlmClient
from .config import ACCEPTED_MEDIA_TYPES, MAX_ATTACHMENTS
from .errors import LlmError
from .plan.limits import CHAT_DOC_VERSION, MAX_HISTORY, chat_display_text
from .plan.parse import parse_op_plan
from .prompt import LLM_SYSTEM_PROMPT
from .run import wire_images
from .types import OpPlan

Messages = list[dict]


class Chat:
  """A client-side conversation: the newest ``MAX_HISTORY`` messages replayed on every call,
  with the current turn's images plus the one most recent prior image; older turns text-only.
  """

  def __init__(self, client: (LlmClient | NoneType) = None) -> None:
    self.client = client if client is not None else LlmClient()
    # {"role", "text", "images"}, newest last; send() blanks images that can never replay.
    self.history: Messages = list()

  @staticmethod
  def _check_images(images: (Iterable | NoneType), cap: (int | NoneType) = MAX_ATTACHMENTS) -> list:
    """Attachments as ``[(media_type, bytes), ...]``; over ``cap`` (§7) raises rather than
    trims. ``cap=None`` lifts the count for surface-internal transients (the edge map)."""
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
    if len(self.history) > MAX_HISTORY:
      del self.history[: len(self.history) - MAX_HISTORY]

  def _wire_messages(self) -> Messages:
    last = len(self.history) - 1
    prior: (int | NoneType) = None
    for i in range(last - 1, -1, -1):
      if self.history[i]["images"]:
        prior = i
        break
    out: Messages = list()
    for i, m in enumerate(self.history):
      if i == last:
        images = list(m["images"])
      elif i == prior:
        images = [m["images"][-1]]
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
    """Send one user turn; returns ``(reply, plan)``, nothing executed.

    The assistant turn stores the raw text, so the model sees its own JSON later.
    ``system`` overrides the prompt for this call only; ``transient_images`` ride this turn
    only, never stored (§7's edge map).
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

  @staticmethod
  def _display_text(raw: str) -> str:
    """An assistant turn as displayed: the plan's ``reply``, or the raw text if it is no plan."""
    try:
      return parse_op_plan(raw).reply
    except LlmError:
      return raw

  def to_doc(self, now_ms: (int | NoneType) = None) -> dict:
    """The §12.1 persisted-chat document: text only, displayed replies, empty turns dropped,
    the newest ``MAX_HISTORY``. ``now_ms`` pins ``savedAt`` (epoch ms; default now)."""
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

  # §12.3 spelling.
  to_dict = to_doc

  @classmethod
  def from_doc(cls, doc: Any, client: (LlmClient | NoneType) = None) -> "Chat":
    """A Chat from a §12.1 document (dict or JSON). Forgiving by contract: anything malformed
    degrades to dropped messages or an empty Chat, never an exception; no model call."""
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
    """Forget the conversation; the client is kept."""
    del self.history[:]
