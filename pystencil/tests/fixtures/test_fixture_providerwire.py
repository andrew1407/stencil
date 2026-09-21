"""LlmClient request building and reply/error extraction over the shared wire corpus.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.helpers.fixturebase`.
"""

from __future__ import annotations

import io
import json
import unittest
import urllib.error

from tests.helpers.fixturebase import _LLM_FIXTURES, _OVERRIDES, _load

from pystencil.llm import LlmClient, LlmConfig, LlmError, _clean_detail

_WIRE_DIR = _LLM_FIXTURES / "providerWire"
_WIRE_PROVIDERS = {"ollama": "ollama", "openai": "openai-compat", "server": "stencil-server"}


def _wire_client(case) -> LlmClient:
  s = case["settings"]
  provider = _WIRE_PROVIDERS[case["provider"]]
  if provider == "stencil-server":
    cfg = LlmConfig(provider=provider, server_url=s["serverUrl"], model=s.get("model", ""))
    return LlmClient(cfg, token=case.get("token", ""))
  cfg = LlmConfig(
    provider=provider, base_url=s["baseUrl"], model=s.get("model", ""),
    api_key=s.get("apiKey", ""),
  )
  return LlmClient(cfg)


def _wire_messages(chat) -> list:
  return [
    {
      "role": m["role"],
      "text": m["text"],
      "images": [(i["mediaType"], i["data"]) for i in m.get("images", [])],
    }
    for m in chat["messages"]
  ]


def _http_error(case) -> urllib.error.HTTPError:
  er = case["errorResponse"]
  body = er["body"]
  raw = body.encode("utf-8") if isinstance(body, str) else json.dumps(body).encode("utf-8")
  return urllib.error.HTTPError(case["expectUrl"], er["status"], "err", {}, io.BytesIO(raw))


# Parsed once per module, not once per test method.
_CASES = [
  case
  for fn in ("ollama.json", "openai.json", "server.json", "httpErrors.json")
  for case in _load(_WIRE_DIR / fn)
]


class TestProviderWireFixtures(unittest.TestCase):
  def test_the_corpus_is_real(self):
    self.assertGreaterEqual(len(_CASES), 20)

  def test_request_building(self):
    # The builders are pure — no network, no seam patching needed.
    for case in _CASES:
      with self.subTest(case=case["name"]):
        client = _wire_client(case)
        req = client._build_request(_wire_messages(case["chat"]), case["chat"]["system"])
        self.assertEqual(req.full_url, case["expectUrl"])
        self.assertEqual(req.get_method(), "POST")
        # urllib stores header keys .capitalize()d ("Content-type").
        self.assertEqual(req.get_header("Content-type"), "application/json")
        # Deep equality: field ABSENCE is part of the contract.
        self.assertEqual(json.loads(req.data.decode("utf-8")), case["expectBody"])
        # Header absent in the fixture means it must not be sent.
        self.assertEqual(req.get_header("Authorization"), case.get("expectAuthorization"))

  def test_reply_and_error_extraction(self):
    overrides = _OVERRIDES["providerWire"]
    for case in _CASES:
      with self.subTest(case=case["name"]):
        client = _wire_client(case)
        self.assertNotIn(case["name"], overrides)  # no pinned wire divergences remain
        if "response" in case and "expectReply" in case:
          self.assertEqual(client._extract_reply(case["response"]), case["expectReply"])
        else:
          err = case["expectError"]
          if "response" in case:  # stopReason errors ride a 200 body
            with self.assertRaises(LlmError) as ctx:
              client._extract_reply(case["response"])
          else:
            with self.assertRaises(LlmError) as ctx:
              raise LlmClient._error_from(_http_error(case))
          e = ctx.exception
          # Kind mapping: pystencil types errors via code/stop_reason.
          kind = err["kind"]
          if kind == "disabled":
            self.assertEqual(e.code, "llmDisabled")
          elif kind == "truncated":
            self.assertEqual(e.stop_reason, "max_tokens")
          elif kind == "refusal":
            self.assertEqual(e.stop_reason, "refusal")
          elif kind == "badReply":
            # Same typed error; pystencil's own phrasing of the message.
            self.assertIsNone(e.stop_reason)
            self.assertIn("no reply text", e.message)
          else:
            self.assertEqual(kind, "http")
            self.assertIsNone(e.stop_reason)
          self.assertEqual(e.status, err.get("status"))
          # The schema lets a walker substitute its own sanitizer output; pystencil only reads the
          # server-shaped {code,message} error body.
          if "errorResponse" in case:
            body = case["errorResponse"]["body"]
            if isinstance(body, dict) and body.get("message"):
              want_msg = _clean_detail(body["message"])
            else:
              want_msg = "HTTP %d" % case["errorResponse"]["status"]
            self.assertEqual(e.message, want_msg)
          else:
            self.assertTrue(e.message)


if __name__ == "__main__":
  unittest.main()
