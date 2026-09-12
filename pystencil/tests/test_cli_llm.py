"""The /llm config command, /connect's `token=` argument, and the /prompt paths that need
no native core. All offline: an injected mock client, never a real endpoint.
"""

from __future__ import annotations

import io
import unittest

from pystencil import cli
from pystencil.llm import LlmConfig, LlmError

from tests.clicase import _MockLlmClient, _ReplCase


class ReplLlmConfigTest(unittest.TestCase):
  """The /llm command: config display (masked) and in-session overrides.

  These never build a client or touch the native core, so they run everywhere.
  """

  def _repl(self, **cfg_kw):
    out = io.StringIO()
    repl = cli._Repl(out)
    # Pin the session config so the host's STENCIL_LLM_* env can't leak in.
    repl._llm = LlmConfig(**cfg_kw)
    return repl, out

  def test_bare_llm_masks_credentials(self) -> None:
    repl, out = self._repl(provider="openai-compat", api_key="sk-secret1234")
    repl.run(io.StringIO("/llm\n"))
    text = out.getvalue()
    self.assertIn("llm provider openai-compat", text)
    self.assertIn("http://localhost:1234/v1", text)
    self.assertNotIn("sk-secret1234", text)  # never echo the key
    self.assertIn("****1234", text)  # masked to stars + last 4
    self.assertIn("model  (default)", text)

  def test_provider_switch_refills_default_url(self) -> None:
    repl, out = self._repl()  # ollama, default URL
    repl.run(io.StringIO("/llm provider openai-compat\n"))
    self.assertEqual(repl._llm.provider, "openai-compat")
    self.assertEqual(repl._llm.base_url, "http://localhost:1234/v1")
    repl.run(io.StringIO("/llm provider ollama\n"))
    self.assertEqual(repl._llm.base_url, "http://localhost:11434")

  def test_user_url_survives_provider_switch(self) -> None:
    repl, out = self._repl()
    repl.run(io.StringIO("/llm url http://box:9999/v1\n/llm provider openai-compat\n"))
    self.assertEqual(repl._llm.provider, "openai-compat")
    # The session override wins over the provider's default re-fill.
    self.assertEqual(repl._llm.base_url, "http://box:9999/v1")

  def test_unknown_provider_is_rejected(self) -> None:
    repl, out = self._repl()
    repl.run(io.StringIO("/llm provider anthropic-direct\n"))
    self.assertIn("error: unknown provider 'anthropic-direct'", out.getvalue())
    self.assertEqual(repl._llm.provider, "ollama")  # unchanged

  def test_model_key_server_setters(self) -> None:
    repl, out = self._repl()
    repl.run(
      io.StringIO(
        "/llm model llava\n/llm key sk-1\n/llm server https://srv:8090\n"
        "/llm model\n/llm key\n"
      )
    )
    self.assertEqual(repl._llm.server_url, "https://srv:8090")
    self.assertEqual(repl._llm.model, "")  # bare value clears back to default
    self.assertEqual(repl._llm.api_key, "")
    text = out.getvalue()
    self.assertIn("llm model llava", text)
    self.assertIn("llm key set", text)
    self.assertIn("llm key cleared", text)
    self.assertNotIn("sk-1", text)  # the key value is never echoed

  def test_unknown_subcommand_hints(self) -> None:
    repl, out = self._repl()
    repl.run(io.StringIO("/llm temperature 0.7\n"))
    self.assertIn("error: /llm takes provider | url | model | key | server",
           out.getvalue())

  def test_help_lists_llm_commands(self) -> None:
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO("/help\n"))
    text = out.getvalue()
    self.assertIn("/prompt <text>", text)
    self.assertIn("/llm [key value]", text)

  def test_stencil_server_reuses_live_connection_token(self) -> None:
    # A /connect-ed server whose URL matches (or the first one, when no server
    # URL is configured) lends its bearer token to the LLM client.
    from pystencil.server import ServerConnection

    repl, out = self._repl(provider="stencil-server")
    conn = ServerConnection("http://host:8090", token="sess-tok")
    repl._manager._conns[conn.base] = conn  # seed without a network handshake
    client = repl._llm_client()
    req = client._build_request([{"role": "user", "text": "hi"}])
    self.assertEqual(req.full_url, "http://host:8090/llm/chat")
    self.assertEqual(req.get_header("Authorization"), "Bearer sess-tok")
    # An explicit non-matching server URL does NOT borrow the token.
    repl._llm.server_url = "https://other:8090"
    req = repl._llm_client()._build_request([{"role": "user", "text": "hi"}])
    self.assertEqual(req.full_url, "https://other:8090/llm/chat")
    self.assertEqual(req.get_header("Authorization"), "Bearer ")


