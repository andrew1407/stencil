from __future__ import annotations

"""The collaboration-server commands: /connect, /disconnect, /connections,
/projects, /delete and /fetch.
"""

import urllib.parse

from ... import _net
from ...editor import Editor
from ...server import (
  ServerError,
  credential_filter_matches,
  normalize_url,
  parse_credential_filter,
)
from ...sitesource import _sub_strict
from ..console import mask as _mask
from ..registry import command


class _ServerCommands:
  """Connections, their projects, and pulling one into the session."""

  @command("delete", "del", "remove", "rm", usage="/delete <x.stencil>",
      help="delete a local .stencil project file (aliases: del, rm)")
  def _cmd_delete(self, arg: str) -> None:
    """/delete <x.stencil> (aliases del/remove/rm) — delete a local .stencil project
    file, with the cli console's full guard set and wording (deleteReject)."""
    reject = Editor.delete_reject(arg)
    if reject == "empty":
      self._err("delete needs a .stencil path — e.g. '/delete project.stencil'")
      return
    if reject == "url":
      self._err("delete only removes local files, not URLs")
      return
    if reject == "not_stencil":
      self._err("delete only removes .stencil project files (got '%s')" % arg)
      return
    if reject == "traversal":
      self._err(
        "refusing to delete a path that escapes the working directory: '%s'" % arg
      )
      return
    try:
      Editor.delete_project(arg)
    except OSError as e:
      self._err("could not delete %s (%s)" % (arg, e))
      return
    self._say("deleted %s" % arg)

  @command(
    "connect",
    usage="/connect <url[ url2]> [token=<tok>]",
    help="connect to collaboration server(s); token= is required by a\n"
      "server that has ADMIN_TOKEN set (it won't issue one)",
  )
  def _cmd_connect(self, arg: str) -> None:
    # A trailing `token=<tok>` supplies the bearer. A server with ADMIN_TOKEN set refuses to
    # mint one, so an existing token is the only way onto it — and the LLM proxy needs it.
    urls, token = [], ""
    for word in arg.split():
      if word.startswith("token="):
        token = word[len("token="):]
      else:
        urls.append(word)
    if not urls:
      self._err("/connect needs one or more server URLs")
      return
    for url in urls:
      try:
        self._manager.connect({"url": url, "token": token} if token else url)
        self._say("connected %s" % normalize_url(url))
      except (ServerError, OSError, ValueError) as e:
        self._err("could not connect to %s (%s)" % (url, e))

  @command("disconnect", usage="/disconnect [url]",
      help="close one connection (or the most recent when omitted)")
  def _cmd_disconnect(self, arg: str) -> None:
    """/disconnect [url] — close one connection (or the most recent when omitted),
    mirroring the Zig console's doDisconnect wording."""
    urls = self._manager.connections
    if not urls:
      self._err("no server connections — use '/connect <url>'")
      return
    target = normalize_url(arg) if arg else urls[-1]
    if not self._manager.has(target):
      self._err("not connected to %s" % target)
      return
    self._detach_remote_on(target)
    self._manager.disconnect(target)
    self._say("disconnected from %s" % target)

  def _detach_remote_on(self, url: str) -> None:
    """Forget the active remote project when its server is being dropped."""
    if self._remote is not None and getattr(self._remote[0], "base", "") == url: self._remote = None

  @command(
    "connections", "servers",
    usage="/connections [admin|session]",
    help="list the connected servers (alias: servers); admin-token\n"
      "credentials are tagged [admin], and the word filters the list",
  )
  def _cmd_connections(self, arg: str = "") -> None:
    """`/connections [admin|session]` — list the live servers, tagging the ones
    whose credential is a proven admin token. The argument filters the listing;
    an unknown word prints a usage note. Token values are never printed."""
    flt = parse_credential_filter(arg)
    if flt is None:
      self._err("usage: /connections [admin|session]")
      return
    conns = self._manager.connections
    if not conns:
      self._say("no server connections — use '/connect <url>'")
      return
    shown = 0
    for url in conns:
      conn = self._manager.get(url)
      kind = getattr(conn, "credential_kind", "")
      if not credential_filter_matches(flt, kind): continue
      shown += 1
      self._say(url + ("  [admin]" if kind == "admin" else ""))
    if shown == 0: self._say("no %s connections (of %d)" % (flt, len(conns)))

  @command("projects", "ls", usage="/projects [url]",
      help="list a server's projects (alias: ls)")
  def _cmd_projects(self, arg: str) -> None:
    url = arg.strip()
    if url:
      conn = self._manager.get(url)
      if conn is None:
        self._err("not connected to %s" % url)
        return
      projects = conn.list_projects()
    else:
      projects = self._manager.remote_projects()
    if not projects:
      self._say("no projects")
      return
    for proj in projects:
      name = proj.get("name", "?") if isinstance(proj, dict) else str(proj)
      pid = proj.get("id", "?") if isinstance(proj, dict) else ""
      self._say("%-24s %s" % (name, pid))

  @command("fetch", "pull", usage="/fetch <name> [url]",
      help="load a server project's image (alias: pull)")
  def _cmd_fetch(self, arg: str) -> None:
    parts = arg.split()
    if not parts:
      self._err("/fetch needs a project name")
      return
    name = parts[0]
    url = parts[1] if len(parts) > 1 else None
    conns = [self._manager.get(url)] if url else None
    if conns is None:
      conns = [self._manager.get(u) for u in self._manager.connections]
    for conn in conns:
      if conn is None: continue
      for proj in conn.list_projects():
        if isinstance(proj, dict) and proj.get("name") == name:
          data = conn.get_file(proj["id"], "original")
          self._editor.load(bytes(data), name=name)
          self._image_replaced()
          # Track the active remote project for /chat clear's server-side
          # delete; restore its saved chat only while /chat is on (§12).
          self._remote = (conn, proj["id"])
          if self._chat_on: self._restore_remote_chat(conn, proj["id"])
          w, h = self._editor.image_size
          self._say('fetched "%s" (%dx%d)' % (name, w, h))
          return
    self._err('no server project named "%s"' % name)
