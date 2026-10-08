"""Stencil collaboration-server client (REST over urllib).

Twin of browser/js/net/connectionManager.js + remoteSync.js over
server/internal/protocol/protocol.go. REST-only: a "connection" is a validated token + base URL.
"""

from __future__ import annotations

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
