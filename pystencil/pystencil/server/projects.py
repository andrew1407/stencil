"""The project-metadata half of a connection's REST surface (protocol.go routes).

Poll-based change tracking stands in for the /ws feed this REST-only client does not open.
"""

from __future__ import annotations

import urllib.parse
from typing import Any

from .._ffi.types import NoneType
from .diff import _FIELD_WRITE_RETRIES, _poll_loop, diff_projects
from .http import ServerError


# The CLI's max_pages: a server that keeps handing out cursors is cut off, not followed forever.
_MAX_LIST_PAGES = 1000


class _ProjectApi:
  def list_projects(self) -> list:
    """GET /projects, following each page's nextCursor → every project record, in page order."""
    out: list = list()
    seen: set = set()
    page: dict = dict()
    for _ in range(_MAX_LIST_PAGES):
      r = self._request("GET", "/projects", **page)
      out.extend((r or {}).get("projects", []) or [])
      cursor = (r or {}).get("nextCursor")
      if not isinstance(cursor, str) or not cursor: return out
      if cursor in seen:
        raise ServerError("badResponse", "the server handed back the same page cursor twice")
      seen.add(cursor)
      page = {"query": {"after": cursor}}
    raise ServerError("badResponse", "the server kept paging past %d pages" % _MAX_LIST_PAGES)

  def poll_project_changes(self, previous: (list | NoneType) = None) -> tuple:
    """``(current_list, changes)`` against ``previous``; ``previous=None`` reports every
    project as 'created'."""
    current = self.list_projects()
    return current, diff_projects(previous, current)

  def watch_projects(self, on_change, *, interval: float = 2.0, stop=None) -> None:
    """Block, polling every `interval` s, calling on_change(change) per project change; the
    first list seeds the baseline silently. A threading.Event `stop` ends it, else it loops forever."""
    _poll_loop(self.list_projects, on_change, interval, stop)

  def get_project(self, pid: str) -> dict:
    """GET /projects/{id} → {project, layout?}."""
    return self._request("GET", "/projects/" + urllib.parse.quote(str(pid)))

  def _project_record(self, pid: str) -> (dict | NoneType):
    full = self.get_project(pid)
    proj = (full or {}).get("project") if isinstance(full, dict) else None
    return proj if isinstance(proj, dict) else None

  def create_project(self, **kw: Any) -> dict:
    """POST /projects → the created ProjectRecord; `expiresAt` is epoch ms, and a None value is
    dropped so the server applies its default."""
    body = {k: v for k, v in kw.items() if v is not None}
    return self._request("POST", "/projects", body=body)

  def update_project(
    self,
    pid: str,
    layout: Any = None,
    name: (str | NoneType) = None,
    color: (str | NoneType) = None,
    description: (str | NoneType) = None,
    expires_at: (int | NoneType) = None,
    version: int = 0,
  ) -> dict:
    """PUT /projects/{id} → the updated ProjectRecord; a stale `version` is ServerError("conflict").

    None keeps a field; "" clears `color` or `description`; `expires_at` is epoch ms, 0 = forever."""
    body: dict[str, Any] = {"version": version}
    if name is not None: body["name"] = name
    if color is not None: body["color"] = color
    if description is not None: body["description"] = description
    if expires_at is not None: body["expiresAt"] = expires_at
    if layout is not None: body["layout"] = layout
    return self._request("PUT", "/projects/" + urllib.parse.quote(str(pid)), body=body)

  def _update_field_with_retry(self, pid: str, **fields: Any) -> dict:
    """A version-guarded one-field write, re-read and retried on a conflict up to
    _FIELD_WRITE_RETRIES times (twin of cli/src/console/handlers.zig putProjectField)."""
    last: (ServerError | NoneType) = None
    for _ in range(_FIELD_WRITE_RETRIES):
      version = self._current_version(pid, 0)
      try:
        return self.update_project(pid, version=version, **fields)
      except ServerError as err:
        if err.code != "conflict": raise
        last = err
    raise last if last is not None else ServerError(
      "conflict", "gave up after repeated version conflicts", 409)

  def rename_project(self, pid: str, name: str) -> dict:
    return self._update_field_with_retry(pid, name=name)

  def set_project_description(self, pid: str, description: str) -> dict:
    """"" clears it."""
    return self._update_field_with_retry(pid, description=description)

  def set_project_expiration(self, pid: str, expires_at: int) -> dict:
    """`expires_at` in epoch ms, 0 = keep forever; the server's sweep reaps a past expiry."""
    return self._update_field_with_retry(pid, expires_at=expires_at)

  def get_project_expiration(self, pid: str) -> int:
    """The record's `expiresAt` in epoch ms; 0 (keep forever) when unset."""
    proj = self._project_record(pid)
    return int(proj.get("expiresAt", 0) or 0) if proj else 0

  def get_project_color(self, pid: str) -> str:
    """The record's `color`; "" (theme fallback) when unset."""
    proj = self._project_record(pid)
    return (proj.get("color", "") or "") if proj else ""

  def get_project_description(self, pid: str) -> str:
    """The record's `description`; "" when unset."""
    proj = self._project_record(pid)
    return (proj.get("description", "") or "") if proj else ""

  def delete_project(self, pid: str) -> None:
    """DELETE /projects/{id} (204 No Content)."""
    self._request("DELETE", "/projects/" + urllib.parse.quote(str(pid)))
