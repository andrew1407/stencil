"""Server URL handling: loopback detection, scheme/port normalization, invite links."""

from __future__ import annotations

import urllib.parse

from .._ffi.types import NoneType


# Default port the collaboration server listens on (server/.env.example). Used
# only as documentation here; normalize_url keeps whatever port the caller gave.
DEFAULT_PORT = 8090


def is_loopback_host(host: (str | NoneType)) -> bool:
  """True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where
  plaintext http is safe because the bytes never leave the machine. Port of
  connectionManager.js isLoopbackHost."""
  if not host: return False
  h = host.lower().strip("[]")  # strip any IPv6 brackets
  if h == "localhost" or h.endswith(".localhost"): return True
  if h == "::1": return True
  parts = h.split(".")
  return len(parts) == 4 and parts[0] == "127" and all(
    # isascii(): str.isdigit alone accepts Unicode digits; the JS \d does not.
    p.isascii() and p.isdigit() and len(p) <= 3 for p in parts[1:]
  )


def normalize_url(raw: (str | NoneType)) -> str:
  """Turn 'host:8090' / 'http://host:8090/path' into a clean origin.

  Port of connectionManager.js normalizeUrl: trim, then keep only
  scheme://host[:port] (drop any path, query or fragment) so every connection
  is keyed by a stable origin. Secure by default: a bare REMOTE host gets
  https; loopback keeps http (dev servers run plaintext on localhost). An
  explicit scheme is preserved — the caller opts into cleartext.
  """
  s = str(raw if raw is not None else "").strip()
  if not s: raise ValueError("Server URL is required")
  if not s.lower().startswith(("http://", "https://")):
    host = urllib.parse.urlsplit("http://" + s).hostname
    s = ("http://" if is_loopback_host(host) else "https://") + s
  parts = urllib.parse.urlsplit(s)
  if not parts.netloc: raise ValueError(f"Invalid server URL: {raw!r}")
  # origin == scheme://netloc, nothing else.
  return f"{parts.scheme}://{parts.netloc}"


def split_invite_token(url: (str | NoneType), token: (str | NoneType) = None) -> tuple[str, (str | NoneType)]:
  """Split an invite link's '#token=<value>' fragment off a connect URL.

  Returns (url, token): the fragment is stripped and its value becomes the
  supplied token — unless an explicit token was passed, which wins.
  """
  s = str(url if url is not None else "")
  i = s.find("#token=")
  if i < 0: return s, token
  frag = s[i + len("#token="):].strip()
  return s[:i], token or frag or None

