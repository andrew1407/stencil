"""Request assembly: headers, JSON vs octet-stream bodies, and the file routes."""

from __future__ import annotations

import json
import unittest

from pystencil.server import ServerConnection

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
