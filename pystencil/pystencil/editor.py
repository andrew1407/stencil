"""The chainable editor facade — pystencil's port of the browser ``window.stencil``
surface and the Zig CLI's structured editing session (``cli/src/console/session.zig``).

The model mirrors the CLI's ``Session``/``EditState`` exactly: we keep one untouched
ORIGINAL :class:`Image` plus a history of edit *snapshots* (rotation + crop + filter +
lines) and a cursor into it. The current view is never baked eagerly — it is DERIVED on
demand by :meth:`result`, applying the same pipeline the CLI's ``rebuild()`` uses:

    rotate → crop → filter → rasterize lines

so any edit can be serialized back to a browser-compatible layout JSON (see :meth:`layout`),
not just flattened into pixels. ``/undo``, ``/redo`` and ``/reset`` move the cursor and the
view re-derives. Every mutator is chainable (returns ``self``).

Geometry composition (crop into rotated-original space, the crop riding along through a
rotation) is ported one-to-one from ``session.applyCrop`` / ``session.applyRotate`` /
``rotateRectQuarters``; crop-spec page metrics come from ``cli/src/pipeline.zig``
(``resolveCropSpec`` + ``pageForImage``).
"""

from __future__ import annotations

import base64
import binascii
import json
import os
import urllib.request
import urllib.parse
from dataclasses import dataclass, field
from typing import List, Optional, Tuple, Union

from .core import Core, get_core
from .image import Image
from .layout import Layout, Line


# Cap the history depth like the CLI (`max_states`): the pristine state plus up to 63
# undoable edits; older edits fall off the front so memory stays bounded.
_MAX_STATES = 64

# Fallback A4 page (cm) if the core has no named page table — matches pipeline.zig's
# `core.namedPageSize("A4") orelse core.Page{ .w = 21.0, .h = 29.7 }`.
_A4_FALLBACK = (21.0, 29.7)

# Image extension → MIME for the `.stencil` data URL (default image/png).
_EXT_MIME = {
    "jpg": "image/jpeg", "jpeg": "image/jpeg", "gif": "image/gif",
    "webp": "image/webp", "bmp": "image/bmp",
}
_BASE64_MARKER = "base64,"


@dataclass
class _Snapshot:
    """One editing snapshot — the Python mirror of the Zig ``EditState``.

    ``rotation`` is 0..3 clockwise quarter-turns applied to the original FIRST; ``crop``
    is an ``(x, y, w, h)`` rect in rotated-original pixel space (or ``None``); the filter
    is a mode string ("none"|"bw"|"sepia"|"custom"|"invert"|"contour") plus a custom hex
    colour; ``lines`` is the list of drawn :class:`Line` objects.
    """

    rotation: int = 0
    crop: Optional[Tuple[int, int, int, int]] = None
    filter_mode: str = ""
    filter_color: str = ""
    lines: List[Line] = field(default_factory=list)

    def copy(self) -> "_Snapshot":
        """A shallow-but-safe clone: the line list is copied so appends don't alias."""
        return _Snapshot(
            rotation=self.rotation,
            crop=self.crop,
            filter_mode=self.filter_mode,
            filter_color=self.filter_color,
            lines=list(self.lines),
        )


def _clean_keywords(kw) -> List[str]:
    """Trim keywords and drop empties/non-strings — port of projectFile.js ``cleanKeywords``.

    Kept deliberately simple (no dedupe/lower-casing) so a ``.stencil`` round-trip preserves
    the exact tag list every other surface reads/writes.
    """
    if not isinstance(kw, list):
        return []
    return [k.strip() for k in kw if isinstance(k, str) and k.strip()]


def _sniff_image_ext(data: bytes) -> Optional[str]:
    """Best-effort image format from magic bytes (for a lossless save when there's no filename)."""
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return "png"
    if data[:2] == b"\xff\xd8":
        return "jpg"
    if data[:6] in (b"GIF87a", b"GIF89a"):
        return "gif"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    if data[:2] == b"BM":
        return "bmp"
    return None


# Source object types accepted by Editor.load().
LoadSource = Union[str, bytes, bytearray, Image]
# Layout-ish inputs accepted by draw()/apply_layout().
LayoutLike = Union[Layout, dict, str, List[Line]]


