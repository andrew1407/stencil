"""The project description field, on create and on update."""

from __future__ import annotations

import json
import unittest

from pystencil.server import ServerConnection


class ProjectDescriptionTest(unittest.TestCase):
  """The project description field, on create and on update."""

  def setUp(self) -> None:
    self.conn = ServerConnection("http://host:8090", token="tok123")

  def test_update_project_body_includes_description(self) -> None:
    # description rides the PUT body like color/name (nil => unchanged contract).
    captured = dict()

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
    captured = dict()

    def stub_open(req, raw=False):
      captured["body"] = json.loads(req.data.decode("utf-8"))
      return {}

    self.conn._open = stub_open
    self.conn.update_project("p1", color="#fff", version=5)
    self.assertNotIn("description", captured["body"])

  def test_update_project_clears_description_with_empty_string(self) -> None:
    # An explicit "" is a clear request and MUST be sent (it is not None).
    captured = dict()

    def stub_open(req, raw=False):
      captured["body"] = json.loads(req.data.decode("utf-8"))
      return {}

    self.conn._open = stub_open
    self.conn.update_project("p1", description="", version=1)
    self.assertEqual(captured["body"]["description"], "")

  def test_set_project_description_reads_version_then_puts(self) -> None:
    # Mirrors rename_project: GET for the current version, then a version-guarded PUT.
    calls = list()

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
    captured = dict()

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
    captured = dict()

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
