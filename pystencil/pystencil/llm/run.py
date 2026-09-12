from __future__ import annotations

"""Per-plan execution state and its helpers: the attachment wire form, the §2.1
multi-image run record, output-path derivation and coordinate clamping.

Sits below the appliers so both they and :mod:`.execute` can reach it.
"""

import os
import re
from typing import Any, Iterable, List, Optional, Sequence, Tuple

from .limits import MAX_SAVE_NAME

def wire_images(images: Optional[Iterable]) -> list:
  """Attachments in their WIRE form: strictly ``(media_type, bytes)`` pairs.

  An attachment may carry a third element — the source file name §2.1's ``save``
  derives its default project name from — which never rides the wire."""
  out: list = []
  for item in images or []:
    try:
      parts = tuple(item)
      pair = (parts[0], parts[1])
    except (TypeError, IndexError, ValueError):
      raise ValueError("each image must be a (media_type, bytes) tuple") from None
    if len(parts) > 3:
      raise ValueError("each image must be a (media_type, bytes) tuple")
    out.append(pair)
  return out


def _attachment_parts(item: Any) -> Tuple[Any, str]:
  """``(bytes, name)`` from one attachment.

  Attachments are the contract's ``(media_type, bytes)`` tuples — the very list a
  caller passed to :meth:`Chat.send` / :meth:`Editor.prompt`. An optional third
  element carries the original file name, which §2.1's ``save`` derives its default
  project name from (a plain 2-tuple simply has none)."""
  parts = tuple(item)
  return parts[1], (str(parts[2]) if len(parts) > 2 and parts[2] else "")


class _PlanRun:
  """Per-execution state the §2.1 multi-image ops need (contract §2.1).

  ``attachments`` is the turn's attached images in attachment order — the auto-attached
  working snapshot is not one of them; ``save_dir`` is where ``save`` writes its
  ``.stencil`` files (the console/API's own output directory, cwd by default);
  ``notes`` collects per-action skip warnings (an unsatisfiable index or a save with
  nothing loaded costs that ACTION, never the plan); ``saved`` records the written
  paths and ``active_name`` the name a later unnamed ``save`` derives from.
  """

  def __init__(self, attachments: Optional[Sequence] = None, save_dir: str = "",
        console: Any = None) -> None:
    self.attachments: list = list(attachments or [])
    self.save_dir = save_dir or ""
    # The §10 console-profile hook object (the REPL), or None at the library level.
    self.console = console
    self.notes: List[str] = []
    self.saved: List[str] = []
    self.active_name = ""
    # One attachment and nothing else: it names an unnamed save even before an
    # explicit `image` op adopts it (the browser's activeAttachment rule).
    if len(self.attachments) == 1:
      self.active_name = _save_stem(_attachment_parts(self.attachments[0])[1])


def _save_stem(name: str) -> str:
  """A file name reduced to the project name a ``save`` uses: no directories, no
  extension. Empty when nothing usable is left — the caller then falls back."""
  base = str(name or "").replace("\\", "/").rsplit("/", 1)[-1]
  base = re.sub(r"\.[^.]+$", "", base).strip()
  return "" if set(base) <= {"."} else base


def _unique_save_path(directory: str, name: str) -> str:
  """``<name>.stencil`` in ``directory``, suffixed " 2", " 3"… past a name already
  on disk — so a plan saving several images never overwrites its own output."""
  stem = _save_stem(name) or "project"
  path = os.path.join(directory, stem + ".stencil") if directory else stem + ".stencil"
  n = 2
  while os.path.exists(path):
    candidate = "%s %d.stencil" % (stem, n)
    path = os.path.join(directory, candidate) if directory else candidate
    n += 1
  return path


def _clamp_point(x: float, y: float, w: float, h: float) -> Tuple[float, float]:
  """Clamp a layout point into the current image's bounds (contract §1)."""
  return (min(max(x, 0.0), float(w)), min(max(y, 0.0), float(h)))


# One applier per op, mapping the validated action onto the Editor method it
# stands for (contract §2). Each applier rides the op's OP_REGISTRY entry beside
# its validator, so dispatch is table-driven on both the parse and execute sides.
# ``frame`` is the plan's running §1 re-mapping transform (None for direct
# hand-built calls).
