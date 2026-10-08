"""How the drawn lines follow a rotate, a flip or a crop, as the browser's do: the twin of
``rotateLinePointsQuarter`` / ``mirrorLinePoints`` / ``scaleLinePoints`` in ``browser/js/core/parse/cropGeometry.js``
and ``core::cropChange``, and of the CLI console's ``turnLinesJson`` / ``mirrorLinesJson`` / ``recropLinesJson``.
Lines are view-local, so every result is a fresh list of fresh lines (history never aliases).
"""

from __future__ import annotations

from dataclasses import replace

from ..layout import Line, Point

Rect = tuple[int, int, int, int]


def _turn_point(p: Point, q: int, w: float, h: float) -> Point:
  """``q`` (1..3) clockwise quarter-turns of a point inside a ``w`` x ``h`` view, unclamped."""
  if q == 3: return Point(p.y, w - p.x)  # the browser's one left press, exact
  x, y = p.x, p.y
  for _ in range(q):
    x, y = h - y, x
    w, h = h, w
  return Point(x, y)


def turn_lines(lines: list[Line], quarters: int, w: int, h: int) -> list[Line]:
  """Every line turned by ``quarters`` clockwise quarters inside the pre-turn ``w`` x ``h`` view."""
  q = quarters % 4
  if q == 0: return list(lines)
  return [replace(ln, points=[_turn_point(p, q, float(w), float(h)) for p in ln.points]) for ln in lines]


def mirror_lines(lines: list[Line], w: int) -> list[Line]:
  """Every line mirrored left-right inside a ``w``-wide view: ``x -> w - x``."""
  return [replace(ln, points=[Point(float(w) - p.x, p.y) for p in ln.points]) for ln in lines]


def crop_change(old: Rect, new: Rect) -> tuple[bool, float]:
  """``(orientation_changed, scale)`` of a crop window moving from ``old`` to ``new``: an
  album/portrait flip (width > height is album), else the width ratio."""
  flipped = (old[2] > old[3]) != (new[2] > new[3])
  if flipped or old[2] <= 0: return flipped, 1.0
  return False, new[2] / old[2]


def recrop_lines(lines: list[Line], old: Rect, new: Rect) -> list[Line]:
  """The lines as the browser's crop recalcs them: cleared on a flip, else scaled by the ratio."""
  flipped, scale = crop_change(old, new)
  if flipped: return list()
  if scale == 1: return list(lines)
  return [replace(ln, points=[Point(p.x * scale, p.y * scale) for p in ln.points]) for ln in lines]
