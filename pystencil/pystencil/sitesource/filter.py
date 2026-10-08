"""Category / format / dimension filters over :class:`MediaItem` (twin of ``filters.js``)."""

from __future__ import annotations

from .._ffi.types import NoneType
from .format import MediaItem


_CATEGORY_KIND = {"img": "img", "video": "video", "background": "bg", "poster": "poster"}


def _category_kinds(category: str) -> (set | NoneType):
  """Selected internal kinds, or ``None`` (every one) when empty or any token is ``all``."""
  if not category or not category.strip(): return None
  kinds = set()
  for tok in category.split("|"):
    t = tok.strip().lower()
    if t == "all": return None
    if t in _CATEGORY_KIND: kinds.add(_CATEGORY_KIND[t])
  return kinds


def _format_tokens(formats: str) -> (set | NoneType):
  """Selected format tokens, or ``None`` (every format) for empty / any ``all`` token."""
  if not formats or not formats.strip(): return None
  tokens = set()
  for tok in formats.split("|"):
    t = tok.strip().lower()
    if t == "all": return None
    if t: tokens.add(t)
  return tokens


def _passes_dimension(
  item: MediaItem, min_w: int, max_w: int, min_h: int, max_h: int
) -> bool:
  """Inclusive bounds, each axis alone; ``-1`` = unset and an unmeasured side (``<= 0``) passes."""
  if item.width > 0:
    if min_w != -1 and item.width < min_w: return False
    if max_w != -1 and item.width > max_w: return False
  if item.height > 0:
    if min_h != -1 and item.height < min_h: return False
    if max_h != -1 and item.height > max_h: return False
  return True


def _is_image_kind(item: MediaItem) -> bool:
  return item.kind in ("img", "bg", "poster")

