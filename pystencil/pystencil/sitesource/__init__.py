from __future__ import annotations

"""Source-site scraping for pystencil — the stdlib-only port of the Zig CLI's
``cli/src/scrape.zig`` and a parity twin of the Chrome extension's page scanner
(``browser-extension/src/lib/image/scan.js`` + ``filters.js``).

Fetch a web page over http(s), parse its HTML with :mod:`html.parser` (no third-party
libs), pull out image / video / background / poster media URLs, filter them by
category / format / pixel dimensions, and download the matching subset into a directory.

This is adapter territory (HTML parsing lives outside ``core/``), so nothing here touches
the shared C++ core. The semantics — tokens, extension normalization, scan ordering,
group/count math, and the stderr output grammar — are pinned by the cross-surface DESIGN
contract and MUST stay identical to the Zig CLI.

Static-HTML adaptations vs. the live-DOM extension (documented, deliberate):
 * ``<img>`` has no resolved ``currentSrc``, so we fall back to ``data-src`` /
  ``data-original`` / ``data-lazy-src`` / the first ``srcset`` URL — a superset.
 * background images are read from inline ``style="..."`` and ``<style>`` blocks only
  (no computed style / external stylesheets).

Split at its section banners across ``format`` / ``scan`` / ``filter`` / ``download`` /
``net``; this module is the façade and holds :func:`scan_page`, which spans them.
"""

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
  """Fetch ``url``, scan it, filter (category → format → dimension), then window it.

  ``-1`` on any min/max bound = unset; ``category``/``formats`` are ``|``-joined token
  strings (``all`` = every one). ``count`` is items per group (``None`` = all matches,
  group ignored); ``group`` is the 0-based page index, windowing ``filtered[group*count :
  group*count+count]``. When a dimension bound is active, image-category candidates are
  fetched and measured BEFORE windowing (so the window is over the size-passing list).
  """
  # The page URL is user-named → non-strict (loopback allowed for the user's own dev server).
  page = _net._fetch(url, strict=False).decode("utf-8", "replace")
  page_host = urllib.parse.urlsplit(url).hostname or ""
  items = scan_html(page, url)

  kinds = _category_kinds(category)
  if kinds is not None: items = [it for it in items if it.kind in kinds]

  tokens = _format_tokens(formats)
  if tokens is not None: items = [it for it in items if (it.ext or "etc") in tokens]

  if name:
    # Regex matched against each media URL — parity with the CLI's --source-name. The common
    # metacharacter subset behaves identically across Python re, the CLI's regex.h and RegExp.
    rx = re.compile(name, re.IGNORECASE)
    items = [it for it in items if rx.search(it.url)]

  dim_active = any(b != -1 for b in (min_width, max_width, min_height, max_height))
  if dim_active:
    # Measure image-category candidates up front, then apply the size filter. The
    # measurements are independent fetches, so they go out together.
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

