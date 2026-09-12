"""/chat (§12): the on/off toggle, multi-turn /prompt routing, clear — and the §10
`clearChat` op that routes through the same clear path behind a y/N confirm.
"""

from __future__ import annotations

import io

from pystencil import cli

from tests.clicase import _MockLlmClient, _ReplCase


class ReplChatModeTest(_ReplCase):
  """/chat (§12): the on/off toggle, multi-turn /prompt routing, and clear.

  Offline like ReplPromptOfflineTest — no image loaded, injected mock client.
  """

  def test_chat_toggles_and_show(self) -> None:
    repl, out = self._repl(_MockLlmClient("words"))
    repl.run(io.StringIO("/chat\n/chat on\n/chat show\n/chat off\n/chat\n"))
    text = out.getvalue()
    # Default OFF, bare /chat and /chat show print mode + turn count.
    self.assertIn("chat off (0 message(s))", text.splitlines()[0])
    self.assertIn("chat on", text)
    self.assertIn("chat on (0 message(s))", text)
    self.assertEqual(text.splitlines()[-1], "chat off (0 message(s))")

  def test_chat_on_says_who_can_read_a_saved_chat(self) -> None:
    # §12.2: a transcript records what the user asked for in their own words, and on
    # a server project it carries the PROJECT's access — which is not what "save
    # chats with the project" sounds like it promises. So the console has to say it
    # at the toggle, on the turn that enables saving, before anything is written.
    repl, out = self._repl(_MockLlmClient("words"))
    repl.run(io.StringIO("/chat on\n"))
    after_toggle = out.getvalue().split("chat on", 1)[1]
    self.assertIn("shared with", after_toggle)
    self.assertIn("readable by everyone", after_toggle)
    # Both destinations named: the local .stencil file and the server project.
    self.assertIn(".stencil project", after_toggle)
    self.assertIn("server project", after_toggle)

  def test_chat_unknown_subcommand_hints(self) -> None:
    repl, out = self._repl(_MockLlmClient("words"))
    repl.run(io.StringIO("/chat maybe\n"))
    self.assertIn("error: /chat takes on | off | clear | show", out.getvalue())
    self.assertFalse(repl._chat_on)  # unchanged

  def test_chat_on_accumulates_turns_across_prompts(self) -> None:
    client = _MockLlmClient("Just words, no plan.")
    repl, out = self._repl(client)
    repl.run(io.StringIO("/chat on\n/prompt first\n/prompt second\n/chat\n"))
    # Turn two replayed the whole bounded history: user, assistant, user.
    self.assertEqual(len(client.sent[0]), 1)
    self.assertEqual(
      [m["role"] for m in client.sent[1]], ["user", "assistant", "user"]
    )
    self.assertEqual(client.sent[1][0]["text"], "first")
    self.assertEqual(client.sent[1][2]["text"], "second")
    self.assertEqual(len(repl._chat.history), 4)
    self.assertIn("chat on (4 message(s))", out.getvalue())

  def test_chat_clear_empties_the_conversation(self) -> None:
    client = _MockLlmClient("Just words, no plan.")
    repl, out = self._repl(client)
    repl.run(io.StringIO("/chat on\n/prompt first\n/chat clear\n/chat\n"))
    self.assertIn("chat cleared", out.getvalue())
    self.assertEqual(repl._chat.history, [])
    self.assertIn("chat on (0 message(s))", out.getvalue())

  def test_default_off_keeps_prompts_single_turn(self) -> None:
    # With the toggle off (the default), /prompt stays exactly single-turn:
    # each request carries exactly one user message, nothing accumulates.
    client = _MockLlmClient("Just words, no plan.")
    repl, out = self._repl(client)
    repl.run(io.StringIO("/prompt first\n/prompt second\n"))
    self.assertEqual(len(client.sent), 2)
    for sent in client.sent:
      self.assertEqual(len(sent), 1)
      self.assertEqual(sent[0]["role"], "user")
    self.assertIsNone(repl._chat)

  class _RecordingConn:
    """The one ServerConnection method /chat clear touches, recorded."""

    def __init__(self) -> None:
      self.deleted = []

    def delete_file(self, pid, kind) -> None:
      self.deleted.append((pid, kind))

  def test_chat_clear_deletes_the_active_remote_chat(self) -> None:
    # While a fetched project is active and /chat is on, clear also drops
    # the server-side `chat` file (§12).
    conn = self._RecordingConn()
    repl, out = self._repl(_MockLlmClient("words"))
    repl._remote = (conn, "p1")  # as recorded by /fetch
    repl.run(io.StringIO("/chat on\n/chat clear\n"))
    self.assertEqual(conn.deleted, [("p1", "chat")])
    self.assertIn("chat cleared", out.getvalue())

  def test_image_swap_detaches_the_previous_remote_project(self) -> None:
    # Replacing the working image (/drop here) must reset the remote scope
    # and the conversation, so a later /chat clear can never delete the
    # PREVIOUS project's server-side chat file.
    conn = self._RecordingConn()
    repl, out = self._repl(_MockLlmClient("words"))
    repl._remote = (conn, "p1")  # as recorded by /fetch
    repl.run(io.StringIO("/chat on\n/prompt hello\n/drop\n/chat clear\n"))
    self.assertIsNone(repl._remote)
    self.assertIsNone(repl._chat)  # the conversation is image-scoped too
    self.assertEqual(conn.deleted, [])
    self.assertIn("chat cleared", out.getvalue())

  def test_help_lists_chat_command(self) -> None:
    out = io.StringIO()
    cli._Repl(out).run(io.StringIO("/help\n"))
    self.assertIn("/chat [on|off|clear]", out.getvalue())


