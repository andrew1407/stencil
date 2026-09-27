"""The direct ``anthropic`` wire (contract §6.5) over the shared providerWire corpus.

Every case runs through ``LlmClient.chat`` against a capturing transport at the one
network call (``_http_open``), so the request, the headers, the reply and the typed error
are the ones a real turn produces; ``expectNoRequest`` cases prove nothing was sent.
"""

from __future__ import annotations

import json
import unittest
from unittest import mock

import pystencil.llm.client as client_mod
from pystencil.llm import LlmError
from tests.fixtures.test_fixture_providerwire import (
  _WALKED, _WIRE_DIR, _http_error, _wire_client, _wire_messages,
)
from tests.helpers.fixturebase import _OVERRIDES, _load

_CASES = _load(_WIRE_DIR / "anthropic.json")
_BROWSER_ONLY = "anthropic-dangerous-direct-browser-access"


class _Capture:
  """Stands in for ``_http_open``: records each request, answers with the case's reply."""

  def __init__(self, case) -> None:
    self.case = case
    self.sent: list = list()

  def __call__(self, req, error_from, *, context=None, timeout=None):
    self.sent.append(req)
    if "errorResponse" in self.case: raise error_from(_http_error(self.case))
    return 200, json.dumps(self.case["response"]).encode("utf-8")


def _turn(case):
  """One chat turn for ``case``: (requests sent, reply or None, LlmError or None)."""
  capture = _Capture(case)
  client = _wire_client(case)
  with mock.patch.object(client_mod, "_http_open", capture):
    try:
      return capture.sent, client.chat(_wire_messages(case["chat"]), case["chat"]["system"]), None
    except LlmError as e:
      return capture.sent, None, e


class TestAnthropicWireFixtures(unittest.TestCase):
  def test_the_corpus_is_real_and_every_file_is_walked(self):
    self.assertGreaterEqual(len(_CASES), 19)
    on_disk = {p.name for p in _WIRE_DIR.glob("*.json")}
    self.assertEqual(on_disk, set(_WALKED) | {"anthropic.json"})

  def test_requests(self):
    for case in _CASES:
      with self.subTest(case=case["name"]):
        self.assertNotIn(case["name"], _OVERRIDES["providerWire"])
        sent, _reply, _err = _turn(case)
        if case.get("expectNoRequest"):
          self.assertEqual(sent, [])
          continue
        self.assertEqual(len(sent), 1)
        req = sent[0]
        self.assertEqual(req.full_url, case["expectUrl"])
        self.assertEqual(req.get_method(), "POST")
        self.assertEqual(req.get_header("Content-type"), "application/json")
        self.assertEqual(json.loads(req.data.decode("utf-8")), case["expectBody"])
        self.assertEqual(req.get_header("Authorization"), case.get("expectAuthorization"))
        # urllib stores header names .capitalize()d; the corpus spells them lower-case.
        for name, value in case["expectHeaders"].items():
          self.assertEqual(req.get_header(name.capitalize()), value)
        self.assertNotIn(_BROWSER_ONLY, {k.lower() for k, _v in req.header_items()})

  def test_replies_and_typed_errors(self):
    for case in _CASES:
      with self.subTest(case=case["name"]):
        _sent, reply, e = _turn(case)
        if "expectReply" in case:
          self.assertIsNone(e)
          self.assertEqual(reply, case["expectReply"])
          continue
        want = case["expectError"]
        self.assertIsNotNone(e, "expected a %s error" % want["kind"])
        self.assertEqual(e.status, want.get("status"))
        kind = want["kind"]
        if kind == "truncated":
          # Same kind; pystencil's own wording, shared with the stencil-server wire.
          self.assertEqual(e.stop_reason, "max_tokens")
          self.assertTrue(e.message.startswith("response truncated"))
        elif kind == "badReply":
          self.assertIsNone(e.stop_reason)
          self.assertIn("no reply text", e.message)
        else:
          if kind == "disabled": self.assertEqual(e.code, "llmDisabled")
          if kind == "refusal": self.assertEqual(e.stop_reason, "refusal")
          if kind == "http": self.assertIsNone(e.stop_reason)
          self.assertEqual(e.message, want["message"])
        key = case["settings"].get("apiKey", "")
        for i in range(max(len(key) - 7, 0)):
          self.assertNotIn(key[i:i + 8], e.message)


if __name__ == "__main__":
  unittest.main()
