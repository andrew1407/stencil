"""The executor-side coordinate re-mapping (contract §1).

Self-contained: every plan coordinate arrives in the frame the model SAW, and this
composes the crop translations and quarter-turn rotations that have run since.
"""

from __future__ import annotations

from ..._ffi.types import NoneType

# ── plan execution over the Editor facade ─────────────────────────────────────
class _FrameMap:
  """Contract §1 executor-side coordinate re-mapping: the running transform from
  the frame the model SAW (the pre-plan snapshot) into the current working frame.

  Every plan coordinate arrives in the pre-plan frame; each executed ``crop``
  composes a translation by minus its resolved rect origin, and each ``rotate``
  composes core ``rotateImageRGBA``'s quarter-turn mapping in continuous
  coordinates — one clockwise turn of a w×h view sends (x, y) to (h − y, x).
  Layout points are pushed through the accumulated steps, clamped into the
  current bounds, then drawn. Bare stub editors without geometry (no
  ``resolve_crop_rect``/``image_size``) record nothing and draw plans as-is.
  """

  def __init__(self, steps: (list | NoneType) = None) -> None:
    # ("crop", ox, oy) subtracts a resolved crop origin; ("cw", h) is one
    # clockwise quarter-turn of the view whose height was h at that step.
    self._steps: list = list(steps or [])

  def branch(self) -> "_FrameMap":
    """An independent copy for a variant (which composes its own crops/rotates
    on top of the top-level actions' transform)."""
    return _FrameMap(self._steps)

  def push_crop(self, origin_x: float, origin_y: float) -> None:
    if origin_x or origin_y:
      self._steps.append(("crop", float(origin_x), float(origin_y)))

  def push_rotate(self, quarters: int, view_w: float, view_h: float) -> None:
    w, h = float(view_w), float(view_h)
    for _ in range(quarters % 4):  # left turns normalize to 1..3 clockwise
      self._steps.append(("cw", h))
      w, h = h, w

  def map_point(self, x: float, y: float) -> tuple[float, float]:
    for step in self._steps:
      if step[0] == "crop":
        x, y = x - step[1], y - step[2]
      else:
        x, y = step[1] - y, x
    return x, y

  def reset(self) -> None:
    """Drop the accumulated steps: a fresh picture (a §2.1 ``image`` switch) starts
    a fresh coordinate frame, so nothing planned before it applies any more."""
    self._steps.clear()
