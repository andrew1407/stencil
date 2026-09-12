from __future__ import annotations

"""/chat: multi-turn prompting and the §12 persisted transcript."""

import json

from ...llm import Chat
from ...server import ServerError
from ..registry import command


class _ChatCommands:
  """/chat on|off|clear and the remote transcript it restores."""
  @command("chat", usage="/chat [on|off|clear]",
      help="multi-turn /prompt + save chats with the project (default off)")
  def _cmd_chat(self, arg: str) -> None:
    """/chat: bare/``show`` prints the mode + turn count; ``on``/``off`` toggles
    §12 chat persistence for the session (default OFF — /prompt stays
    single-turn); ``clear`` empties the conversation (and best-effort drops the
    active remote project's server `chat` file while the mode is on)."""
    sub = arg.strip().lower()
    handlers = {"": self.__chat_show, "show": self.__chat_show, "on": self.__chat_on,
                "off": self.__chat_off, "clear": self.__chat_clear}
    if sub not in handlers:
      self._err("/chat takes on | off | clear | show (bare /chat shows the mode)")
      return
    handlers[sub]()

  def __chat_show(self) -> None:
    turns = len(self._chat.history) if self._chat is not None else 0
    self._say("chat %s (%d message(s))" % ("on" if self._chat_on else "off", turns))

  def __chat_on(self) -> None:
    self._chat_on = True
    self._say("chat on")
    # §12.2: say who can read a saved chat BEFORE one is written anywhere.
    self._say("  saved into the .stencil project on /save; on a server "
         "project, readable by everyone it is shared with")

  def __chat_off(self) -> None:
    self._chat_on = False
    self._say("chat off")

  def __chat_clear(self) -> None:
    if self._chat is not None:
      self._chat.clear()
    # §12: clearing the conversation clears the persisted server copy too.
    if self._chat_on and self._remote is not None:
      conn, pid = self._remote
      try:
        conn.delete_file(pid, "chat")
      except ServerError as e:
        self._note("could not delete the server chat (%s)" % e)
    self._say("chat cleared")

  def _restore_remote_chat(self, conn, pid: str) -> None:
    """Best-effort §12 restore of a fetched project's saved `chat` file.

    A missing file, a fetch error, or a document that parses to no messages all
    mean "no saved chat" — never a console error (the contract's forgiving-read
    rule). Restoring never triggers a model call.
    """
    try:
      doc = json.loads(bytes(conn.get_file(pid, "chat")).decode("utf-8"))
    except (ServerError, OSError, ValueError):
      return
    chat = Chat.from_doc(doc, self._llm_client())
    if chat.history:
      self._chat = chat
