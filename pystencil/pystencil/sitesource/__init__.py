"""Source-site scraping: twin of ``cli/src/scrape.zig`` and the extension's
``browser-extension/src/lib/image/scan.js`` + ``filters.js``.

Tokens, extension normalization, scan order, group/count math and the stderr grammar stay
identical to the Zig CLI. Static HTML has no ``currentSrc`` or computed style, so ``<img>``
falls back to the lazy-load attributes and backgrounds come from inline and ``<style>`` CSS.
"""

from __future__ import annotations

import re
import urllib.parse

from .._ffi.types import NoneType
from .. import _net
from .._net import USER_AGENT
from .download import download_media
from .filter import _category_kinds, _format_tokens, _is_image_kind, _passes_dimension
from .format import MediaItem, format_of
from .net import _measure_item, _sub_strict
from .scan import scan_html

__all__ = [
  "MediaItem",
  "USER_AGENT",
  "scan_html",
  "format_of",
  "scan_page",
  "download_media",
]


def scan_page(
  url: str,
  *,
  category: str = "all",
  formats: str = "all",
  name: (str | NoneType) = None,
  min_width: int = -1,
  max_width: int = -1,
  min_height: int = -1,
  max_height: int = -1,
  count: (int | NoneType) = None,
  group: int = 0,
) -> list[MediaItem]:
  """Fetch ``url``, scan it, filter (category → format → name → dimension), then window it.

  ``-1`` on a bound = unset; ``category``/``formats`` are ``|``-joined tokens (``all`` = every
  one). ``count`` is items per group (``None`` = all, group ignored), ``group`` the 0-based
  window; with a size bound, candidates are measured before windowing.
  """
  # User-named, so loopback is allowed.
  page = _net._fetch(url, strict=False).decode("utf-8", "replace")
  page_host = urllib.parse.urlsplit(url).hostname or ""
  items = scan_html(page, url)

  kinds = _category_kinds(category)
  if kinds is not None: items = [it for it in items if it.kind in kinds]

  tokens = _format_tokens(formats)
  if tokens is not None: items = [it for it in items if (it.ext or "etc") in tokens]

  if name:
    # The CLI's --source-name: the common metacharacter subset matches alike in re, regex.h
    # and RegExp.
    rx = re.compile(name, re.IGNORECASE)
    items = [it for it in items if rx.search(it.url)]

  dim_active = any(b != -1 for b in (min_width, max_width, min_height, max_height))
  if dim_active:
    unmeasured = [it for it in items if _is_image_kind(it) and it.width <= 0]
    _net._fetch_all(unmeasured, lambda it: _measure_item(it, page_host))
    items = [
      it
      for it in items
      if _passes_dimension(it, min_width, max_width, min_height, max_height)
    ]

  if count is None: return items
  start = max(0, group) * count
  return items[start : start + count]

