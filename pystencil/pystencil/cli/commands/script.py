from __future__ import annotations

"""``/script`` and ``/script-run`` — run a ``.stc`` against the loaded image.

The console owns a session, not a file, so a ``@source`` block is reported and its ops
still apply to what is open: that is what the user is looking at. Twin of
``cli/src/console/handlers/script.zig``.
"""

from ..._script import ScriptError, parse_script
from ...scriptpaths import read_script
from ..registry import command

USAGE = (
  "usage: /script <directives…>  (';' separates statements, "
  "e.g. /script @crop 25%;@filter bw)"
)
RUN_NEEDS_PATH = "usage: /script-run <file.stc>"
UNREADABLE = "could not read that script"
FAILED = "the script stopped partway — earlier edits stand"
NO_EDITS = "that script recorded no edit here"
SOURCE_IGNORED = "/script ignores @source — the ops apply to the loaded image"


class _ScriptCommands:
  """The two script verbs, both running against the one working image."""

  @command("script", "stc", usage="/script <directives>",
      help="run a .stc script on this image (';' separates statements)")
  def _cmd_script(self, arg: str) -> None:
    text = arg.strip()
    if not text:
      self._say(USAGE)
      return
    self.__run_source(text, "<console>")

  @command("script-run", "scriptrun", "runscript", usage="/script-run <file.stc>",
      help="run a .stc script file on this image")
  def _cmd_script_run(self, arg: str) -> None:
    path = arg.strip()
    if not path:
      self._say(RUN_NEEDS_PATH)
      return
    try:
      text = read_script(path)
    except (OSError, ScriptError, UnicodeDecodeError):
      self._err(UNREADABLE)
      return
    self.__run_source(text, path)

  def __run_source(self, text: str, label: str) -> None:
    """Parse, report every diagnostic, then apply the ops to the working image."""
    with parse_script(text) as program:
      for diag in program.diagnostics:
        report = self._err if diag.severity == "error" else self._note
        report(diag.console_line(label))
      if program.has_errors: return
      if not self._editor.has_image():
        self._err("no image loaded")
        return
      if any(b.kind != "project" for b in program.blocks): self._note(SOURCE_IGNORED)
      self.__apply(program)

  def __apply(self, program) -> None:
    """Run the whole op stream, reporting each ``@save`` as it lands."""
    try:
      result = self._editor.apply_script_ops(program, program.ops, on_save=self._report_wrote)
    except (ScriptError, RuntimeError, OSError, ValueError) as exc:
      self._err("%s" % exc if isinstance(exc, ScriptError) else FAILED)
      return
    if result.applied == 0 and not result.saved:
      self._say(NO_EDITS)
      return
    w, h = self._editor.image_size
    self._say("script -> %dx%d" % (w, h))
