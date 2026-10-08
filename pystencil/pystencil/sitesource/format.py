"""The scanned-media record and its format token (twin of ``formatOf`` + ``norm`` in
``browser-extension/src/lib/image/scan.js``)."""

from __future__ import annotations

import re
import urllib.parse
from dataclasses import dataclass


@dataclass
class MediaItem:
  """One scanned media candidate: ``kind`` img/video/bg/poster (category ``bg`` = background),
  ``width``/``height`` in px (``0`` = unmeasured), ``ext`` the format token (``""`` = ``etc``)."""

  url: str
  kind: str
  width: int = 0
  height: int = 0
  ext: str = ""
  alt: str = ""


_DATA_FMT_RE = re.compile(r"^data:(?:image|video)/([a-z0-9.+-]+)", re.I)
_EXT_RE = re.compile(r"\.([a-z0-9]{2,5})(?:[?#]|$)", re.I)
_CSS_URL_RE = re.compile(r"""url\((['"]?)(.*?)\1\)""")


def _norm(ext: str) -> str:
  return ext.lower().replace("jpeg", "jpg").replace("svg+xml", "svg").replace(
    "quicktime", "mov"
  )


def format_of(url: str) -> str:
  """The format token of a URL's last ``.<ext>`` (2–5 chars) or a ``data:`` URI's subtype; ``""``
  if unknown."""
  if not url: return ""
  if url.startswith("data:"):
    m = _DATA_FMT_RE.match(url)
    return _norm(m.group(1)) if m else ""
  path = url
  try:
    path = urllib.parse.urlparse(url).path or url
  except ValueError:
    pass
  m = _EXT_RE.search(path)
  return _norm(m.group(1)) if m else ""


def _extract_css_urls(css: str) -> list[str]:
  """Every ``url(...)`` target in a CSS value, skipping inline ``data:image/svg`` icons."""
  out: list[str] = list()
  for m in _CSS_URL_RE.finditer(css or ""):
    u = m.group(2)
    if u and not u.lower().startswith("data:image/svg"): out.append(u)
  return out

