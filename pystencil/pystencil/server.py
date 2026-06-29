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

import json
import ssl
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Iterable, Optional, Union


# Default port the collaboration server listens on (server/.env.example). Used
# only as documentation here; normalize_url keeps whatever port the caller gave.
DEFAULT_PORT = 8090


def normalize_url(raw: Optional[str]) -> str:
    """Turn 'host:8090' / 'http://host:8090/path' into a clean origin.

    Port of connectionManager.js normalizeUrl: trim, default the scheme to
    http:// when absent, then keep only scheme://host[:port] (drop any path,
    query or fragment) so every connection is keyed by a stable origin.
    """
    s = str(raw if raw is not None else "").strip()
    if not s:
        raise ValueError("Server URL is required")
    # No scheme → assume http:// (matches the browser's regex default).
    if not s.lower().startswith(("http://", "https://")):
        s = "http://" + s
    parts = urllib.parse.urlsplit(s)
    if not parts.netloc:
        raise ValueError(f"Invalid server URL: {raw!r}")
    # origin == scheme://netloc, nothing else.
    return f"{parts.scheme}://{parts.netloc}"


class ServerError(Exception):
    """A non-2xx REST response.

    Carries the server's structured {code, message} (protocol.ErrorResponse)
    when present, plus the raw HTTP status. `code` mirrors protocol's error
    codes (e.g. "conflict", "notFound", "unauthorized").
    """

    def __init__(self, code: str, message: str, status: int | None = None) -> None:
        super().__init__(f"{code}: {message}" if code else message)
        self.code = code
        self.message = message
        self.status = status


