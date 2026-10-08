"""The editor's LLM entry point (llm-contract.md): one prompt round, executed against
this editor through the same op-plan validator every surface uses.
"""

from __future__ import annotations

from .._ffi.types import NoneType


class _AssistantApi:
  def prompt(
    self,
    text: str,
    images: (list | NoneType) = None,
    llm=None,
    execute: bool = True,
  ) -> tuple[str, list]:
    """Ask the configured LLM to edit this image; returns ``(reply, outputs)``.

    One turn through :mod:`pystencil.llm`. ``images`` are ``(media_type, bytes[, name])`` tuples,
    also the turn's §2.1 attachments: an ``image`` op switches to the Nth (1-based), and an
    unnamed ``save`` writes ``<name>.stencil`` in the cwd after the active one's name. With
    ``execute`` the plan runs against THIS editor and ``outputs`` holds the result Images (one
    extra per variant), else it is empty, as for a chat-only turn. ``llm`` defaults to a client
    built from ``STENCIL_LLM_*``; multi-turn is :class:`pystencil.llm.Chat` + ``execute_op_plan``.
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
    outputs = execute_op_plan(plan, self, attachments=imgs) if execute else []
    return plan.reply, outputs
