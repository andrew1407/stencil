from __future__ import annotations

"""One connected Stencil server: identity, request plumbing and the handshake.

The REST surface itself lives in the two mixins this class composes —
:mod:`.projects` (project metadata) and :mod:`.files` (file bytes + remoteSync) —
so each stays readable on its own. Every call ultimately routes through
``_request`` here, which owns the one-shot session-token re-mint.
"""

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

from .._net import _unverified_ssl_context
from .files import _FileApi
from .http import ServerError, _http_open, _json_request, _parse_http_error
from .projects import _ProjectApi
from .urls import normalize_url, split_invite_token


class ServerConnection(_ProjectApi, _FileApi):
    """A single connected Stencil server (validated token + REST surface)."""

    def __init__(self, url: str, token: str | None = None, *, verify: bool = True) -> None:
        # Invite links carry the token as a '#token=' fragment; explicit wins.
        url, token = split_invite_token(url, token)
        self.base = normalize_url(url)
        self.token = token or ""
        # What the user supplied — outlives a server restart (_request re-mints
        # with it when the stored session token goes stale). "" = none supplied.
        self.credential = token or ""
        # What that credential turned out to BE (browser connectionManager parity):
        # "admin" once it has PROVEN it can mint a session token (at connect or on a
        # mid-session re-mint), "session" once it passed the GET /projects probe
        # directly, "none" when nothing was supplied. "" until connect() classifies it.
        self.credential_kind = "" if self.credential else "none"
        # 'disconnected' until connect() validates/acquires a token, then
        # 'connected', or 'error' if the handshake fails (mirrors the browser
        # UI-dot status, minus the live 'connecting' transition we don't model).
        self.status = "disconnected"
        # A stable client id, namespaced like the browser's c_<rand>. Derived
        # from object identity so it's deterministic per instance without RNG.
        self.client_id = "c_" + format(id(self) & 0xFFFFFFFF, "08x")
        # When False, accept self-signed certs (dev servers); default verifies.
        self._verify = verify
        self._ssl_ctx = None if verify else _unverified_ssl_context()

    # ── request plumbing ──
    def _build_request(
        self,
        method: str,
        path: str,
        body: Any = None,
        token: str | None = None,
        *,
        raw: bool = False,
        query: dict | None = None,
    ) -> urllib.request.Request:
        """Pure builder: assemble a urllib Request for `method base+path`.

        Kept side-effect free (no network) so it can be unit-tested directly.
        `raw=True` sends `body` verbatim as application/octet-stream (file
        uploads); otherwise a non-None body is JSON-encoded. The Authorization
        header is always present (Bearer <token>), matching the browser client.
        """
        url = self.base + path
        if query:
            # Stable, urlencoded query string (?ext=png&w=320&h=240).
            url += "?" + urllib.parse.urlencode(query)
        tok = self.token if token is None else token
        if raw and body is not None:
            headers = {
                "Authorization": "Bearer " + (tok or ""),
                "Content-Type": "application/octet-stream",
            }
            return urllib.request.Request(
                url, data=bytes(body), headers=headers, method=method
            )
        return _json_request(method, url, body, bearer=tok or "")

    def _open(self, req: urllib.request.Request, raw: bool = False) -> Any:
        """Execute a Request, translating non-2xx into ServerError.

        Returns parsed JSON for normal calls, raw bytes when `raw=True`
        (file downloads), or None for empty/204 responses.
        """
        status, payload = _http_open(req, self._error_from, context=self._ssl_ctx)
        if raw:
            return payload
        if status == 204 or not payload:
            return None
        return json.loads(payload.decode("utf-8"))

    @staticmethod
    def _error_from(e: urllib.error.HTTPError) -> ServerError:
        """Build a ServerError from an HTTPError, parsing {code,message}."""
        code, message = _parse_http_error(e)
        return ServerError(code, message, status=e.code)

    def _request(
        self,
        method: str,
        path: str,
        *,
        body: Any = None,
        raw: bool = False,
        query: dict | None = None,
        _retried: bool = False,
    ) -> Any:
        req = self._build_request(method, path, body, raw=raw, query=query)
        try:
            return self._open(req, raw=raw)
        except ServerError as err:
            # A stored session token dies with a server restart — when we still
            # hold the original credential, re-mint once and retry in place
            # (port of extension connections.js req()).
            if (_retried or path == "/auth/token" or not self.credential
                    or err.status not in (401, 403)):
                raise
            try:
                mint = self._build_request(
                    "POST", "/auth/token", {}, token=self.credential)
                r = self._open(mint)
            except Exception:
                raise err from None  # failed re-mint: surface the original error
            self.token = (r or {}).get("token", "")
            out = self._request(
                method, path, body=body, raw=raw, query=query, _retried=True)
            # It minted AND the retried request works: the credential is an admin token.
            self.credential_kind = "admin"
            return out

    # ── handshake ──
    def connect(self) -> "ServerConnection":
        """Acquire (or validate) a token, mirroring browser handshake().

        Without a token we mint one via POST /auth/token; with a token we
        validate it by listing projects. Sets `status` accordingly.
        """
        try:
            if not self.token:
                r = self._request("POST", "/auth/token", body={})
                self.token = (r or {}).get("token", "")
            else:
                try:
                    self._request("GET", "/projects")  # validate access
                    # A probe that passed without _request re-minting (which would have
                    # said "admin" already) means an ordinary session token.
                    if self.credential_kind != "admin":
                        self.credential_kind = "session"
                except ServerError as err:
                    # Browser/desktop parity: the value may be the server's
                    # ADMIN token — it can't list projects, but it can MINT.
                    # Only an auth failure (or a status-less error) means that;
                    # a 500 etc. propagates as-is.
                    if err.status is not None and err.status not in (401, 403):
                        raise
                    r = self._request("POST", "/auth/token", body={})
                    self.token = (r or {}).get("token", "")
                    self._request("GET", "/projects")
                    # Minted, and the session it minted works: proven admin credential.
                    self.credential_kind = "admin"
        except Exception:
            self.status = "error"
            raise
        self.status = "connected"
        return self

    def close(self) -> None:
        """Drop the connection (REST-only, so just flips status)."""
        self.status = "disconnected"

