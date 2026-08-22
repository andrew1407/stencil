from __future__ import annotations

# Unit tests for the collaboration-server client. They are pure: URL
# normalization, header/body construction and the high-level request builder
# are exercised WITHOUT any network, so the suite never needs a running server.

import json
import unittest

import threading

from pystencil.server import (
    ConnectionManager,
    ServerConnection,
    ServerError,
    diff_projects,
    is_loopback_host,
    normalize_url,
    split_invite_token,
)


class NormalizeUrlTest(unittest.TestCase):
    def test_secure_by_default_scheme(self) -> None:
        self.assertEqual(normalize_url("host:8090"), "https://host:8090")
        self.assertEqual(normalize_url("localhost:8090"), "http://localhost:8090")
        self.assertEqual(normalize_url("127.0.0.1:8090"), "http://127.0.0.1:8090")

    def test_is_loopback_host(self) -> None:
        self.assertTrue(is_loopback_host("localhost"))
        self.assertTrue(is_loopback_host("127.0.0.1"))
        self.assertTrue(is_loopback_host("::1"))
        self.assertFalse(is_loopback_host("example.com"))

    def test_strips_path_and_trailing_slash(self) -> None:
        self.assertEqual(normalize_url("http://host:8090/"), "http://host:8090")
        self.assertEqual(normalize_url("http://host:8090/projects/x"), "http://host:8090")

    def test_preserves_https_and_port(self) -> None:
        self.assertEqual(normalize_url("https://example.com:8443/api"), "https://example.com:8443")

    def test_trims_whitespace(self) -> None:
        self.assertEqual(normalize_url("  example.com  "), "https://example.com")

    def test_empty_raises(self) -> None:
        with self.assertRaises(ValueError):
            normalize_url("")
        with self.assertRaises(ValueError):
            normalize_url(None)


class SplitInviteTokenTest(unittest.TestCase):
    def test_fragment_parsed(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token=abc123"),
            ("http://localhost:8090", "abc123"),
        )

    def test_explicit_token_wins(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token=abc123", "explicit"),
            ("http://localhost:8090", "explicit"),
        )

    def test_fragmentless_url_unchanged(self) -> None:
        self.assertEqual(
            split_invite_token("https://host:8090", None),
            ("https://host:8090", None),
        )

    def test_empty_fragment_value_supplies_no_token(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token="),
            ("http://localhost:8090", None),
        )

    def test_connection_populates_token_and_credential(self) -> None:
        conn = ServerConnection("http://host:8090#token=abc123")
        self.assertEqual(conn.base, "http://host:8090")
        self.assertEqual(conn.token, "abc123")
        self.assertEqual(conn.credential, "abc123")

    def test_connection_explicit_token_wins(self) -> None:
        conn = ServerConnection("http://host:8090#token=abc123", token="explicit")
        self.assertEqual(conn.base, "http://host:8090")
        self.assertEqual(conn.token, "explicit")
        self.assertEqual(conn.credential, "explicit")


class BuildRequestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.conn = ServerConnection("http://host:8090", token="tok123")

    def test_get_has_no_body_and_bearer_header(self) -> None:
        req = self.conn._build_request("GET", "/projects")
        self.assertEqual(req.get_method(), "GET")
        self.assertEqual(req.full_url, "http://host:8090/projects")
        self.assertIsNone(req.data)
        self.assertEqual(req.get_header("Authorization"), "Bearer tok123")

    def test_json_body_is_encoded(self) -> None:
        req = self.conn._build_request("POST", "/projects", {"name": "Demo"})
        self.assertEqual(req.get_method(), "POST")
        self.assertEqual(req.get_header("Content-type"), "application/json")
        self.assertEqual(json.loads(req.data.decode("utf-8")), {"name": "Demo"})

    def test_token_override(self) -> None:
        req = self.conn._build_request("GET", "/projects", token="other")
        self.assertEqual(req.get_header("Authorization"), "Bearer other")

    def test_empty_token_still_sends_bearer(self) -> None:
        conn = ServerConnection("host:8090")  # no token yet
        req = conn._build_request("POST", "/auth/token", {})
        self.assertEqual(req.get_header("Authorization"), "Bearer ")
        self.assertEqual(json.loads(req.data.decode("utf-8")), {})

    def test_update_project_body_includes_color(self) -> None:
        # Capture the request update_project builds without hitting the network,
        # so we can assert the `color` field rides the PUT body like `name`.
        captured = {}

        def stub_open(req, raw=False):
            captured["method"] = req.get_method()
            captured["url"] = req.full_url
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 3, "color": "#abcdef"}

        self.conn._open = stub_open
        rec = self.conn.update_project("p1", name="Demo", color="#abcdef", version=2)
        self.assertEqual(captured["method"], "PUT")
        self.assertEqual(captured["url"], "http://host:8090/projects/p1")
        self.assertEqual(
            captured["body"],
            {"version": 2, "name": "Demo", "color": "#abcdef"},
        )
        self.assertEqual(rec["color"], "#abcdef")

    def test_update_project_omits_color_when_none(self) -> None:
        # color=None must NOT appear in the body (nil => unchanged, like name).
        captured = {}

        def stub_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = stub_open
        self.conn.update_project("p1", layout={"lines": []}, version=5)
        self.assertNotIn("color", captured["body"])
        self.assertNotIn("name", captured["body"])
        self.assertEqual(captured["body"]["version"], 5)

    def test_update_project_clears_color_with_empty_string(self) -> None:
        # An explicit "" is a clear request and MUST be sent (it is not None).
        captured = {}

        def stub_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = stub_open
        self.conn.update_project("p1", color="", version=1)
        self.assertEqual(captured["body"]["color"], "")

    def test_update_project_includes_expires_at(self) -> None:
        # expires_at rides the PUT body like color/name; None omits it, 0 is sent (clear).
        captured = {}

        def stub_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 4, "expiresAt": 5000}

        self.conn._open = stub_open
        rec = self.conn.update_project("p1", expires_at=5000, version=3)
        self.assertEqual(captured["body"], {"version": 3, "expiresAt": 5000})
        self.assertEqual(rec["expiresAt"], 5000)

        # None omits it (keep current); explicit 0 (keep forever) is still sent.
        self.conn.update_project("p1", layout={"lines": []}, version=1)
        self.assertNotIn("expiresAt", captured["body"])
        self.conn.update_project("p1", expires_at=0, version=2)
        self.assertEqual(captured["body"]["expiresAt"], 0)

    def test_set_project_expiration_reads_version_then_puts(self) -> None:
        # Mirrors rename_project: GET for the current version, then PUT expiresAt.
        calls = []

        def stub_open(req, raw=False):
            calls.append((req.get_method(), req.full_url))
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": 7}}
            body = json.loads(req.data.decode("utf-8"))
            self.assertEqual(body, {"version": 7, "expiresAt": 9000})
            return {"id": "p1", "version": 8, "expiresAt": 9000}

        self.conn._open = stub_open
        rec = self.conn.set_project_expiration("p1", 9000)
        self.assertEqual(rec["expiresAt"], 9000)
        self.assertEqual(calls[0][0], "GET")
        self.assertEqual(calls[-1][0], "PUT")

    def test_get_project_expiration_reads_record(self) -> None:
        self.conn._open = lambda req, raw=False: {"project": {"id": "p1", "expiresAt": 4242}}
        self.assertEqual(self.conn.get_project_expiration("p1"), 4242)
        # Missing → 0 (keep forever).
        self.conn._open = lambda req, raw=False: {"project": {"id": "p1"}}
        self.assertEqual(self.conn.get_project_expiration("p1"), 0)

    def test_field_write_retries_on_conflict_then_succeeds(self) -> None:
        # A stale-version 409 (a peer saved between our read and PUT) is recovered:
        # re-read the version and retry, so the change isn't silently dropped.
        versions = iter([5, 6])  # GET returns v5, then v6 after the conflict
        attempts = {"put": 0}

        def stub_open(req, raw=False):
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": next(versions)}}
            attempts["put"] += 1
            body = json.loads(req.data.decode("utf-8"))
            if attempts["put"] == 1:
                self.assertEqual(body["version"], 5)          # stale → conflict
                raise ServerError("conflict", "stale version", status=409)
            self.assertEqual(body["version"], 6)              # retried with the fresh version
            return {"id": "p1", "version": 7, "expiresAt": 9000}

        self.conn._open = stub_open
        rec = self.conn.set_project_expiration("p1", 9000)
        self.assertEqual(rec["expiresAt"], 9000)
        self.assertEqual(attempts["put"], 2)                  # retried exactly once

    def test_field_write_gives_up_after_sustained_conflict(self) -> None:
        # Never-winning contention surfaces the conflict (not a silent no-op).
        def stub_open(req, raw=False):
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": 1}}
            raise ServerError("conflict", "stale version", status=409)

        self.conn._open = stub_open
        with self.assertRaises(ServerError) as ctx:
            self.conn.rename_project("p1", "nope")
        self.assertEqual(ctx.exception.code, "conflict")

    def test_field_write_reraises_non_conflict_errors(self) -> None:
        # A non-conflict error (e.g. notFound) is not retried — it propagates immediately.
        calls = {"put": 0}

        def stub_open(req, raw=False):
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": 1}}
            calls["put"] += 1
            raise ServerError("notFound", "gone", status=404)

        self.conn._open = stub_open
        with self.assertRaises(ServerError) as ctx:
            self.conn.set_project_expiration("p1", 5)
        self.assertEqual(ctx.exception.code, "notFound")
        self.assertEqual(calls["put"], 1)                     # no retry on non-conflict

    def test_get_project_color_reads_record(self) -> None:
        # get_project_color extracts ProjectRecord.color from the GET payload.
        def stub_open(req, raw=False):
            return {"project": {"id": "p1", "color": "#112233"}}

        self.conn._open = stub_open
        self.assertEqual(self.conn.get_project_color("p1"), "#112233")

    def test_get_project_color_defaults_to_empty(self) -> None:
        # A project with no color comes back as "" (theme fallback).
        def stub_open(req, raw=False):
            return {"project": {"id": "p1"}}

        self.conn._open = stub_open
        self.assertEqual(self.conn.get_project_color("p1"), "")

    def test_update_project_body_includes_description(self) -> None:
        # description rides the PUT body like color/name (nil => unchanged contract).
        captured = {}

        def stub_open(req, raw=False):
            captured["method"] = req.get_method()
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 3, "description": "a note"}

        self.conn._open = stub_open
        rec = self.conn.update_project("p1", description="a note", version=2)
        self.assertEqual(captured["method"], "PUT")
        self.assertEqual(captured["body"], {"version": 2, "description": "a note"})
        self.assertEqual(rec["description"], "a note")

    def test_update_project_omits_description_when_none(self) -> None:
        # description=None must NOT appear (keep the server's current value).
        captured = {}

        def stub_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = stub_open
        self.conn.update_project("p1", color="#fff", version=5)
        self.assertNotIn("description", captured["body"])

    def test_update_project_clears_description_with_empty_string(self) -> None:
        # An explicit "" is a clear request and MUST be sent (it is not None).
        captured = {}

        def stub_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = stub_open
        self.conn.update_project("p1", description="", version=1)
        self.assertEqual(captured["body"]["description"], "")

    def test_set_project_description_reads_version_then_puts(self) -> None:
        # Mirrors rename_project: GET for the current version, then a version-guarded PUT.
        calls = []

        def stub_open(req, raw=False):
            calls.append(req.get_method())
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": 4}}
            body = json.loads(req.data.decode("utf-8"))
            self.assertEqual(body, {"version": 4, "description": "note"})
            return {"id": "p1", "version": 5, "description": "note"}

        self.conn._open = stub_open
        rec = self.conn.set_project_description("p1", "note")
        self.assertEqual(rec["description"], "note")
        self.assertEqual(calls[0], "GET")
        self.assertEqual(calls[-1], "PUT")

    def test_set_project_description_clears_with_empty(self) -> None:
        # Clearing sends "" under the version guard (not a silent no-op).
        captured = {}

        def stub_open(req, raw=False):
            if req.get_method() == "GET":
                return {"project": {"id": "p1", "version": 2}}
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 3, "description": ""}

        self.conn._open = stub_open
        self.conn.set_project_description("p1", "")
        self.assertEqual(captured["body"], {"version": 2, "description": ""})

    def test_get_project_description_reads_record(self) -> None:
        # get_project_description extracts ProjectRecord.description; missing → "".
        self.conn._open = lambda req, raw=False: {"project": {"id": "p1", "description": "hello"}}
        self.assertEqual(self.conn.get_project_description("p1"), "hello")
        self.conn._open = lambda req, raw=False: {"project": {"id": "p1"}}
        self.assertEqual(self.conn.get_project_description("p1"), "")

    def test_create_project_includes_description_when_given(self) -> None:
        # create_project passes description through; a None value is dropped (server default).
        captured = {}

        def stub_open(req, raw=False):
            captured["method"] = req.get_method()
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 1}

        self.conn._open = stub_open
        self.conn.create_project(name="Demo", description="a note")
        self.assertEqual(captured["method"], "POST")
        self.assertEqual(captured["body"], {"name": "Demo", "description": "a note"})
        self.conn.create_project(name="Demo", description=None)
        self.assertNotIn("description", captured["body"])

    def test_raw_body_is_octet_stream_with_query(self) -> None:
        payload = b"\x89PNGstubbytes"
        req = self.conn._build_request(
            "POST",
            "/projects/p1/files/original",
            payload,
            raw=True,
            query={"ext": "png", "w": "320", "h": "240"},
        )
        self.assertEqual(req.get_header("Content-type"), "application/octet-stream")
        self.assertEqual(req.data, payload)
        self.assertEqual(
            req.full_url,
            "http://host:8090/projects/p1/files/original?ext=png&w=320&h=240",
        )

    def test_delete_file_sends_delete_and_returns_none(self) -> None:
        # delete_file drops a filestore-only kind (video/variantN/chat) with the
        # §9 per-file DELETE; the idempotent 204 comes back as None.
        captured = {}

        def stub_open(req, raw=False):
            captured["method"] = req.get_method()
            captured["url"] = req.full_url
            return None  # what _open yields for a 204 No Content

        self.conn._open = stub_open
        self.assertIsNone(self.conn.delete_file("p1", "chat"))
        self.assertEqual(captured["method"], "DELETE")
        self.assertEqual(captured["url"], "http://host:8090/projects/p1/files/chat")


