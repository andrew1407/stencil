"""The editor's LLM entry point (llm-contract.md): one prompt round, executed against
this editor through the same op-plan validator every surface uses.
"""

from __future__ import annotations

from .._ffi.types import NoneType


class _AssistantApi:
  """The editor-side LLM prompt round."""
  # ── LLM assistant (llm-contract.md) ────────────────────────────────────
  def prompt(
    self,
    text: str,
    images: (list | NoneType) = None,
    llm=None,
    execute: bool = True,
  ) -> tuple[str, list]:
    """Ask the configured LLM to edit this image; returns ``(reply, outputs)``.

    A thin single-turn delegate over :mod:`pystencil.llm`: the prompt (plus any
    ``images`` as ``(media_type, bytes)`` tuples) is sent to the provider, the
    reply is parsed into an op-plan, and — when ``execute`` — the plan runs against
    THIS editor via :func:`pystencil.llm.execute_op_plan` (top-level actions mutate
    the editor; each variant yields one extra :class:`Image`). ``llm`` is an
    optional :class:`pystencil.llm.LlmClient`; when None one is built from the
    ``STENCIL_LLM_*`` env keys. ``outputs`` is the list of result Images (empty for
    chat-only turns or ``execute=False``). For a multi-turn conversation, use
    :class:`pystencil.llm.Chat` and call ``execute_op_plan`` yourself.

    ``images`` are also the turn's ATTACHMENTS for contract §2.1: an ``image`` op
    switches the working image to the Nth of them (1-based) and a ``save`` op
    writes ``<name>.stencil`` in the cwd, so one prompt can edit and keep several
    pictures. Add a third tuple element — ``(media_type, bytes, "cat.jpg")`` — to
    name the file an unnamed ``save`` derives its project name from.
    """
    # Imported lazily so constructing/using an Editor never pulls the LLM module.
    from ..llm import (
      MAX_ATTACHMENTS,
      LlmClient,
      execute_op_plan,
      parse_op_plan,
      wire_images,
    )

    client = llm if llm is not None else LlmClient()
    # This path talks to the client DIRECTLY (no Chat), so the §7 attachment cap has
    # to be enforced here too — otherwise the single-turn API is a way around it.
    imgs = list(images or [])
    if len(imgs) > MAX_ATTACHMENTS:
      raise ValueError(
        "up to %d images per message (got %d)" % (MAX_ATTACHMENTS, len(imgs))
      )
    raw = client.chat(
      [{"role": "user", "text": str(text), "images": wire_images(imgs)}]
    )
    plan = parse_op_plan(raw)
    # The turn's attachments are what a §2.1 `image` op indexes (1-based), and an
    # unnamed `save` names its .stencil after the active one.
    outputs = execute_op_plan(plan, self, attachments=imgs) if execute else []
    return plan.reply, outputs
