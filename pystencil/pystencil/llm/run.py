"""Per-plan execution state and its helpers: the attachment wire form, the §2.1
multi-image run record, output-path derivation and coordinate clamping.
"""

from __future__ import annotations

import os
import re
from typing import Any, Iterable, Sequence

from .._ffi.types import NoneType

def wire_images(images: (Iterable | NoneType)) -> list:
  """Attachments as ``(media_type, bytes)`` pairs; an optional third element (the source
  file name) never rides the wire."""
  out: list = list()
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


def _attachment_parts(item: Any) -> tuple[Any, str]:
  """``(bytes, name)`` from one attachment; ``name`` is "" for a plain pair."""
  parts = tuple(item)
  return parts[1], (str(parts[2]) if len(parts) > 2 and parts[2] else "")


class _PlanRun:
  """Per-execution state the §2.1 multi-image ops need. ``attachments`` excludes the
  auto-attached working snapshot; ``notes`` are per-action skip warnings."""

  def __init__(self, attachments: (Sequence | NoneType) = None, save_dir: str = "",
        console: Any = None) -> None:
    self.attachments: list = list(attachments or [])
    self.save_dir = save_dir or ""
    self.console = console
    self.notes: list[str] = list()
    self.saved: list[str] = list()
    self.active_name = ""
    # One attachment and nothing else: it names an unnamed save even before an
    # explicit `image` op adopts it (the browser's activeAttachment rule).
    if len(self.attachments) == 1:
      self.active_name = _save_stem(_attachment_parts(self.attachments[0])[1])


def _save_stem(name: str) -> str:
  """A file name without directories or extension; "" when nothing usable is left."""
  base = str(name or "").replace("\\", "/").rsplit("/", 1)[-1]
  base = re.sub(r"\.[^.]+$", "", base).strip()
  return "" if set(base) <= {"."} else base


def _unique_save_path(directory: str, name: str) -> str:
  """``<name>.stencil`` in ``directory``, suffixed " 2", " 3"… past a name already on disk."""
  stem = _save_stem(name) or "project"
  path = os.path.join(directory, stem + ".stencil") if directory else stem + ".stencil"
  n = 2
  while os.path.exists(path):
    candidate = "%s %d.stencil" % (stem, n)
    path = os.path.join(directory, candidate) if directory else candidate
    n += 1
  return path


def _clamp_point(x: float, y: float, w: float, h: float) -> tuple[float, float]:
  return (min(max(x, 0.0), float(w)), min(max(y, 0.0), float(h)))

