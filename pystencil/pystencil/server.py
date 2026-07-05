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
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Iterable, Optional, Union


# Default port the collaboration server listens on (server/.env.example). Used
# only as documentation here; normalize_url keeps whatever port the caller gave.
DEFAULT_PORT = 8090


def is_loopback_host(host: Optional[str]) -> bool:
    """True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where
    plaintext http is safe because the bytes never leave the machine. Port of
    connectionManager.js isLoopbackHost."""
    if not host:
        return False
    h = host.lower().strip("[]")  # strip any IPv6 brackets
    if h == "localhost" or h.endswith(".localhost"):
        return True
    if h == "::1":
        return True
    parts = h.split(".")
    return len(parts) == 4 and parts[0] == "127" and all(
        # isascii(): str.isdigit alone accepts Unicode digits; the JS \d does not.
        p.isascii() and p.isdigit() and len(p) <= 3 for p in parts[1:]
    )


def normalize_url(raw: Optional[str]) -> str:
    """Turn 'host:8090' / 'http://host:8090/path' into a clean origin.

    Port of connectionManager.js normalizeUrl: trim, then keep only
    scheme://host[:port] (drop any path, query or fragment) so every connection
    is keyed by a stable origin. Secure by default: a bare REMOTE host gets
    https; loopback keeps http (dev servers run plaintext on localhost). An
    explicit scheme is preserved — the caller opts into cleartext.
    """
    s = str(raw if raw is not None else "").strip()
    if not s:
        raise ValueError("Server URL is required")
    if not s.lower().startswith(("http://", "https://")):
        host = urllib.parse.urlsplit("http://" + s).hostname
        s = ("http://" if is_loopback_host(host) else "https://") + s
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


# Project-metadata fields a watcher reports on. version is the server's monotonic
# edit counter (any save bumps it); name/color are the user-visible metadata. A change
# in any of these marks a project "updated" — the same fields the browser/desktop pick
# up when they reload a peer's change.
_WATCHED_FIELDS = ("version", "name", "color")
_FIELD_DEFAULT = {"version": 0, "name": "", "color": ""}

# Attempts for a version-guarded field write before giving up on sustained conflict
# (matches the CLI's putProjectField retry count).
_FIELD_WRITE_RETRIES = 4


def diff_projects(prev: list, curr: list) -> list:
    """Diff two `GET /projects` lists into project-change events. Pure (no network),
    so it's unit-tested without a server — the building block for poll-based watching.

    Returns a list of dicts ``{id, kind, fields, project}`` where ``kind`` is
    ``'created'`` | ``'updated'`` | ``'deleted'`` and ``fields`` lists which of
    name/color/version changed (only for 'updated'; empty for created/deleted).
    `project` is the current record (the prior record for a deletion).
    """
    prev_by = {p.get("id"): p for p in (prev or []) if p.get("id")}
    curr_by = {p.get("id"): p for p in (curr or []) if p.get("id")}
    changes: list = []
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
        baseline = []
    while not (stop is not None and stop.is_set()):
        if stop is not None:
            if stop.wait(interval):
                break
        else:
            time.sleep(interval)
        try:
            current = fetch()
        except Exception:
            continue  # transient error — keep the baseline, retry next tick
        for change in diff_projects(baseline, current):
            on_change(change)
        baseline = current


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
        expires_at: int | None = None,
        version: int = 0,
    ) -> dict:
        """PUT /projects/{id} → the updated ProjectRecord.

        version guards the last-writer-wins update; a stale version yields a
        409 which surfaces as ServerError(code="conflict"). `color` rides the
        same nil-means-unchanged contract as `name` (UpdateProjectRequest.Color
        is *string): pass "" to clear the custom accent, a "#rrggbb" hex to set
        it, or leave it None to keep the server's current value. `expires_at`
        (epoch ms; 0 = keep forever) follows the same contract via
        UpdateProjectRequest.ExpiresAt (*int64): leave it None to keep the
        current expiry.
        """
        body: dict[str, Any] = {"version": version}
        if name is not None:
            body["name"] = name
        if color is not None:
            body["color"] = color
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
            proj = self._project_record(pid)
            v = proj.get("version") if proj else None
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
        color: str | None = None,
    ) -> dict:
        """Version-guarded save-back (layout/name/color) plus optional result upload.

        Port of remoteSync.js saveRemoteProject: update → putFile('result'). A
        409 (lost last-writer-wins race) is surfaced as ServerError(code=
        "conflict"). Returns the refreshed project record. `color` follows the
        same nil-means-unchanged contract as `name`.
        """
        try:
            rec = self.update_project(pid, layout=layout, name=name, color=color, version=version)
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

    # ── aggregate project-change tracking (poll-based) ──
    # The session-wide analogue of ServerConnection.watch_projects: polls every connected
    # server and reports name/color/version changes across all of them, the way the
    # extension popup tracks its pinned projects as a set rather than one active project.
    def poll_project_changes(self, previous: list | None = None) -> tuple:
        """One-shot poll across every connection. Returns ``(current_list, changes)``
        (see diff_projects). Pass the prior list back to detect what moved."""
        current = self.remote_projects()
        return current, diff_projects(previous, current)

    def watch_projects(self, on_change, *, interval: float = 2.0, stop=None) -> None:
        """Block, polling every connected server every `interval` s, calling
        on_change(change) per project create/update/delete across all of them. The first
        poll seeds the baseline silently. Pass a threading.Event as `stop` to end it."""
        _poll_loop(self.remote_projects, on_change, interval, stop)


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
