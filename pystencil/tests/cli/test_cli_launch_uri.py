"""A launch link at the console and on the command line (cli/launch/link.py): ``/upload <link>``
lands on the REPL's own commands, says what it leaves out, and a one-shot ``--input`` link
becomes the flags it stands for.
"""

from __future__ import annotations

import argparse
import io
import unittest

from pystencil.cli.launch.link import expand_args
from pystencil.editor import Editor

from tests.helpers.clicase import _MockLlmClient, _StubConn, _wire_repl, require_core


class _ProjectConn(_StubConn):
  """A stub connection whose one project downloads a real picture."""

  def __init__(self, base, projects=()):
    super().__init__(base, projects)
    self.downloads = []

  def get_file(self, project_id, kind):
    self.downloads.append((project_id, kind))
    return Editor().blank(4, 3, "white").result(with_lines=False).encode("png")


class LaunchLinkConsoleTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls) -> None:
    require_core()

  def test_a_blank_link_opens_a_blank_and_says_what_it_left_out(self) -> None:
    repl, out = _wire_repl(_MockLlmClient(""))
    repl.run(io.StringIO("/upload https://app.example/?blank=%23336699&width=90&height=60&theme=dark&llm-api-key=k\n"))
    text = out.getvalue()
    self.assertTrue(repl._editor.has_image())
    self.assertIn('"llm-api-key" is ignored in the ?query', text)
    self.assertIn("the look and editor settings is not something this console sets", text)

  def test_a_project_link_connects_first_and_fetches_by_id_or_name(self) -> None:
    conn = _ProjectConn("http://a.example:8090", projects=("Sketch",))
    repl, out = _wire_repl(_MockLlmClient(""), [conn])
    repl.run(io.StringIO("/upload https://app.example/?project=http://a.example:8090/projects/p0\n"))
    self.assertEqual(conn.downloads, [("p0", "original")])
    self.assertIn('fetched "p0"', out.getvalue())
    self.assertEqual(repl._remote, (conn, "p0"))

    conn2 = _ProjectConn("http://a.example:8090", projects=("Sketch",))
    repl2, out2 = _wire_repl(_MockLlmClient(""), [conn2])
    repl2.run(io.StringIO("/open https://app.example/?connect=http://a.example:8090&project=Sketch\n"))
    self.assertEqual(conn2.downloads, [("p0", "original")])
    self.assertIn('fetched "Sketch"', out2.getvalue())

  def test_a_bare_project_ref_without_a_server_is_refused_here(self) -> None:
    repl, out = _wire_repl(_MockLlmClient(""))
    repl.run(io.StringIO("/upload https://app.example/?project=Sketch\n"))
    self.assertIn("names no server, and this console keeps no local projects", out.getvalue())
    self.assertFalse(repl._editor.has_image())


class LaunchLinkAssistantTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls) -> None:
    require_core()

  def _keyed(self):
    repl, out = _wire_repl(_MockLlmClient(""))
    repl._llm.set_provider("openai-compat")
    repl._llm.set_base_url("https://llm.example/v1")
    repl._llm.api_key = "sk-mine-0123456789"
    return repl, out

  def test_a_link_that_moves_the_base_url_drops_the_key_and_says_so(self) -> None:
    repl, out = self._keyed()
    repl.run(io.StringIO("/upload https://app.example/#llm-url=https://evil.example/v1\n"))
    self.assertEqual(repl._llm.base_url, "https://evil.example/v1")
    self.assertEqual(repl._llm.api_key, "")
    self.assertIn("the API key held for the old endpoint was dropped", out.getvalue())
    self.assertNotIn("sk-mine", out.getvalue())

  def test_a_link_with_its_own_key_or_the_same_origin_keeps_a_key(self) -> None:
    repl, _ = self._keyed()
    repl.run(io.StringIO("/upload https://app.example/#llm-url=https://evil.example/v1&llm-api-key=sk-theirs\n"))
    self.assertEqual(repl._llm.api_key, "sk-theirs")
    repl, out = self._keyed()
    repl.run(io.StringIO("/upload https://app.example/#llm-url=https://llm.example/v2\n"))
    self.assertEqual(repl._llm.api_key, "sk-mine-0123456789")
    self.assertNotIn("was dropped", out.getvalue())


class LaunchLinkOneShotTests(unittest.TestCase):
  def _args(self, **over):
    base = dict(input=None, output=None, frame=0, blank=None, layout=None)
    base.update(over)
    return argparse.Namespace(**base)

  def test_a_source_link_becomes_input_frame_and_layout(self) -> None:
    args = self._args(input="https://app.example/?source=https://x/clip.mp4&frame=12&layout=https://x/l.json")
    expand_args(args)
    self.assertEqual(args.input, "https://x/clip.mp4")
    self.assertEqual(args.frame, 12)
    self.assertEqual(args.layout, "https://x/l.json")

  def test_a_blank_link_becomes_the_blank_tokens_and_inline_layout_text(self) -> None:
    args = self._args(output="https://app.example/?blank=%23336699&width=890&height=243&layout-raw=%7B%22lines%22%3A%5B%5D%7D")
    expand_args(args)
    self.assertIsNone(args.input)
    self.assertIsNone(args.output, "the positional was the link, not an output path")
    self.assertEqual(args.blank, ["890", "243", "#336699"])
    self.assertEqual(args.layout, '{"lines":[]}')

  def test_a_picture_url_or_a_server_link_is_left_as_it_is(self) -> None:
    plain = self._args(input="https://cdn.example/a.png?width=800")
    expand_args(plain)
    self.assertEqual(plain.input, "https://cdn.example/a.png?width=800")
    server = self._args(input="https://app.example/?project=localhost:8080/projects/p1")
    expand_args(server)
    self.assertEqual(server.input, "https://app.example/?project=localhost:8080/projects/p1")
