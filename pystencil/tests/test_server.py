from __future__ import annotations

# Unit tests for the collaboration-server client. They are pure: URL
# normalization, header/body construction and the high-level request builder
# are exercised WITHOUT any network, so the suite never needs a running server.

import json
import unittest

from pystencil.server import (
    ConnectionManager,
    ServerConnection,
    ServerError,
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


if __name__ == "__main__":
    unittest.main()
