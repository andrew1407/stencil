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
    normalize_url,
)


class NormalizeUrlTest(unittest.TestCase):
    def test_adds_default_scheme(self) -> None:
        self.assertEqual(normalize_url("host:8090"), "http://host:8090")

    def test_strips_path_and_trailing_slash(self) -> None:
        self.assertEqual(normalize_url("http://host:8090/"), "http://host:8090")
        self.assertEqual(normalize_url("http://host:8090/projects/x"), "http://host:8090")

    def test_preserves_https_and_port(self) -> None:
        self.assertEqual(normalize_url("https://example.com:8443/api"), "https://example.com:8443")

    def test_trims_whitespace(self) -> None:
        self.assertEqual(normalize_url("  example.com  "), "http://example.com")

    def test_empty_raises(self) -> None:
        with self.assertRaises(ValueError):
            normalize_url("")
        with self.assertRaises(ValueError):
            normalize_url(None)


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

        def fake_open(req, raw=False):
            captured["method"] = req.get_method()
            captured["url"] = req.full_url
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {"id": "p1", "version": 3, "color": "#abcdef"}

        self.conn._open = fake_open
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

        def fake_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = fake_open
        self.conn.update_project("p1", layout={"lines": []}, version=5)
        self.assertNotIn("color", captured["body"])
        self.assertNotIn("name", captured["body"])
        self.assertEqual(captured["body"]["version"], 5)

    def test_update_project_clears_color_with_empty_string(self) -> None:
        # An explicit "" is a clear request and MUST be sent (it is not None).
        captured = {}

        def fake_open(req, raw=False):
            captured["body"] = json.loads(req.data.decode("utf-8"))
            return {}

        self.conn._open = fake_open
        self.conn.update_project("p1", color="", version=1)
        self.assertEqual(captured["body"]["color"], "")

    def test_get_project_color_reads_record(self) -> None:
        # get_project_color extracts ProjectRecord.color from the GET payload.
        def fake_open(req, raw=False):
            return {"project": {"id": "p1", "color": "#112233"}}

        self.conn._open = fake_open
        self.assertEqual(self.conn.get_project_color("p1"), "#112233")

    def test_get_project_color_defaults_to_empty(self) -> None:
        # A project with no color comes back as "" (theme fallback).
        def fake_open(req, raw=False):
            return {"project": {"id": "p1"}}

        self.conn._open = fake_open
        self.assertEqual(self.conn.get_project_color("p1"), "")

    def test_raw_body_is_octet_stream_with_query(self) -> None:
        payload = b"\x89PNGfakebytes"
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
        c = ServerConnection("http://a:8090", token="t")
        mgr._conns[c.base] = c
        self.assertEqual(mgr.connections, ["http://a:8090"])
        self.assertTrue(mgr.has("a:8090"))
        self.assertIs(mgr.get("http://a:8090/"), c)

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

        def fake_update(pid, layout=None, name=None, color=None, version=0):  # noqa: ANN001
            captured.update(pid=pid, name=name, version=version)
            return {"id": pid, "name": name, "version": version + 1}

        conn.update_project = fake_update  # type: ignore[method-assign]
        rec = conn.rename_project("p_1", "New Name")
        self.assertEqual(captured, {"pid": "p_1", "name": "New Name", "version": 7})
        self.assertEqual(rec["name"], "New Name")


if __name__ == "__main__":
    unittest.main()
