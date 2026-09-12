from __future__ import annotations

"""The project-metadata half of a connection's REST surface (protocol.go routes).

A mixin: every method reaches the network through the ``_request`` that
:class:`ServerConnection` owns. Includes the poll-based change tracking that stands
in for the /ws feed this REST-only client does not open.
"""

import urllib.parse
from typing import Any, Optional

from .diff import _FIELD_DEFAULT, _FIELD_WRITE_RETRIES, _WATCHED_FIELDS, _poll_loop, diff_projects
from .http import ServerError


class _ProjectApi:
  """Project listing, creation, metadata writes and change tracking."""

  def list_projects(self) -> list:
    """GET /projects → the project records list (ProjectListResponse)."""
    r = self._request("GET", "/projects")
    return (r or {}).get("projects", []) or []

  # ── project-change tracking (poll-based) ──
  # This client stays REST-only (no /ws feed), so "listening" for a peer's name/color
  # change is modelled as polling, exactly like the desktop's QTimer poll. Two flavours:
  # poll_project_changes() is one-shot (the caller owns the loop), watch_projects() is a
  # ready-made blocking loop. Use get_project(id)'s version to confirm a single project.
  def poll_project_changes(self, previous: list | None = None) -> tuple:
    """One-shot poll: fetch the current project list and diff it against `previous`.

    Returns ``(current_list, changes)`` (see diff_projects for the change shape).
    Pass the prior list back in on the next call to detect what moved; a first call
    with ``previous=None`` reports every project as 'created', so seed a baseline with
    list_projects() when you only want subsequent changes.
    """
    current = self.list_projects()
    return current, diff_projects(previous, current)

  def watch_projects(self, on_change, *, interval: float = 2.0, stop=None) -> None:
    """Block, polling every `interval` s, calling on_change(change) per project
    create/update/delete — mirroring the desktop poll loop. The first list seeds the
    baseline silently (only later changes fire). Pass a threading.Event as `stop`
    (and/or run this in a thread) to end it; without one it loops forever.
    """
    _poll_loop(self.list_projects, on_change, interval, stop)

  def get_project(self, pid: str) -> dict:
    """GET /projects/{id} → {project, layout?, originalContent?}."""
    return self._request("GET", "/projects/" + urllib.parse.quote(str(pid)))

  def _project_record(self, pid: str) -> Optional[dict]:
    """Fetch a project and unwrap its ProjectRecord from the {project: ...} envelope."""
    full = self.get_project(pid)
    proj = (full or {}).get("project") if isinstance(full, dict) else None
    return proj if isinstance(proj, dict) else None

  def create_project(self, **kw: Any) -> dict:
    """POST /projects → the created ProjectRecord (id, version, …).

    Accepts name/source/resource/hasImage/imageW/imageH/layout and an
    optional expiresAt (epoch ms; omit for the server default — no expiry
    unless the server sets PROJECT_TTL). None values are dropped so the
    server applies its own defaults.
    """
    body = {k: v for k, v in kw.items() if v is not None}
    return self._request("POST", "/projects", body=body)

  def update_project(
    self,
    pid: str,
    layout: Any = None,
    name: str | None = None,
    color: str | None = None,
    description: str | None = None,
    expires_at: int | None = None,
    version: int = 0,
  ) -> dict:
    """PUT /projects/{id} → the updated ProjectRecord.

    version guards the last-writer-wins update; a stale version yields a
    409 which surfaces as ServerError(code="conflict"). `color` rides the
    same nil-means-unchanged contract as `name` (UpdateProjectRequest.Color
    is *string): pass "" to clear the custom accent, a "#rrggbb" hex to set
    it, or leave it None to keep the server's current value. `description`
    rides the same contract (UpdateProjectRequest.Description is *string):
    pass "" to clear, text to set, or None to keep the current value.
    `expires_at` (epoch ms; 0 = keep forever) follows the same contract via
    UpdateProjectRequest.ExpiresAt (*int64): leave it None to keep the
    current expiry.
    """
    body: dict[str, Any] = {"version": version}
    if name is not None:
      body["name"] = name
    if color is not None:
      body["color"] = color
    if description is not None:
      body["description"] = description
    if expires_at is not None:
      body["expiresAt"] = expires_at
    if layout is not None:
      body["layout"] = layout
    return self._request("PUT", "/projects/" + urllib.parse.quote(str(pid)), body=body)

  def _update_field_with_retry(self, pid: str, **fields: Any) -> dict:
    """Version-guarded single-field write (name / color / expires_at) with a bounded
    conflict retry — the read-then-PUT is not atomic, so a peer that saves between our
    version read and our PUT would 409 and silently drop the change. On a conflict we
    re-read the current version and retry, mirroring the CLI's putProjectField loop
    (cli/src/console/handlers.zig). Raises the last ServerError if it can't win within
    _FIELD_WRITE_RETRIES attempts."""
    last: Optional[ServerError] = None
    for _ in range(_FIELD_WRITE_RETRIES):
      version = self._current_version(pid, 0)
      try:
        return self.update_project(pid, version=version, **fields)
      except ServerError as err:
        if err.code != "conflict":
          raise
        last = err  # a peer won the race — re-read the version and retry
    raise last if last is not None else ServerError(
      "conflict", "gave up after repeated version conflicts", 409)

  def rename_project(self, pid: str, name: str) -> dict:
    """Rename a server project (PUT name) under the last-writer-wins guard, retrying on a
    conflict so a peer's concurrent edit doesn't drop the rename (see
    _update_field_with_retry). The server broadcasts the change to every connected client,
    so other front-ends (browser/desktop/CLI) pick the new name up live."""
    return self._update_field_with_retry(pid, name=name)

  def set_project_description(self, pid: str, description: str) -> dict:
    """Set a server project's description under the last-writer-wins guard, retrying on a
    conflict so a peer's concurrent edit doesn't drop the change (see
    _update_field_with_retry). Pass "" to clear it. The server broadcasts the change to
    every connected client, so other front-ends pick the new description up live."""
    return self._update_field_with_retry(pid, description=description)

  def set_project_expiration(self, pid: str, expires_at: int) -> dict:
    """Set a server project's expiry (epoch ms; 0 = keep forever) under the last-writer-wins
    guard, retrying on a conflict (see _update_field_with_retry). The server stamps it and
    every other front-end picks the change up live; a past expiry is reaped by the server's
    sweep. Server projects have no expiry until one is set here."""
    return self._update_field_with_retry(pid, expires_at=expires_at)

  def get_project_expiration(self, pid: str) -> int:
    """GET /projects/{id} and return its ProjectRecord `expiresAt` (epoch ms; 0 = never).

    Mirrors get_project_color — an unset/missing value comes back as 0 (keep forever).
    """
    proj = self._project_record(pid)
    return int(proj.get("expiresAt", 0) or 0) if proj else 0

  def get_project_color(self, pid: str) -> str:
    """GET /projects/{id} and return its ProjectRecord `color`.

    Mirrors the browser reading record.color off the fetched project; an
    unset/missing value comes back as "" (theme fallback).
    """
    proj = self._project_record(pid)
    return (proj.get("color", "") or "") if proj else ""

  def get_project_description(self, pid: str) -> str:
    """GET /projects/{id} and return its ProjectRecord `description`.

    Mirrors get_project_color — an unset/missing value comes back as "".
    """
    proj = self._project_record(pid)
    return (proj.get("description", "") or "") if proj else ""

  def delete_project(self, pid: str) -> None:
    """DELETE /projects/{id} (204 No Content)."""
    self._request("DELETE", "/projects/" + urllib.parse.quote(str(pid)))
