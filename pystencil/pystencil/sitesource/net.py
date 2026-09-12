from __future__ import annotations

"""Networking helpers for the scraper (the guarded fetcher itself lives in
``pystencil._net``, shared with ``Editor.load``): the sub-resource strictness rule and
the best-effort dimension probe built on it.
"""

import urllib.parse

from .. import _net
from ..codecs import image_dimensions
from .format import MediaItem


def _sub_strict(media_url: str, page_host: str) -> bool:
  """Whether a media sub-resource fetch runs strict (loopback blocked). Loopback/internal is
  tolerated only when the media is on the SAME host the user named (so a ``localhost`` gallery
  stays scrapeable), while a public page smuggling ``<img src="http://127.0.0.1/…">`` (a
  DIFFERENT internal host) is refused. Parity with the Zig CLI's ``scrape.subStrict``."""
  mh = urllib.parse.urlsplit(media_url).hostname or ""
  return mh.lower() != (page_host or "").lower()


def _measure_item(item: MediaItem, page_host: str = "") -> None:
  """Fetch an image item's bytes and record its pixel dimensions (best-effort).

  The URL is a sub-resource harvested from page content, so loopback is blocked unless it is
  on the same host the user named (``_sub_strict``); a blocked/failed fetch is silently skipped.
  """
  try:
    data = _net._fetch(item.url, strict=_sub_strict(item.url, page_host))
  except (OSError, ValueError):
    return
  dims = image_dimensions(data)
  if dims: item.width, item.height = dims


