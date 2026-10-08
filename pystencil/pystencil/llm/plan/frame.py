"""The executor-side coordinate re-mapping (contract §1)."""

from __future__ import annotations

from ..._ffi.types import NoneType

class _FrameMap:
  """The running transform from the frame the model SAW into the current working frame.

  A ``crop`` translates by minus its rect origin; a ``rotate`` composes core
  ``rotateImageRGBA``'s mapping — one clockwise turn of a w×h view sends (x, y) to (h − y, x).
  """

  def __init__(self, steps: (list | NoneType) = None) -> None:
    # ("crop", ox, oy) subtracts a resolved crop origin; ("cw", h) is one
    # clockwise quarter-turn of the view whose height was h at that step.
    self._steps: list = list(steps or [])

  def branch(self) -> "_FrameMap":
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
    """A §2.1 ``image`` switch starts a fresh coordinate frame."""
    self._steps.clear()
