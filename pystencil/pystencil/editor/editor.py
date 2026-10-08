"""The chainable editor facade — the port of the browser's ``window.stencil`` and the CLI's
``Session``/``EditState`` (``cli/src/console/session.zig``): one untouched ORIGINAL image, a
history of edit snapshots and a cursor. The view is derived on demand by :meth:`result`, so
any edit serializes back to layout JSON; every mutator returns ``self``.
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
  """Chainable image-annotation editor over the shared Stencil core: :meth:`load` or
  :meth:`blank`, chain edits, then :meth:`result`, :meth:`save` or :meth:`layout`."""

  def __init__(self, core: (Core | NoneType) = None) -> None:
    self._core = core
    self._original: (Image | NoneType) = None
    # The original's encoded bytes and ext, embedded verbatim by save_project; None = none.
    self._source_bytes: (bytes | NoneType) = None
    self._source_ext: (str | NoneType) = None
    self._history: list[_Snapshot] = list()
    self._cursor: int = 0
    self._revision: int = 0
    # One-slot memo for result(): ((revision, with_lines), derived Image).
    self._result: (tuple[tuple[int, bool], Image] | NoneType) = None
    # Image basename without extension; "layout" when nothing better is known.
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

  def _get_core(self) -> Core:
    if self._core is None: self._core = get_core()
    return self._core


  def save(self, path: str, fmt: (str | NoneType) = None) -> Image:
    """Render the current view, write it to ``path``, and return the :class:`Image`."""
    img = self.result()
    img.save(path, fmt)
    return img


  @property
  def revision(self) -> int:
    """Bumped on every edit-state change (load/blank, each edit, a moving undo/redo, reset); an
    unchanged revision means :meth:`result` derives the same view, so it keys caches."""
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
    """Set the accent colour as lower-case "#rrggbb"; blank clears it to "" (theme fallback), an
    unparseable one raises ``ValueError`` — the browser's ``normalizeHex`` and the CLI's
    ``/project-color`` contract."""
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
    """Replace the keywords, trimmed with empties and non-strings dropped (project/file.js
    ``cleanKeywords``, the browser's ``projectsStore.setKeywords``)."""
    self._keywords = _clean_keywords(keywords)
    return self

  def has_image(self) -> bool:
    """True once a source has been loaded (an original image is present)."""
    return self._original is not None
