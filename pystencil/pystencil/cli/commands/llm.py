from __future__ import annotations

"""The LLM assistant commands (llm-contract.md): /prompt, /llm, and the transport,
image and context plumbing one prompt round needs.
"""

import json

from ..._types import NoneType
from ..._severity import emit_error
from ...editor import Editor
from ...llm import (
  CONSOLE_SYSTEM_PROMPT,
  CONTINUATION_NOTE,
  EDGE_MAP_SUFFIX,
  PROVIDERS,
  AskCard,
  Chat,
  ConsoleServer,
  LlmClient,
  LlmConfig,
  LlmError,
  ask_answer_text,
  blocked_open_url,
  console_context,
  execute_op_plan,
  format_ask,
  parse_op_plan,
  variant_slugs,
)
from ..console import mask as _mask
from ..registry import command


class _LlmCommands:
  """The provider transport, the images a round attaches, and /llm itself."""
  # ── LLM assistant (llm-contract.md) ──
  def _llm_server_conn(self):
    """The live /connect-ed ServerConnection the stencil-server provider reuses.

    A configured server_url must match a connected server; an empty server_url
    falls back to the first connected server. None when not applicable — the
    client then authenticates with whatever token the config carries (none).
    """
    if self._llm.provider != "stencil-server":
      return None
    target = self._llm.server_url
    try:
      if target:
        return self._manager.get(target)
      urls = self._manager.connections
      return self._manager.get(urls[0]) if urls else None
    except ValueError:
      return None

  def _llm_client(self) -> LlmClient:
    """Build the provider client for the session's current /llm config."""
    conn = self._llm_server_conn()
    if conn is not None:
      return LlmClient(self._llm, server=conn)
    return LlmClient(self._llm)

  def _current_png(self) -> bytes:
    """Encode the working image to PNG, reusing the previous encode while the
    editor's public ``revision`` (bumped on every mutation — its documented role
    as a render-cache key) is unchanged. The editor itself is part of the key so
    /drop's fresh Editor can't collide with the old one's revision numbers."""
    ed = self._editor
    cache = self._png_cache
    if cache is not None and cache[0] is ed and cache[1] == ed.revision:
      return cache[2]
    data = ed.result().encode("png")
    self._png_cache = (ed, ed.revision, data)
    return data

  def _edge_map_png(self) -> bytes:
    """§7 edge map: the current render run through the core contour op, encoded
    with the same PNG writer as the working snapshot."""
    from ...core import get_core

    img = self._editor.result()
    get_core().apply_contour(img.data, img.width, img.height)
    return img.encode("png")


  def _console_context(self) -> str:
    """The §4 dynamic console-context suffix (the cli console's rule): connection
    URLs only — tokens have no field to ride in — the active project, and each
    server's project names fetched best-effort (an unreachable server just omits
    its listing)."""
    active_url = getattr(self._remote[0], "base", "") if self._remote is not None else ""
    servers: list[ConsoleServer] = []
    for url in self._manager.connections:
      conn = self._manager.get(url)
      names: (list[str] | NoneType) = None
      if conn is not None:
        try:
          names = [
            p.get("name", "?") if isinstance(p, dict) else str(p)
            for p in conn.list_projects()
          ]
        except Exception:  # noqa: BLE001 - unreachable ≠ empty: line omitted
          names = None
      servers.append(ConsoleServer(url=url, active=url == active_url, projects=names))
    active = self._editor.name if self._remote is not None else ""
    return console_context(servers, active)


  def _show_llm(self) -> None:
    """Bare /llm: print the session's provider config (credentials masked)."""
    cfg = self._llm
    self._say("llm provider %s" % cfg.provider)
    self._say("  url    %s" % (cfg.base_url or "(none)"))
    self._say("  model  %s" % (cfg.model or "(default)"))
    self._say("  key    %s" % _mask(cfg.api_key))
    conn = self._llm_server_conn()
    if conn is not None:
      self._say("  server %s (token from live connection)" % conn.base)
    else:
      self._say("  server %s" % (cfg.server_url or "(none)"))

  @command("llm", usage="/llm [key value]",
      help="show the LLM config / set provider | url | model | key | server")
  def _cmd_llm(self, arg: str) -> None:
    """/llm: bare shows the config; ``<key> <value>`` sets an in-session override."""
    parts = arg.split(None, 1)
    if not parts:
      self._show_llm()
      return
    key = parts[0].lower()
    val = parts[1].strip() if len(parts) > 1 else ""
    cfg = self._llm
    if key == "provider":
      if val not in PROVIDERS:
        self._err(
          "unknown provider '%s' — use %s" % (val, " | ".join(PROVIDERS))
        )
        return
      # set_provider re-fills the provider's default base URL unless the user
      # pinned one with /llm url (set_base_url); everything else carries over.
      cfg.set_provider(val)
      self._say("llm provider %s (url %s)" % (val, cfg.base_url or "-"))
    elif key == "url":
      if not val:
        self._err("/llm url needs a base URL")
        return
      cfg.set_base_url(val)
      self._say("llm url %s" % val)
    elif key == "model":
      cfg.model = val
      self._say("llm model %s" % (val or "(default)"))
    elif key == "key":
      cfg.api_key = val
      self._say("llm key %s" % ("set" if val else "cleared"))
    elif key == "server":
      cfg.server_url = val
      self._say("llm server %s" % (val or "cleared"))
    else:
      self._err(
        "/llm takes provider | url | model | key | server "
        "(bare /llm shows the config)"
      )
