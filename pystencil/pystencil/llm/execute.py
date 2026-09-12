from __future__ import annotations

"""Plan execution over the Editor facade: :func:`execute_op_plan`, the one entry
point that walks a validated plan (and its variants) against an editor.
"""

from typing import Any, Optional, Sequence

from .._parallel import map_parallel
from .errors import LlmExecutionError
from .frame import _FrameMap
from .registry import FORBIDDEN_OPS, _ACTION_APPLIERS
from .run import _PlanRun
from .types import OpPlan

# 4 already saturates the memory bandwidth one image walk needs.
MAX_VARIANT_WORKERS = 4


def _apply_action(action: dict, editor: Any, frame: Optional[_FrameMap] = None,
                 run: Optional["_PlanRun"] = None) -> None:
    """Apply one validated action via the Editor method the op maps to (contract §2)."""
    op = action["op"]
    if op in FORBIDDEN_OPS:  # §13's second tooth — guards hand-built plans too
        raise LlmExecutionError(
            'the "%s" op is never model-drivable on any surface (contract §13)' % op
        )
    applier = _ACTION_APPLIERS.get(op)
    if applier is None:  # unreachable after parse_op_plan; guards hand-built plans
        raise LlmExecutionError("unsupported op %r" % op)
    applier(action, editor, frame, run)


def execute_op_plan(
    plan: OpPlan,
    editor: Any,
    attachments: Optional[Sequence] = None,
    save_dir: str = "",
    console: Any = None,
) -> list:
    """Execute a validated plan against an :class:`~pystencil.editor.Editor`.

    Top-level ``actions`` mutate ``editor`` in place (contract semantics: at most one
    updated result). Each variant then branches from the *flattened pixels* of the
    post-actions state — a fresh editor of the same class is loaded with that snapshot,
    the variant's actions run on it, and it yields one extra output. Returns the list
    of result :class:`~pystencil.image.Image` objects: the updated working image first
    (only when there were top-level actions), then one image per variant, in order
    (labels live in ``plan.variants[i].label``). A chat-only plan returns ``[]``.

    Plan coordinates arrive in the frame of the image the model was shown, so a
    running :class:`_FrameMap` re-maps later layout points through the plan's own
    crops/rotates (contract §1); variants continue from the top-level transform.

    ``attachments`` are the images the turn attached, in attachment order — the same
    list handed to :meth:`Chat.send` / :meth:`Editor.prompt`, as ``(media_type, bytes)``
    tuples (an optional third element names the source file, which an unnamed ``save``
    derives its project name from). The §2.1 ``image`` op indexes them 1-based and
    ``save`` writes ``<name>.stencil`` into ``save_dir`` (the cwd by default), with the
    written paths collected on ``plan.saved``. Per §2.1 an index the turn cannot
    satisfy, or a save with nothing loaded, costs that ACTION only: the note lands in
    ``plan.warnings`` (and, like a parse warning, on ``plan.reply``).

    ``console`` attaches the interactive console's §10 hook object (the REPL passes
    itself) so the console-profile ops — connect/disconnect/delete/openUrl/clear/
    clearChat — execute through the same paths its commands use; without one they
    are skipped with a note (the library API has no connections, no /delete scope,
    no user-echo guard for openUrl, and no conversation for clearChat).
    """
    outputs: list = []
    base = None  # the post-actions snapshot, rendered at most once
    frame = _FrameMap()  # §1: plan coordinates are in the pre-plan frame
    run = _PlanRun(attachments, save_dir, console)
    for action in plan.actions:
        _apply_action(action, editor, frame, run)
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
            base = editor.result()  # snapshot AFTER the top-level actions
        branches = []
        for variant in plan.variants:
            branch = type(editor)()
            branch.load(base, name=variant.label)
            branches.append((branch, variant, frame.branch()))
        outputs.extend(map_parallel(branches, _render_branch, MAX_VARIANT_WORKERS))
    return outputs


def _render_branch(job) -> Any:
    """Apply one variant's actions to its own editor and render it — the unit of fan-out.

    Runnable on any thread by construction: the branch editor, its pixels and its
    coordinate frame are this job's alone, and a variant's ops are pure edits (no
    attachments, no save, no console hooks). The branches themselves are built in plan
    order by the caller, so a branch's identity never depends on when it finished.
    """
    branch, variant, vframe = job
    for action in variant.actions:
        _apply_action(action, branch, vframe)
    return branch.result()
