from __future__ import annotations

"""The §10 console profile's executor side: the hook names the REPL implements, the
applier that calls them, and the helpers that guard and describe console state.
"""

from dataclasses import dataclass
from typing import Any, Sequence

from .._ffi.types import NoneType
from .plan.frame import _FrameMap
from .types import OpPlan

Messages = Sequence[dict]


_CONSOLE_HOOKS = {
  "connect": "plan_connect",
  "disconnect": "plan_disconnect",
  "delete": "plan_delete",
  "openUrl": "plan_open_url",
  "clear": "plan_clear",
  "clearChat": "plan_clear_chat",
}


def _apply_console_op(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
           run: ("_PlanRun" | NoneType) = None) -> None:
  """Dispatch a §10 console-profile op to the surface's console hooks.

  Without a console session (the library API — ``Editor.prompt`` / a bare
  ``execute_op_plan``) the op is skipped with a note: connections, local project
  files, the working-image slot and the conversation (``clearChat`` — a
  single-turn ``Editor.prompt`` has none to clear) belong to the interactive
  console, and ``openUrl`` additionally needs the console's user-echo guard, so
  nothing here may fetch. A hook returns an optional note string (a §1
  execution miss)."""
  op = action["op"]
  console = run.console if run is not None else None
  if console is None:
    if run is not None:
      run.notes.append(
        'Skipped "%s" — this op drives the interactive console, and no '
        "console session is attached" % op
      )
    return
  note = getattr(console, _CONSOLE_HOOKS[op])(action)
  if note and run is not None:
    run.notes.append(note)
  # A load/clear that actually replaced the working picture starts a fresh frame; a miss
  # (note returned) changed nothing, so the running transform still applies.
  if op in ("openUrl", "clear") and not note:
    if frame is not None:
      frame.reset()
    if run is not None:
      run.active_name = ""


# ── §10 console-profile executor helpers (the cli console's, ported) ─────────
def url_echoed_by_user(history: Messages, current_text: str, url: str) -> bool:
  """§10 openUrl guard: the model may only ECHO the user — true when ``url`` appears
  verbatim in the current turn's text or a replayed USER turn (assistant text and
  fetched/attached content never count). ``history`` is Chat-shaped message dicts."""
  if url in (current_text or ""):
    return True
  for m in history or []:
    if m.get("role") == "user" and url in (m.get("text") or ""):
      return True
  return False


def blocked_open_url(plan: OpPlan, history: Messages, current_text: str) -> (str | NoneType):
  """The first top-level ``openUrl`` whose URL the user never wrote (→ the whole
  plan is blocked, nothing executes), or None when every openUrl is an echo."""
  for a in plan.actions:
    if a.get("op") == "openUrl" and not url_echoed_by_user(
      history, current_text, a["url"]
    ):
      return a["url"]
  return None


def resolve_server(urls: Sequence[str], want: str):
  """§10's connect/disconnect stance over the console's own URL list: exact URL
  match, else a UNIQUE host (or host:port) match, case-insensitive. Returns the
  matched index, ``"none"``, or ``"ambiguous"`` — the model can never introduce a
  new address (an unmatched name is the user's to /connect)."""
  want = (want or "").strip()
  if not want:
    return "none"
  for i, u in enumerate(urls):
    if u == want:
      return i
  found: (int | NoneType) = None
  low = want.lower()
  for i, u in enumerate(urls):
    auth = u.split("://", 1)[-1].split("/", 1)[0]
    if auth.startswith("["):  # a bracketed IPv6 literal keeps its brackets
      host = auth[: auth.index("]") + 1] if "]" in auth else auth
    else:
      host = auth.rsplit(":", 1)[0]
    if auth.lower() == low or host.lower() == low:
      if found is not None:
        return "ambiguous"
      found = i
  return found if found is not None else "none"


@dataclass
class ConsoleServer:
  """One live connection as the console-context suffix sees it: the URL and
  (optionally) its project names — NEVER a token; this class has nowhere to put one."""

  url: str
  active: bool = False  # hosts the active fetched project
  projects: (list[str] | NoneType) = None  # None = not fetched/unreachable (line omitted)


# How many project names one server contributes to the context suffix.
MAX_CONTEXT_PROJECTS = 20


def console_context(servers: Sequence[ConsoleServer], active_project: str = "") -> str:
  """The console's dynamic system-prompt suffix (§4 allows one; the cli console's
  ``consoleContextAlloc`` ported verbatim): the connection list (URLs only), the
  active project, and each server's project names — so "what am I connected to?" /
  "which projects are on my server?" are answered from context, and the
  connect/disconnect ops resolve against addresses the user already owns."""
  out = [
    "Console state (the console's own connections and project, for answering "
    "questions about it):\n"
  ]
  if not servers:
    out.append("Connections: none — the user can add one with '/connect <url>'.")
  else:
    out.append("Connections (%d): " % len(servers))
    for i, s in enumerate(servers):
      if i:
        out.append(", ")
      out.append(s.url)
      if s.active:
        out.append(" (active project's server)")
    out.append(".")
  if active_project:
    out.append('\nActive server project: "%s".' % active_project)
  else:
    out.append("\nActive server project: none.")
  for s in servers:
    if s.projects is None:  # unknown (unreachable) ≠ empty
      continue
    out.append("\nProjects on %s: " % s.url)
    if not s.projects:
      out.append("(none)")
      continue
    shown = s.projects[:MAX_CONTEXT_PROJECTS]
    out.append(", ".join(shown))
    if len(s.projects) > len(shown):
      out.append(" (+%d more)" % (len(s.projects) - len(shown)))
    out.append(".")
  return "".join(out)
