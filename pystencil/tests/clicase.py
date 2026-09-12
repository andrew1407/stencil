"""Shared fixtures and test doubles for the CLI console suite.

The console tests split by theme across ``test_cli_*.py``; the REPL wiring, the offline
LLM client and the temp-directory fixtures they all need live here once.
"""

from __future__ import annotations

import contextlib
import io
import os
import tempfile
import unittest

from tests.nativecase import require_core

from pystencil import cli
from pystencil.llm import LlmConfig


class _MockLlmClient:
  """A canned LlmClient stand-in recording the messages /prompt sends (offline)."""

  def __init__(self, reply: str = "", raises: Exception = None, replies: list = None) -> None:
    self.reply = reply
    self.replies = list(replies or [])
    self.raises = raises
    self.sent: list = []
    self.systems: list = []

  def chat(self, messages, system=None):
    self.sent.append([dict(m) for m in messages])
    self.systems.append(system)
    if self.raises is not None:
      raise self.raises
    return self.replies.pop(0) if self.replies else self.reply


class _StubConn:
  """A stand-in live ServerConnection: a base URL, a token that must never leak
  into a prompt, and a canned project listing."""

  def __init__(self, base, projects=(), token="sekrit-token", credential_kind="none") -> None:
    self.base = base
    self.token = token
    self.credential_kind = credential_kind
    self._projects = [
      {"name": n, "id": "p%d" % i} for i, n in enumerate(projects)
    ]
    self.closed = False

  def list_projects(self):
    return list(self._projects)

  def close(self):
    self.closed = True


def _wire_repl(client, urls=()):
  """A REPL with an offline LLM client and stub live connections installed."""
  out = io.StringIO()
  repl = cli._Repl(out)
  repl._llm = LlmConfig()
  repl._llm_client = lambda: client
  for u in urls:
    conn = u if isinstance(u, _StubConn) else _StubConn(u)
    repl._manager._conns[conn.base] = conn
  return repl, out


class _PipelineCase(unittest.TestCase):
  """cli.main() driven with an argv list inside a temp directory."""

  @classmethod
  def setUpClass(cls) -> None:
    # The one-shot pipeline always touches the core (blank/crop/filter), so
    # skip the whole suite if the shared library is unavailable.
    require_core()

  def setUp(self) -> None:
    self._dir = tempfile.TemporaryDirectory()
    self.tmp = self._dir.name

  def tearDown(self) -> None:
    self._dir.cleanup()

  def _path(self, name: str) -> str:
    """Absolute path inside this test's temp directory."""
    return os.path.join(self.tmp, name)

  def _run(self, args: list) -> str:
    """Invoke cli.main(args), asserting success, and return captured stderr."""
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
      code = cli.main(args)
    self.assertEqual(code, 0, "cli.main exited non-zero; stderr=%r" % err.getvalue())
    return err.getvalue()


class _ReplCase(unittest.TestCase):
  """A console session with an offline LLM client (and any stub servers) injected."""

  def _repl(self, client, urls=()) -> tuple:
    return _wire_repl(client, urls)


class _CwdCase(_ReplCase):
  """_ReplCase in a temp working directory — /prompt writes variant files into the cwd."""

  def setUp(self) -> None:
    self._dir = tempfile.TemporaryDirectory()
    self._old_cwd = os.getcwd()
    os.chdir(self._dir.name)

  def tearDown(self) -> None:
    os.chdir(self._old_cwd)
    self._dir.cleanup()


class _NativeReplCase(_CwdCase):
  """_CwdCase that also needs the native core (PNG encode/decode, real edits)."""

  @classmethod
  def setUpClass(cls) -> None:
    require_core()
