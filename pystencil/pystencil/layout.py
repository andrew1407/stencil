"""The layout JSON as dataclasses — twin of ``buildLayoutPayload`` in
``browser/js/core/layout.js`` and ``cli/src/media/layout.zig``. Coordinates are image
pixels, keys camelCase; parsing is tolerant, a missing key taking the defaults below.
"""

from __future__ import annotations

import itertools
import json
from dataclasses import dataclass, field, replace
from typing import Any

from ._ffi.types import NoneType
from .core import get_core
from ._ffi.coerce import _as_float, _as_int, _as_str, _opt_bool, _opt_float, _opt_int, _opt_str


# Per-line defaults, shared by the dataclass fields and from_dict so they cannot drift.
DEFAULT_COLOR = "#FFFF00"
DEFAULT_THICKNESS = 2.0
DEFAULT_POINT_SIZE = 4.0
DEFAULT_STYLE = "solid"
DEFAULT_LOCKED = False
DEFAULT_FILL_COLOR = "transparent"


@dataclass
class Point:
  """A single vertex in image-pixel space."""

  x: float
  y: float

  def to_dict(self) -> dict:
    """Serialize to the ``{x, y}`` JSON shape."""
    return {"x": self.x, "y": self.y}

  @classmethod
  def from_dict(cls, d: Any) -> "Point":
    """Parse a ``{x, y}`` mapping; missing coordinates default to 0."""
    # Non-dict junk is the origin, as the browser's ``p && p.x`` reads it.
    if not isinstance(d, dict): return cls(0.0, 0.0)
    return cls(_as_float(d.get("x"), 0.0), _as_float(d.get("y"), 0.0))


@dataclass
class Line:
  """One polyline / closed shape with its stroke + fill styling."""

  points: list[Point] = field(default_factory=list)
  color: str = DEFAULT_COLOR
  thickness: float = DEFAULT_THICKNESS
  point_size: float = DEFAULT_POINT_SIZE
  style: str = DEFAULT_STYLE
  locked: bool = DEFAULT_LOCKED
  fill_color: str = DEFAULT_FILL_COLOR
  #: Point colour, independent of ``color``. Empty means the points inherit ``color`` — how
  #: every layout written before this field existed behaves (core ``Line::pointColor``).
  point_color: str = ""

  def to_dict(self) -> dict:
    """The shared camelCase shape, every key in the browser's order; ``pointColor`` only when
    set, so a layout without one keeps its bytes."""
    out = {
      "points": [p.to_dict() for p in self.points],
      "color": self.color,
      "thickness": self.thickness,
      "pointSize": self.point_size,
      "style": self.style,
      "locked": self.locked,
      "fillColor": self.fill_color,
    }
    if self.point_color: out["pointColor"] = self.point_color
    return out

  @classmethod
  def from_dict(cls, d: Any, max_points: (int | NoneType) = None) -> "Line":
    """Parse a line mapping, applying per-line defaults for missing keys; a non-mapping point
    is skipped, as the browser skips it, and at most ``max_points`` of the rest are kept."""
    if not isinstance(d, dict): return cls()
    raw_points = d.get("points")
    points: list[Point] = list()
    if isinstance(raw_points, list):
      mappings = (p for p in raw_points if isinstance(p, dict))
      points = [Point.from_dict(p) for p in itertools.islice(mappings, max_points)]
    return cls(
      points=points,
      color=_as_str(d.get("color"), DEFAULT_COLOR),
      thickness=_as_float(d.get("thickness"), DEFAULT_THICKNESS),
      point_size=_as_float(d.get("pointSize"), DEFAULT_POINT_SIZE),
      style=_as_str(d.get("style"), DEFAULT_STYLE),
      locked=bool(d.get("locked", DEFAULT_LOCKED)),
      fill_color=_as_str(d.get("fillColor"), DEFAULT_FILL_COLOR),
      point_color=_as_str(d.get("pointColor"), ""),
    )


def _caps() -> tuple[int, int, int]:
  """(lines, points per line, points in all): core's caps, constants.json LIMITS."""
  return get_core().layout_caps()


def _read_lines(raw: Any) -> list[Line]:
  """``lines`` cut as ``sanitizeLines`` (browser/js/core/layout.js) cuts them: a non-mapping line
  is skipped before it counts, each line keeps at most the per-line cap, and the line spending the
  last of the total is cut there, every one after it dropped."""
  if not isinstance(raw, list) or not raw: return list()
  max_lines, line_points, budget = _caps()
  lines: list[Line] = list()
  for d in raw:
    if len(lines) >= max_lines or budget <= 0: break
    if not isinstance(d, dict): continue
    line = Line.from_dict(d, min(line_points, budget))
    budget -= len(line.points)
    lines.append(line)
  return lines


