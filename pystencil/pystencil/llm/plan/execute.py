"""Plan execution over the Editor facade: :func:`execute_op_plan` walks a validated plan."""

from __future__ import annotations

from typing import Any, Sequence

from ..._ffi.types import NoneType
from ..._raster.parallel import map_parallel
from ..errors import LlmExecutionError
from .frame import _FrameMap
from .registry import FORBIDDEN_OPS, _ACTION_APPLIERS
from ..run import _PlanRun
from ..types import OpPlan

# 4 already saturates the memory bandwidth one image walk needs.
MAX_VARIANT_WORKERS = 4

# The ops that transform pixels: with nothing loaded each is a skipped action, like save.
_NEEDS_IMAGE = ("crop", "rotate", "filter", "layout")


def __apply_action(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: ("_PlanRun" | NoneType) = None) -> None:
  """Apply one validated action via the Editor method the op maps to (contract §2)."""
  op = action["op"]
  if op in FORBIDDEN_OPS:  # §13's second tooth — guards hand-built plans too
    raise LlmExecutionError(
      'the "%s" op is never model-drivable on any surface (contract §13)' % op
    )
  applier = _ACTION_APPLIERS.get(op)
  if applier is None:  # unreachable after parse_op_plan; guards hand-built plans
    raise LlmExecutionError("unsupported op %r" % op)
  has_image = getattr(editor, "has_image", None)
  if op in _NEEDS_IMAGE and has_image is not None and not has_image():
    if run is not None: run.notes.append("Skipped %s — no working image to edit" % op)
    return
  applier(action, editor, frame, run)


def execute_op_plan(
  plan: OpPlan,
  editor: Any,
  attachments: (Sequence | NoneType) = None,
  save_dir: str = "",
  console: Any = None,
) -> list:
  """Execute a validated plan against an :class:`~pystencil.editor.Editor`.

  Top-level ``actions`` mutate ``editor`` in place; each variant branches from the
  flattened post-actions pixels into a fresh editor. Returns the working image first
  (only when there were top-level actions), then one image per variant in order; a
  chat-only plan returns ``[]``. Coordinates re-map through the plan's own crops/rotates (§1).

  ``attachments`` are the turn's ``(media_type, bytes[, name])`` tuples, indexed 1-based by
  the §2.1 ``image`` op; ``save`` writes ``<name>.stencil`` into ``save_dir`` (cwd by
  default), paths on ``plan.saved``. An unsatisfiable index or a save with nothing loaded
  costs that action only, noted in ``plan.warnings`` and ``plan.reply``. ``console`` is the
  §10 hook object the console-profile ops run through; without it they skip with a note.
  """
  outputs: list = list()
  base = None  # the post-actions snapshot, rendered at most once
  frame = _FrameMap()  # §1: plan coordinates are in the pre-plan frame
  run = _PlanRun(attachments, save_dir, console)
  for action in plan.actions: __apply_action(action, editor, frame, run)
  plan.saved.extend(run.saved)
  if run.notes:
    plan.warnings.extend(run.notes)
    plan.reply = plan.reply + "\n" + "\n".join("[warning] " + w for w in run.notes)
  # A plan may legitimately end with nothing loaded (a §10 `clear`, or console ops
  # alone, which per §10 run without a working image) — then there is no result.
  has_image = getattr(editor, "has_image", None)
  empty = has_image is not None and not has_image()
  if plan.actions and not empty:
    base = editor.result()
    outputs.append(base)
  if plan.variants:
    if empty:
      note = "Skipped the variants — no working image is left to branch from"
      plan.warnings.append(note)
      plan.reply = plan.reply + "\n[warning] " + note
      return outputs
    if base is None:
      base = editor.result()
    branches = list()
    for variant in plan.variants:
      # The core rides along: a branch left to load its own would race the singleton
      # (and its lazy c++ build) from a worker thread.
      branch = type(editor)(core=getattr(editor, "_core", None))
      branch.load(base, name=variant.label)
      branches.append((branch, variant, frame.branch()))
    outputs.extend(map_parallel(branches, __render_branch, MAX_VARIANT_WORKERS))
  return outputs


def __render_branch(job) -> Any:
  """One variant on its own editor, rendered: the editor, pixels and frame are this job's
  alone and a variant's ops are pure edits, so it runs on any thread."""
  branch, variant, vframe = job
  for action in variant.actions: __apply_action(action, branch, vframe)
  return branch.result()
