"""Running a whole ``.stc`` file: which inputs each block opens, which editor carries them,
and where each ``@save`` lands — twin of ``cli/src/script/run.zig``.
"""

from __future__ import annotations

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
  """Run a parsed program whose handle is still open. A block with no ``@source`` runs on
  ``source`` and raises without one; a directory or glob opens a fresh ``Editor`` per input,
  sorted. A program with any error runs nothing and returns its diagnostics."""
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