def cap_lines(lines: list[Line]) -> list[Line]:
  """Two capped lists joined (a combine) cut as one layout, as ``capLayoutPoints``
  (browser/js/core/layout.js) cuts them; the same list when under the caps."""
  max_lines, _, budget = _caps()
  for i, line in enumerate(lines):
    if i >= max_lines: return lines[:i]
    if len(line.points) >= budget: return lines[:i] + [replace(line, points=line.points[:budget])]
    budget -= len(line.points)
  return lines


@dataclass
class Layout:
  """Dimensions and lines, plus optional filter, geometry, page and formulas — each omitted
  when ``None``, so a bare layout is exactly ``{imageWidth, imageHeight, lines}``."""

  image_width: int
  image_height: int
  lines: list[Line] = field(default_factory=list)
  image_filter: (str | NoneType) = None
  filter_color: (str | NoneType) = None
  crop_rect: (dict | NoneType) = None
  rotation_quarters: (int | NoneType) = None
  mirrored: (bool | NoneType) = None  # the original mirrored before the turn; True only
  page_size: (str | NoneType) = None
  custom_page_width: (float | NoneType) = None
  custom_page_height: (float | NoneType) = None
  allow_formulas: (bool | NoneType) = None
  formula_x: (str | NoneType) = None
  formula_y: (str | NoneType) = None

  def to_dict(self) -> dict:
    """Serialize, omitting optional fields that are ``None``."""
    out: dict = {
      "imageWidth": self.image_width,
      "imageHeight": self.image_height,
      "lines": [ln.to_dict() for ln in self.lines],
    }
    if self.image_filter is not None: out["imageFilter"] = self.image_filter
    if self.filter_color is not None: out["filterColor"] = self.filter_color
    if self.crop_rect is not None: out["cropRect"] = self.crop_rect
    if self.rotation_quarters is not None: out["rotationQuarters"] = self.rotation_quarters
    if self.page_size is not None: out["pageSize"] = self.page_size
    if self.custom_page_width is not None: out["customPageWidth"] = self.custom_page_width
    if self.custom_page_height is not None: out["customPageHeight"] = self.custom_page_height
    if self.allow_formulas is not None: out["allowFormulas"] = self.allow_formulas
    if self.formula_x is not None: out["formulaX"] = self.formula_x
    if self.formula_y is not None: out["formulaY"] = self.formula_y
    if self.mirrored: out["mirrored"] = True
    return out

  def to_json(self, indent: (int | NoneType) = None) -> str:
    """Serialize to a JSON string (compact by default, ``indent`` to pretty-print)."""
    return json.dumps(self.to_dict(), indent=indent)

  @classmethod
  def from_dict(cls, d: Any) -> "Layout":
    """Parse a layout mapping; missing fields fall back to defaults/empty, and the lines are
    held to the layout caps."""
    if not isinstance(d, dict): d = dict()
    return cls(
      image_width=_as_int(d.get("imageWidth"), 0),
      image_height=_as_int(d.get("imageHeight"), 0),
      lines=_read_lines(d.get("lines")),
      # "imageFilter" wins over the older "filter" key.
      image_filter=_opt_str(d.get("imageFilter", d.get("filter"))),
      filter_color=_opt_str(d.get("filterColor")),
      crop_rect=d.get("cropRect") if isinstance(d.get("cropRect"), dict) else None,
      rotation_quarters=_opt_int(d.get("rotationQuarters")),
      page_size=_opt_str(d.get("pageSize")),
      custom_page_width=_opt_float(d.get("customPageWidth")),
      custom_page_height=_opt_float(d.get("customPageHeight")),
      allow_formulas=_opt_bool(d.get("allowFormulas")),
      formula_x=_opt_str(d.get("formulaX")),
      formula_y=_opt_str(d.get("formulaY")),
      mirrored=True if d.get("mirrored") is True else None,
    )

  @classmethod
  def from_json(cls, text: str) -> "Layout":
    """Parse a layout from a JSON string (tolerant of missing fields)."""
    return cls.from_dict(json.loads(text))

  @classmethod
  def parse(cls, text: str) -> "Layout":
    """Alias for :meth:`from_json`."""
    return cls.from_json(text)
