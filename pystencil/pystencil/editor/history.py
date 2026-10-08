"""The snapshot stack: ``_push`` for every mutator; ``undo``/``redo``/``reset``/``clear`` move the cursor."""

from __future__ import annotations

from ..image import Image
from ._snapshot import _MAX_STATES, _Snapshot


class _HistoryApi:
  def _require_original(self) -> Image:
    if self._original is None:
      raise RuntimeError("no image loaded — call load()/blank() first")
    return self._original

  def _current(self) -> _Snapshot:
    return self._history[self._cursor]

  def _push(self, snapshot: _Snapshot) -> None:
    """The CLI's ``pushState``: truncate the redo tail, append, advance, then evict the oldest
    EDIT past ``_MAX_STATES`` (index 1, never the pristine [0])."""
    self._history = self._history[: self._cursor + 1]
    self._history.append(snapshot)
    self._cursor = len(self._history) - 1
    while len(self._history) > _MAX_STATES:
      del self._history[1]
      self._cursor -= 1
    self._revision += 1


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
    """Reset every image-scoped field to the constructed state IN PLACE (§10 ``clear``, the
    console's ``/drop``), so a plan executor holding this editor keeps driving it; the
    injected core and the ``save_chats`` opt-in survive."""
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
