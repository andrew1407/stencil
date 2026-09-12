from __future__ import annotations

"""The §10 console-profile op hooks: the interface ``execute_op_plan`` duck-types
into, declared as a Protocol so the contract is checkable, and the REPL's
implementation of it.
"""

from typing import Protocol, runtime_checkable

from .._types import NoneType
from .. import codecs
from ..llm import resolve_server, url_echoed_by_user


@runtime_checkable
class PlanConsole(Protocol):
  """What ``execute_op_plan(console=...)`` calls into for the §10 console ops.

  The executor duck-types this; declaring it makes the contract checkable — a return
  of ``None`` is success, a string is an execution-miss note (§1), and a hook is free
  to only RECORD a request it defers to the end of the turn.
  """

  def plan_connect(self, action: dict) -> (str | NoneType): ...

  def plan_disconnect(self, action: dict) -> (str | NoneType): ...

  def plan_delete(self, action: dict) -> (str | NoneType): ...

  def plan_open_url(self, action: dict) -> (str | NoneType): ...

  def plan_clear(self, action: dict) -> (str | NoneType): ...

  def plan_clear_chat(self, action: dict) -> (str | NoneType): ...


class _PlanHooks:
  """The REPL side of :class:`PlanConsole` — each hook runs the SAME path the
  matching slash command uses."""
  # ── §10 console-profile op hooks (execute_op_plan calls these via console=self;
  # a returned string is an execution-miss note per §1, None is success) ──
  def plan_connect(self, action: dict) -> (str | NoneType):
    """A plan `connect`: resolve ONLY among the session's live connections (§10's
    stance — the model can never introduce a host); anything else is a note."""
    want = action["server"]
    match = resolve_server(self._manager.connections, want)
    if match == "ambiguous":
      return (
        'skipped connect — "%s" matches several of this session\'s servers; '
        "use the full URL" % want
      )
    if match == "none":
      return (
        'skipped connect — "%s" is not a server you connected this session; '
        "run '/connect <url>' yourself" % want
      )
    self._say("already connected to %s" % self._manager.connections[match])
    return None

  def plan_disconnect(self, action: dict) -> (str | NoneType):
    """A plan `disconnect`: resolved against the LIVE connections (exact URL, else
    unique host), then through the same path the /disconnect command takes."""
    want = action["server"]
    urls = self._manager.connections
    match = resolve_server(urls, want)
    if match == "ambiguous":
      return (
        'skipped disconnect — "%s" matches several connected servers; '
        "use the full URL" % want
      )
    if match == "none":
      return 'skipped disconnect — not connected to "%s" (\'/connections\' lists them)' % want
    url = urls[match]
    self._detach_remote_on(url)
    self._manager.disconnect(url)
    self._say("disconnected from %s" % url)
    return None

  def plan_delete(self, action: dict) -> (str | NoneType):
    """A plan `delete`: the SAME guards + messages as /delete (cli parity — a
    guard rejection prints its error and the plan carries on)."""
    self._cmd_delete(action["path"])
    return None

  def plan_open_url(self, action: dict) -> (str | NoneType):
    """§10 openUrl (user-echo pre-checked in _prompt_round): the same load
    /upload <url> performs, synchronous — later actions see the fetched picture.
    `incognito` is not a console concept and is ignored with a note."""
    if action.get("incognito"):
      self._note("incognito is not a console concept — loading normally")
    url = action["url"]
    try:
      self._editor.load(url)
    except (OSError, ValueError, RuntimeError, codecs.CodecError) as e:
      return 'skipped openUrl — could not load "%s" (%s)' % (url, e)
    # A fresh picture detaches any fetched server project; the running
    # conversation survives — the load happened INSIDE it, at the user's ask.
    self._remote = None
    w, h = self._editor.image_size
    self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))
    return None

  def plan_clear(self, action: dict) -> (str | NoneType):
    """§10 `clear` → the /drop path's state, in place: drop the working image and
    its lines, leaving the session empty. The conversation survives (the §10 stance
    for model-driven clears: image and edits only — clearing the CHAT is the
    confirmed clearChat op)."""
    self._editor.clear()
    self._remote = None
    self._say("dropped the working image")
    return None

  def plan_clear_chat(self, action: dict) -> (str | NoneType):
    """§10 `clearChat`: only RECORD the request — the confirm and the clear are
    deferred to the end of the turn (_confirm_clear_chat), never run mid-plan."""
    self._clear_chat_pending = True
    return None

  def _confirm_clear_chat(self) -> None:
    """The deferred §10 clearChat: ask y/N on the console's own input stream; a
    typed yes runs the exact /chat clear path (Chat.clear + the §12 server-side
    `chat` delete); anything else — EOF / non-interactive input included — is a
    "clear canceled" note, never a failed plan."""
    if not self._clear_chat_pending:
      return
    self._clear_chat_pending = False
    self._say("clear this conversation's history? [y/N]")
    line = self._in.readline() if self._in is not None else ""
    if line.strip().lower() in ("y", "yes"):
      self._cmd_chat("clear")
    else:
      self._say("clear canceled")
