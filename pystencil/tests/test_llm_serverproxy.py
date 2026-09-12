"""Contract §6.3: chatting through a collaboration server's /llm/chat proxy."""

from __future__ import annotations

import io
import json
import unittest
import urllib.error

import pystencil.llm as llm_module
from pystencil.llm import LLM_SYSTEM_PROMPT, LlmClient, LlmConfig, LlmError
from pystencil.server import ServerConnection


class StencilServerRequestTest(unittest.TestCase):
  """Contract §6.3: POST {serverUrl}/llm/chat with the existing session token."""

  def _client(self, model: str = "") -> LlmClient:
    return LlmClient(
      LlmConfig(
        provider="stencil-server", model=model, server_url="https://srv:8090"
      ),
      token="tok123",
    )

  def test_request_shape(self) -> None:
    msgs = [
      {"role": "user", "text": "hi", "images": [("image/png", b"hi")]},
      {"role": "assistant", "text": "prior reply"},
    ]
    req = self._client()._build_request(msgs)
    self.assertEqual(req.full_url, "https://srv:8090/llm/chat")
    self.assertEqual(req.get_header("Authorization"), "Bearer tok123")
    self.assertEqual(req.get_header("Content-type"), "application/json")
    body = json.loads(req.data.decode("utf-8"))
    self.assertEqual(body["system"], LLM_SYSTEM_PROMPT)
    self.assertEqual(
      body["messages"][0],
      {
        "role": "user",
        "text": "hi",
        "images": [{"mediaType": "image/png", "data": "aGk="}],
      },
    )
    self.assertEqual(body["messages"][1], {"role": "assistant", "text": "prior reply"})
    self.assertNotIn("model", body)  # empty model = server default (omitted)

  def test_model_included_when_set(self) -> None:
    req = self._client(model="claude-opus-5")._build_request(
      [{"role": "user", "text": "hi"}]
    )
    self.assertEqual(json.loads(req.data.decode("utf-8"))["model"], "claude-opus-5")

  def test_reads_url_and_token_from_server_connection(self) -> None:
    conn = ServerConnection("http://host:8090", token="sess-tok")
    client = LlmClient(LlmConfig(provider="stencil-server"), server=conn)
    req = client._build_request([{"role": "user", "text": "hi"}])
    self.assertEqual(req.full_url, "http://host:8090/llm/chat")
    self.assertEqual(req.get_header("Authorization"), "Bearer sess-tok")

  def test_missing_server_url_raises(self) -> None:
    client = LlmClient(LlmConfig(provider="stencil-server"))
    with self.assertRaises(LlmError):
      client._build_request([{"role": "user", "text": "hi"}])

  def test_chat_extracts_text_on_end_turn(self) -> None:
    client = self._client()
    client._open = lambda req: {
      "model": "claude-opus-5",
      "text": "the answer",
      "stopReason": "end_turn",
    }
    self.assertEqual(client.chat([{"role": "user", "text": "go"}]), "the answer")

  def test_max_tokens_stop_reason_is_typed_error(self) -> None:
    # A truncated reply must never be parsed as a plan (contract §6.3).
    client = self._client()
    client._open = lambda req: {
      "text": '{"version":1,"reply":"trunca',
      "stopReason": "max_tokens",
    }
    with self.assertRaises(LlmError) as ctx:
      client.chat([{"role": "user", "text": "go"}])
    self.assertEqual(ctx.exception.stop_reason, "max_tokens")

  def test_refusal_stop_reason_is_typed_error(self) -> None:
    client = self._client()
    client._open = lambda req: {"text": "no.", "stopReason": "refusal"}
    with self.assertRaises(LlmError) as ctx:
      client.chat([{"role": "user", "text": "go"}])
    self.assertEqual(ctx.exception.stop_reason, "refusal")

  def test_error_from_parses_code_and_message(self) -> None:
    # The server's 503 {"code":"llmDisabled",...} surfaces code/status/message.
    e = urllib.error.HTTPError(
      "https://srv:8090/llm/chat",
      503,
      "Service Unavailable",
      {},
      io.BytesIO(b'{"code":"llmDisabled","message":"no ANTHROPIC_API_KEY"}'),
    )
    err = LlmClient._error_from(e)
    self.assertEqual(err.code, "llmDisabled")
    self.assertEqual(err.status, 503)
    self.assertIn("no ANTHROPIC_API_KEY", err.message)

  def test_error_from_tolerates_non_json_body(self) -> None:
    e = urllib.error.HTTPError("http://x", 502, "Bad Gateway", {}, io.BytesIO(b"<html>"))
    err = LlmClient._error_from(e)
    self.assertEqual(err.status, 502)
    self.assertEqual(err.message, "HTTP 502")

  def test_error_from_says_the_reason_once(self) -> None:
    # Contract §6.3: the server's message IS the text — the machine code stays on
    # the exception, not in front of the sentence the console prints.
    body = (
      b'{"code":"llmUpstream","message":"the LLM provider is out of credits or '
      b'has no active billing"}'
    )
    e = urllib.error.HTTPError("http://s:8090/llm/chat", 502, "Bad Gateway", {}, io.BytesIO(body))
    err = LlmClient._error_from(e)
    self.assertEqual(
      err.message, "the LLM provider is out of credits or has no active billing"
    )
    self.assertEqual(err.code, "llmUpstream")  # still there for callers
    self.assertNotIn("llmUpstream", str(err))
    self.assertNotIn("502", str(err))

  def test_clean_detail_bounds_and_redacts_provider_prose(self) -> None:
    clean = llm_module._clean_detail
    self.assertEqual(clean("model\nnot\tfound"), "model not found")
    self.assertEqual(
      clean("Incorrect API key provided: sk-abcdef1234567890"),
      "Incorrect API key provided: [redacted]",
    )
    self.assertEqual(
      clean("failed to reach http://10.0.0.5:11434/api/chat now"),
      "failed to reach [redacted] now",
    )
    cut = clean("the model is very busy right now. " * 30)
    self.assertLessEqual(len(cut), 200)
    self.assertTrue(cut.endswith("…"))
