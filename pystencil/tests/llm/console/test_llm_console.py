"""§10 console helpers: the openUrl echo guard, server resolution, the context line."""

from __future__ import annotations

import unittest

from pystencil.llm import (
  ConsoleServer,
  MAX_CONTEXT_PROJECTS,
  blocked_open_url,
  console_context,
  parse_op_plan,
  resolve_server,
  url_echoed_by_user,
)
from tests.helpers.stubs import _plan_json


class EchoGuardTest(unittest.TestCase):
  """§10 openUrl: the model may only ECHO the user — never introduce a URL."""

  URL = "https://pics.example/cat.png"

  def _plan(self, url=None):
    return parse_op_plan(
      _plan_json(actions=[{"op": "openUrl", "url": url or self.URL}])
    )

  def test_url_in_the_current_text_passes(self):
    self.assertTrue(url_echoed_by_user([], "load %s please" % self.URL, self.URL))
    self.assertIsNone(blocked_open_url(self._plan(), [], "get %s" % self.URL))

  def test_url_in_a_replayed_user_turn_passes(self):
    history = [
      {"role": "user", "text": "here: %s" % self.URL, "images": []},
      {"role": "assistant", "text": "noted", "images": []},
    ]
    self.assertTrue(url_echoed_by_user(history, "load it", self.URL))
    self.assertIsNone(blocked_open_url(self._plan(), history, "load it"))

  def test_an_untyped_url_blocks_the_plan(self):
    # The model introduced the host — the guard names the URL, nothing executes.
    self.assertFalse(url_echoed_by_user([], "load the cat picture", self.URL))
    self.assertEqual(
      blocked_open_url(self._plan(), [], "load the cat picture"), self.URL
    )

  def test_assistant_text_never_authorises_a_url(self):
    history = [{"role": "assistant", "text": "try %s" % self.URL, "images": []}]
    self.assertFalse(url_echoed_by_user(history, "yes do that", self.URL))
    self.assertEqual(
      blocked_open_url(self._plan(), history, "yes do that"), self.URL
    )

  def test_a_rewritten_url_is_not_an_echo(self):
    # Substring matching is verbatim: completing/rewriting the URL is not an echo.
    typed = "load https://pics.example/cat"
    self.assertFalse(url_echoed_by_user([], typed, self.URL))


class ResolveServerTest(unittest.TestCase):
  URLS = ["http://alpha.example:8090", "https://beta.example", "http://beta.example:9000"]

  def test_exact_url_match(self):
    self.assertEqual(resolve_server(self.URLS, "https://beta.example"), 1)

  def test_unique_host_match_is_case_insensitive(self):
    self.assertEqual(resolve_server(self.URLS, "Alpha.Example"), 0)
    self.assertEqual(resolve_server(self.URLS, "alpha.example:8090"), 0)

  def test_ambiguous_host(self):
    self.assertEqual(resolve_server(self.URLS, "beta.example"), "ambiguous")

  def test_unknown_or_empty_is_none(self):
    self.assertEqual(resolve_server(self.URLS, "gamma.example"), "none")
    self.assertEqual(resolve_server(self.URLS, "  "), "none")
    self.assertEqual(resolve_server([], "alpha.example"), "none")


class ConsoleContextTest(unittest.TestCase):
  """The §4 dynamic console-context suffix (the cli console's, ported): URLs,
  the active project, capped project names — and nowhere for a token to ride."""

  def test_empty_console(self):
    ctx = console_context([], "")
    self.assertIn("Console state", ctx)
    self.assertIn("Connections: none — the user can add one with '/connect <url>'.", ctx)
    self.assertIn("Active server project: none.", ctx)

  def test_connections_active_project_and_names(self):
    ctx = console_context(
      [
        ConsoleServer("http://a.example:8090", active=True,
               projects=["portrait", "cat 2"]),
        ConsoleServer("http://b.example", projects=[]),
        ConsoleServer("http://c.example"),  # unreachable: line omitted
      ],
      "portrait",
    )
    self.assertIn(
      "Connections (3): http://a.example:8090 (active project's server), "
      "http://b.example, http://c.example.",
      ctx,
    )
    self.assertIn('Active server project: "portrait".', ctx)
    self.assertIn("Projects on http://a.example:8090: portrait, cat 2.", ctx)
    self.assertIn("Projects on http://b.example: (none)", ctx)
    self.assertNotIn("Projects on http://c.example", ctx)
    self.assertNotIn("token", ctx.lower())

  def test_project_names_are_capped(self):
    names = ["p%d" % i for i in range(MAX_CONTEXT_PROJECTS + 5)]
    ctx = console_context([ConsoleServer("http://a.example", projects=names)])
    self.assertIn("p%d" % (MAX_CONTEXT_PROJECTS - 1), ctx)
    self.assertNotIn("p%d," % MAX_CONTEXT_PROJECTS, ctx)
    self.assertIn("(+5 more)", ctx)
