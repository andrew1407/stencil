"""One connected Stencil server: identity, request plumbing and the handshake.

Every call of the :mod:`.projects` and :mod:`.files` mixins routes through ``_request``,
which owns the one-shot session-token re-mint.
"""

from __future__ import annotations

import json
import threading
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

from .._ffi.types import NoneType
from .._net import _unverified_ssl_context
from .files import _FileApi
from .http import ServerError, _http_open, _json_request, _parse_http_error, _set_bearer
from .projects import _ProjectApi
from .urls import normalize_url, split_invite_token


class ServerConnection(_ProjectApi, _FileApi):
  def __init__(self, url: str, token: (str | NoneType) = None, *, verify: bool = True) -> None:
    url, token = split_invite_token(url, token)
    self.base = normalize_url(url)
    self.token = token or ""
    # What the user supplied, kept to re-mint after a server restart; "" = none supplied.
    self.credential = token or ""
    # "admin" once it minted a session token, "session" once it passed a probe, "none" if absent.
    self.credential_kind = "" if self.credential else "none"
    # 'disconnected' | 'connected' | 'error'.
    self.status = "disconnected"
    # The browser's c_<rand> namespace, from object identity rather than an RNG.
    self.client_id = "c_" + format(id(self) & 0xFFFFFFFF, "08x")
    self._verify = verify
    self._ssl_ctx = None if verify else _unverified_ssl_context()
    self._mint_lock = threading.Lock()

  def _build_request(
    self,
    method: str,
    path: str,
    body: Any = None,
    token: (str | NoneType) = None,
    *,
    raw: bool = False,
    query: (dict | NoneType) = None,
  ) -> urllib.request.Request:
    """`method base+path`, no network: `raw` sends `body` as octet-stream, else a non-None body
    is JSON; the Bearer header is always present."""
    url = self.base + path
    if query:
      # Stable, urlencoded query string (?ext=png&w=320&h=240).
      url += "?" + urllib.parse.urlencode(query)
    tok = self.token if token is None else token
    if raw and body is not None:
      req = urllib.request.Request(
        url, data=bytes(body), headers={"Content-Type": "application/octet-stream"},
        method=method,
      )
      _set_bearer(req, tok or "")
      return req
    return _json_request(method, url, body, bearer=tok or "")

  def _open(self, req: urllib.request.Request, raw: bool = False) -> Any:
    """Parsed JSON, the raw bytes when `raw`, or None for an empty/204 reply; non-2xx is ServerError."""
    status, payload = _http_open(req, self._error_from, context=self._ssl_ctx)
    if raw: return payload
    if status == 204 or not payload: return None
    return json.loads(payload.decode("utf-8"))

  @staticmethod
  def _error_from(e: urllib.error.HTTPError) -> ServerError:
    code, message = _parse_http_error(e)
    return ServerError(code, message, status=e.code)

  def _request(
    self,
    method: str,
    path: str,
    *,
    body: Any = None,
    raw: bool = False,
    query: (dict | NoneType) = None,
    _retried: bool = False,
  ) -> Any:
    sent = self.token
    req = self._build_request(method, path, body, raw=raw, query=query)
    try:
      return self._open(req, raw=raw)
    except ServerError as err:
      # A session token dies with a server restart: re-mint once from the credential and
      # retry in place (twin of the extension's connections.js req()).
      if (_retried or path == "/auth/token" or not self.credential
          or err.status not in (401, 403)):
        raise
      with self._mint_lock:
        if self.token == sent:  # else a racing request already re-minted
          try:
            r = self._open(self._build_request("POST", "/auth/token", {}, token=self.credential))
          except Exception:
            raise err from None
          self.token = (r or {}).get("token", "")
      out = self._request(
        method, path, body=body, raw=raw, query=query, _retried=True)
      self.credential_kind = "admin"
      return out

  def connect(self) -> "ServerConnection":
    """Mint a token, or probe the one supplied (twin of the browser's handshake())."""
    try:
      if not self.token:
        r = self._request("POST", "/auth/token", body={})
        self.token = (r or {}).get("token", "")
      else:
        try:
          self._probe()
          if self.credential_kind != "admin": self.credential_kind = "session"
        except ServerError as err:
          # An admin token holds no session but can mint; only an auth failure means that.
          if err.status is not None and err.status not in (401, 403): raise
          r = self._request("POST", "/auth/token", body={})
          self.token = (r or {}).get("token", "")
          self._probe()
          self.credential_kind = "admin"
    except Exception:
      self.status = "error"
      raise
    self.status = "connected"
    return self

  def _probe(self) -> None:
    """Validate the session token with ``GET /auth/session``; a server that predates the
    route answers 404 and is probed with a one-record ``GET /projects?limit=1``."""
    try:
      self._request("GET", "/auth/session")
    except ServerError as err:
      if err.status != 404: raise
      self._request("GET", "/projects", query={"limit": "1"})

  def close(self) -> None:
    """REST-only, so this only flips `status`."""
    self.status = "disconnected"

