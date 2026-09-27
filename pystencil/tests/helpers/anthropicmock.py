"""A loopback stand-in for Anthropic's ``POST /v1/messages``, and a clock a test winds on.

The direct ``anthropic`` suites drive real HTTP against it on 127.0.0.1:0, never the real
API and never a real key.
"""

from __future__ import annotations

import json
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread

#: A key shaped like a real one, so the sanitizer's token rules would catch it too.
FAKE_KEY = "sk-ant-test-0123456789abcdef"


class FakeClock:
  """``LlmConfig.clock``: seconds that only move when the test says so."""

  def __init__(self) -> None:
    self.now = 1000.0

  def __call__(self) -> float:
    return self.now


class _Messages(BaseHTTPRequestHandler):
  """Records each request's path, lower-cased headers and JSON body; answers ``reply``."""

  seen: list = list()
  status = 200
  reply: dict = dict()

  def log_message(self, *args):
    pass

  def do_POST(self):
    raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
    headers = {k.lower(): v for k, v in self.headers.items()}
    type(self).seen.append((self.path, headers, json.loads(raw.decode("utf-8"))))
    body = json.dumps(type(self).reply).encode("utf-8")
    self.send_response(type(self).status)
    self.send_header("Content-Type", "application/json")
    self.send_header("Content-Length", str(len(body)))
    self.end_headers()
    self.wfile.write(body)


def text_reply(text: str, stop: str = "end_turn") -> dict:
  return {"model": "claude-opus-5", "stop_reason": stop, "content": [{"type": "text", "text": text}]}


class AnthropicMockCase(unittest.TestCase):
  """A case with ``self.url`` serving ``/v1/messages``; ``answer`` sets what it says next."""

  @classmethod
  def setUpClass(cls) -> None:
    cls._server = ThreadingHTTPServer(("127.0.0.1", 0), _Messages)
    Thread(target=cls._server.serve_forever, daemon=True).start()
    cls.url = "http://127.0.0.1:%d" % cls._server.server_address[1]

  @classmethod
  def tearDownClass(cls) -> None:
    cls._server.shutdown()
    cls._server.server_close()

  def setUp(self) -> None:
    self.answer(200, text_reply("hello from the mock"))

  def answer(self, status: int, reply: dict) -> None:
    _Messages.seen = list()
    _Messages.status = status
    _Messages.reply = reply

  @property
  def seen(self) -> list:
    return _Messages.seen
