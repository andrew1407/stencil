from __future__ import annotations

"""The derived-view pipeline and the page metrics it rests on.

``result()`` applies the CLI ``rebuild()`` order — rotate → crop → filter →
rasterize lines — and the geometry helpers below are ported one-to-one from
``session.zig`` / ``pipeline.zig``.
"""

from .._types import NoneType
from ..image import Image
from ._snapshot import _A4_FALLBACK, _Snapshot

CropRect = tuple[int, int, int, int]


class _DeriveApi:
  """The derived view, the page format that rides the layout, and the
  geometry helpers both need."""
  # ── render + save ──────────────────────────────────────────────────────────
  def result(self, with_lines: bool = True) -> Image:
    """Derive and return the current view: rotate → crop → filter → rasterize lines.

    Memoized on :attr:`revision`, so repeated calls between edits cost only the copy
    that keeps every caller's :class:`Image` an independent buffer (mirrors
    ``session.rebuild`` producing ``working``).
    ``with_lines=False`` skips the line rasterization (the picture alone).
    """
    key = (self._revision, bool(with_lines))
    if self._result is not None and self._result[0] == key:
      return self._result[1].copy()
    orig = self._require_original()
    core = self._get_core()
    snap = self._current()
    img = orig
    # 1. rotate the original first (core reads src, writes a fresh dst — no extra copy)
    if snap.rotation % 4 != 0:
      data = core.rotate_image_rgba(img.data, img.width, img.height, snap.rotation)
      rot_w, rot_h = core.rotated_dims(img.width, img.height, snap.rotation)
      img = Image(rot_w, rot_h, data)
    # 2. crop (rect is in rotated-original space, which equals img's space here)
    if snap.crop is not None:
      cx, cy, cw, ch = self._clamp_rect(snap.crop, img.width, img.height)
      data = core.crop_image_rgba(img.data, img.width, img.height, cx, cy, cw, ch)
      img = Image(cw, ch, data)
    # 3. filter in place — steps 1-2 hand back fresh buffers, so copy only if neither
    #    ran (custom uses the hex colour as the duotone arg, else the mode; contour is
    #    dimensioned Sobel edge detection, so it takes its own entry point)
    if img is orig:
      img = orig.copy()
    if snap.filter_mode and snap.filter_mode.lower() != "none":
      if snap.filter_mode.lower() == "contour":
        core.apply_contour(img.data, img.width, img.height)
      else:
        is_custom = snap.filter_mode.lower() == "custom"
        arg = snap.filter_color if is_custom else snap.filter_mode
        if arg:
          tint = core.parse_color(arg) or (0, 0, 0, 255)
          core.apply_filter(arg, img.data, img.pixel_count, (tint[0], tint[1], tint[2]))
    # 4. rasterize each drawn line in place
    for line in snap.lines if with_lines else []:
      points = [(p.x, p.y) for p in line.points]
      core.rasterize_line(
        img.data, img.width, img.height, points, line.color, line.thickness,
        line.point_size, line.style, line.locked, line.fill_color, line.point_color,
      )
    self._result = (key, img)
    return img.copy()


  # ── page format (project-level; rides the layout) ───────────────────────────
  def set_page_format(
    self,
    name: str,
    width: (float | NoneType) = None,
    height: (float | NoneType) = None,
  ) -> "Editor":
    """Set the project's page format (mirror the console's ``/format``).

    A named format is matched case-insensitively and stored canonical ("b5" → "B5");
    ``"custom"`` needs ``width``/``height`` in cm within the shared custom-page range
    (0.1–500 cm, mirroring the console's ``parseCmDim`` and the browser/desktop
    inputs; NaN/Infinity are rejected too, so an exported layout stays valid JSON).
    An empty name clears the format back to unset (the layout omits ``pageSize``
    again). Unknown names raise ``ValueError`` listing the valid formats. The format
    rides the saved layout (``pageSize``/``customPageWidth``/``customPageHeight``),
    like every other client.
    """
    spec = (name or "").strip()
    if not spec:
      self._page_size = ""
      self._custom_page_width = 0.0
      self._custom_page_height = 0.0
      return self
    if spec.lower() == "custom":
      w = float(width or 0.0)
      h = float(height or 0.0)
      # Pinned custom-page range (port of the console's parseCmDim): 0.1–500 cm.
      # The inclusive comparisons are False for NaN, so NaN/inf never get stored
      # (json.dumps would otherwise emit non-RFC-8259 `NaN` in the layout).
      if not (0.1 <= w <= 500.0 and 0.1 <= h <= 500.0):
        raise ValueError(
          "custom page format needs width + height in cm within 0.1-500"
        )
      self._page_size = "custom"
      self._custom_page_width = w
      self._custom_page_height = h
      return self
    canonical = self._get_core().canonical_page_format(spec)
    if canonical is None:
      raise ValueError(
        "unknown page format %r — valid names: %s"
        % (name, ", ".join(self._get_core().page_formats()))
      )
    self._page_size = canonical
    self._custom_page_width = 0.0
    self._custom_page_height = 0.0
    return self

  @property
  def page_format(self) -> str:
    """The page format name ("A4"/"B5"/.../"custom"), or "" when unset."""
    return self._page_size

  @property
  def custom_page_width(self) -> float:
    """The custom page width in cm (0.0 when unset / a named format is picked)."""
    return self._custom_page_width

  @property
  def custom_page_height(self) -> float:
    """The custom page height in cm (0.0 when unset / a named format is picked)."""
    return self._custom_page_height


  # ── geometry helpers (ported from session.zig) ─────────────────────────────
  def _view_dims(self, snap: _Snapshot) -> tuple[int, int]:
    """Dimensions of the view a snapshot derives (rotation then crop; filter/lines keep dims)."""
    orig = self._original
    w, h = orig.width, orig.height
    if snap.rotation % 4 != 0:
      w, h = self._get_core().rotated_dims(w, h, snap.rotation)
    if snap.crop is not None:
      _, _, cw, ch = self._clamp_rect(snap.crop, w, h)
      w, h = cw, ch
    return (w, h)

  @staticmethod
  def _clamp_rect(
    rect: CropRect, w: int, h: int
  ) -> CropRect:
    """Clamp a rect to lie within a ``w``×``h`` image (port of ``clampRect``)."""
    x, y, rw, rh = rect
    rw = max(1, min(rw, w))
    rh = max(1, min(rh, h))
    x = max(0, min(x, w - rw))
    y = max(0, min(y, h - rh))
    return (x, y, rw, rh)

  def _rotate_rect_quarters(
    self, rect: CropRect, w: int, h: int, n: int
  ) -> CropRect:
    """Map a rect through ``n`` clockwise quarter-turns of its ``w``×``h`` image.

    Pure axis-aligned 90° steps; a direct port of the Zig ``rotateRectQuarters``.
    """
    x, y, rw, rh = rect
    cw, ch = w, h
    q = self._get_core().normalize_quarters(n)
    while q > 0:
      new_x = ch - y - rh
      new_y = x
      new_w = rh
      new_h = rw
      x, y, rw, rh = new_x, new_y, new_w, new_h
      cw, ch = ch, cw
      q -= 1
    return (x, y, rw, rh)

  def _page_for_image(self, w: int, h: int) -> tuple[float, float]:
    """Page size (cm) for crop metrics — port of ``pipeline.pageForImage``.

    A landscape image lays the page on its side; portrait keeps it upright.
    """
    base = self._get_core().named_page_size("A4") or _A4_FALLBACK
    bw, bh = base
    if w > h:
      return (max(bw, bh), min(bw, bh))
    return (min(bw, bh), max(bw, bh))

  @staticmethod
  def _build_crop_spec(
    x1: (float | NoneType),
    y1: (float | NoneType),
    x2: (float | NoneType),
    y2: (float | NoneType),
  ) -> str:
    """Assemble a ``"x1=.. y1=.. x2=.. y2=.."`` crop spec, omitting None edges."""
    parts: list[str] = []
    if x1 is not None:
      parts.append("x1=%s" % x1)
    if y1 is not None:
      parts.append("y1=%s" % y1)
    if x2 is not None:
      parts.append("x2=%s" % x2)
    if y2 is not None:
      parts.append("y2=%s" % y2)
    return " ".join(parts)