class ReplClearChatOpTest(_ReplCase):
  """§10 clearChat: deferred to the end of the turn, confirmed with a y/N line
  on the console's own input, and on a yes cleared through the exact /chat
  clear path. Offline like ReplChatModeTest — no image, injected mock client."""

  PLAN = '{"version":1,"reply":"Wiping.","actions":[{"op":"clearChat"}]}'

  def test_confirm_is_deferred_past_the_plans_other_actions(self) -> None:
    repl, out = self._repl(_MockLlmClient(self.PLAN))
    repl.run(io.StringIO("/prompt wipe the chat\nn\n"))
    text = out.getvalue()
    # Reply, then the executed actions, THEN the confirm — never mid-plan.
    self.assertLess(text.index("Wiping."), text.index("applied 1 action(s)"))
    self.assertLess(text.index("applied 1 action(s)"),
            text.index("clear this conversation's history? [y/N]"))
    self.assertIn("clear canceled", text)

  def test_declined_confirm_is_a_note_that_keeps_the_conversation(self) -> None:
    client = _MockLlmClient(replies=["just words", self.PLAN])
    repl, out = self._repl(client)
    repl.run(io.StringIO("/chat on\n/prompt hello\n/prompt wipe it\nn\n/chat\n"))
    text = out.getvalue()
    self.assertIn("clear canceled", text)
    self.assertNotIn("chat cleared", text)
    self.assertEqual(len(repl._chat.history), 4)  # both turns survived
    self.assertIn("chat on (4 message(s))", text)

  def test_accepted_confirm_runs_the_chat_clear_path(self) -> None:
    # The confirmed clear IS /chat clear: history emptied and the active
    # remote project's server-side `chat` file dropped (§12).
    conn = ReplChatModeTest._RecordingConn()
    client = _MockLlmClient(replies=["just words", self.PLAN])
    repl, out = self._repl(client)
    repl._remote = (conn, "p1")  # as recorded by /fetch
    repl.run(io.StringIO("/chat on\n/prompt hello\n/prompt wipe it\ny\n/chat\n"))
    text = out.getvalue()
    self.assertIn("chat cleared", text)
    self.assertEqual(repl._chat.history, [])
    self.assertEqual(conn.deleted, [("p1", "chat")])
    self.assertIn("chat on (0 message(s))", text)

  def test_eof_on_the_confirm_declines(self) -> None:
    repl, out = self._repl(_MockLlmClient(self.PLAN))
    repl.run(io.StringIO("/prompt wipe it\n"))  # stream ends before an answer
    self.assertIn("clear canceled", out.getvalue())
    self.assertNotIn("chat cleared", out.getvalue())
