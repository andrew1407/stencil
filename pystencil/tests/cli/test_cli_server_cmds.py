"""The REPL commands that act on live server connections — /disconnect and /delete — and
the §4 console-context suffix /prompt sends about them (which never carries a token).
"""

from __future__ import annotations

import io
import os
import unittest

from pystencil import cli
from pystencil.editor import Editor
from pystencil.llm import CONSOLE_SYSTEM_PROMPT

from tests.helpers.clicase import _CwdCase, _MockLlmClient, _StubConn, _wire_repl


class ReplDisconnectCommandTest(unittest.TestCase):
  """The new /disconnect REPL command (Zig-console wording)."""

  def test_disconnect_without_connections(self) -> None:
    repl, out = _wire_repl(_MockLlmClient(""))
    repl.run(io.StringIO("/disconnect\n"))
    self.assertIn("no server connections", out.getvalue())

  def test_bare_disconnect_drops_the_most_recent(self) -> None:
    repl, out = _wire_repl(_MockLlmClient(""), ["http://a.example:8090", "http://b.example:8090"])
    repl.run(io.StringIO("/disconnect\n"))
    self.assertIn("disconnected from http://b.example:8090", out.getvalue())
    self.assertEqual(repl._manager.connections, ["http://a.example:8090"])

  def test_disconnect_by_url_and_unknown(self) -> None:
    repl, out = _wire_repl(_MockLlmClient(""), ["http://a.example:8090"])
    repl.run(io.StringIO("/disconnect http://b.example:9\n/disconnect http://a.example:8090\n"))
    text = out.getvalue()
    self.assertIn("not connected to http://b.example:9", text)
    self.assertIn("disconnected from http://a.example:8090", text)
    self.assertEqual(repl._manager.connections, [])

  def test_connections_tags_admin_rows_and_filters_on_the_kind(self) -> None:
    admin = _StubConn("http://a.example:8090", credential_kind="admin")
    sess = _StubConn("http://b.example:8090", credential_kind="session")
    repl, out = _wire_repl(_MockLlmClient(""), [admin, sess])
    repl.run(io.StringIO("/connections\n"))
    text = out.getvalue()
    self.assertIn("http://a.example:8090  [admin]", text)
    self.assertIn("http://b.example:8090\n", text)
    self.assertNotIn("sekrit-token", text)  # tokens never printed

    repl, out = _wire_repl(_MockLlmClient(""), [admin, sess])
    repl.run(io.StringIO("/connections admin\n"))
    text = out.getvalue()
    self.assertIn("http://a.example:8090  [admin]", text)
    self.assertNotIn("b.example", text)

    repl, out = _wire_repl(_MockLlmClient(""), [admin, sess])
    repl.run(io.StringIO("/servers session\n"))
    text = out.getvalue()
    self.assertIn("http://b.example:8090", text)
    self.assertNotIn("a.example", text)

  def test_connections_unknown_filter_prints_usage_and_empty_result(self) -> None:
    admin = _StubConn("http://a.example:8090", credential_kind="admin")
    repl, out = _wire_repl(_MockLlmClient(""), [admin])
    repl.run(io.StringIO("/connections bogus\n/connections session\n"))
    text = out.getvalue()
    self.assertIn("usage: /connections [admin|session]", text)
    self.assertIn("no session connections (of 1)", text)

  def test_help_lists_disconnect_and_delete(self) -> None:
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO("/help\n"))
    self.assertIn("/disconnect [url]", out.getvalue())
    self.assertIn("/delete <x.stencil>", out.getvalue())


class ReplDeleteCommandTest(_CwdCase):
  """The new /delete REPL command: the cli console's full guard set + wording."""

  def _run(self, script: str) -> str:
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO(script))
    return out.getvalue()

  def test_delete_removes_a_stencil_file(self) -> None:
    with open("p.stencil", "w", encoding="utf-8") as fh:
      fh.write("{}")
    text = self._run("/delete p.stencil\n")
    self.assertIn("deleted p.stencil", text)
    self.assertFalse(os.path.exists("p.stencil"))

  def test_delete_guards(self) -> None:
    text = self._run(
      "/delete\n"
      "/delete http://x.example/p.stencil\n"
      "/delete image.png\n"
      "/delete ../escape.stencil\n"
    )
    self.assertIn("error: delete needs a .stencil path", text)
    self.assertIn("error: delete only removes local files, not URLs", text)
    self.assertIn("error: delete only removes .stencil project files (got 'image.png')", text)
    self.assertIn(
      "error: refusing to delete a path that escapes the working directory: "
      "'../escape.stencil'",
      text,
    )

  def test_delete_missing_file_reports(self) -> None:
    self.assertIn("error: could not delete ghost.stencil", self._run("/delete ghost.stencil\n"))

  def test_rm_alias_routes_here(self) -> None:
    with open("q.stencil", "w", encoding="utf-8") as fh:
      fh.write("{}")
    self.assertIn("deleted q.stencil", self._run("/rm q.stencil\n"))

  def test_api_delete_project_gains_the_traversal_guard(self) -> None:
    # The cli's guard, now on the API too (contract §10 pystencil paragraph).
    with self.assertRaises(ValueError):
      Editor.delete_project("../escape.stencil")
    with self.assertRaises(ValueError):
      Editor.delete_project("sub/../../escape.stencil")
    with self.assertRaises(ValueError):
      Editor.delete_project("http://x.example/p.stencil")
    self.assertEqual(Editor.delete_reject("ok.stencil"), None)
    self.assertEqual(Editor.delete_reject("..\\win.stencil"), "traversal")


class ReplConsoleContextTest(unittest.TestCase):
  """The §4 console-context suffix /prompt sends: connections + active project +
  capped project names — and never a token."""

  def test_prompt_carries_the_console_state(self) -> None:
    client = _MockLlmClient("just words")
    repl, _out = _wire_repl(
      client,
      [_StubConn("http://a.example:8090", projects=["portrait", "cat 2"])],
    )
    repl.run(io.StringIO("/prompt what is on my server?\n"))
    system = client.systems[0]
    self.assertTrue(system.startswith(CONSOLE_SYSTEM_PROMPT))
    self.assertIn("Console state", system)
    self.assertIn("Connections (1): http://a.example:8090.", system)
    self.assertIn("Projects on http://a.example:8090: portrait, cat 2.", system)
    self.assertIn("Active server project: none.", system)
    self.assertNotIn("sekrit-token", system)
    self.assertNotIn("token", system.split("Console state", 1)[1].lower())

  def test_active_fetched_project_is_named(self) -> None:
    client = _MockLlmClient("noted")
    conn = _StubConn("http://a.example:8090", projects=["portrait"])
    repl, _out = _wire_repl(client, [conn])
    repl._remote = (conn, "p0")  # as /fetch records it
    repl._editor._name = "portrait"  # the fetched project's name
    repl.run(io.StringIO("/prompt which project am I on?\n"))
    system = client.systems[0]
    self.assertIn("http://a.example:8090 (active project's server)", system)
    self.assertIn('Active server project: "portrait".', system)

  def test_unreachable_server_omits_its_listing(self) -> None:
    client = _MockLlmClient("noted")
    conn = _StubConn("http://a.example:8090")
    conn.list_projects = lambda: (_ for _ in ()).throw(OSError("down"))
    repl, _out = _wire_repl(client, [conn])
    repl.run(io.StringIO("/prompt hello\n"))
    system = client.systems[0]
    self.assertIn("Connections (1): http://a.example:8090.", system)
    self.assertNotIn("Projects on http://a.example:8090", system)
