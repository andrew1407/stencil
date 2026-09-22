"""The snapshot stack itself: pushing a new state, and moving the cursor over it.

Every mutator elsewhere in the facade goes through ``_push``; ``undo``/``redo``/
``reset``/``clear`` move or truncate the cursor and let the view re-derive.
"""

from __future__ import annotations

from ..image import Image
from ._snapshot import _MAX_STATES, _Snapshot


class _HistoryApi:
  """History plumbing and navigation over the ``_Snapshot`` stack."""

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
    self._revision += 1


  # ── history navigation ─────────────────────────────────────────────────────
  def undo(self) -> bool:
    """Step the cursor back one edit; False if already at the pristine state."""
    if self._cursor == 0: return False
    self._cursor -= 1
    self._revision += 1
    return True

  def redo(self) -> bool:
    """Step the cursor forward one edit; False if already at the newest state."""
    if self._cursor + 1 >= len(self._history): return False
    self._cursor += 1
    self._revision += 1
    return True

  def reset(self) -> "Editor":
    """Revert to the pristine state, dropping every edit and the redo history."""
    self._cursor = 0
    self._history = self._history[:1]
    self._revision += 1
    return self

  def clear(self) -> "Editor":
    """Drop the working image and its lines IN PLACE, leaving the editor empty.

    The §10 ``clear`` semantics (the editors' "Clear (remove) current project" /
    the console's ``/drop``), kept in place so a plan executor holding this editor
    keeps driving the same object. Everything image-scoped resets — source bytes,
    history, name, metadata, formulas, page format, restored chat — back to the
    constructed state; the injected core and the ``save_chats`` opt-in survive.
    """
    self._original = None
    self._source_bytes = None
    self._source_ext = None
    self._history = list()
    self._cursor = 0
    self._name = "layout"
    self._source = None
    self._resource = None
    self._color = ""
    self._keywords = list()
    self._allow_formulas = False
    self._formula_x = ""
    self._formula_y = ""
    self.chat_doc = None
    self._page_size = ""
    self._custom_page_width = 0.0
    self._custom_page_height = 0.0
    self._revision += 1
    return self
