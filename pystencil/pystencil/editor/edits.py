from __future__ import annotations

"""The chainable edits: rotation, crop, filter/tint and the x/y coordinate formulas.

Each mutator snapshots history through the ``_push`` that :class:`Editor` owns and
returns ``self``.
"""

from typing import Optional, Tuple

from ._snapshot import _Snapshot


class _EditApi:
  """Rotate / crop / filter / formula mutators."""

  # ── edits (chainable; each snapshots history) ──────────────────────────────
  def rotate(self, quarters: int) -> "Editor":
    """Rotate by ``quarters`` clockwise quarter-turns; the crop rect rides along."""
    self._require_original()
    core = self._get_core()
    cur = self._current()
    nxt = cur.copy()
    if cur.crop is not None:
      # Map the crop into the post-rotation rotated-original space (session.applyRotate).
      orig = self._original
      dims_w, dims_h = core.rotated_dims(orig.width, orig.height, cur.rotation)
      nxt.crop = self._rotate_rect_quarters(cur.crop, dims_w, dims_h, quarters)
    nxt.rotation = core.normalize_quarters(cur.rotation + quarters)
    self._push(nxt)
    return self

  def rotate_left(self) -> "Editor":
    """Rotate one quarter-turn counter-clockwise (-1)."""
    return self.rotate(-1)

  def rotate_right(self) -> "Editor":
    """Rotate one quarter-turn clockwise (+1)."""
    return self.rotate(1)

  def crop(
    self,
    spec: Optional[str] = None,
    *,
    x1: Optional[float] = None,
    y1: Optional[float] = None,
    x2: Optional[float] = None,
    y2: Optional[float] = None,
    album: bool = False,
  ) -> "Editor":
    """Crop the current view by a crop spec (or x1/y1/x2/y2 edges).

    When ``spec`` is omitted, a ``"x1=.. y1=.. x2=.. y2=.."`` spec is built from the
    given edges (None edges are omitted). The spec resolves against the CURRENT view's
    dimensions and page metrics (mirroring ``pipeline.resolveCropSpec`` →
    ``pageForImage``), then composes into rotated-original space exactly like
    ``session.applyCrop``. An unparseable spec is a no-op (matching the CLI, which
    prints an error and leaves the image unchanged).
    """
    self._require_original()
    core = self._get_core()
    cur = self._current()
    if spec is None:
      spec = self._build_crop_spec(x1, y1, x2, y2)
    rect = self.resolve_crop_rect(spec, album=album)
    if rect is None:
      # Bad spec: leave the editor untouched, just like the Zig handler.
      return self
    rx, ry, rw, rh = rect
    # The view is rotate(original) cropped to cur.crop; a sub-rect maps back by origin.
    base_x = cur.crop[0] if cur.crop is not None else 0
    base_y = cur.crop[1] if cur.crop is not None else 0
    orig = self._original
    space_w, space_h = core.rotated_dims(orig.width, orig.height, cur.rotation)
    new_crop = self._clamp_rect(
      (base_x + rx, base_y + ry, rw, rh), space_w, space_h
    )
    nxt = cur.copy()
    nxt.crop = new_crop
    self._push(nxt)
    return self

  def resolve_crop_rect(
    self, spec: str, *, album: bool = False
  ) -> Optional[Tuple[int, int, int, int]]:
    """Resolve a crop spec against the CURRENT view without applying it.

    Exactly the resolution :meth:`crop` performs — the same core ``resolveCrop``
    call against the same view dimensions and page metrics — returning the
    ``(x, y, w, h)`` sub-rect of the current view the crop would keep, or ``None``
    for an unparseable spec. The rect's origin is what the LLM executor's §1
    coordinate re-mapping subtracts from later plan coordinates.
    """
    self._require_original()
    core = self._get_core()
    view_w, view_h = self._view_dims(self._current())
    page_w, page_h = self._page_for_image(view_w, view_h)
    return core.resolve_crop(
      spec, view_w, view_h, view_w / page_w, view_h / page_h,
      page_w, page_h, album,
    )

  def set_filter(self, mode: str) -> "Editor":
    """Set the filter mode ("none"|"bw"|"sepia"|"custom"|"invert"|"contour"),
    keeping any custom colour."""
    self._require_original()
    cur = self._current()
    nxt = cur.copy()
    nxt.filter_mode = mode
    self._push(nxt)
    return self

  def set_filter_color(self, color: str) -> "Editor":
    """Set the custom duotone tint (hex); switches the mode to "custom"."""
    self._require_original()
    core = self._get_core()
    # Normalize through the core so "red"/"#f00"/"#ff0000" all land as a #rrggbb hex,
    # matching applyFilterArg's custom branch in the Zig handlers.
    parsed = core.parse_color(color)
    hex_color = "#%02x%02x%02x" % (parsed[0], parsed[1], parsed[2]) if parsed else color
    cur = self._current()
    nxt = cur.copy()
    nxt.filter_mode = "custom"
    nxt.filter_color = hex_color
    self._push(nxt)
    return self

  def apply_filter(self, mode: str) -> "Editor":
    """Convenience filter setter mirroring the CLI's ``/filter`` (``applyFilterArg``).

    "bw"/"sepia"/"invert"/"contour"/"none" set those modes directly (the named modes
    are checked BEFORE the colour fallback); anything else is treated as a colour —
    parsed by the core and stored as a custom #rrggbb duotone tint. An unrecognized
    value raises ``ValueError``.
    """
    low = mode.strip().lower()
    if low in ("bw", "sepia", "invert", "contour", "none"):
      return self.set_filter(low)
    core = self._get_core()
    parsed = core.parse_color(mode.strip())
    if parsed is None:
      raise ValueError(
        "unknown filter %r — use 'bw', 'sepia', 'invert', 'contour', 'none', "
        "or a colour" % mode
      )
    return self.set_filter_color(mode.strip())

  # ── formulas (x/y coordinate transform) ─────────────────────────────────────
  def set_formula(self, axis: str, expr: str) -> "Editor":
    """Set the x or y coordinate-transform formula (validated by the shared parser; raises
    ValueError on a bad expression). A non-empty formula enables formulas. The expression
    rides the saved layout, where the browser applies it; use apply_formula() to evaluate."""
    ax = "y" if axis == "y" else "x"
    expr = (expr or "").strip()
    if expr and not self._get_core().validate_formula(expr, ax):
      raise ValueError(f"invalid {ax} formula: {expr!r}")
    if ax == "y":
      self._formula_y = expr
    else:
      self._formula_x = expr
    if expr:
      self._allow_formulas = True
    return self

  def set_allow_formulas(self, on: bool) -> "Editor":
    """Toggle whether formulas apply (keeps the expressions, so re-enabling restores them)."""
    self._allow_formulas = bool(on)
    return self

  @property
  def allow_formulas(self) -> bool:
    """Whether the x/y formulas are currently applied."""
    return self._allow_formulas

  def apply_formula(self, axis: str, value: float) -> float:
    """Apply the current x or y formula to a coordinate (identity when off/empty/invalid)."""
    ax = "y" if axis == "y" else "x"
    expr = self._formula_y if ax == "y" else self._formula_x
    return self._get_core().apply_formula(expr, ax, value, self._allow_formulas)
