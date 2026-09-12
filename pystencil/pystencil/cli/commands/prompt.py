from __future__ import annotations

"""One /prompt round: build the request, validate the plan, execute it, report."""

import json

from ...llm import (
  CONSOLE_SYSTEM_PROMPT,
  CONTINUATION_NOTE,
  EDGE_MAP_SUFFIX,
  AskCard,
  Chat,
  LlmError,
  ask_answer_text,
  blocked_open_url,
  execute_op_plan,
  format_ask,
  parse_op_plan,
  variant_slugs,
)
from ..registry import command

def _load_only_plan(plan) -> bool:
  acts = getattr(plan, "actions", None) or []
  if not acts or getattr(plan, "variants", None):
    return False
  op_of = lambda a: a.get("op") if isinstance(a, dict) else getattr(a, "op", None)
  ops = [op_of(a) for a in acts]
  return ("blank" in ops or "openUrl" in ops) and "layout" not in ops


# Attachment cap for /prompt: the current image rides along for vision only when its
# encoded PNG stays under ~8 MiB (bigger payloads are skipped with a printed note).
_PROMPT_IMAGE_LIMIT = 8 * 1024 * 1024
Attachments = list[tuple[str, bytes]]


# Image extensions Python can actually encode (codecs is PNG/BMP only). A bare or
# unknown output extension falls back to PNG, matching the codecs default.


class _PromptCommands:
  """/prompt — the model round and everything it applies to the session."""
  @command(
    "prompt", "p",
    usage="/prompt <text>",
    help="ask the LLM to edit the image (alias: p); plan actions apply to\n"
      "the session, each variant lands as variant-<label>.png",
  )
  def _cmd_prompt(self, arg: str) -> None:
    """/prompt <text>: one LLM turn — print the reply, run the plan's actions on
    the session's editor, write each variant as ``variant-<label>.png``."""
    if not arg:
      self._err("/prompt needs text to send")
      return
    # Answering the last `ask` card by number (contract §11.4). "2" or "1,3" becomes the
    # option LABELS, so what reaches the model is the same text every other surface sends;
    # anything else is an ordinary prompt and simply retires the card, since an
    # unanswered question must never block the conversation.
    answer = ask_answer_text(self._ask, arg)
    if self._ask is not None:
      self._ask = None
    if answer is not None:
      self._say("→ %s" % answer)
      arg = answer
    # §7 auto-continuation: a plan that made a picture and drew no layout cannot
    # have finished the looking-work — the attachment rode along before it existed. The
    # turn is re-sent ONCE with the new image, restating the request (with /chat off
    # there is no history to carry it). Whatever happens, this turn owns the /upload
    # set: the next /upload starts a new one (§2.1).
    try:
      text = arg
      for round_no in range(2):
        if self._prompt_round(text) is not True:
          return
        # The note is CONTINUATION_NOTE so this writer and the §12.1 gate that
        # refuses it in the persisted document can never drift apart.
        text = "%s\n\n%s" % (arg, CONTINUATION_NOTE)
    finally:
      self._consume_attachments()
      # §10 clearChat runs LAST: after the plan's other actions and any
      # §7 continuation round settled.
      self._confirm_clear_chat()


  def _prompt_round(self, arg: str):
    """One model round. True = the plan only LOADED an image, so the caller should
    re-send once with it attached; anything else = the turn is finished."""
    images: Attachments = []
    transient: Attachments = []
    # The system prompt is §4 + the console settings-op block; its dynamic suffix
    # carries the console context always, plus the §7 edge-map sentence when the
    # edge map actually rides (the cli console's suffix order).
    suffix_parts: list[str] = [self._console_context()]
    if self._editor.has_image():
      # Attach the current image for vision, unless it encodes too large.
      data = self._current_png()
      if len(data) > _PROMPT_IMAGE_LIMIT:
        self._note(
          "current image is %.1f MiB encoded — sending text only "
          "(limit %d MiB)"
          % (len(data) / (1024.0 * 1024.0), _PROMPT_IMAGE_LIMIT // (1024 * 1024))
        )
      else:
        images.append(("image/png", data))
        # §7 edge map: rides directly after the snapshot, current turn
        # only. Over the limit alone ⇒ just it is dropped, silently.
        edge = self._edge_map_png()
        if len(edge) <= _PROMPT_IMAGE_LIMIT:
          transient.append(("image/png", edge))
          suffix_parts.append(EDGE_MAP_SUFFIX)
    # §2.1: when this turn /upload-ed SEVERAL images they all ride along after the
    # working snapshot (and its edge map), in upload order — that order is what an
    # `image` op indexes (the snapshot itself does not count). They ride transient
    # in chat mode, like the cli console's text-only history: never replayed.
    if len(self._attachments) > 1:
      transient.extend((mt, data) for mt, data, _label in self._attachments)
    system = "%s\n\n%s" % (CONSOLE_SYSTEM_PROMPT, "\n\n".join(suffix_parts))
    try:
      client = self._llm_client()
      if self._chat_on:
        # /chat on: turns accumulate in a session Chat, whose bounded history
        # is replayed on every call. The client is refreshed each turn so
        # in-session /llm changes keep applying.
        if self._chat is None:
          self._chat = Chat(client)
        else:
          self._chat.client = client
        _reply, plan = self._chat.send(
          arg, images=images, system=system, transient_images=transient
        )
      else:
        raw = client.chat(
          [{"role": "user", "text": arg, "images": images + transient}],
          system,
        )
        plan = parse_op_plan(raw)
      # §10 openUrl guard: the model may only ECHO the user — a URL absent from
      # the user's own messages this conversation fails the whole plan, nothing
      # executes (history counts user turns only; with /chat off there are none).
      history = self._chat.history if self._chat_on and self._chat is not None else []
      blocked = blocked_open_url(plan, history, arg)
      if blocked is not None:
        self._err(
          'openUrl blocked: "%s" is not a URL you gave in this '
          "conversation" % blocked
        )
        return False
      outputs = execute_op_plan(
        plan, self._editor, attachments=self._attachments, console=self
      )
    except (LlmError, OSError) as e:
      # Covers plan/validation errors and the stencil-server stopReason
      # truncation/refusal LlmErrors, plus network failures (URLError).
      self._err("%s" % e)
      return False
    self._say(plan.reply)
    variant_outputs = outputs
    # §2.1: each `save` action wrote a project beside the output — report it like
    # /save does, so a multi-image turn says what it kept.
    for path in plan.saved:
      self._say("saved project %s" % path)
    if plan.actions:
      if self._editor.has_image():
        self._say_status_brief("applied %d action(s)" % len(plan.actions))
      else:
        self._say("applied %d action(s)" % len(plan.actions))
      variant_outputs = outputs[1:] if outputs else []
    # Slugs are deduped (a "-2"/"-3"… suffix on a collision) so same-slug
    # variant labels can't overwrite each other's files, like the Zig CLI/mcp.
    for slug, img in zip(variant_slugs(plan.variants), variant_outputs):
      path = "variant-%s.png" % slug
      img.save(path, "png")
      self._report_wrote(path, img.width, img.height)
    # §11: the plan may also ASK. Printed after the edits and remembered, so the next
    # /prompt can answer it by number.
    if plan.ask is not None:
      self._ask = plan.ask
      self._say(format_ask(plan.ask))
      return False
    return _load_only_plan(plan) and self._editor.has_image()
