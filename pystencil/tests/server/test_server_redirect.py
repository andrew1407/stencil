"""The bearer never follows a redirect, the TLS context rides the one opener, and a
response body past the cap is refused — over two real loopback servers."""

from __future__ import annotations

import json
import ssl
import unittest
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from unittest import mock

from pystencil._net import _no_redirect_opener, _unverified_ssl_context
from pystencil.llm import LlmClient, LlmConfig, LlmError
from pystencil.server import ServerConnection, ServerError
from pystencil.server import http as server_http


class _Recorder(BaseHTTPRequestHandler):
  """The second host: records the Authorization header of every request it gets."""

  seen: list = list()
  body = b'{"projects": []}'

  def log_message(self, *args):
    pass

  def _answer(self):
    type(self).seen.append(self.headers.get("Authorization"))
    self.send_response(200)
    self.send_header("Content-Type", "application/json")
    self.send_header("Content-Length", str(len(self.body)))
    self.end_headers()
    self.wfile.write(self.body)

  do_GET = _answer

  def do_POST(self):
    self.rfile.read(int(self.headers.get("Content-Length") or 0))
    self._answer()


class _Bouncer(BaseHTTPRequestHandler):
  """The first host: 302s every GET and 307s every POST onto the recorder."""

  target = ""

  def log_message(self, *args):
    pass

  def _bounce(self, code):
    self.rfile.read(int(self.headers.get("Content-Length") or 0))
    self.send_response(code)
    self.send_header("Location", self.target + self.path)
    self.send_header("Content-Length", "0")
    self.end_headers()

  def do_GET(self):
    self._bounce(302)

  def do_POST(self):
    self._bounce(307)


def _serve(handler):
  server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
  Thread(target=server.serve_forever, daemon=True).start()
  return server, "http://127.0.0.1:%d" % server.server_address[1]


class RedirectCredentialTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    cls._recorder, cls.recorder = _serve(_Recorder)
    _Bouncer.target = cls.recorder
    cls._bouncer, cls.bouncer = _serve(_Bouncer)

  @classmethod
  def tearDownClass(cls):
    for server in (cls._recorder, cls._bouncer):
      server.shutdown()
      server.server_close()

  def setUp(self):
    _Recorder.seen = list()
    _Recorder.body = b'{"projects": []}'

  def test_the_recorder_sees_the_bearer_when_called_directly(self):
    # The control: a refusal below is the redirect guard, not a deaf recorder.
    self.assertEqual(ServerConnection(self.recorder, token="secret").list_projects(), [])
    self.assertEqual(_Recorder.seen, ["Bearer secret"])

  def test_a_rest_302_is_refused_and_the_second_host_gets_nothing(self):
    with self.assertRaises(ServerError) as caught:
      ServerConnection(self.bouncer, token="secret").list_projects()
    self.assertEqual(caught.exception.status, 302)
    self.assertIn("refusing to follow redirect to " + self.recorder, str(caught.exception))
    self.assertEqual(_Recorder.seen, [])

  def test_an_llm_307_is_refused_and_the_second_host_gets_nothing(self):
    _Recorder.body = json.dumps({"model": "m", "text": "ok", "stopReason": "end_turn"}).encode()
    client = LlmClient(
      LlmConfig(provider="stencil-server", server_url=self.bouncer), token="secret")
    with self.assertRaises(LlmError) as caught:
      client.chat([{"role": "user", "text": "go"}])
    self.assertEqual(caught.exception.status, 307)
    self.assertEqual(_Recorder.seen, [])

  def test_the_bearer_is_an_unredirected_header(self):
    req = ServerConnection(self.recorder, token="secret")._build_request("GET", "/projects")
    self.assertEqual(req.unredirected_hdrs.get("Authorization"), "Bearer secret")
    self.assertNotIn("Authorization", req.headers)

  def test_a_body_past_the_cap_is_refused(self):
    with mock.patch.object(server_http, "_MAX_RESPONSE_BYTES", 8):
      with self.assertRaises(OSError) as caught:
        ServerConnection(self.recorder, token="secret").list_projects()
    self.assertIn("exceeds the 8-byte cap", str(caught.exception))


class TlsContextTest(unittest.TestCase):
  def test_the_no_redirect_opener_carries_the_context(self):
    ctx = _unverified_ssl_context()
    handlers = [h for h in _no_redirect_opener(ctx).handlers
                if isinstance(h, urllib.request.HTTPSHandler)]
    self.assertEqual([h._context for h in handlers], [ctx])

  def test_an_unverified_connection_opens_through_its_context(self):
    seen = list()

    def opener(context=None):
      seen.append(context)
      raise OSError("stop here")

    conn = ServerConnection("https://host:8090", token="t", verify=False)
    with mock.patch.object(server_http, "_no_redirect_opener", opener):
      with self.assertRaises(OSError):
        conn.list_projects()
    self.assertEqual(len(seen), 1)
    self.assertIsInstance(seen[0], ssl.SSLContext)
    self.assertEqual(seen[0].verify_mode, ssl.CERT_NONE)


if __name__ == "__main__":
  unittest.main()
