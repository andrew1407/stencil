"""One block's lowered ops as the plans an executor replays. A ``layout`` sets the drawn lines
(llm-contract §2), so each carries every line that should show — the marks before it, moved by
the crops since — in the §1 frame its plan started in. At most MAX_ACTIONS a plan; an ``undo``
steps the history entries actions pushed, never script edits, and ends its plan. Twin of
``cli/src/script/plan/sequence.zig``.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from .._script import Script, ScriptError
from .._ffi.types import NoneType
from . import planactions as acts
from . import planlines

# Copies of the registry's limits.MAX_ACTIONS and MAX_UNDO_STEPS;
# tests/script/test_script_cli.py pins the first against it.
MAX_ACTIONS = 16
_MAX_STEPS = 20
_TOO_MANY = ("more than %d lines would show at once, and a plan's layout carries at most %d "
             "(limits.MAX_LAYOUT_LINES)" % (planlines.MAX_LINES, planlines.MAX_LINES))


class PlanRefused(Exception):
  """Why a block cannot be planned: the op it stopped at, a diagnostic code and message."""

  def __init__(self, op, code: str, message: str) -> None:
    super().__init__(message)
    self.op, self.code, self.message = op, code, message


@dataclass
class _Entry:
  """One history entry group an executor holds: the script edits it made (the newest ``shapes``
  among them), its entries, and the lines and size before it — None lines: nobody knows them."""

  edits: int
  steps: int
  drawn: (list | NoneType)
  dims: (tuple | NoneType)
  shapes: list = field(default_factory=list)


def _round(v: float) -> int:
  """Zig's ``@round``: halves away from zero, where Python's ``round`` goes to even."""
  whole = math.floor(abs(v))
  whole += 1 if abs(v) - whole >= 0.5 else 0
  return int(math.copysign(whole, v))


class _Sequence:
  def __init__(self, dims: (tuple | NoneType), core) -> None:
    self.dims, self.core, self.drawn = dims, core, list()  # drawn: what shows, in view pixels
    self.plans, self.cur, self.pending, self.hist = list(), list(), list(), list()
    self.dx = self.dy = 0.0  # the crop origins this plan has run, which an executor subtracts (§1)

  def push(self, action: dict) -> None:
    if len(self.cur) == MAX_ACTIONS: self.cut()
    self.cur.append(action)

  def cut(self) -> None:
    """Ends the plan being filled: the next is written in the frame the executor then shows."""
    if not self.cur: return
    self.plans.append(self.cur)
    self.cur, self.dx, self.dy = list(), 0.0, 0.0

  def land(self, shown: list, edits: int, shapes: list) -> None:
    """One ``layout`` setting the drawn lines to ``shown``: one entry for ``edits`` script edits."""
    if len(self.cur) == MAX_ACTIONS: self.cut()  # the frame of the plan it lands in
    self.push(acts.lines_action(shown, self.dx, self.dy))
    self.hist.append(_Entry(edits, 1, self.drawn, self.dims, shapes))
    self.drawn = shown

  def flush(self) -> None:
    """Lands the waiting shapes over what shows; they wait only where the lines are known."""
    if not self.pending: return
    shapes, self.pending = self.pending, list()
    self.land(self.drawn + shapes, len(shapes), shapes)

  def edit(self, actions: list) -> None:
    """One script edit that is not a shape: its actions, one history entry each."""
    self.flush()
    for action in actions: self.push(action)
    self.hist.append(_Entry(1, len(actions), self.drawn, self.dims))

  def shape(self, op, shape: dict) -> None:
    if len(self.drawn) + len(self.pending) >= planlines.MAX_LINES:
      raise PlanRefused(op, "E_PLAN_TOO_MANY_LINES", _TOO_MANY)
    self.pending.append(shape)

  def undo(self, n: int) -> None:
    """The newest edits still waiting never land; past them, the executor's own steps."""
    waiting = min(n, len(self.pending))
    del self.pending[len(self.pending) - waiting:]
    left, steps = n - waiting, 0
    if left == 0: return
    while left > 0 and self.hist:
      e = self.hist.pop()
      steps += e.steps
      self.drawn, self.dims = e.drawn, e.dims
      if e.edits > left: self.pending.extend(e.shapes[:e.edits - left])
      left = max(0, left - e.edits)
    if steps == 0: return
    while steps > 0:
      self.push(acts.number_action("undo", "steps", min(steps, _MAX_STEPS)))
      steps -= min(steps, _MAX_STEPS)
    self.cut()

  def restart(self, dims: (tuple | NoneType)) -> None:
    """A fresh picture: nothing placed before it survives, and its frame starts over."""
    self.pending, self.hist, self.dims, self.drawn = list(), list(), dims, list()
    self.dx = self.dy = 0.0

  def crop(self, action: dict, rect: (tuple | NoneType)) -> None:
    before = self.dims
    self.edit([action])
    if before is None:
      # No size to follow the crop by: what it does to the lines drawn is unknown.
      if self.drawn: self.drawn = None
      return
    if rect is None: return
    # The window ``--script`` commits (its crop's clamp), not the unrounded one.
    r = self.core.snap_crop_rect(rect, int(before[0]), int(before[1]))
    if self.drawn is not None:
      self.drawn = planlines.recropped(self.drawn, (0, 0, int(before[0]), int(before[1])), r)
    self.dims, self.dx, self.dy = (float(r[2]), float(r[3])), self.dx + r[0], self.dy + r[1]

  def document(self, op) -> None:
    """``@layout``: the document's lines over what shows, or alone for "replace", as one layout."""
    src = op.str_at(0)
    doc, why = planlines.document(src, int(op.num_at(0, 1.0)) == 2)
    if doc is None:
      raise PlanRefused(op, "E_PLAN_LAYOUT_UNREADABLE", "could not load the layout '%s': %s" % (src, why))
    self.flush()
    under = list() if op.str_at(1) == "replace" else self.drawn
    if under is None:
      raise PlanRefused(op, "E_PLAN_LINES_UNKNOWN",
                        "a crop with no image size to follow moved the lines this layout lands on")
    if len(under) + len(doc) > planlines.MAX_LINES:
      raise PlanRefused(op, "E_PLAN_TOO_MANY_LINES", _TOO_MANY)
    self.land(under + doc, 1, list())


