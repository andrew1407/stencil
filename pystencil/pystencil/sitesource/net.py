"""The scraper's sub-resource strictness rule and the best-effort dimension probe over it."""

from __future__ import annotations

import urllib.parse

from .. import _net
from ..codecs import image_dimensions
from .format import MediaItem


def _sub_strict(media_url: str, page_host: str) -> bool:
  """Strict (loopback refused) unless the media is on the host the user named, so a
  ``localhost`` gallery stays scrapeable (twin of the Zig CLI's ``scrape.subStrict``)."""
  mh = urllib.parse.urlsplit(media_url).hostname or ""
  return mh.lower() != (page_host or "").lower()


def _measure_item(item: MediaItem, page_host: str = "") -> None:
  """Record the item's pixel dimensions; a refused or failed fetch leaves it unmeasured."""
  try:
    data = _net._fetch(item.url, strict=_sub_strict(item.url, page_host))
  except (OSError, ValueError):
    return
  dims = image_dimensions(data)
  if dims: item.width, item.height = dims


