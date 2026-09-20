from __future__ import annotations

"""One applier per op — the validated action mapped onto the Editor method it stands
for (contract §2). Each rides its op's OP_REGISTRY entry beside the validator, so
dispatch is table-driven on both the parse and the execute side.
"""

from typing import Any

from .._types import NoneType
from ..layout import Line
from .errors import LlmExecutionError
from .frame import _FrameMap
from .run import (
  _PlanRun,
  _attachment_parts,
  _clamp_point,
  _save_stem,
  _unique_save_path,
)

def _apply_crop(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  spec = action["spec"]
  # §10 console profile: the "album": true spec key rides beside the tokens and maps
  # onto the /crop … album axis derivation, never onto the token spec string.
  album = bool(spec.get("album"))
  spec_str = " ".join(
    "%s=%s" % (k, spec[k]) for k in ("x1", "y1", "x2", "y2", "aspect") if k in spec
  )
  if frame is not None:
    # Resolve exactly as Editor.crop is about to (the same core resolveCrop path); a bad spec
    # is the same no-op crop() performs, so the transform stays unchanged.
    resolve = getattr(editor, "resolve_crop_rect", None)
    rect = None
    if callable(resolve):
      rect = resolve(spec_str, album=album) if album else resolve(spec_str)
    if rect is not None: frame.push_crop(rect[0], rect[1])
  editor.crop(spec_str, album=album) if album else editor.crop(spec_str)


def _apply_rotate(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  times = action.get("times", 1)
  quarters = -times if action["dir"] == "left" else times
  if frame is not None:
    size = getattr(editor, "image_size", None)
    if size is not None: frame.push_rotate(quarters, size[0], size[1])
  editor.rotate(quarters)


def _apply_filter(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  if action["mode"] == "custom":
    editor.set_filter_color(action["tint"])
  else:
    editor.set_filter(action["mode"])


def _apply_layout(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  lines = [Line.from_dict(d) for d in action["lines"]]
  size = getattr(editor, "image_size", None)
  for line in lines:
    for p in line.points:
      x, y = frame.map_point(p.x, p.y) if frame is not None else (p.x, p.y)
      if size is not None: x, y = _clamp_point(x, y, size[0], size[1])
      p.x, p.y = x, y
  editor.draw(lines)


def _apply_formula(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  # §2: `enabled` alone toggles formulas (false restores identity, keeping the expressions);
  # otherwise set_formula validates via the shared parser.
  if "enabled" in action:
    editor.set_allow_formulas(action["enabled"])
  else:
    editor.set_formula(action["axis"], action["expr"])


def _apply_page(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  # §2: custom cm dims map onto the editors' custom page size (one form or the other).
  if "width" in action:
    editor.set_page_format("custom", action["width"], action["height"])
  else:
    editor.set_page_format(action["format"])


def _apply_blank(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  # §2: explicit cm dims override "format" — rendered at the core's default DPI,
  # exactly like the console's `/blank <w> <h>` custom flow.
  if "width" in action:
    from ..core import get_core  # lazy, like Editor's own core access

    w_px, h_px = get_core().default_blank_size_px(action["width"], action["height"])
    editor.blank(w_px, h_px, color=action["color"])
  else:
    editor.blank(color=action["color"], page=action.get("format") or "A4")


def _apply_frame(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  raise LlmExecutionError(
    'the "frame" op needs a video input; pystencil has no video decoding '
    "(no ffmpeg) and operates on supplied frames — extract the frame with "
    "the CLI/desktop first and load() it as an image"
  )


def _apply_image(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  """§2.1: adopt the turn's Nth attached image as the working image.

  An index the turn cannot satisfy is a skipped ACTION with a note, never a failed
  plan — and the adopted picture starts a fresh coordinate frame (§1's accumulated
  crop/rotate re-mapping no longer describes it)."""
  index = action["index"]
  attachments = run.attachments if run is not None else []
  if index > len(attachments):
    note = "Skipped switching to attached image %d — this message attached %d image(s)" % (
      index, len(attachments)
    )
    if run is not None: run.notes.append(note)
    return
  data, name = _attachment_parts(attachments[index - 1])
  stem = _save_stem(name)
  editor.load(data, name=stem) if stem else editor.load(data)
  run.active_name = stem
  if frame is not None: frame.reset()


def _apply_save(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  """§2.1: write the current image + layout as ``<name>.stencil`` beside the output.

  The name comes from the action, else from the active attachment's file name, else
  from the editor's own project name; a name already on disk gains a " 2"/" 3"…
  suffix. Nothing loaded is a skipped action with a note, never a failed plan."""
  has_image = getattr(editor, "has_image", None)
  if has_image is not None and not has_image():
    if run is not None: run.notes.append("Skipped save — no working image to save")
    return
  # A "path" destination is valid but not honoured here — note + usual place.
  if action.get("path") and run is not None:
    run.notes.append("Saved to the usual place — this surface cannot save to a path")
  active = run.active_name if run is not None else ""
  name = action.get("name") or active or getattr(editor, "name", "") or "project"
  path = _unique_save_path(run.save_dir if run is not None else "", name)
  editor.save_project(path)
  if run is not None: run.saved.append(path)


def _apply_history_step(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
            run: (_PlanRun | NoneType) = None) -> None:
  """§2 ``undo``/``redo``: step the editor's OWN edit history. One step is one
  history entry; running out of entries is a note, never a failed plan."""
  op = action["op"]
  step = editor.undo if op == "undo" else editor.redo
  steps = action.get("steps", 1)
  done = 0
  while done < steps and step(): done += 1
  if done < steps and run is not None:
    run.notes.append(
      "%s stopped after %d step(s) — no more history entries" % (op, done)
    )


def _apply_reset(action: dict, editor: Any, frame: (_FrameMap | NoneType) = None,
        run: (_PlanRun | NoneType) = None) -> None:
  """§2 ``reset``: drop every pending edit back to the original (the console's
  /reset). Nothing loaded is a skipped action with a note, never a failed plan."""
  has_image = getattr(editor, "has_image", None)
  if has_image is not None and not has_image():
    if run is not None: run.notes.append("Skipped reset — no working image")
    return
  editor.reset()


# The console-settings ops' executor hooks (§10 console profile): the REPL passes itself as
# `console`. Misses are notes per §1; clearChat's hook only RECORDS the request.
