from __future__ import annotations

"""Project-change diffing and the poll loop that drives watchers, plus the
connections-listing credential filter. Pure functions over project records.
"""

from typing import Callable

import time

from .._types import NoneType


# Project-metadata fields a watcher reports on. `version` is the server's monotonic edit
# counter (any save bumps it); name/color/description are the user-visible metadata.
_WATCHED_FIELDS = ("version", "name", "color", "description")
_FIELD_DEFAULT = {"version": 0, "name": "", "color": "", "description": ""}

# Attempts for a version-guarded field write before giving up on sustained conflict
# (matches the CLI's putProjectField retry count).
_FIELD_WRITE_RETRIES = 4


def diff_projects(prev: list, curr: list) -> list:
  """Diff two `GET /projects` lists into project-change events. Pure (no network),
  so it's unit-tested without a server — the building block for poll-based watching.

  Returns a list of dicts ``{id, kind, fields, project}`` where ``kind`` is
  ``'created'`` | ``'updated'`` | ``'deleted'`` and ``fields`` lists which of
  name/color/description/version changed (only for 'updated'; empty for created/deleted).
  `project` is the current record (the prior record for a deletion).
  """
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
  """Shared blocking poll loop for the *_changes watchers. Seeds a silent baseline
  from `fetch()`, then every `interval` seconds re-fetches and fires on_change for
  each diff. A failed fetch is skipped (keeps the baseline) so a transient outage
  doesn't look like mass deletes. `stop` (a threading.Event or None) ends the loop;
  when given, its .wait() makes the sleep interruptible so stop() returns promptly.
  """
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
      continue  # transient error — keep the baseline, retry next tick
    for change in diff_projects(baseline, current): on_change(change)
    baseline = current


def parse_credential_filter(arg: str) -> (str | NoneType):
  """Parse a connections-listing filter word (CLI `/connections [admin|session]`).

  "" (or "all") keeps everything, "admin" keeps admin-credential connections,
  "session" keeps the rest. None = unrecognised, for a usage note.
  """
  w = (arg or "").strip().lower()
  if w in ("", "all"): return "all"
  if w in ("admin", "session"): return w
  return None


def credential_filter_matches(flt: str, kind: str) -> bool:
  """True when a connection of credential `kind` belongs in a `flt` listing."""
  if flt == "admin": return kind == "admin"
  if flt == "session":
    return kind != "admin"  # a plain session token, or none supplied
  return True