class ServerConnection:
    """A single connected Stencil server (validated token + REST surface)."""

    def __init__(self, url: str, token: str | None = None, *, verify: bool = True) -> None:
        self.base = normalize_url(url)
        self.token = token or ""
        # 'disconnected' until connect() validates/acquires a token, then
        # 'connected', or 'error' if the handshake fails (mirrors the browser
        # UI-dot status, minus the live 'connecting' transition we don't model).
        self.status = "disconnected"
        # A stable client id, namespaced like the browser's c_<rand>. Derived
        # from object identity so it's deterministic per instance without RNG.
        self.client_id = "c_" + format(id(self) & 0xFFFFFFFF, "08x")
        # When False, accept self-signed certs (dev servers); default verifies.
        self._verify = verify
        self._ssl_ctx = None if verify else ssl._create_unverified_context()

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
        headers = {"Authorization": "Bearer " + (tok or "")}
        data: bytes | None = None
        if body is not None:
            if raw:
                # Raw image bytes upload.
                headers["Content-Type"] = "application/octet-stream"
                data = bytes(body)
            else:
                headers["Content-Type"] = "application/json"
                data = json.dumps(body).encode("utf-8")
        return urllib.request.Request(url, data=data, headers=headers, method=method)

    def _open(self, req: urllib.request.Request, raw: bool = False) -> Any:
        """Execute a Request, translating non-2xx into ServerError.

        Returns parsed JSON for normal calls, raw bytes when `raw=True`
        (file downloads), or None for empty/204 responses.
        """
        try:
            resp = urllib.request.urlopen(req, context=self._ssl_ctx)
        except urllib.error.HTTPError as e:
            raise self._error_from(e) from None
        with resp:
            payload = resp.read()
            status = getattr(resp, "status", resp.getcode())
        if raw:
            return payload
        if status == 204 or not payload:
            return None
        return json.loads(payload.decode("utf-8"))

    @staticmethod
    def _error_from(e: urllib.error.HTTPError) -> ServerError:
        """Build a ServerError from an HTTPError, parsing {code,message}."""
        code = ""
        message = f"HTTP {e.code}"
        try:
            body = e.read()
            if body:
                parsed = json.loads(body.decode("utf-8"))
                if isinstance(parsed, dict):
                    code = parsed.get("code", "") or ""
                    message = parsed.get("message", message) or message
        except Exception:
            # Non-JSON error body — keep the generic "HTTP <status>" message.
            pass
        return ServerError(code, message, status=e.code)

    def _request(
        self,
        method: str,
        path: str,
        *,
        body: Any = None,
        raw: bool = False,
        query: dict | None = None,
    ) -> Any:
        req = self._build_request(method, path, body, raw=raw, query=query)
        return self._open(req, raw=raw)

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
                self._request("GET", "/projects")  # validate access
        except Exception:
            self.status = "error"
            raise
        self.status = "connected"
        return self

    # ── REST surface (protocol.go routes) ──
    def list_projects(self) -> list:
        """GET /projects → the project records list (ProjectListResponse)."""
        r = self._request("GET", "/projects")
        return (r or {}).get("projects", []) or []

    def get_project(self, pid: str) -> dict:
        """GET /projects/{id} → {project, layout?, originalContent?}."""
        return self._request("GET", "/projects/" + urllib.parse.quote(str(pid)))

    def create_project(self, **kw: Any) -> dict:
        """POST /projects → the created ProjectRecord (id, version, …).

        Accepts name/source/resource/hasImage/imageW/imageH/layout; None
        values are dropped so the server applies its own defaults.
        """
        body = {k: v for k, v in kw.items() if v is not None}
        return self._request("POST", "/projects", body=body)

    def update_project(
        self,
        pid: str,
        layout: Any = None,
        name: str | None = None,
        version: int = 0,
    ) -> dict:
        """PUT /projects/{id} → the updated ProjectRecord.

        version guards the last-writer-wins update; a stale version yields a
        409 which surfaces as ServerError(code="conflict").
        """
        body: dict[str, Any] = {"version": version}
        if name is not None:
            body["name"] = name
        if layout is not None:
            body["layout"] = layout
        return self._request("PUT", "/projects/" + urllib.parse.quote(str(pid)), body=body)

    def delete_project(self, pid: str) -> None:
        """DELETE /projects/{id} (204 No Content)."""
        self._request("DELETE", "/projects/" + urllib.parse.quote(str(pid)))

    def get_file(self, pid: str, kind: str) -> bytes:
        """GET /projects/{id}/files/{kind} → raw image bytes."""
        path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
        return self._request("GET", path, raw=True)

    def put_file(self, pid: str, kind: str, data: bytes, ext: str, w: int, h: int) -> dict:
        """POST /projects/{id}/files/{kind}?ext&w&h → {path, w, h}.

        The server is codec-free, so dimensions/extension are passed in query
        params while the pixel bytes go in the octet-stream body.
        """
        path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
        query = {"ext": ext, "w": str(w), "h": str(h)}
        return self._request("POST", path, body=data, raw=True, query=query)

    # ── high-level sync (remoteSync.js) ──
    def _current_version(self, pid: str, fallback: int) -> int:
        """Re-read a project's version after a file write (which bumps it but
        returns none of its own), mirroring remoteSync.js currentVersion."""
        try:
            full = self.get_project(pid)
            proj = (full or {}).get("project") if isinstance(full, dict) else None
            v = proj.get("version") if isinstance(proj, dict) else None
            return fallback if v is None else int(v)
        except Exception:
            return fallback

    @staticmethod
    def _image_bytes(image: Any) -> tuple[bytes, int, int]:
        """Encode an Image to PNG bytes + dimensions (duck-typed to avoid a
        hard import of pystencil.image)."""
        data = image.encode("png")
        return bytes(data), int(image.width), int(image.height)

    def create_remote_project(
        self,
        name: str,
        image: Any = None,
        source: str | None = None,
        resource: str | None = None,
        layout: Any = None,
    ) -> dict:
        """Create a project and (when an image is given) upload the original.

        Port of remoteSync.js createRemoteProject: create → putFile('original').
        Returns the created project record with its version refreshed after the
        upload (which bumps it server-side).
        """
        has_image = image is not None
        rec = self.create_project(
            name=name or "Untitled",
            source=source or "",
            resource=resource or "",
            hasImage=has_image,
            layout=layout,
        )
        rec = rec or {}
        version = rec.get("version", 0)
        if has_image:
            data, w, h = self._image_bytes(image)
            if data:
                self.put_file(rec["id"], "original", data, "png", w, h)
                rec["version"] = self._current_version(rec["id"], version)
        return rec

    def save_remote_project(
        self,
        pid: str,
        version: int,
        layout: Any,
        image: Any = None,
        name: str | None = None,
    ) -> dict:
        """Version-guarded save-back (layout/name) plus optional result upload.

        Port of remoteSync.js saveRemoteProject: update → putFile('result'). A
        409 (lost last-writer-wins race) is surfaced as ServerError(code=
        "conflict"). Returns the refreshed project record.
        """
        try:
            rec = self.update_project(pid, layout=layout, name=name, version=version)
        except ServerError as err:
            if err.status == 409 or err.code == "conflict":
                raise ServerError(
                    "conflict",
                    "This project was edited elsewhere — reload it from the "
                    "server before saving again.",
                    status=err.status,
                ) from None
            raise
        rec = rec or {}
        new_version = rec.get("version", version)
        if image is not None:
            data, w, h = self._image_bytes(image)
            if data:
                self.put_file(pid, "result", data, "png", w, h)
                new_version = self._current_version(pid, new_version)
        rec["version"] = new_version
        return rec

    def close(self) -> None:
        """Drop the connection (REST-only, so just flips status)."""
        self.status = "disconnected"