class CredentialRetentionTest(unittest.TestCase):
    """Credential retention + mid-session re-mint (port of extension
    connections.js req()/connect(): the user-supplied value outlives the
    session token, so a server restart/DB wipe is recovered in place)."""

    def _stubbed(self, token, handler, url="http://host:8090"):
        # A connection whose _open is replaced by `handler(bearer, method, path)`;
        # returns (conn, calls) with each call recorded as (bearer, method, path).
        conn = ServerConnection(url, token=token)
        calls = []

        def stub_open(req, raw=False):
            bearer = (req.get_header("Authorization") or "").removeprefix("Bearer ")
            path = req.full_url[len("http://host:8090"):]
            calls.append((bearer, req.get_method(), path))
            return handler(bearer, req.get_method(), path)

        conn._open = stub_open
        return conn, calls

    def test_connect_keeps_credential_and_admin_mints(self) -> None:
        # The supplied value is the ADMIN token: probe fails 401, mint succeeds,
        # and the original value is retained as `credential`.
        def handler(bearer, method, path):
            if path == "/auth/token":
                return {"token": "sess1"}
            if bearer != "sess1":
                raise ServerError("unauthorized", "bad token", status=401)
            return {"projects": []}

        conn, calls = self._stubbed("admintok", handler)
        conn.connect()
        self.assertEqual(conn.status, "connected")
        self.assertEqual(conn.token, "sess1")
        self.assertEqual(conn.credential, "admintok")
        self.assertEqual(calls, [
            ("admintok", "GET", "/projects"),
            ("admintok", "POST", "/auth/token"),
            ("sess1", "GET", "/projects"),
        ])

    def test_invite_fragment_token_feeds_credential_mint(self) -> None:
        # An invite link's fragment token is the ADMIN credential: probe 401s,
        # the mint uses it, and it is retained as `credential` for re-mints.
        def handler(bearer, method, path):
            if path == "/auth/token":
                return {"token": "sess1"}
            if bearer != "sess1":
                raise ServerError("unauthorized", "bad token", status=401)
            return {"projects": []}

        conn, calls = self._stubbed(None, handler, url="http://host:8090#token=admintok")
        conn.connect()
        self.assertEqual(conn.status, "connected")
        self.assertEqual(conn.token, "sess1")
        self.assertEqual(conn.credential, "admintok")
        self.assertEqual(calls, [
            ("admintok", "GET", "/projects"),
            ("admintok", "POST", "/auth/token"),
            ("sess1", "GET", "/projects"),
        ])

    def test_connect_probe_500_propagates_without_minting(self) -> None:
        def handler(bearer, method, path):
            raise ServerError("internal", "boom", status=500)

        conn, calls = self._stubbed("tok", handler)
        with self.assertRaises(ServerError) as cm:
            conn.connect()
        self.assertEqual(cm.exception.status, 500)
        self.assertEqual(conn.status, "error")
        self.assertEqual(calls, [("tok", "GET", "/projects")])  # no mint attempt

    def test_connect_statusless_error_still_falls_back_to_mint(self) -> None:
        # A ServerError without an HTTP status keeps the pre-existing behavior
        # (mint fallback) rather than guessing it was a 500.
        def handler(bearer, method, path):
            if path == "/auth/token":
                return {"token": "sess1"}
            if bearer != "sess1":
                raise ServerError("", "opaque failure", status=None)
            return {"projects": []}

        conn, _ = self._stubbed("tok", handler)
        conn.connect()
        self.assertEqual(conn.token, "sess1")

    def test_stale_session_re_mints_once_and_retries(self) -> None:
        # Mid-session 401 (server restarted): one re-mint with the retained
        # credential, then the original request is retried in place.
        def handler(bearer, method, path):
            if path == "/auth/token":
                self.assertEqual(bearer, "admintok")
                return {"token": "sess2"}
            if bearer != "sess2":
                raise ServerError("unauthorized", "expired", status=401)
            return {"projects": [{"id": "p1"}]}

        conn, calls = self._stubbed("admintok", handler)
        conn.token = "stale"  # session token from before the wipe
        self.assertEqual(conn.list_projects(), [{"id": "p1"}])
        self.assertEqual(conn.token, "sess2")
        self.assertEqual(calls, [
            ("stale", "GET", "/projects"),
            ("admintok", "POST", "/auth/token"),
            ("sess2", "GET", "/projects"),
        ])

    def test_stale_session_without_credential_propagates(self) -> None:
        def handler(bearer, method, path):
            raise ServerError("unauthorized", "expired", status=401)

        conn, calls = self._stubbed(None, handler)  # nothing supplied
        conn.token = "stale"
        with self.assertRaises(ServerError):
            conn.list_projects()
        self.assertEqual(len(calls), 1)  # no mint, no retry

    def test_failed_re_mint_surfaces_original_error_no_loop(self) -> None:
        # The re-mint itself failing must surface the ORIGINAL 401, after
        # exactly one mint attempt (no retry loops on /auth/token).
        def handler(bearer, method, path):
            if path == "/auth/token":
                raise ServerError("forbidden", "mint refused", status=403)
            raise ServerError("unauthorized", "expired", status=401)

        conn, calls = self._stubbed("admintok", handler)
        conn.token = "stale"
        with self.assertRaises(ServerError) as cm:
            conn.list_projects()
        self.assertEqual(cm.exception.status, 401)
        self.assertEqual(cm.exception.message, "expired")
        self.assertEqual([c[2] for c in calls], ["/projects", "/auth/token"])

    def test_non_auth_error_does_not_re_mint(self) -> None:
        def handler(bearer, method, path):
            raise ServerError("notFound", "gone", status=404)

        conn, calls = self._stubbed("admintok", handler)
        with self.assertRaises(ServerError):
            conn.get_project("p1")
        self.assertEqual(len(calls), 1)


