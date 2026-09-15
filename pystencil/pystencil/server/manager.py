from __future__ import annotations

"""The set of connected servers for one session — a port of the browser's
ConnectionManager, REST-only.
"""

from typing import Any, Iterable, Union

from .._types import NoneType
from .connection import ServerConnection
from .diff import _poll_loop, diff_projects
from .urls import normalize_url, split_invite_token
from .._net import _fetch_all


# Specs accepted by ConnectionManager.connect: a url string, a {url, token}
# mapping, or an iterable of either.
ConnectSpec = Union[str, dict, Iterable[Any]]


class ConnectionManager:
  """The set of connected servers for one session (port of the browser's
  ConnectionManager, REST-only)."""

  def __init__(self, *, verify: bool = True) -> None:
    self._verify = verify
    self._conns: dict[str, ServerConnection] = dict()  # url -> connection
    self._last: list[tuple[str, str]] = list()  # for reconnect(): [(url, token)]

  @property
  def connections(self) -> list:
    """The connected server URLs (insertion order)."""
    return list(self._conns.keys())

  def get(self, url: str) -> (ServerConnection | NoneType):
    return self._conns.get(normalize_url(url))

  def has(self, url: str) -> bool:
    return normalize_url(url) in self._conns

  def connect(self, spec: ConnectSpec) -> "ConnectionManager":
    """Connect one or more servers; already-connected urls are no-ops."""
    for url, token in _iter_specs(spec):
      # Split any invite-link fragment before normalizing (it drops fragments).
      url, token = split_invite_token(url, token)
      norm = normalize_url(url)
      if norm in self._conns: continue
      conn = ServerConnection(norm, token, verify=self._verify)
      conn.connect()
      self._conns[norm] = conn
    # Remember the live set so reconnect() can rebuild it (with tokens).
    self._last = [(c.base, c.token) for c in self._conns.values()]
    return self

  def disconnect(self, url: (str | NoneType) = None) -> "ConnectionManager":
    """Disconnect a specific url, or the most recently added when omitted."""
    if url is None:
      urls = list(self._conns.keys())
      if not urls: return self
      target = urls[-1]
    else:
      target = normalize_url(url)
    conn = self._conns.pop(target, None)
    if conn: conn.close()
    return self

  def disconnect_all(self) -> "ConnectionManager":
    for conn in self._conns.values(): conn.close()
    self._conns.clear()
    return self

  def reconnect(self) -> "ConnectionManager":
    """Re-establish the last connected set (tokens re-validated/re-issued)."""
    previous = list(self._last)
    self.disconnect_all()
    for url, token in previous: self.connect({"url": url, "token": token})
    return self

  def remote_projects(self) -> list:
    """Aggregate every connection's projects, polled in PARALLEL (one round-trip per
    server, all at once); an unreachable or erroring one is skipped, as the browser does."""
    def listing(conn) -> list:
      try:
        return conn.list_projects()
      except Exception:
        return []
    return [p for got in _fetch_all(self._conns.values(), listing) for p in got]

  # ── aggregate project-change tracking (poll-based) ──
  # The session-wide analogue of ServerConnection.watch_projects: polls every connected
  # server and reports name/color/version changes across all of them, the way the
  # extension popup tracks its pinned projects as a set rather than one active project.
  def poll_project_changes(self, previous: (list | NoneType) = None) -> tuple:
    """One-shot poll across every connection. Returns ``(current_list, changes)``
    (see diff_projects). Pass the prior list back to detect what moved."""
    current = self.remote_projects()
    return current, diff_projects(previous, current)

  def watch_projects(self, on_change, *, interval: float = 2.0, stop=None) -> None:
    """Block, polling every connected server every `interval` s, calling
    on_change(change) per project create/update/delete across all of them. The first
    poll seeds the baseline silently. Pass a threading.Event as `stop` to end it."""
    _poll_loop(self.remote_projects, on_change, interval, stop)


def __one_spec(item: Any) -> tuple[Any, str]:
  """A single (url, token) from a url string or a {url, token?} mapping."""
  if isinstance(item, str): return item, ""
  if isinstance(item, dict): return item.get("url"), item.get("token") or ""
  raise TypeError(f"Unsupported connection spec: {item!r}")


def _iter_specs(spec: ConnectSpec):
  """Yield (url, token) pairs from a url string, {url,token} dict, or an
  iterable of either. Centralizes the browser's flexible connect() input."""
  if isinstance(spec, (str, dict)):
    yield __one_spec(spec)
    return
  for item in spec: yield __one_spec(item)
