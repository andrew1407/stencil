"""The lines an executor shows after each planned ``layout``, as the planner keeps them: a plan's
``layout`` REPLACES the drawn lines (llm-contract §2), so each one carries the whole set. They
follow a crop by ``core::cropChange`` as the run's marks do, and a layout document's lines are
read by the parser the run reads them with. Twin of ``cli/src/script/plan/lines.zig``.
"""

from __future__ import annotations

from .. import _net
from ..editor._lines import crop_change
from ..layout import Layout

# limits.MAX_LAYOUT_LINES of browser/js/config/llm/opRegistry.json: the most one layout holds.
MAX_LINES = 200


def shape_of(line) -> dict:
  """A parsed layout line in the planner's own shape: its points one flat x, y list."""
  points = list()
  for p in line.points: points += [p.x, p.y]
  return {"points": points, "color": line.color, "style": line.style,
          "fill_color": line.fill_color, "point_color": line.point_color,
          "thickness": line.thickness, "point_size": line.point_size, "locked": line.locked}


def recropped(lines: list, old: tuple, new: tuple) -> list:
  """``lines`` after a crop moved the window from ``old`` to ``new``, as the run's marks move."""
  flipped, scale = crop_change(old, new)
  if flipped: return list()
  if scale == 1: return lines
  return [dict(line, points=[v * scale for v in line["points"]]) for line in lines]


def document(src: str, is_url: bool) -> tuple:
  """A layout document's lines, or why they did not load, as ``(lines, reason)``: read, or
  fetched through the guard the run fetches it with, then parsed as the run parses it."""
  try:
    if is_url: text = _net._fetch(src, strict=False).decode("utf-8")
    else:
      with open(src, "r", encoding="utf-8") as handle:
        text = handle.read()
  except (OSError, ValueError):
    return None, "the fetch failed" if is_url else "the file cannot be read"
  try:
    layout = Layout.from_json(text)
  except (ValueError, TypeError, KeyError, AttributeError):
    return None, "it is not layout JSON"
  return [shape_of(line) for line in layout.lines], ""