class ErrorParsingTest(unittest.TestCase):
    def test_server_error_fields(self) -> None:
        err = ServerError("conflict", "stale version", status=409)
        self.assertEqual(err.code, "conflict")
        self.assertEqual(err.message, "stale version")
        self.assertEqual(err.status, 409)


class ConnectionManagerUnitTest(unittest.TestCase):
    def test_normalizes_and_dedupes(self) -> None:
        mgr = ConnectionManager()
        # Directly seed the internal map (no network) to exercise the views.
        c = ServerConnection("https://a:8090", token="t")
        mgr._conns[c.base] = c
        self.assertEqual(mgr.connections, ["https://a:8090"])
        self.assertTrue(mgr.has("a:8090"))
        self.assertIs(mgr.get("https://a:8090/"), c)

    def test_disconnect_last(self) -> None:
        mgr = ConnectionManager()
        for url in ("http://a:8090", "http://b:8090"):
            conn = ServerConnection(url)
            mgr._conns[conn.base] = conn
        mgr.disconnect()  # drops most-recently added
        self.assertEqual(mgr.connections, ["http://a:8090"])


class DiffProjectsTest(unittest.TestCase):
    """The pure diff that powers poll-based project-change watching."""

    def test_empty_inputs(self) -> None:
        self.assertEqual(diff_projects(None, None), [])
        self.assertEqual(diff_projects([], []), [])

    def test_created(self) -> None:
        curr = [{"id": "a", "name": "A", "color": "", "version": 1}]
        changes = diff_projects([], curr)
        self.assertEqual(len(changes), 1)
        self.assertEqual(changes[0]["kind"], "created")
        self.assertEqual(changes[0]["fields"], [])
        self.assertEqual(changes[0]["project"], curr[0])

    def test_deleted_carries_prior_record(self) -> None:
        prev = [{"id": "a", "name": "A", "version": 1}]
        changes = diff_projects(prev, [])
        self.assertEqual(changes, [{"id": "a", "kind": "deleted", "fields": [], "project": prev[0]}])

    def test_unchanged_emits_nothing(self) -> None:
        same = [{"id": "a", "name": "A", "color": "", "version": 1}]
        self.assertEqual(diff_projects(same, list(same)), [])

    def test_color_only_change_reports_color_and_version(self) -> None:
        prev = [{"id": "a", "name": "A", "color": "", "version": 1}]
        curr = [{"id": "a", "name": "A", "color": "#80868f", "version": 2}]
        changes = diff_projects(prev, curr)
        self.assertEqual(changes[0]["kind"], "updated")
        self.assertEqual(set(changes[0]["fields"]), {"color", "version"})

    def test_rename_reports_name(self) -> None:
        prev = [{"id": "a", "name": "A", "color": "#fff", "version": 1}]
        curr = [{"id": "a", "name": "Renamed", "color": "#fff", "version": 2}]
        changes = diff_projects(prev, curr)
        self.assertEqual(set(changes[0]["fields"]), {"name", "version"})

    def test_description_only_change_reports_description(self) -> None:
        # description is a watched field (new _WATCHED_FIELDS entry): a peer's edit surfaces it.
        prev = [{"id": "a", "name": "A", "color": "", "description": "", "version": 1}]
        curr = [{"id": "a", "name": "A", "color": "", "description": "now set", "version": 2}]
        changes = diff_projects(prev, curr)
        self.assertEqual(changes[0]["kind"], "updated")
        self.assertEqual(set(changes[0]["fields"]), {"description", "version"})

    def test_description_change_detected_when_field_absent_before(self) -> None:
        # A record missing the key defaults to "", so adding a description reports it even
        # when the version counter didn't move in the two snapshots we diffed.
        prev = [{"id": "a", "name": "A", "version": 1}]
        curr = [{"id": "a", "name": "A", "description": "x", "version": 1}]
        changes = diff_projects(prev, curr)
        self.assertEqual(set(changes[0]["fields"]), {"description"})


