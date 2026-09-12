from __future__ import annotations

# ── Stencil collaboration-server client (REST over urllib) ───────────────────
# A faithful Python port of the browser net layer
# (browser/js/net/connectionManager.js + remoteSync.js) speaking the same REST
# contract declared in server/internal/protocol/protocol.go. Unlike the browser
# client this one is REST-only: it does NOT open the /ws live-events feed (the
# Python package is a headless editing/automation surface, not a live co-editor),
# so a "connection" here is just a validated token + base URL.
#
# Memory/format note mirrored from the browser: the server is codec-free, so
# every file upload passes the pixel dimensions (w/h) and an extension hint
# explicitly via the ?ext&w&h query — the bytes are sent as octet-stream.
#
# Split across http/urls/diff/projects/files/connection/manager; this module is the
# façade that re-exports the surface callers bind to.

from .connection import ServerConnection
from .diff import (
  credential_filter_matches,
  diff_projects,
  parse_credential_filter,
)
from .http import (
  ServerError,
  _http_open,
  _json_request,
  _parse_http_error,
  _LLM_TIMEOUT,
  _REQUEST_TIMEOUT,
)
from .manager import ConnectionManager, ConnectSpec
from .urls import DEFAULT_PORT, is_loopback_host, normalize_url, split_invite_token

__all__ = [
  "DEFAULT_PORT",
  "ConnectSpec",
  "ConnectionManager",
  "ServerConnection",
  "ServerError",
  "credential_filter_matches",
  "diff_projects",
  "is_loopback_host",
  "normalize_url",
  "parse_credential_filter",
  "split_invite_token",
]
