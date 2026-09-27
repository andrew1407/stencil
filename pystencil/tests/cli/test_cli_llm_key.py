"""The console's anthropic session key (contract §5): ``/llm key`` asks without echo,
``/llm key forget`` drops it, the TTL expires it, and the key never lands anywhere but the
session config — over a loopback ``/v1/messages``, never the real API.
"""

from __future__ import annotations

import io
import sys
from unittest import mock

import pystencil.llm.client as client_mod
from pystencil import cli
from pystencil.llm import LlmConfig
from pystencil.llm.config import KEY_TTL_SECONDS
from tests.helpers.anthropicmock import FAKE_KEY, AnthropicMockCase, FakeClock, text_reply
from tests.helpers.nativecase import needs_core

_GETPASS = "pystencil.cli.commands.llm.getpass.getpass"
_HINT = "'/llm key' asks for it, hidden"


class _Terminal(io.StringIO):
  """A command stream that says it is a terminal, so ``/llm key`` goes to getpass."""

  def isatty(self) -> bool:
    return True


def _leaks(text: str) -> bool:
  """Whether ``text`` shows any 8-character run of the key (the §6.5 veto's rule)."""
  return any(FAKE_KEY[i:i + 8] in text for i in range(len(FAKE_KEY) - 7))


def _strings(value, depth=0):
  """Every str reachable from ``value`` through containers and plain objects."""
  if isinstance(value, str): yield value
  elif depth > 4 or isinstance(value, (bytes, bytearray, int, float, type(None))): return
  elif isinstance(value, dict):
    for k, v in value.items():
      yield from _strings(k, depth + 1)
      yield from _strings(v, depth + 1)
  elif isinstance(value, (list, tuple, set)):
    for v in value: yield from _strings(v, depth + 1)
  elif hasattr(value, "__dict__"):
    for v in vars(value).values(): yield from _strings(v, depth + 1)


class ReplSessionKeyTest(AnthropicMockCase):
  def _repl(self, clock=None):
    out = io.StringIO()
    repl = cli._Repl(out)
    kw = {"clock": clock} if clock is not None else {}
    repl._llm = LlmConfig(**kw)  # the host's STENCIL_LLM_* must not leak in
    return repl, out

  def test_choosing_anthropic_without_a_key_says_how_to_enter_one(self):
    repl, out = self._repl()
    repl.run(io.StringIO("/llm provider anthropic\n"))
    text = out.getvalue()
    self.assertIn("llm provider anthropic (url https://api.anthropic.com)", text)
    self.assertIn("note: no API key for this session — " + _HINT, text)

  def test_on_a_terminal_the_key_is_read_hidden_and_never_echoed(self):
    repl, out = self._repl()
    with mock.patch(_GETPASS, return_value=FAKE_KEY) as asked:
      repl.run(_Terminal("/llm provider anthropic\n/llm key\n/llm\n"))
    asked.assert_called_once_with("llm key (hidden): ", stream=out)
    self.assertEqual(repl._llm.api_key, FAKE_KEY)
    text = out.getvalue()
    self.assertIn("llm key set (memory only, expires in 12h 00m)", text)
    self.assertIn("key    ****cdef (memory only, expires in 12h 00m)", text)
    self.assertFalse(_leaks(text))

  def test_the_key_lives_in_the_session_config_alone(self):
    repl, out = self._repl()
    with mock.patch(_GETPASS, return_value=FAKE_KEY):
      repl.run(_Terminal("/llm provider anthropic\n/llm key\n/llm\n/status\n"))
    holders = [name for name, value in vars(repl).items()
               if name != "_llm" and any(FAKE_KEY in s for s in _strings(value))]
    self.assertEqual(holders, [])
    readline = sys.modules.get("readline")
    if readline is not None:
      items = (readline.get_history_item(i)
               for i in range(1, readline.get_current_history_length() + 1))
      self.assertFalse(any(FAKE_KEY in (item or "") for item in items))

  def test_a_cancelled_prompt_leaves_the_key_as_it_was(self):
    repl, out = self._repl()
    repl._llm.api_key = "kept-key-value"
    with mock.patch(_GETPASS, side_effect=KeyboardInterrupt):
      repl.run(_Terminal("/llm key\n"))
    self.assertEqual(repl._llm.api_key, "kept-key-value")
    self.assertIn("llm key unchanged", out.getvalue())

  def test_without_a_terminal_the_next_line_is_the_key(self):
    repl, out = self._repl()
    with mock.patch(_GETPASS) as asked:
      repl.run(io.StringIO("/llm provider anthropic\n/llm key\n%s\n/llm\n" % FAKE_KEY))
    asked.assert_not_called()
    self.assertEqual(repl._llm.api_key, FAKE_KEY)
    self.assertNotIn("unknown command", out.getvalue())
    self.assertFalse(_leaks(out.getvalue()))

  def test_forget_drops_the_key(self):
    repl, out = self._repl()
    repl._llm.set_provider("anthropic").api_key = FAKE_KEY
    repl.run(io.StringIO("/llm key forget\n/llm\n"))
    self.assertEqual(repl._llm.api_key, "")
    self.assertIn("llm key forgotten", out.getvalue())
    self.assertIn("key    (none) — " + _HINT, out.getvalue())

  def test_an_expired_key_is_gone_and_a_prompt_sends_nothing(self):
    clock = FakeClock()
    repl, out = self._repl(clock=clock)
    repl._llm.set_provider("anthropic").api_key = FAKE_KEY
    clock.now += KEY_TTL_SECONDS
    with mock.patch.object(client_mod, "_http_open", side_effect=AssertionError("sent")):
      repl.run(io.StringIO("/llm\n/prompt crop it\n"))
    text = out.getvalue()
    self.assertIn("key    (none) — " + _HINT, text)
    self.assertIn("error: no API key for this session", text)
    self.assertIn("note: " + _HINT, text)
    self.assertEqual(repl._llm.api_key, "")

  @needs_core
  def test_a_prompt_goes_straight_to_the_messages_api(self):
    self.answer(200, text_reply("Just words, no plan."))
    repl, out = self._repl()
    script = "/llm provider anthropic\n/llm url %s\n/llm key\n%s\n/prompt hello\n"
    repl.run(io.StringIO(script % (self.url, FAKE_KEY)))
    self.assertIn("Just words, no plan.", out.getvalue())
    self.assertEqual(len(self.seen), 1)
    path, headers, body = self.seen[0]
    self.assertEqual((path, headers["x-api-key"]), ("/v1/messages", FAKE_KEY))
    self.assertNotIn(FAKE_KEY, body["system"])
    self.assertEqual(body["messages"][0]["content"], [{"type": "text", "text": "hello"}])

  def test_a_rejected_key_is_a_console_error(self):
    self.answer(401, {"type": "error", "error": {"type": "authentication_error",
                                                 "message": "invalid x-api-key " + FAKE_KEY}})
    repl, out = self._repl()
    repl._llm.set_provider("anthropic").set_base_url(self.url).api_key = FAKE_KEY
    repl.run(io.StringIO("/prompt hello\n"))
    self.assertIn("error: the LLM provider rejected the API key", out.getvalue())
    self.assertFalse(_leaks(out.getvalue()))
