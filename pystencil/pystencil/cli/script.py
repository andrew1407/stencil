from __future__ import annotations

"""The three one-shot script modes: ``--script``, ``--script-check``, ``--script-plan``.

``--script`` edits and writes like the rest of the pipeline (everything human on
stderr); the other two are written for an editor or an adapter to parse, so they print
to **stdout**. Twin of ``cli/src/script/{run,check,plan}.zig``.
"""

import sys
from typing import TextIO

from .._script import ScriptError, parse_script
from .._severity import emit_error, emit_note
from ..core import get_core
from ..script import label_for, read_script, run_script_text
from .scriptplan import lower_to_plan


def _report_diagnostics(program, label: str, err: TextIO) -> None:
  """Every diagnostic on the human channel, errors as ``error:`` and warnings as ``note:``."""
  for diag in program.diagnostics:
    line = "%s:%d:%d: %s [%s]" % (label, diag.line, diag.col, diag.message, diag.code)
    (emit_error if diag.severity == "error" else emit_note)(err, line)


def run_check(path: str, out: (TextIO | None) = None) -> int:
  """``--script-check``: one parsable line per diagnostic; exit 1 on any error."""
  out = out if out is not None else sys.stdout
  label = label_for(path)
  with parse_script(read_script(path)) as program:
    for diag in program.diagnostics: out.write(diag.format(label) + "\n")
    return 1 if program.has_errors else 0


def run_plan(path: str, source: (str | None) = None, out: (TextIO | None) = None) -> int:
  """``--script-plan``: the script lowered to op-plan JSON on stdout; exit 1 on any error."""
  out = out if out is not None else sys.stdout
  with parse_script(read_script(path)) as program:
    out.write(lower_to_plan(program, label_for(path), source, get_core()) + "\n")
    return 1 if program.has_errors else 0


def run_edit(path: str, source: (str | None), confine_output: bool, err: TextIO) -> int:
  """``--script``: run the script over its inputs, reporting each file it wrote."""
  label = label_for(path)
  text = read_script(path)

  def wrote(target: str, w: int, h: int) -> None:
    err.write("wrote %s (%dx%d)\n" % (target, w, h))

  with parse_script(text) as program:
    _report_diagnostics(program, label, err)
    if program.has_errors: return 1
  run = run_script_text(text, source=source, confine_output=confine_output, on_save=wrote)
  for spec in run.empty_sources: emit_note(err, "%s: no files matched" % spec)
  if not run.saved: emit_note(err, "the script saved nothing — add a @save")
  return 0


def run_mode(args, err: TextIO) -> int:
  """Dispatch the one script flag that was given. Returns a process exit code."""
  given = [f for f in ("script", "script_check", "script_plan") if getattr(args, f)]
  if len(given) > 1:
    emit_error(err, "pass only one of --script, --script-check, --script-plan")
    return 2
  try:
    if args.script_check: return run_check(args.script_check)
    if args.script_plan: return run_plan(args.script_plan, args.input)
    return run_edit(args.script, args.input, args.confine_output, err)
  except ScriptError as exc:
    emit_error(err, "%s" % exc)
    return 1
