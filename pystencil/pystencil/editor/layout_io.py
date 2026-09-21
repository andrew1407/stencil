from __future__ import annotations

"""Layout in and out: drawing lines onto the current snapshot, serializing the
project to browser-compatible layout JSON, and coercing whatever the caller passed.
"""

import json
import os

from .._ffi.types import NoneType
from ..layout import Layout, Line
from ..scriptpaths import is_url
from ._snapshot import _Snapshot, LayoutLike
from .source import _SourceApi


class _LayoutApi:
  """Drawing lines, serializing the layout, and coercing layout-ish inputs."""
  def draw(self, layout: LayoutLike, combine: bool = True) -> "Editor":
    """Draw a layout's lines (mirror ``session.addLines``).

    ``combine`` (the default) APPENDS them after the lines already drawn — the same
    choice the GUI editors offer when a layout lands on an existing one. Pass
    ``combine=False`` to REPLACE the current lines instead, keeping the rest of the
    state (use :meth:`apply_layout` to adopt a layout's rotation/crop/filter too).
    ``layout`` may be a :class:`Layout`, dict, JSON string, path, URL or Line list.
    """
    self._require_original()
    add = self.__coerce_lines(layout)
    cur = self._current()
    nxt = cur.copy()
    nxt.lines = (list(cur.lines) + add) if combine else add
    self._push(nxt)
    return self

  def apply_layout(self, layout: LayoutLike) -> "Editor":
    """ADOPT a layout's rotation+crop+filter+lines wholesale (mirror ``adoptServerLayout``).

    Unlike :meth:`draw` (which appends), this replaces the geometry/filter/lines of the
    new state from the layout — used when reopening a peer's stored project layout.
    """
    self._require_original()
    core = self._get_core()
    L = self.__coerce_layout(layout)
    crop: (tuple[int, int, int, int] | NoneType) = None
    if isinstance(L.crop_rect, dict):
      cr = L.crop_rect
      # Canonical {w,h} wins; legacy {width,height} (pre-Phase-6) still reads.
      crop = (
        int(cr.get("x", 0)),
        int(cr.get("y", 0)),
        int(cr.get("w", cr.get("width", 0))),
        int(cr.get("h", cr.get("height", 0))),
      )
    snapshot = _Snapshot(
      rotation=core.normalize_quarters(L.rotation_quarters or 0),
      crop=crop,
      filter_mode=L.image_filter or "",
      filter_color=L.filter_color or "",
      lines=list(L.lines),
    )
    # The page format is project-level: adopt it raw and unvalidated, exactly like
    # the CLI session's adoptLayoutMeta (an unknown stored name round-trips as-is).
    self._page_size = L.page_size or ""
    self._custom_page_width = L.custom_page_width or 0.0
    self._custom_page_height = L.custom_page_height or 0.0
    self._push(snapshot)
    return self


  def layout(self) -> Layout:
    """Build the structured layout for the current state (mirror ``currentLayoutJson``).

    ``imageWidth``/``imageHeight`` are the RESULT dimensions; the optional
    filter/rotation fields are emitted only when meaningful (filter present and not
    "none", a non-zero rotation), exactly like ``server.buildLayout``. ``cropRect`` is
    always emitted — see below.
    """
    orig = self._require_original()
    snap = self._current()
    img_w, img_h = self._view_dims(snap)
    image_filter = None
    if snap.filter_mode and snap.filter_mode.lower() != "none": image_filter = snap.filter_mode
    filter_color = snap.filter_color if snap.filter_color else None
    if snap.crop is not None:
      cx, cy, cw, ch = snap.crop
    else:
      # No explicit crop means the WHOLE rotated original — the GUIs auto-crop a fresh image to
      # the page aspect unless the layout names a cropRect.
      cx, cy = 0, 0
      cw, ch = self._get_core().rotated_dims(orig.width, orig.height, snap.rotation)
    # Canonical browser keys ({w,h}) since Phase 6.
    crop_rect = {"x": cx, "y": cy, "w": cw, "h": ch}
    rotation_quarters = snap.rotation if snap.rotation != 0 else None
    return Layout(
      image_width=img_w,
      image_height=img_h,
      lines=list(snap.lines),
      image_filter=image_filter,
      filter_color=filter_color,
      crop_rect=crop_rect,
      rotation_quarters=rotation_quarters,
      # Page format only when picked; custom dims only when set (mirror pageMeta()).
      page_size=self._page_size or None,
      custom_page_width=self._custom_page_width or None,
      custom_page_height=self._custom_page_height or None,
      # allowFormulas only when on; expressions kept whenever non-empty (preserve on off).
      allow_formulas=True if self._allow_formulas else None,
      formula_x=self._formula_x or None,
      formula_y=self._formula_y or None,
    )

  def save_layout(self, path: (str | NoneType) = None) -> str:
    """Write the current layout JSON, returning the path written.

    Path semantics (identical to the Zig CLI's ``/layout``):
     * ``*.json`` (case-insensitive) → write to exactly that path.
     * a non-empty path without ``.json`` → treat as a directory/prefix and write
      ``<path>/<project_name>.json`` (without doubling a trailing slash).
     * empty / None → ``<project_name>.json`` in the current directory.
    """
    name = self._name or "layout"
    if path and path.lower().endswith(".json"):
      out_path = path
    elif path:
      # Directory/prefix form: join with a single separator.
      sep = "" if path.endswith("/") else "/"
      out_path = "%s%s%s.json" % (path, sep, name)
    else:
      out_path = "%s.json" % name
    text = self.layout().to_json(indent=2)
    with open(out_path, "w", encoding="utf-8") as fh:
      fh.write(text)
    return out_path


  # ── layout coercion ────────────────────────────────────────────────────────
  @staticmethod
  def __read_layout_source(src: str) -> str:
    """Turn a layout-source string into layout JSON text.

    A string may be inline JSON, an http(s) URL, or a local file path — mirroring the
    Zig CLI's ``/apply``/``-l`` which accept a path or URL. Inline JSON (starts with
    ``{`` or ``[``) is returned as-is; a URL is fetched; an existing file is read;
    anything else is returned unchanged so :meth:`Layout.from_json` raises a clear error.
    """
    stripped = src.lstrip()
    if stripped.startswith("{") or stripped.startswith("["): return src
    if is_url(src): return _SourceApi._fetch_url(src).decode("utf-8")
    if os.path.exists(src):
      with open(src, "r", encoding="utf-8") as handle:
        return handle.read()
    return src

  @staticmethod
  def __coerce_layout(layout: LayoutLike) -> Layout:
    """Coerce a Layout|dict|json-str|json-path|url|list[Line] into a :class:`Layout`."""
    if isinstance(layout, Layout): return layout
    if isinstance(layout, str):
      return Layout.from_json(_LayoutApi.__read_layout_source(layout))
    if isinstance(layout, dict): return Layout.from_dict(layout)
    if isinstance(layout, list): return Layout(0, 0, lines=_LayoutApi.__coerce_lines(layout))
    raise TypeError("unsupported layout input: %r" % type(layout))

  @staticmethod
  def __coerce_lines(layout: LayoutLike) -> list[Line]:
    """Extract a list of :class:`Line` from any accepted layout input."""
    # A raw list may hold Line objects or line dicts; everything else routes
    # through __coerce_layout so the str/dict/Layout parsing lives in one place.
    if isinstance(layout, list):
      return [ln if isinstance(ln, Line) else Line.from_dict(ln) for ln in layout]
    return list(_LayoutApi.__coerce_layout(layout).lines)
