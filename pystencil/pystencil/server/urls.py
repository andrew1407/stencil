"""Server URL handling: loopback detection, scheme/port normalization, invite links."""

from __future__ import annotations

import urllib.parse

from .._ffi.types import NoneType


# The server's default port (server/.env.example); normalize_url never adds it.
DEFAULT_PORT = 8090


def is_loopback_host(host: (str | NoneType)) -> bool:
  """True for localhost, *.localhost, 127.0.0.0/8 or ::1 (twin of connectionManager.js)."""
  if not host: return False
  h = host.lower().strip("[]")
  if h == "localhost" or h.endswith(".localhost"): return True
  if h == "::1": return True
  parts = h.split(".")
  return len(parts) == 4 and parts[0] == "127" and all(
    # isascii(): str.isdigit alone accepts Unicode digits; the JS \d does not.
    p.isascii() and p.isdigit() and len(p) <= 3 for p in parts[1:]
  )


def normalize_url(raw: (str | NoneType)) -> str:
  """``scheme://host[:port]`` of ``raw``; a bare remote host gets https, a bare loopback one
  http, and an explicit scheme is kept (twin of connectionManager.js normalizeUrl)."""
  s = str(raw if raw is not None else "").strip()
  if not s: raise ValueError("Server URL is required")
  if not s.lower().startswith(("http://", "https://")):
    host = urllib.parse.urlsplit("http://" + s).hostname
    s = ("http://" if is_loopback_host(host) else "https://") + s
  parts = urllib.parse.urlsplit(s)
  if not parts.netloc: raise ValueError(f"Invalid server URL: {raw!r}")
  return f"{parts.scheme}://{parts.netloc}"


def split_invite_token(url: (str | NoneType), token: (str | NoneType) = None) -> tuple[str, (str | NoneType)]:
  """``(url, token)`` with an invite link's '#token=' fragment split off; an explicit token wins."""
  s = str(url if url is not None else "")
  i = s.find("#token=")
  if i < 0: return s, token
  frag = s[i + len("#token="):].strip()
  return s[:i], token or frag or None

