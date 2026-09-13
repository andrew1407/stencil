from __future__ import annotations

"""Running a whole ``.stc`` file: the block loop over the inputs each ``@source`` names.

The core decides *what* to do; this decides what that means for a file — which inputs a
block opens, which editor carries them, and where each ``@save`` lands. Twin of
``cli/src/script/run.zig``. ``Editor.script`` is the single-image door; this is the
batch one.
"""

from dataclasses import dataclass, field

from ._script import Diagnostic, Script, ScriptError, parse_script
from ._types import NoneType
from .editor import Editor
from .editor.script import MAX_SCRIPT_BYTES, ScriptResult
from .scriptpaths import expand_source

__all__ = [
  "Diagnostic", "Script", "ScriptError", "ScriptRun", "parse_script",
  "read_script", "run_script", "run_script_text",
]


@dataclass
class ScriptRun:
  """One whole-file run: its diagnostics, the inputs it opened, the files it wrote."""

  diagnostics: tuple = tuple()
  inputs: list = field(default_factory=list)
  saved: list = field(default_factory=list)
  applied: int = 0
  empty_sources: list = field(default_factory=list)

  @property
  def has_errors(self) -> bool:
    return any(d.severity == "error" for d in self.diagnostics)


def read_script(path: str) -> str:
  """Read a ``.stc`` file, or stdin when ``path`` is ``"-"``."""
  if path == "-":
    import sys

    return sys.stdin.read(MAX_SCRIPT_BYTES)
  if ".." in path.replace("\\", "/").split("/"):
    raise ScriptError("refusing to read through '..': %s" % path)
  with open(path, "r", encoding="utf-8") as handle:
    text = handle.read(MAX_SCRIPT_BYTES + 1)
  if len(text) > MAX_SCRIPT_BYTES: raise ScriptError("that script is too large: %s" % path)
  return text


def label_for(path: str) -> str:
  """The name a diagnostic carries: the path as given, or ``<stdin>``."""
  return "<stdin>" if path == "-" else path


def run_script(
  path: str, *, source: (str | NoneType) = None, confine_output: bool = False, on_save=None
) -> ScriptRun:
  """Read the ``.stc`` at ``path`` and run it. ``source`` feeds a sourceless script."""
  return run_script_text(read_script(path), source=source, confine_output=confine_output,
               on_save=on_save)


def run_script_text(
  text: str, *, source: (str | NoneType) = None, confine_output: bool = False, on_save=None
) -> ScriptRun:
  """Run ``.stc`` source over every input its blocks name.

  A block with no ``@source`` runs against ``source`` (the one-shot ``-i`` input) and
  raises when there is none. A directory or glob block opens one fresh
  :class:`~pystencil.editor.Editor` per input, in sorted order. A script carrying any
  error runs nothing — the returned :class:`ScriptRun` still carries its diagnostics.
  """
  with parse_script(text) as program:
    run = ScriptRun(diagnostics=program.diagnostics)
    if run.has_errors: return run
    for block in program.blocks:
      inputs = _block_inputs(block, source)
      if not inputs and block.kind != "project": run.empty_sources.append(block.source)
      for path in inputs:
        run.inputs.append(path)
        _run_block_on(program, block, path, run, confine_output, on_save)
    return run


def _block_inputs(block, source: (str | NoneType)) -> list:
  """The concrete inputs one block opens."""
  if block.kind != "project": return expand_source(block.source, block.kind)
  if source is None:
    raise ScriptError("this script has no @source block — pass an input")
  return [source]


def _run_block_on(
  program: Script, block, path: str, run: ScriptRun, confine_output: bool, on_save
) -> None:
  """Open one input, replay the block's ops over it, and collect what it saved."""
  editor = Editor()
  editor.load(path, source=path)
  result: ScriptResult = editor.apply_script_ops(
    program, program.block_ops(block), confine_output=confine_output, on_save=on_save
  )
  run.applied += result.applied
  run.saved.extend(result.saved)
