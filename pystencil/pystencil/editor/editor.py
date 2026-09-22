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

Split across _snapshot / source / edits / derive / layout_io / project / assistant;
what stays here is the history itself — the original image, the snapshot stack and the
cursor every mixin reads and pushes through.
"""

from __future__ import annotations

import os

from .._ffi.types import NoneType
from ..core import Core, get_core
from ..image import Image
from ._snapshot import _Snapshot, _clean_keywords
from .assistant import _AssistantApi
from .derive import _DeriveApi
from .edits import _EditApi
from .history import _HistoryApi
from .layout_io import _LayoutApi
from .project import _ProjectApi
from .script import _ScriptApi
from .source import _SourceApi


class Editor(
  _SourceApi,
  _HistoryApi,
  _EditApi,
  _DeriveApi,
  _LayoutApi,
  _ProjectApi,
  _AssistantApi,
  _ScriptApi,
):
  """Chainable image-annotation editor over the shared Stencil core.

  Construct one, :meth:`load` (or :meth:`blank`) a source, then chain edits
  (:meth:`rotate`, :meth:`crop`, :meth:`set_filter`, :meth:`draw`, ...). Call
  :meth:`result` for the derived :class:`Image`, :meth:`save` to write it, or
  :meth:`layout`/:meth:`save_layout` for the structured payload.
  """

  def __init__(self, core: (Core | NoneType) = None) -> None:
    # A caller may inject a Core; otherwise we lazily share the process singleton so
    # codec-only construction stays cheap and tests can pass an explicit handle.
    self._core = core
    self._original: (Image | NoneType) = None
    # Raw encoded bytes of the original + its ext (None ⇒ none), kept so save_project embeds
    # the untouched source (lossless) instead of a PNG re-encode. See _set_source.
    self._source_bytes: (bytes | NoneType) = None
    self._source_ext: (str | NoneType) = None
    self._history: list[_Snapshot] = list()
    self._cursor: int = 0
    # Monotonic edit-state counter backing the public `revision` property.
    self._revision: int = 0
    # One-slot memo for result(): ((revision, with_lines), derived Image).
    self._result: (tuple[tuple[int, bool], Image] | NoneType) = None
    # Project name = image basename without extension; "layout" is the documented
    # fallback used by save_layout when nothing better is known.
    self._name: str = "layout"
    # Optional provenance metadata (mirrors the server project's source/resource fields).
    self._source: (str | NoneType) = None
    self._resource: (str | NoneType) = None
    # Custom per-project accent colour painting the project name; "" = theme fallback
    # (mirrors ProjectMeta.color / the server ProjectRecord `color` field).
    self._color: str = ""
    # Free-text keywords/tags (project-level; ride the .stencil file + the server
    # ProjectRecord.keywords). Trimmed, empties dropped — matches project/file.js cleanKeywords.
    self._keywords: list[str] = list()
    # x/y coordinate-transform formulas (project-level; ride the layout, browser applies them).
    self._allow_formulas: bool = False
    self._formula_x: str = ""
    self._formula_y: str = ""
    # §12 chat persistence (opt-in, default OFF everywhere): the attached chat document is
    # written under the top-level "chat" key only while save_chats is on.
    self.save_chats: bool = False
    self.chat_doc: (dict | NoneType) = None
    # Page format (project-level; rides the layout like the CLI session's page_size).
    # "" = unset (the layout omits pageSize); custom dims are cm, 0 = unset.
    self._page_size: str = ""
    self._custom_page_width: float = 0.0
    self._custom_page_height: float = 0.0

  # ── core access ────────────────────────────────────────────────────────────
  def _get_core(self) -> Core:
    """Return the injected Core or the lazily-loaded process singleton."""
    if self._core is None: self._core = get_core()
    return self._core


  def save(self, path: str, fmt: (str | NoneType) = None) -> Image:
    """Render the current view, write it to ``path``, and return the :class:`Image`."""
    img = self.result()
    img.save(path, fmt)
    return img


  # ── introspection ──────────────────────────────────────────────────────────
  @property
  def revision(self) -> int:
    """A monotonic counter bumped on every edit-state mutation: load()/blank()
    (and everything routing through them), each edit, undo/redo (when they
    move), and reset. An unchanged revision means :meth:`result` derives the
    same view, so it is the public key for caches over the rendered image —
    e.g. the console's encoded-PNG memo for /prompt attachments."""
    return self._revision

  @property
  def image_size(self) -> tuple[int, int]:
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
    if parsed is None: raise ValueError("invalid project colour: %r" % color)
    self._color = "#%02x%02x%02x" % (parsed[0], parsed[1], parsed[2])
    return self

  @property
  def keywords(self) -> list[str]:
    """The project's keywords/tags (trimmed, empties dropped). These ride the saved
    ``.stencil`` file and a server project's ``ProjectRecord.keywords``."""
    return list(self._keywords)

  def set_keywords(self, keywords) -> "Editor":
    """Replace the project keywords with a list of strings (trimmed, empties/non-strings
    dropped — mirrors project/file.js ``cleanKeywords`` and the browser
    ``projectsStore.setKeywords``). Returns self for chaining."""
    self._keywords = _clean_keywords(keywords)
    return self

  def has_image(self) -> bool:
    """True once a source has been loaded (an original image is present)."""
    return self._original is not None
