from __future__ import annotations

"""The scanned-media record and its format-token derivation.

Port of the extension's ``formatOf`` + ``norm`` (``extension/src/lib/imageScan.js``).
The leaf of the package: nothing here imports a sibling.
"""

import re
import urllib.parse
from dataclasses import dataclass
from typing import List


@dataclass
class MediaItem:
  """One scanned media candidate.

  ``kind`` is the internal record kind — ``"img"``, ``"video"``, ``"bg"`` (CSS
  background) or ``"poster"`` (a ``<video poster>`` still) — which maps to the
  user-facing category tokens ``img`` / ``video`` / ``background`` / ``poster``.
  ``width``/``height`` are pixel dimensions once measured (``0`` = unknown), ``ext`` is
  the normalized format token (``""`` when undetectable, bucketed as ``etc`` in
  filters), ``alt`` is any alt/aria text.
  """

  url: str
  kind: str
  width: int = 0
  height: int = 0
  ext: str = ""
  alt: str = ""


_DATA_FMT_RE = re.compile(r"^data:(?:image|video)/([a-z0-9.+-]+)", re.I)
_EXT_RE = re.compile(r"\.([a-z0-9]{2,5})(?:[?#]|$)", re.I)
# Extract every url(...) target from a CSS value (single / double / no quotes).
_CSS_URL_RE = re.compile(r"""url\((['"]?)(.*?)\1\)""")


def _norm(ext: str) -> str:
  """Normalize a raw extension: lowercase, then jpeg→jpg, svg+xml→svg, quicktime→mov."""
  return ext.lower().replace("jpeg", "jpg").replace("svg+xml", "svg").replace(
    "quicktime", "mov"
  )


def format_of(url: str) -> str:
  """Lowercase media format token for a URL or ``data:`` URI (``""`` if unknown).

  Exact port of the extension's ``formatOf``: a ``data:`` URI yields the subtype after
  ``data:image/`` / ``data:video/``; otherwise the last ``.<ext>`` (2–5 chars) of the
  pathname (query/fragment stripped) is taken and normalized.
  """
  if not url:
    return ""
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


def _extract_css_urls(css: str) -> List[str]:
  """Every ``url(...)`` target in a CSS value, skipping inline ``data:image/svg`` icons."""
  out: List[str] = []
  for m in _CSS_URL_RE.finditer(css or ""):
    u = m.group(2)
    if u and not u.lower().startswith("data:image/svg"):
      out.append(u)
  return out

