from __future__ import annotations

"""Running a whole ``.stc`` file: the block loop over the inputs each ``@source`` names.

The core decides *what* to do; this decides what that means for a file — which inputs a
block opens, which editor carries them, and where each ``@save`` lands. Twin of
``cli/src/script/run.zig``. ``Editor.script`` is the single-image door; this is the
batch one.
"""

from dataclasses import dataclass, field

from ._script import Diagnostic, Diagnostics, Script, ScriptError, parse_script
from ._ffi.types import NoneType
from .editor import Editor
from .editor.script import ScriptResult
from .scriptpaths import expand_source, read_script

__all__ = [
  "Diagnostic", "Script", "ScriptError", "ScriptRun", "parse_script",
  "read_script", "run_program", "run_script", "run_script_text",
]

Paths = list[str]


@dataclass
class ScriptRun:
  """One whole-file run: its diagnostics, the inputs it opened, the files it wrote."""

  diagnostics: Diagnostics = tuple()
  inputs: Paths = field(default_factory=list)
  saved: Paths = field(default_factory=list)
  applied: int = 0
  empty_sources: Paths = field(default_factory=list)

  @property
  def has_errors(self) -> bool:
    return any(d.severity == "error" for d in self.diagnostics)


def run_script(
  path: str, *, source: (str | NoneType) = None, confine_output: bool = False, on_save=None
) -> ScriptRun:
  """Read the ``.stc`` at ``path`` and run it. ``source`` feeds a sourceless script."""
  return run_script_text(read_script(path), source=source, confine_output=confine_output,
               on_save=on_save)


def run_script_text(
  text: str, *, source: (str | NoneType) = None, confine_output: bool = False, on_save=None
) -> ScriptRun:
  """Parse ``.stc`` source and run it over every input its blocks name."""
  with parse_script(text) as program:
    return run_program(program, source=source, confine_output=confine_output,
               on_save=on_save)


def run_program(
  program: Script, *, source: (str | NoneType) = None, confine_output: bool = False,
  on_save=None
) -> ScriptRun:
  """Run an already-parsed program, so a caller that reported its diagnostics need not
  parse it twice. The handle must still be open — the shape ops resolve through it.

  A block with no ``@source`` runs against ``source`` (the one-shot ``-i`` input) and
  raises when there is none. A directory or glob block opens one fresh
  :class:`~pystencil.editor.Editor` per input, in sorted order. A script carrying any
  error runs nothing — the returned :class:`ScriptRun` still carries its diagnostics.
  """
  run = ScriptRun(diagnostics=program.diagnostics)
  if run.has_errors: return run
  for block in program.blocks:
    inputs = _block_inputs(block, source)
    if not inputs and block.kind != "project": run.empty_sources.append(block.source)
    for path in inputs:
      run.inputs.append(path)
      _run_block_on(program, block, path, run, confine_output, on_save)
  return run


def _block_inputs(block, source: (str | NoneType)) -> Paths:
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