# Specs accepted by ConnectionManager.connect: a url string, a {url, token}
# mapping, or an iterable of either.
ConnectSpec = Union[str, dict, Iterable[Any]]


class ConnectionManager:
    """The set of connected servers for one session (port of the browser's
    ConnectionManager, REST-only)."""

    def __init__(self, *, verify: bool = True) -> None:
        self._verify = verify
        self._conns: dict[str, ServerConnection] = {}  # url -> connection
        self._last: list[tuple[str, str]] = []  # for reconnect(): [(url, token)]

    @property
    def connections(self) -> list:
        """The connected server URLs (insertion order)."""
        return list(self._conns.keys())

    def get(self, url: str) -> ServerConnection | None:
        return self._conns.get(normalize_url(url))

    def has(self, url: str) -> bool:
        return normalize_url(url) in self._conns

    def connect(self, spec: ConnectSpec) -> "ConnectionManager":
        """Connect one or more servers; already-connected urls are no-ops."""
        for url, token in _iter_specs(spec):
            norm = normalize_url(url)
            if norm in self._conns:
                continue
            conn = ServerConnection(norm, token, verify=self._verify)
            conn.connect()
            self._conns[norm] = conn
        # Remember the live set so reconnect() can rebuild it (with tokens).
        self._last = [(c.base, c.token) for c in self._conns.values()]
        return self

    def disconnect(self, url: str | None = None) -> "ConnectionManager":
        """Disconnect a specific url, or the most recently added when omitted."""
        if url is None:
            urls = list(self._conns.keys())
            if not urls:
                return self
            target = urls[-1]
        else:
            target = normalize_url(url)
        conn = self._conns.pop(target, None)
        if conn:
            conn.close()
        return self

    def disconnect_all(self) -> "ConnectionManager":
        for conn in self._conns.values():
            conn.close()
        self._conns.clear()
        return self

    def reconnect(self) -> "ConnectionManager":
        """Re-establish the last connected set (tokens re-validated/re-issued)."""
        previous = list(self._last)
        self.disconnect_all()
        for url, token in previous:
            self.connect({"url": url, "token": token})
        return self

    def remote_projects(self) -> list:
        """Aggregate every connection's projects (unreachable servers skipped)."""
        out: list = []
        for conn in self._conns.values():
            try:
                out.extend(conn.list_projects())
            except Exception:
                # Skip an unreachable/erroring server, like the browser does.
                pass
        return out


def _one_spec(item: Any) -> tuple[Any, str]:
    """A single (url, token) from a url string or a {url, token?} mapping."""
    if isinstance(item, str):
        return item, ""
    if isinstance(item, dict):
        return item.get("url"), item.get("token") or ""
    raise TypeError(f"Unsupported connection spec: {item!r}")


def _iter_specs(spec: ConnectSpec):
    """Yield (url, token) pairs from a url string, {url,token} dict, or an
    iterable of either. Centralizes the browser's flexible connect() input."""
    if isinstance(spec, (str, dict)):
        yield _one_spec(spec)
        return
    for item in spec:
        yield _one_spec(item)