def _resolved(program: Script, op, dims: tuple) -> (list | NoneType):
  try:
    r = program.resolve(op.index, dims[0], dims[1])
  except ScriptError:
    return None
  return r if len(r) >= 4 else None


def _shape(op, r) -> dict:
  return {"points": list(r[:-2]), "color": op.str_at(0), "style": op.str_at(1),
          "fill_color": op.str_at(2), "point_color": op.str_at(3), "thickness": r[-2],
          "point_size": r[-1], "locked": op.kind == "rect"}


def _plan_open(seq, program, op, ctx) -> None:
  if ctx["first"]:
    seq.push(acts.open_action(ctx["first"], ctx["url"]))
    seq.restart(ctx["dims"])


def _plan_frame(seq, program, op, ctx) -> None:
  seq.push(acts.number_action("frame", "index", int(max(0.0, op.num_at(0)))))
  seq.restart(ctx["dims"])  # --script decodes the frame afresh: the marks and edits go


def _plan_crop(seq, program, op, ctx) -> None:
  r = _resolved(program, op, seq.dims or (0.0, 0.0))
  seq.crop(acts.crop_action(op, seq.dims), tuple(_round(v) for v in r[:4]) if r else None)


def _plan_filter(seq, program, op, ctx) -> None:
  seq.edit([acts.filter_action(op.str_at(0), op.str_at(1), seq.core)])


def _plan_shape(seq, program, op, ctx) -> None:
  """A shape with nothing to resolve against is dropped, but stays an edit an @undo counts."""
  r = _resolved(program, op, seq.dims) if seq.dims else None
  if r: seq.shape(op, _shape(op, r))
  else: seq.edit([])


def _plan_save(seq, program, op, ctx) -> None:
  seq.flush()
  seq.push(acts.save_action(op.str_at(0)))


ACTIONS = {
  "open": _plan_open, "frame": _plan_frame, "crop": _plan_crop, "filter": _plan_filter,
  "line": _plan_shape, "rect": _plan_shape, "layout": lambda seq, program, op, ctx: seq.document(op),
  "save": _plan_save, "undo": lambda seq, program, op, ctx: seq.undo(max(1, int(op.num_at(0, 1.0)))),
}  # no "redo": stc-contract §7 resolves every @redo statically


def build(program: Script, block, first_input: str, dims: (tuple | NoneType), core) -> list:
  """The plans one block lowers to, in order. ``dims`` is None when nothing local could be
  probed — the shape ops are then dropped rather than resolved against a size nobody has.
  Raises PlanRefused with the reason a block cannot be planned."""
  seq = _Sequence(dims, core)
  ctx = {"first": first_input, "url": block.kind == "url", "dims": dims}
  for op in program.block_ops(block):
    ACTIONS[op.kind](seq, program, op, ctx)
  seq.flush()
  seq.cut()
  return seq.plans
