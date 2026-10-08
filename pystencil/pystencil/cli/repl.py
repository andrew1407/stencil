"""The interactive console: one Editor plus the server connections, and the command
table the mixins below register into.
"""

from __future__ import annotations

from typing import TextIO

from .._ffi.types import NoneType
from ..editor import Editor
from ..llm import AskCard, Chat, LlmConfig
from ..server import ConnectionManager, ServerError
from .commands.chat import _ChatCommands
from .commands.edit import _EditCommands
from .commands.llm import _LlmCommands
from .commands.page import _PageCommands
from .commands.prompt import _PromptCommands
from .commands.script import _ScriptCommands
from .commands.server import _ServerCommands
from .commands.source import _SourceCommands
from .console import Console
from .hooks import _PlanHooks
from .registry import build_help, build_table, command


def _parse_command(line: str) -> tuple[str, str]:
  """(verb, arg) split at the first whitespace, one leading '/' dropped (commands.zig
  parseCommand); a ``://`` in the argument is preserved."""
  s = line.strip()
  if s.startswith("/"): s = s[1:].lstrip(" \t")
  if not s: return ("", "")
  parts = s.split(None, 1)
  if len(parts) == 1: return (parts[0], "")
  return (parts[0], parts[1].strip())


class _Repl(
  _SourceCommands,
  _PageCommands,
  _EditCommands,
  _ScriptCommands,
  _ServerCommands,
  _PromptCommands,
  _LlmCommands,
  _ChatCommands,
  _PlanHooks,
):
  """The interactive console state: one Editor plus the server connections."""

  def __init__(self, out: TextIO) -> None:
    self._editor = Editor()
    # The last `ask` card the assistant printed (contract §11), so the next /prompt can
    # answer it by number. Replaced by a later card; cleared once answered.
    self._ask: (AskCard | NoneType) = None
    self._manager = ConnectionManager()
    self._console = Console(out)
    # A bad STENCIL_LLM_* provider falls back to the defaults (llm-contract.md §5).
    try:
      self._llm = LlmConfig.from_env()
    except ValueError:
      self._llm = LlmConfig()
    # /prompt attachment memo: (editor, its revision, png bytes) — reused while the
    # edit state is unchanged so repeated prompts don't re-encode (contract §7).
    self._png_cache: (tuple[Editor, int, bytes] | NoneType) = None
    # §2.1: this turn's /upload set — (media_type, png bytes, label) in upload order, what a
    # plan's `image` op indexes. Capped at MAX_UPLOAD_ATTACHMENTS, the oldest falling off.
    self._attachments: list[tuple[str, bytes, str]] = list()
    self._attachments_used: bool = False
    # §12: /chat is session-scoped and off by default, so /prompt stays single-turn.
    self._chat_on: bool = False
    self._chat: (Chat | NoneType) = None
    # The active remote project recorded by /fetch, so /chat clear can also drop the
    # server-side `chat` file. Cleared whenever the working image is replaced.
    self._remote: (tuple | NoneType) = None
    self._clear_chat_pending: bool = False
    # None until run() starts, which reads a clearChat confirm as declined.
    self._in: (TextIO | NoneType) = None

  @property
  def _out(self) -> TextIO:
    return self._console.out

  def _say(self, msg: str) -> None:
    self._console.say(msg)

  def _err(self, msg: str) -> None:
    self._console.err(msg)

  def _note(self, msg: str) -> None:
    self._console.note(msg)

  def _report_wrote(self, path: str, w: int, h: int) -> None:
    self._console.report_wrote(path, w, h)

  def _image_replaced(self, editor: (Editor | NoneType) = None) -> None:
    """The one chokepoint for "the working image was replaced": resets the remote project
    and the conversation, so ``/chat clear`` never deletes the previous project's chat
    (twin of ``cli/src/console/session.zig`` setRemote()/clearRemote())."""
    if editor is not None: self._editor = editor
    self._remote = None
    self._chat = None

  def run(self, src: TextIO) -> int:
    """Read/dispatch ``/command`` lines until EOF or ``/exit``."""
    self._in = src
    for raw in src:
      word, arg = _parse_command(raw)
      if not word: continue
      try:
        if self.__dispatch(word, arg): break
      except (ValueError, RuntimeError, OSError, ServerError) as e:
        self._err("%s" % e)
    return 0

  def __dispatch(self, word: str, arg: str) -> bool:
    """Run one command. Returns True to request exiting the REPL."""
    handler = self._TABLE.get(word.lower())
    if handler is None:
      self._err("unknown command '/%s' (try /help)" % word)
      return False
    return bool(handler(self, arg))

  @command("drop", "close", "forget")
  def _cmd_drop(self, arg: str) -> None:
    self._image_replaced(Editor())
    self._say("dropped the working image")

  @command("help", "?", "h", usage="/help", help="this list (aliases: ?, h)")
  def _cmd_help(self, arg: str) -> None:
    self._say(self._HELP)

  @command("exit", "quit", "q", usage="/exit", help="leave (aliases: quit, q)")
  def _cmd_exit(self, arg: str) -> bool:
    return True


# The verb table and the /help listing both come from the @command declarations, so a
# new command cannot be dispatchable but unlisted (or listed but undispatchable).
_Repl._TABLE = build_table(_Repl)
_Repl._HELP = build_help(_Repl)
_HELP = _Repl._HELP
