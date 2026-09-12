from __future__ import annotations

"""Category / format / dimension filters, ported from the extension's ``filters.js``.

Pure predicates over :class:`MediaItem`; the page scan applies them in that order.
"""

from .._types import NoneType
from .format import MediaItem


_CATEGORY_KIND = {"img": "img", "video": "video", "background": "bg", "poster": "poster"}


def _category_kinds(category: str) -> (set | NoneType):
  """Selected internal kinds, or ``None`` (every category) for empty / any ``all`` token.

  Matches the Zig ``tokenSelected``: as soon as ANY ``|``-separated token equals ``all``
  (case-insensitive) the whole filter passes.
  """
  if not category or not category.strip():
    return None
  kinds = set()
  for tok in category.split("|"):
    t = tok.strip().lower()
    if t == "all":
      return None
    if t in _CATEGORY_KIND:
      kinds.add(_CATEGORY_KIND[t])
  return kinds


def _format_tokens(formats: str) -> (set | NoneType):
  """Selected format tokens, or ``None`` (every format) for empty / any ``all`` token."""
  if not formats or not formats.strip():
    return None
  tokens = set()
  for tok in formats.split("|"):
    t = tok.strip().lower()
    if t == "all":
      return None
    if t:
      tokens.add(t)
  return tokens


def _passes_dimension(
  item: MediaItem, min_w: int, max_w: int, min_h: int, max_h: int
) -> bool:
  """Inclusive width/height bounds, checked independently; unknown-size items pass.

  A bound applies only when set (``!= -1``). Port of ``filters.js:81-88``: an item with a
  known width is rejected only when ``< min_w`` or ``> max_w`` (likewise height); an
  unmeasured dimension (``<= 0``) passes unconditionally.
  """
  if item.width > 0:
    if min_w != -1 and item.width < min_w:
      return False
    if max_w != -1 and item.width > max_w:
      return False
  if item.height > 0:
    if min_h != -1 and item.height < min_h:
      return False
    if max_h != -1 and item.height > max_h:
      return False
  return True


def _is_image_kind(item: MediaItem) -> bool:
  """True for decodable-still categories (img / bg / poster), i.e. not video."""
  return item.kind in ("img", "bg", "poster")

