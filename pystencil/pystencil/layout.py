"""Layout dataclasses: the structured drawing payload mirrored across Stencil.

Python port of the layout JSON the browser exports (``browser/js/core/layout.js``
``buildLayoutPayload``) and the CLI/MCP server parse (``mcp/src/layout.rs`` ←
``cli/src/layout.zig`` ← ``core/raster``). Coordinates are **image pixels**; JSON keys
are camelCase to match the other front-ends. Like ``buildLayoutPayload``,
:meth:`Layout.to_dict` always emits ``imageWidth``/``imageHeight``/``lines`` and omits
the optional geometry/filter fields when ``None``. Parsing is tolerant: missing keys
fall back to the per-line defaults below.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any

from ._types import NoneType
from ._coerce import _as_float, _as_int, _as_str, _opt_bool, _opt_float, _opt_int, _opt_str


# Per-line defaults, applied when a field is omitted. Kept as module constants so
# the dataclass field defaults and the tolerant from_dict parsing cannot drift.
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
    # Tolerate non-dict junk by treating it as the origin, mirroring the
    # browser's defensive ``p && p.x`` handling in lineDedupeKey.
    if not isinstance(d, dict):
      return cls(0.0, 0.0)
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
  #: Point colour, independent of ``color``. Empty means the points inherit
  #: ``color`` — the behaviour of every layout written before this field existed
  #: (core ``Line::pointColor``).
  point_color: str = ""

  def to_dict(self) -> dict:
    """Serialize to the camelCase JSON shape the front-ends share.

    All fields are always present (the browser export keeps a fixed field
    order); only ``pointSize``/``fillColor``/``pointColor`` are renamed to
    camelCase. ``pointColor`` is the one exception to "always present": it is
    emitted only when set, so an unset point colour round-trips as an absent key
    and the bytes of a layout that predates the field are unchanged.
    """
    out = {
      "points": [p.to_dict() for p in self.points],
      "color": self.color,
      "thickness": self.thickness,
      "pointSize": self.point_size,
      "style": self.style,
      "locked": self.locked,
      "fillColor": self.fill_color,
    }
    if self.point_color:
      out["pointColor"] = self.point_color
    return out

  @classmethod
  def from_dict(cls, d: Any) -> "Line":
    """Parse a line mapping, applying per-line defaults for missing keys."""
    if not isinstance(d, dict):
      return cls()
    raw_points = d.get("points")
    points: list[Point] = list()
    if isinstance(raw_points, list):
      points = [Point.from_dict(p) for p in raw_points]
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


@dataclass
class Layout:
  """A full layout: required dimensions + lines, plus optional geometry/filter.

  The optional fields (``image_filter``/``filter_color``/``crop_rect``/
  ``rotation_quarters``) round-trip the editor's filter and geometry to peers
  and on reopen; they are omitted from the JSON when ``None`` so a bare layout
  serializes to exactly ``{imageWidth, imageHeight, lines}``.
  """

  image_width: int
  image_height: int
  lines: list[Line] = field(default_factory=list)
  image_filter: (str | NoneType) = None
  filter_color: (str | NoneType) = None
  crop_rect: (dict | NoneType) = None
  rotation_quarters: (int | NoneType) = None
  # Page format + x/y coordinate-transform formulas (the browser applies them).
  page_size: (str | NoneType) = None
  custom_page_width: (float | NoneType) = None
  custom_page_height: (float | NoneType) = None
  allow_formulas: (bool | NoneType) = None
  formula_x: (str | NoneType) = None
  formula_y: (str | NoneType) = None

  def to_dict(self) -> dict:
    """Serialize, omitting optional fields that are ``None``.

    Mirrors ``buildLayoutPayload``: start from the required trio and append
    each optional camelCase key only when its value is present.
    """
    out: dict = {
      "imageWidth": self.image_width,
      "imageHeight": self.image_height,
      "lines": [ln.to_dict() for ln in self.lines],
    }
    if self.image_filter is not None:
      out["imageFilter"] = self.image_filter
    if self.filter_color is not None:
      out["filterColor"] = self.filter_color
    if self.crop_rect is not None:
      out["cropRect"] = self.crop_rect
    if self.rotation_quarters is not None:
      out["rotationQuarters"] = self.rotation_quarters
    if self.page_size is not None:
      out["pageSize"] = self.page_size
    if self.custom_page_width is not None:
      out["customPageWidth"] = self.custom_page_width
    if self.custom_page_height is not None:
      out["customPageHeight"] = self.custom_page_height
    if self.allow_formulas is not None:
      out["allowFormulas"] = self.allow_formulas
    if self.formula_x is not None:
      out["formulaX"] = self.formula_x
    if self.formula_y is not None:
      out["formulaY"] = self.formula_y
    return out

  def to_json(self, indent: (int | NoneType) = None) -> str:
    """Serialize to a JSON string (compact by default, ``indent`` to pretty-print)."""
    return json.dumps(self.to_dict(), indent=indent)

  @classmethod
  def from_dict(cls, d: Any) -> "Layout":
    """Parse a layout mapping; missing fields fall back to defaults/empty."""
    if not isinstance(d, dict):
      d = dict()
    raw_lines = d.get("lines")
    lines: list[Line] = list()
    if isinstance(raw_lines, list):
      lines = [Line.from_dict(ln) for ln in raw_lines]
    return cls(
      image_width=_as_int(d.get("imageWidth"), 0),
      image_height=_as_int(d.get("imageHeight"), 0),
      lines=lines,
      # Canonical "imageFilter" wins; legacy "filter" (pre-Phase-6) still reads.
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
    )

  @classmethod
  def from_json(cls, text: str) -> "Layout":
    """Parse a layout from a JSON string (tolerant of missing fields)."""
    return cls.from_dict(json.loads(text))

  # ``parse`` is an alias so callers can read either name; both the browser
  # paste path and the CLI accept a raw JSON string here.
  @classmethod
  def parse(cls, text: str) -> "Layout":
    """Alias for :meth:`from_json`."""
    return cls.from_json(text)
