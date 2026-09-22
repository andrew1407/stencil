"""Running a ``.stc`` script against this editor.

Each lowered op becomes one ordinary editor call — ``crop``/``set_filter``/``draw``/
``undo``/``redo``/``save`` — so a script and a hand-typed session move through exactly
the same code. Lengths resolve against the view as it stands at that op, because an
earlier crop already changed it. Twin of ``cli/src/console/handlers/script.zig``.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from .._script import Diagnostics, Op, Pixels, Script, ScriptError, parse_script
from ..layout import Line, Point
from ..scriptpaths import guard_target, read_script, resolve_target, save_format

Paths = list[str]


@dataclass
class ScriptResult:
  """What one script run did: its diagnostics, the edits it recorded, the files written."""

  diagnostics: Diagnostics = tuple()
  applied: int = 0
  saved: Paths = field(default_factory=list)
  sources_ignored: bool = False

  @property
  def has_errors(self) -> bool:
    return any(d.severity == "error" for d in self.diagnostics)


@dataclass
class _OpContext:
  """What a handler needs beyond the op: where saves collect, and how they are guarded.
  ``on_save`` is called with (path, width, height) as each ``@save`` lands."""

  result: ScriptResult
  confine_output: bool = False
  on_save: object = None


def _lround(v: float) -> int:
  """C's ``lround`` — half away from zero, which Python's banker's ``round`` is not."""
  return int(math.copysign(math.floor(abs(v) + 0.5), v))


def _line_from(op: Op, resolved: Pixels) -> Line:
  """One ``@line``/``@rect`` op plus its resolved pixels -> a drawable :class:`Line`."""
  coords, thickness, point_size = resolved[:-2], resolved[-2], resolved[-1]
  points = [Point(coords[i], coords[i + 1]) for i in range(0, len(coords) - 1, 2)]
  return Line(
    points=points,
    color=op.str_at(0),
    thickness=thickness,
    point_size=point_size,
    style=op.str_at(1) or "solid",
    locked=op.kind == "rect",
    fill_color=op.str_at(2),
    point_color=op.str_at(3),
  )


def _op_header(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  """``@open`` is the block header — the editor already holds what it names."""
  return 0


def _op_frame(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  """``@frame`` needs a video decoder this surface does not have (the same named
  deviation the ``frame`` op carries)."""
  raise ScriptError("line %d: @frame needs a video decoder — use the CLI" % op.line)


def _op_crop(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  width, height = editor.image_size
  editor.crop_rect(*(_lround(v) for v in program.resolve(op.index, width, height)[:4]))
  return 1


def _op_filter(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  mode, tint = op.str_at(0), op.str_at(1)
  editor.apply_filter(tint if mode == "custom" else mode)
  return 1


def _op_shape(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  width, height = editor.image_size
  editor.draw([_line_from(op, program.resolve(op.index, width, height))])
  return 1


def _op_layout(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  editor.draw(op.str_at(0), combine=op.str_at(1) != "replace")
  return 1


def _op_save(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  ctx.result.saved.append(editor._script_save(op, ctx))
  return 0


def _op_history(editor, program: Script, op: Op, ctx: _OpContext) -> int:
  step = editor.undo if op.kind == "undo" else editor.redo
  for _ in range(max(1, int(op.num_at(0, 1.0)))): step()
  return 0


HANDLERS = {
  "open": _op_header, "frame": _op_frame, "crop": _op_crop, "filter": _op_filter,
  "line": _op_shape, "rect": _op_shape, "layout": _op_layout, "save": _op_save,
  "undo": _op_history, "redo": _op_history,
}


class _ScriptApi:
  """``Editor.script`` / ``Editor.script_run`` — the ``.stc`` surface of the facade."""

  def script(self, text: str, *, confine_output: bool = False) -> ScriptResult:
    """Parse and run ``text`` against this editor, returning a :class:`ScriptResult`.

    A script with any error applies nothing. ``@source`` names a file this editor is
    not holding, so a sourced block is reported and its ops still run on what IS
    loaded — the console semantics, where the user is looking at one image.
    """
    with parse_script(text) as program:
      result = ScriptResult(diagnostics=program.diagnostics)
      result.sources_ignored = any(b.kind != "project" for b in program.blocks)
      if result.has_errors: return result
      applied = self.apply_script_ops(program, program.ops, confine_output=confine_output)
      result.applied, result.saved = applied.applied, applied.saved
      return result

  def apply_script_ops(self, program: Script, ops, *, confine_output: bool = False,
             on_save=None) -> ScriptResult:
    """Apply an already-parsed op run to this editor (the block loop's entry point)."""
    result = ScriptResult(diagnostics=program.diagnostics)
    ctx = _OpContext(result, confine_output, on_save)
    self._require_original()
    for op in ops:
      result.applied += HANDLERS[op.kind](self, program, op, ctx)
    return result

  def script_run(self, path: str, *, confine_output: bool = False) -> ScriptResult:
    """Read a ``.stc`` file and run it against this editor."""
    return self.script(read_script(path), confine_output=confine_output)

  def _script_save(self, op: Op, ctx: _OpContext) -> str:
    """Write the current view where ``@save`` points, and return the path."""
    source = self._source or ("%s.png" % (self._name or "image"))
    path = resolve_target(op.str_at(0), source, None, save_format(self._source_ext))
    guard_target(path, ctx.confine_output)
    img = self.save(path)
    if ctx.on_save is not None: ctx.on_save(path, img.width, img.height)
    return path