class WatchProjectsTest(unittest.TestCase):
    """poll_project_changes (one-shot) + watch_projects (blocking loop), driven by a
    monkeypatched list_projects so the suite stays network-free."""

    def test_poll_project_changes_one_shot(self) -> None:
        conn = ServerConnection("http://h:8090", token="t")
        snap = [{"id": "a", "name": "A", "color": "", "version": 1}]
        conn.list_projects = lambda: snap  # type: ignore[method-assign]
        current, changes = conn.poll_project_changes(previous=[])
        self.assertEqual(current, snap)
        self.assertEqual(changes[0]["kind"], "created")
        # Feeding the snapshot back reports no further change.
        _, changes2 = conn.poll_project_changes(previous=current)
        self.assertEqual(changes2, [])

    def test_watch_projects_seeds_baseline_then_fires(self) -> None:
        conn = ServerConnection("http://h:8090", token="t")
        seq = [
            [{"id": "a", "name": "A", "color": "", "version": 1}],       # baseline (silent)
            [{"id": "a", "name": "A", "color": "#000", "version": 2}],   # a colour change
        ]
        n = {"i": 0}

        def fetch() -> list:
            i = min(n["i"], len(seq) - 1)
            n["i"] += 1
            return seq[i]

        conn.list_projects = fetch  # type: ignore[method-assign]
        stop = threading.Event()
        received: list = []

        def on_change(change) -> None:
            received.append(change)
            stop.set()  # end the loop after the first change

        conn.watch_projects(on_change, interval=0.01, stop=stop)
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0]["kind"], "updated")
        self.assertEqual(set(received[0]["fields"]), {"color", "version"})


class RenameProjectTest(unittest.TestCase):
    def test_rename_reads_version_then_puts_name(self) -> None:
        conn = ServerConnection("http://h:8090", token="t")
        conn._current_version = lambda pid, fb: 7  # type: ignore[method-assign]
        captured: dict = {}

        def stub_update(pid, layout=None, name=None, color=None, version=0):  # noqa: ANN001
            captured.update(pid=pid, name=name, version=version)
            return {"id": pid, "name": name, "version": version + 1}

        conn.update_project = stub_update  # type: ignore[method-assign]
        rec = conn.rename_project("p_1", "New Name")
        self.assertEqual(captured, {"pid": "p_1", "name": "New Name", "version": 7})
        self.assertEqual(rec["name"], "New Name")


if __name__ == "__main__":
    unittest.main()
