"""Project-change diffing, the poll loop that drives watchers, and the connections-listing
credential filter."""

from __future__ import annotations

from typing import Callable

import time

from .._ffi.types import NoneType


# `version` is the server's monotonic edit counter: any save bumps it.
_WATCHED_FIELDS = ("version", "name", "color", "description")
_FIELD_DEFAULT = {"version": 0, "name": "", "color": "", "description": ""}

# The CLI's putProjectField retry count.
_FIELD_WRITE_RETRIES = 4


def diff_projects(prev: list, curr: list) -> list:
  """Two `GET /projects` lists as ``{id, kind, fields, project}`` events: ``kind`` is
  created/updated/deleted, ``fields`` the watched fields that changed (only on 'updated'),
  ``project`` the current record (the prior one for a deletion)."""
  prev_by = {p.get("id"): p for p in (prev or []) if p.get("id")}
  curr_by = {p.get("id"): p for p in (curr or []) if p.get("id")}
  changes: list = list()
  for pid, new in curr_by.items():
    old = prev_by.get(pid)
    if old is None:
      changes.append({"id": pid, "kind": "created", "fields": [], "project": new})
      continue
    fields = [
      f for f in _WATCHED_FIELDS
      if old.get(f, _FIELD_DEFAULT[f]) != new.get(f, _FIELD_DEFAULT[f])
    ]
    if fields:
      changes.append({"id": pid, "kind": "updated", "fields": fields, "project": new})
  for pid, old in prev_by.items():
    if pid not in curr_by:
      changes.append({"id": pid, "kind": "deleted", "fields": [], "project": old})
  return changes


def _poll_loop(fetch: Callable[[], list], on_change, interval: float, stop) -> None:
  """Seed a silent baseline, then every `interval` s fire on_change per diff. A failed fetch
  keeps the baseline, so an outage never reads as mass deletes; `stop` interrupts the sleep."""
  try:
    baseline = fetch()
  except Exception:
    baseline = list()
  while not (stop is not None and stop.is_set()):
    if stop is not None:
      if stop.wait(interval): break
    else:
      time.sleep(interval)
    try:
      current = fetch()
    except Exception:
      continue
    for change in diff_projects(baseline, current): on_change(change)
    baseline = current


def parse_credential_filter(arg: str) -> (str | NoneType):
  """The `/connections [admin|session]` filter: "" or "all" → "all"; None = unrecognised."""
  w = (arg or "").strip().lower()
  if w in ("", "all"): return "all"
  if w in ("admin", "session"): return w
  return None


def credential_filter_matches(flt: str, kind: str) -> bool:
  """True when a connection of credential `kind` belongs in a `flt` listing."""
  if flt == "admin": return kind == "admin"
  if flt == "session":
    return kind != "admin"
  return True

