from __future__ import annotations

"""Running a ``.stc`` script against this editor.

Each lowered op becomes one ordinary editor call — ``crop``/``set_filter``/``draw``/
``undo``/``redo``/``save`` — so a script and a hand-typed session move through exactly
the same code. Lengths resolve against the view as it stands at that op, because an
earlier crop already changed it. Twin of ``cli/src/console/handlers/script.zig``.
"""

import math
from dataclasses import dataclass, field

from .._script import Op, Script, ScriptError, parse_script
from .._types import NoneType
from ..layout import Line, Point
from ..scriptpaths import guard_target, resolve_target

MAX_SCRIPT_BYTES = 4 << 20


@dataclass
class ScriptResult:
  """What one script run did: its diagnostics, the edits it recorded, the files written."""

  diagnostics: tuple = tuple()
  applied: int = 0
  saved: list = field(default_factory=list)
  sources_ignored: bool = False
  #: Called with (path, width, height) as each @save lands, so a caller reports in order.
  on_save: object = None

  @property
  def has_errors(self) -> bool:
    return any(d.severity == "error" for d in self.diagnostics)


def _lround(v: float) -> int:
  """C's ``lround`` — half away from zero, which Python's banker's ``round`` is not."""
  return int(math.copysign(math.floor(abs(v) + 0.5), v))


def _line_from(op: Op, resolved: list) -> Line:
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
    result = ScriptResult(diagnostics=program.diagnostics, on_save=on_save)
    self._require_original()
    for op in ops:
      result.applied += self._apply_script_op(program, op, result, confine_output)
    return result

  def script_run(self, path: str, *, confine_output: bool = False) -> ScriptResult:
    """Read a ``.stc`` file and run it against this editor."""
    with open(path, "r", encoding="utf-8") as handle:
      text = handle.read(MAX_SCRIPT_BYTES + 1)
    if len(text) > MAX_SCRIPT_BYTES: raise ScriptError("that script is too large: %s" % path)
    return self.script(text, confine_output=confine_output)

  def _apply_script_op(
    self, program: Script, op: Op, result: ScriptResult, confine_output: bool
  ) -> int:
    """Apply one lowered op; returns 1 when it recorded an edit, 0 otherwise."""
    if op.kind in ("open", "frame"): return self._skip_script_op(op)
    width, height = self.image_size
    if op.kind == "crop":
      self.crop_rect(*(_lround(v) for v in program.resolve(op.index, width, height)[:4]))
    elif op.kind == "filter":
      mode, tint = op.str_at(0), op.str_at(1)
      self.apply_filter(tint if mode == "custom" else mode)
    elif op.kind in ("line", "rect"):
      self.draw([_line_from(op, program.resolve(op.index, width, height))])
    elif op.kind == "layout":
      self.draw(op.str_at(0), combine=op.str_at(1) != "replace")
    elif op.kind == "save":
      result.saved.append(self._script_save(op, result, confine_output))
      return 0
    elif op.kind in ("undo", "redo"):
      step = self.undo if op.kind == "undo" else self.redo
      for _ in range(max(1, int(op.num_at(0, 1.0)))): step()
      return 0
    return 1

  @staticmethod
  def _skip_script_op(op: Op) -> int:
    """``@open`` is the block header; ``@frame`` needs a video decoder this surface
    does not have (the same named deviation the ``frame`` op carries)."""
    if op.kind == "frame":
      raise ScriptError("line %d: @frame needs a video decoder — use the CLI" % op.line)
    return 0

  def _script_save(self, op: Op, result: ScriptResult, confine_output: bool) -> str:
    """Write the current view where ``@save`` points, and return the path."""
    source = self._source or ("%s.png" % (self._name or "image"))
    fmt = self._source_ext if self._source_ext in ("png", "bmp") else "png"
    path = resolve_target(op.str_at(0), source, None, fmt)
    guard_target(path, confine_output)
    img = self.save(path, fmt)
    if result.on_save is not None: result.on_save(path, img.width, img.height)
    return path