class ReplConnectTokenTest(unittest.TestCase):
  """/connect's `token=` argument — the only way onto an ADMIN_TOKEN-gated server."""

  def _repl(self) -> tuple:
    out = io.StringIO()
    repl = cli._Repl(out)
    seen: list = []
    # Stand in for the network handshake; record what the manager was handed.
    repl._manager.connect = lambda spec: seen.append(spec) or repl._manager
    return repl, out, seen

  def test_token_argument_is_passed_to_the_manager(self) -> None:
    repl, out, seen = self._repl()
    repl.run(io.StringIO("/connect http://host:8090 token=sess-tok\n"))
    self.assertEqual(seen, [{"url": "http://host:8090", "token": "sess-tok"}])
    self.assertIn("connected http://host:8090", out.getvalue())

  def test_token_applies_to_every_url_in_the_call(self) -> None:
    repl, _out, seen = self._repl()
    repl.run(io.StringIO("/connect http://a:8090 http://b:8090 token=t\n"))
    self.assertEqual(seen, [{"url": "http://a:8090", "token": "t"},
                {"url": "http://b:8090", "token": "t"}])

  def test_without_a_token_the_url_is_passed_as_before(self) -> None:
    repl, _out, seen = self._repl()
    repl.run(io.StringIO("/connect http://host:8090\n"))
    self.assertEqual(seen, ["http://host:8090"])

  def test_a_token_alone_is_not_a_url(self) -> None:
    repl, out, seen = self._repl()
    repl.run(io.StringIO("/connect token=t\n"))
    self.assertEqual(seen, [])
    self.assertIn("error: /connect needs one or more server URLs", out.getvalue())


class ReplPromptOfflineTest(_ReplCase):
  """/prompt paths that need no native core (no image loaded, mock client)."""

  def test_prompt_requires_text(self) -> None:
    repl, out = self._repl(_MockLlmClient("unused"))
    repl.run(io.StringIO("/prompt\n"))
    self.assertIn("error: /prompt needs text to send", out.getvalue())

  def test_chat_only_prompt_prints_reply(self) -> None:
    client = _MockLlmClient("Just words, no plan.")
    repl, out = self._repl(client)
    repl.run(io.StringIO("/prompt what can you do?\n"))
    self.assertIn("Just words, no plan.", out.getvalue())
    # No image loaded => a text-only turn (no attachment).
    self.assertEqual(client.sent[0][0]["text"], "what can you do?")
    self.assertEqual(client.sent[0][0]["images"], [])

  def test_stop_reason_error_prints_as_console_error(self) -> None:
    client = _MockLlmClient(
      raises=LlmError("response truncated (max_tokens) — not parsed as a plan",
              stop_reason="max_tokens")
    )
    repl, out = self._repl(client)
    repl.run(io.StringIO("/prompt do things\n/status\n"))
    text = out.getvalue()
    self.assertIn("error: response truncated (max_tokens)", text)
    self.assertIn("no image loaded", text)  # the REPL kept running

  def test_invalid_plan_prints_as_console_error(self) -> None:
    client = _MockLlmClient(
      '{"version":1,"reply":"bad","actions":[{"op":"rotate","dir":"up"}]}'
    )
    repl, out = self._repl(client)
    repl.run(io.StringIO("/p spin it\n"))  # the /p alias routes here too
    self.assertIn('error: invalid "rotate" action', out.getvalue())
