"""Project colour and expiration writes, including the version-guarded retry."""

from __future__ import annotations

import json
import unittest

from pystencil.server import ServerConnection, ServerError


class ProjectMetadataTest(unittest.TestCase):
    """Project colour and expiration writes, and the version-guarded retry."""

    def setUp(self) -> None:
        self.conn = ServerConnection("http://host:8090", token="tok123")


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