class Editor:
    """Chainable image-annotation editor over the shared Stencil core.

    Construct one, :meth:`load` (or :meth:`blank`) a source, then chain edits
    (:meth:`rotate`, :meth:`crop`, :meth:`set_filter`, :meth:`draw`, ...). Call
    :meth:`result` for the derived :class:`Image`, :meth:`save` to write it, or
    :meth:`layout`/:meth:`save_layout` for the structured payload.
    """

    def __init__(self, core: Optional[Core] = None) -> None:
        # A caller may inject a Core; otherwise we lazily share the process singleton so
        # codec-only construction stays cheap and tests can pass an explicit handle.
        self._core = core
        self._original: Optional[Image] = None
        # Raw encoded bytes of the original + its ext (None ⇒ none), kept so save_project embeds
        # the untouched source (lossless) instead of a PNG re-encode. See _set_source.
        self._source_bytes: Optional[bytes] = None
        self._source_ext: Optional[str] = None
        self._history: List[_Snapshot] = []
        self._cursor: int = 0
        # Project name = image basename without extension; "layout" is the documented
        # fallback used by save_layout when nothing better is known.
        self._name: str = "layout"
        # Optional provenance metadata (mirrors the server project's source/resource fields).
        self._source: Optional[str] = None
        self._resource: Optional[str] = None
        # Custom per-project accent colour painting the project name; "" = theme fallback
        # (mirrors ProjectMeta.color / the server ProjectRecord `color` field).
        self._color: str = ""
        # Free-text keywords/tags (project-level; ride the .stencil file + the server
        # ProjectRecord.keywords). Trimmed, empties dropped — matches projectFile.js cleanKeywords.
        self._keywords: List[str] = []
        # x/y coordinate-transform formulas (project-level; ride the layout, browser applies them).
        self._allow_formulas: bool = False
        self._formula_x: str = ""
        self._formula_y: str = ""
        # Page format (project-level; rides the layout like the CLI session's page_size).
        # "" = unset (the layout omits pageSize); custom dims are cm, 0 = unset.
        self._page_size: str = ""
        self._custom_page_width: float = 0.0
        self._custom_page_height: float = 0.0

    # ── core access ────────────────────────────────────────────────────────────
    def _get_core(self) -> Core:
        """Return the injected Core or the lazily-loaded process singleton."""
        if self._core is None:
            self._core = get_core()
        return self._core

    # ── source ─────────────────────────────────────────────────────────────────
    def load(
        self,
        src: LoadSource,
        *,
        frame: Optional[int] = None,
        name: Optional[str] = None,
        source: Optional[str] = None,
        resource: Optional[str] = None,
    ) -> "Editor":
        """Load a new original from a path, an http(s) URL, raw bytes, or an :class:`Image`.

        ``frame`` is accepted for API parity with the CLI's video-frame extraction but is
        not used here (codecs live in the adapters, not this stdlib-only package).
        ``name`` overrides the derived project name; ``source``/``resource`` record
        provenance for later server uploads. Replaces any current image + history.
        """
        img: Image
        derived_name: str
        # Raw encoded source bytes + ext, kept verbatim for a lossless save_project (None ⇒ none).
        src_bytes: Optional[bytes] = None
        src_ext: Optional[str] = None
        if isinstance(src, Image):
            # Copy so later in-place core ops never mutate the caller's image.
            img = src.copy()
            derived_name = "image"
        elif isinstance(src, (bytes, bytearray)):
            src_bytes = bytes(src)
            img = Image.decode(src_bytes)
            src_ext = _sniff_image_ext(src_bytes)
            derived_name = "image"
        elif isinstance(src, str):
            if self._is_url(src):
                src_bytes = self._fetch_url(src)
                img = Image.decode(src_bytes)
                src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
                derived_name = self._name_from_url(src)
                # Default the recorded source to the URL we fetched.
                if source is None:
                    source = src
            else:
                # Read the file once and decode from the bytes (Image.open is just read+decode),
                # keeping the verbatim bytes for lossless .stencil embedding.
                with open(src, "rb") as fh:
                    src_bytes = fh.read()
                img = Image.decode(src_bytes)
                derived_name = self._name_from_path(src)
                src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
        else:
            raise TypeError("unsupported load source: %r" % type(src))
        self._set_source(img, name=name or derived_name, source=source, resource=resource,
                         source_bytes=src_bytes, source_ext=src_ext)
        return self

    def blank(
        self,
        width: Optional[int] = None,
        height: Optional[int] = None,
        color: str = "#ffffff",
        page: str = "A4",
    ) -> "Editor":
        """Create a solid-colour blank page.

        With no explicit size, the dimensions come from the named ``page`` size rendered
        at the core's default DPI (``default_blank_size_px(named_page_size(page))``), so a
        blank A4 matches the CLI/browser blank exactly. ``page`` is any ISO A/B/C name
        (case-insensitive, e.g. "b5"); an unknown name quietly falls back to A4 —
        mirroring the Zig console, whose ``canonicalPageFormat`` maps unknown names to
        null and blanks on the default A4 page. ``color`` is any CSS colour the core
        understands; an unparseable colour falls back to opaque white.
        """
        core = self._get_core()
        if width is None or height is None:
            canonical = core.canonical_page_format(page)
            size = (core.named_page_size(canonical) if canonical else None) or _A4_FALLBACK
            default_w, default_h = core.default_blank_size_px(size[0], size[1])
            if width is None:
                width = default_w
            if height is None:
                height = default_h
        rgba = core.parse_color(color) or (255, 255, 255, 255)
        img = Image.blank(width, height, rgba)
        self._set_source(img, name="blank")
        return self

    def _set_source(
        self,
        img: Image,
        *,
        name: str,
        source: Optional[str] = None,
        resource: Optional[str] = None,
        source_bytes: Optional[bytes] = None,
        source_ext: Optional[str] = None,
    ) -> None:
        """Adopt ``img`` as the new original and reset history to a single pristine state."""
        self._original = img
        # Retain the raw source only with a known ext (else save_project re-encodes to PNG).
        self._source_bytes = source_bytes if source_ext else None
        self._source_ext = (source_ext or "").lower() or None
        self._name = name or "layout"
        self._source = source
        self._resource = resource
        # A fresh source is a fresh project, so its custom accent + keywords reset.
        self._color = ""
        self._keywords = []
        self._history = [_Snapshot()]
        self._cursor = 0

    # ── history plumbing ───────────────────────────────────────────────────────
    def _require_original(self) -> Image:
        """Return the original image or raise — every edit/render needs a loaded source."""
        if self._original is None:
            raise RuntimeError("no image loaded — call load()/blank() first")
        return self._original

    def _current(self) -> _Snapshot:
        """The snapshot under the cursor (the live editing state)."""
        return self._history[self._cursor]

    def _push(self, snapshot: _Snapshot) -> None:
        """Make ``snapshot`` the new current state, dropping any redo history.

        Mirrors the CLI's ``pushState``: truncate at the cursor, append, advance, then cap
        the depth by evicting the oldest *edit* (index 1, never the pristine [0]).
        """
        self._history = self._history[: self._cursor + 1]
        self._history.append(snapshot)
        self._cursor = len(self._history) - 1
        while len(self._history) > _MAX_STATES:
            del self._history[1]
            self._cursor -= 1

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
        # Resolve against the dimensions of the view this crop is applied to.
        view_w, view_h = self._view_dims(cur)
        page_w, page_h = self._page_for_image(view_w, view_h)
        px_per_cm_x = view_w / page_w
        px_per_cm_y = view_h / page_h
        rect = core.resolve_crop(
            spec, view_w, view_h, px_per_cm_x, px_per_cm_y, page_w, page_h, album
        )
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

    # ── page format (project-level; rides the layout) ───────────────────────────
    def set_page_format(
        self,
        name: str,
        width: Optional[float] = None,
        height: Optional[float] = None,
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

    def draw(self, layout: LayoutLike) -> "Editor":
        """APPEND the lines from a layout to the drawing (mirror ``session.addLines``)."""
        self._require_original()
        add = self._coerce_lines(layout)
        cur = self._current()
        nxt = cur.copy()
        nxt.lines = list(cur.lines) + add
        self._push(nxt)
        return self

    def apply_layout(self, layout: LayoutLike) -> "Editor":
        """ADOPT a layout's rotation+crop+filter+lines wholesale (mirror ``adoptServerLayout``).

        Unlike :meth:`draw` (which appends), this replaces the geometry/filter/lines of the
        new state from the layout — used when reopening a peer's stored project layout.
        """
        self._require_original()
        core = self._get_core()
        L = self._coerce_layout(layout)
        crop: Optional[Tuple[int, int, int, int]] = None
        if isinstance(L.crop_rect, dict):
            cr = L.crop_rect
            crop = (
                int(cr.get("x", 0)),
                int(cr.get("y", 0)),
                int(cr.get("width", 0)),
                int(cr.get("height", 0)),
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

    # ── history navigation ─────────────────────────────────────────────────────
    def undo(self) -> bool:
        """Step the cursor back one edit; False if already at the pristine state."""
        if self._cursor == 0:
            return False
        self._cursor -= 1
        return True

    def redo(self) -> bool:
        """Step the cursor forward one edit; False if already at the newest state."""
        if self._cursor + 1 >= len(self._history):
            return False
        self._cursor += 1
        return True

    def reset(self) -> "Editor":
        """Revert to the pristine state, dropping every edit and the redo history."""
        self._cursor = 0
        self._history = self._history[:1]
        return self

    # ── render + save ──────────────────────────────────────────────────────────
    def result(self) -> Image:
        """Derive and return the current view: rotate → crop → filter → rasterize lines.

        Each call rebuilds from the untouched original, so the returned :class:`Image` is a
        fresh, independent buffer (mirrors ``session.rebuild`` producing ``working``).
        """
        orig = self._require_original()
        core = self._get_core()
        snap = self._current()
        img = orig.copy()
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
        # 3. filter in place (custom uses the hex colour as the duotone arg, else the mode;
        #    contour is dimensioned Sobel edge detection, so it takes its own entry point)
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
        for line in snap.lines:
            points = [(p.x, p.y) for p in line.points]
            core.rasterize_line(
                img.data,
                img.width,
                img.height,
                points,
                line.color,
                line.thickness,
                line.marker_size,
                line.style,
                line.locked,
                line.fill_color,
            )
        return img

    def save(self, path: str, fmt: Optional[str] = None) -> Image:
        """Render the current view, write it to ``path``, and return the :class:`Image`."""
        img = self.result()
        img.save(path, fmt)
        return img

    def layout(self) -> Layout:
        """Build the structured layout for the current state (mirror ``currentLayoutJson``).

        ``imageWidth``/``imageHeight`` are the RESULT dimensions; the optional
        filter/crop/rotation fields are emitted only when meaningful (filter present and
        not "none", a crop set, a non-zero rotation), exactly like ``server.buildLayout``.
        """
        img = self.result()
        snap = self._current()
        image_filter = None
        if snap.filter_mode and snap.filter_mode.lower() != "none":
            image_filter = snap.filter_mode
        filter_color = snap.filter_color if snap.filter_color else None
        crop_rect = None
        if snap.crop is not None:
            cx, cy, cw, ch = snap.crop
            crop_rect = {"x": cx, "y": cy, "width": cw, "height": ch}
        rotation_quarters = snap.rotation if snap.rotation != 0 else None
        return Layout(
            image_width=img.width,
            image_height=img.height,
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

    def save_layout(self, path: Optional[str] = None) -> str:
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

    # ── portable .stencil project files ─────────────────────────────────────────
    def save_project(self, path: str) -> str:
        """Write the project (ORIGINAL image + export layout + metadata) as one portable ``.stencil`` file; returns the path."""
        orig = self._require_original()
        # Embed the untouched source bytes verbatim (lossless); only re-encode from pixels for a
        # synthetic original (blank / an in-memory Image) that has none.
        if self._source_bytes is not None:
            image_bytes = self._source_bytes
            ext = self._source_ext or "png"
        else:
            image_bytes = orig.encode("png")
            ext = "png"
        mime = _EXT_MIME.get(ext, "image/png")
        doc: dict = {
            "format": "stencil-project",
            "version": 1,
            "name": self._name or "Untitled",
        }
        if self._color:
            doc["color"] = self._color
        if self._keywords:
            doc["keywords"] = list(self._keywords)
        if self._source:
            doc["source"] = self._source
        if self._resource:
            doc["resource"] = self._resource
        doc["image"] = {
            "dataUrl": "data:%s;base64,%s" % (mime, base64.b64encode(image_bytes).decode("ascii")),
            "ext": ext,
            "w": orig.width,
            "h": orig.height,
        }
        doc["layout"] = self.layout().to_dict()
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(json.dumps(doc, indent=2))
        return path

    def open_project(self, src) -> "Editor":
        """Load a ``.stencil`` project (path, JSON ``bytes``/``str``, or ``dict``) — image + layout + metadata — into this editor; returns self. A ``theme`` block is ignored."""
        if isinstance(src, dict):
            doc = src
        else:
            if isinstance(src, (bytes, bytearray)):
                text = bytes(src).decode("utf-8")
            elif isinstance(src, str) and src.lstrip().startswith("{"):
                text = src  # a JSON string, not a path
            elif isinstance(src, str):
                with open(src, "r", encoding="utf-8") as fh:
                    text = fh.read()
            else:
                raise TypeError("unsupported project source: %r" % type(src))
            doc = json.loads(text)
        if not isinstance(doc, dict) or doc.get("format") != "stencil-project":
            raise ValueError("not a Stencil project file")
        version = doc.get("version", 0)
        if not isinstance(version, int) or version < 1:
            raise ValueError("unrecognized project-file version")
        if version > 1:
            raise ValueError("this project needs a newer pystencil (file version %d)" % version)
        image = doc.get("image")
        if not isinstance(image, dict):
            raise ValueError("project file has no embedded image")
        data_url = image.get("dataUrl", "")
        marker = data_url.find(_BASE64_MARKER)
        if marker < 0:
            raise ValueError("project file has no embedded image")
        try:
            image_bytes = base64.b64decode(data_url[marker + len(_BASE64_MARKER) :])
        except (binascii.Error, ValueError):
            # Match every other bad-input path here (and C#/Zig): a malformed payload is a ValueError.
            raise ValueError("project file has a malformed embedded image") from None
        self.load(
            image_bytes,
            name=doc.get("name") or "Untitled",
            source=doc.get("source"),
            resource=doc.get("resource"),
        )
        # Keep the embedded original verbatim (authoritative ext from the doc) so a re-save is lossless.
        self._source_bytes = image_bytes
        self._source_ext = (image.get("ext") or self._source_ext or "png").lower()
        self._color = doc.get("color") or ""
        self._keywords = _clean_keywords(doc.get("keywords"))
        layout = doc.get("layout")
        if isinstance(layout, dict):
            self.apply_layout(layout)
        return self

    # ── introspection ──────────────────────────────────────────────────────────
    @property
    def image_size(self) -> Tuple[int, int]:
        """The current view's (width, height) — derived cheaply without rasterizing."""
        self._require_original()
        return self._view_dims(self._current())

    @property
    def name(self) -> str:
        """The project name (image basename without extension; "layout"/"blank" fallbacks)."""
        return self._name

    @property
    def project_color(self) -> str:
        """The project's custom accent colour ("#rrggbb"), or "" for the theme fallback."""
        return self._color

    def set_project_color(self, color: str) -> "Editor":
        """Set the project's custom accent colour (normalised to lower-case "#rrggbb").

        An empty/blank value clears it back to "" (theme fallback); any other value is
        parsed by the shared core and rejected with ValueError when unrecognised — the
        same contract the browser's normalizeHex and the CLI's /project-color enforce.
        Push the result to a server project via
        ``ServerConnection.update_project(..., color=editor.project_color)``.
        """
        spec = (color or "").strip()
        if not spec:
            self._color = ""
            return self
        parsed = self._get_core().parse_color(spec)
        if parsed is None:
            raise ValueError("invalid project colour: %r" % color)
        self._color = "#%02x%02x%02x" % (parsed[0], parsed[1], parsed[2])
        return self

    @property
    def keywords(self) -> List[str]:
        """The project's keywords/tags (trimmed, empties dropped). These ride the saved
        ``.stencil`` file and a server project's ``ProjectRecord.keywords``."""
        return list(self._keywords)

    def set_keywords(self, keywords) -> "Editor":
        """Replace the project keywords with a list of strings (trimmed, empties/non-strings
        dropped — mirrors projectFile.js ``cleanKeywords`` and the browser
        ``projectsStore.setKeywords``). Returns self for chaining."""
        self._keywords = _clean_keywords(keywords)
        return self

    def has_image(self) -> bool:
        """True once a source has been loaded (an original image is present)."""
        return self._original is not None

    # ── geometry helpers (ported from session.zig) ─────────────────────────────
    def _view_dims(self, snap: _Snapshot) -> Tuple[int, int]:
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
        rect: Tuple[int, int, int, int], w: int, h: int
    ) -> Tuple[int, int, int, int]:
        """Clamp a rect to lie within a ``w``×``h`` image (port of ``clampRect``)."""
        x, y, rw, rh = rect
        rw = max(1, min(rw, w))
        rh = max(1, min(rh, h))
        x = max(0, min(x, w - rw))
        y = max(0, min(y, h - rh))
        return (x, y, rw, rh)

    def _rotate_rect_quarters(
        self, rect: Tuple[int, int, int, int], w: int, h: int, n: int
    ) -> Tuple[int, int, int, int]:
        """Map a rect through ``n`` clockwise quarter-turns of its ``w``×``h`` image.

        Pure axis-aligned 90° steps; a direct port of the Zig ``rotateRectQuarters``.
        """
        x, y, rw, rh = rect
        cw, ch = w, h
        q = self._get_core().normalize_quarters(n)
        while q > 0:
            # One clockwise step: new dims (ch, cw); (x,y) → (ch - y - rh, x).
            new_x = ch - y - rh
            new_y = x
            new_w = rh
            new_h = rw
            x, y, rw, rh = new_x, new_y, new_w, new_h
            cw, ch = ch, cw
            q -= 1
        return (x, y, rw, rh)

    def _page_for_image(self, w: int, h: int) -> Tuple[float, float]:
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
        x1: Optional[float],
        y1: Optional[float],
        x2: Optional[float],
        y2: Optional[float],
    ) -> str:
        """Assemble a ``"x1=.. y1=.. x2=.. y2=.."`` crop spec, omitting None edges."""
        parts: List[str] = []
        if x1 is not None:
            parts.append("x1=%s" % x1)
        if y1 is not None:
            parts.append("y1=%s" % y1)
        if x2 is not None:
            parts.append("x2=%s" % x2)
        if y2 is not None:
            parts.append("y2=%s" % y2)
        return " ".join(parts)

    # ── layout coercion ────────────────────────────────────────────────────────
    @staticmethod
    def _read_layout_source(src: str) -> str:
        """Turn a layout-source string into layout JSON text.

        A string may be inline JSON, an http(s) URL, or a local file path — mirroring the
        Zig CLI's ``/apply``/``-l`` which accept a path or URL. Inline JSON (starts with
        ``{`` or ``[``) is returned as-is; a URL is fetched; an existing file is read;
        anything else is returned unchanged so :meth:`Layout.from_json` raises a clear error.
        """
        stripped = src.lstrip()
        if stripped.startswith("{") or stripped.startswith("["):
            return src
        if Editor._is_url(src):
            return Editor._fetch_url(src).decode("utf-8")
        if os.path.exists(src):
            with open(src, "r", encoding="utf-8") as handle:
                return handle.read()
        return src

    @staticmethod
    def _coerce_layout(layout: LayoutLike) -> Layout:
        """Coerce a Layout|dict|json-str|json-path|url|list[Line] into a :class:`Layout`."""
        if isinstance(layout, Layout):
            return layout
        if isinstance(layout, str):
            return Layout.from_json(Editor._read_layout_source(layout))
        if isinstance(layout, dict):
            return Layout.from_dict(layout)
        if isinstance(layout, list):
            return Layout(0, 0, lines=Editor._coerce_lines(layout))
        raise TypeError("unsupported layout input: %r" % type(layout))

    @staticmethod
    def _coerce_lines(layout: LayoutLike) -> List[Line]:
        """Extract a list of :class:`Line` from any accepted layout input."""
        # A raw list may hold Line objects or line dicts; everything else routes
        # through _coerce_layout so the str/dict/Layout parsing lives in one place.
        if isinstance(layout, list):
            return [ln if isinstance(ln, Line) else Line.from_dict(ln) for ln in layout]
        return list(Editor._coerce_layout(layout).lines)

    # ── source helpers ─────────────────────────────────────────────────────────
    @staticmethod
    def _is_url(src: str) -> bool:
        """True for http(s) URLs (the only remote scheme load() fetches via urllib)."""
        low = src.lower()
        return low.startswith("http://") or low.startswith("https://")

    @staticmethod
    def _fetch_url(url: str, timeout: float = 30.0) -> bytes:
        """Fetch raw bytes from an http(s) URL with urllib (stdlib, no deps).

        Only http(s) is accepted: urllib would otherwise open file://, ftp://, or
        data: URLs, turning "fetch this image" into a local-file/SSRF read. A
        timeout bounds a hostile or hung server."""
        if not Editor._is_url(url):
            raise ValueError(f"refusing to fetch non-http(s) URL: {url!r}")
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.read()

    @staticmethod
    def _name_from_path(path: str) -> str:
        """Project name = file basename without extension (fallback "image")."""
        stem = os.path.splitext(os.path.basename(path))[0]
        return stem or "image"

    @staticmethod
    def _name_from_url(url: str) -> str:
        """Derive a project name from a URL's path basename (fallback "image")."""
        return Editor._name_from_path(urllib.parse.urlparse(url).path)
