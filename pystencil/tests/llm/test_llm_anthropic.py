"""The direct ``anthropic`` wire (contract §6.5) end to end over a loopback ``/v1/messages``:
what reaches the wire, a reply, a classified failure, a refused redirect, and a missing or
expired session key or a plain-http host off loopback that sends nothing.
"""

from __future__ import annotations

import unittest
from unittest import mock

import pystencil.llm.client as client_mod
from pystencil.llm import LlmClient, LlmConfig, LlmError
from pystencil.llm.config import ANTHROPIC_VERSION, DEFAULT_MODEL, KEY_TTL_SECONDS, MAX_TOKENS
from tests.helpers.anthropicmock import FAKE_KEY, AnthropicMockCase, FakeClock, text_reply
from tests.server.test_server_redirect import _Bouncer, _Recorder, _serve


def _client(url: str, key: str = FAKE_KEY, clock=None) -> LlmClient:
  kw = {"clock": clock} if clock is not None else {}
  return LlmClient(LlmConfig(provider="anthropic", base_url=url, api_key=key, **kw))


class DirectTurnTest(AnthropicMockCase):
  def test_a_turn_reaches_the_messages_api_and_returns_its_text(self):
    reply = _client(self.url).chat([{"role": "user", "text": "hi"}], "sys")
    self.assertEqual(reply, "hello from the mock")
    self.assertEqual(len(self.seen), 1)
    path, headers, body = self.seen[0]
    self.assertEqual(path, "/v1/messages")
    self.assertEqual(headers["x-api-key"], FAKE_KEY)
    self.assertEqual(headers["anthropic-version"], ANTHROPIC_VERSION)
    self.assertEqual(headers["content-type"], "application/json")
    self.assertNotIn("authorization", headers)
    self.assertNotIn("anthropic-dangerous-direct-browser-access", headers)
    self.assertEqual(body["model"], DEFAULT_MODEL)
    self.assertEqual(body["max_tokens"], MAX_TOKENS)
    self.assertEqual(body["system"], "sys")

  def test_a_failure_says_why_in_the_users_terms(self):
    self.answer(402, {"type": "error", "error": {"type": "billing_error", "message": "pay up"}})
    with self.assertRaises(LlmError) as caught:
      _client(self.url).chat([{"role": "user", "text": "hi"}])
    self.assertEqual(caught.exception.status, 402)
    self.assertEqual(
      caught.exception.message, "the LLM provider is out of credits or has no active billing")

  def test_a_refusal_is_an_error_never_a_reply(self):
    self.answer(200, text_reply("I cannot help with that.", stop="refusal"))
    with self.assertRaises(LlmError) as caught:
      _client(self.url).chat([{"role": "user", "text": "hi"}])
    self.assertEqual(caught.exception.stop_reason, "refusal")

  def test_no_key_sends_nothing(self):
    with self.assertRaises(LlmError) as caught:
      _client(self.url, key="").chat([{"role": "user", "text": "hi"}])
    self.assertEqual(caught.exception.message, "no API key for this session")
    self.assertEqual(caught.exception.code, "llmDisabled")
    self.assertEqual(self.seen, [])

  def test_an_expired_key_is_dropped_before_the_next_request(self):
    clock = FakeClock()
    client = _client(self.url, clock=clock)
    client.chat([{"role": "user", "text": "hi"}])
    clock.now += KEY_TTL_SECONDS
    with self.assertRaises(LlmError) as caught:
      client.chat([{"role": "user", "text": "again"}])
    self.assertEqual(caught.exception.code, "llmDisabled")
    self.assertEqual(client.config.api_key, "")
    self.assertEqual(len(self.seen), 1)  # only the turn before the expiry went out


class _Seen(_Recorder):
  seen: list = list()


class _ToSeen(_Bouncer):
  target = ""


class DirectRedirectTest(unittest.TestCase):
  def test_a_307_is_refused_and_the_key_never_reaches_the_second_host(self):
    recorder, recorder_url = _serve(_Seen)
    _ToSeen.target = recorder_url
    bouncer, bouncer_url = _serve(_ToSeen)
    try:
      with self.assertRaises(LlmError) as caught:
        _client(bouncer_url).chat([{"role": "user", "text": "go"}])
    finally:
      for server in (recorder, bouncer):
        server.shutdown()
        server.server_close()
    self.assertEqual(caught.exception.status, 307)
    self.assertEqual(_Seen.seen, [])

  def test_the_key_headers_are_unredirected(self):
    req = _client("https://api.anthropic.test")._build_request([{"role": "user", "text": "x"}])
    self.assertEqual(req.unredirected_hdrs.get("X-api-key"), FAKE_KEY)
    self.assertNotIn("X-api-key", req.headers)


class PlainHttpTest(unittest.TestCase):
  def test_an_ipv6_loopback_carries_the_key_and_a_lan_host_gets_nothing(self):
    req = _client("http://[::1]:8787")._build_request([{"role": "user", "text": "x"}])
    self.assertEqual(req.full_url, "http://[::1]:8787/v1/messages")
    self.assertEqual(req.unredirected_hdrs.get("X-api-key"), FAKE_KEY)
    sent: list = list()
    with mock.patch.object(client_mod, "_http_open", lambda req, *a, **kw: sent.append(req)):
      with self.assertRaises(LlmError) as caught:
        _client("http://192.168.1.5").chat([{"role": "user", "text": "x"}])
    self.assertEqual(caught.exception.code, "llmDisabled")
    self.assertEqual(
      caught.exception.message,
      "refusing to send the API key to '192.168.1.5' over plain http — use https")
    self.assertEqual(sent, [])


if __name__ == "__main__":
  unittest.main()
