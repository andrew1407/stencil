from __future__ import annotations

"""Download the scanned subset to disk: filename derivation and the parallel fetch."""

import os
import re
import urllib.parse
from typing import List, Optional, TextIO

from .. import _net
from .._severity import emit_error
from .. import codecs
from ..codecs import image_dimensions
from .format import MediaItem
from .net import _sub_strict


def download_media(
  items: List[MediaItem],
  out_dir: str,
  *,
  host: str,
  name: Optional[str] = None,
  err: Optional[TextIO] = None,
) -> List[str]:
  """Download ``items`` into ``out_dir`` (created if missing); return the written paths.

  Each file is named from the sanitized last path segment of its URL with a correct
  extension; a missing/colliding name falls back to ``source-{index}.{ext}``. Pass
  ``name`` to override that: the sanitized custom stem is used as the base filename (with
  the per-item extension appended), and — when more than one item is written — an
  ``-{index}`` suffix keeps the names distinct (``photo.png`` alone, else ``photo-0.png``,
  ``photo-1.jpg`` …). Per-item fetch failures are non-fatal (skipped). When ``err`` is
  given, the DESIGN §3 stderr lines are written there (``wrote …`` per file, ``error:
  could not fetch …`` per failure); the caller prints the final summary.
  """
  os.makedirs(out_dir, exist_ok=True)
  written: List[str] = []
  used: set = set()
  multiple = len(items) > 1
  # Fetch every item at once (each carries its own guard + cap), then name and write in
  # INPUT order so filenames, the `used` set and the stderr lines stay deterministic.
  fetched = _net._fetch_all(items, lambda it: _fetch_media(it, host))
  for idx, (item, data) in enumerate(zip(items, fetched)):
    if not isinstance(data, bytes):
      if err is not None:
        emit_error(err, "could not fetch %s (%s)" % (item.url, data))
      continue
    dims = image_dimensions(data)
    fname = _safe_filename(item, idx, data, dims, used, custom=name, multiple=multiple)
    used.add(fname)
    path = os.path.join(out_dir, fname)
    with open(path, "wb") as fh:
      fh.write(data)
    written.append(path)
    if dims:
      item.width, item.height = dims
      if err is not None:
        err.write(
          "wrote %s (%dx%d px · source %s)\n" % (path, dims[0], dims[1], host)
        )
    elif err is not None:
      err.write("wrote %s (source %s)\n" % (path, host))
  return written


def _fetch_media(item: MediaItem, host: str):
  """Fetch one scraped media URL; returns the bytes, or the exception to report.

  Sub-resource URL: loopback is blocked unless it is on the user-named page's host.
  """
  try:
    return _net._fetch(item.url, strict=_sub_strict(item.url, host))
  except (OSError, ValueError) as e:
    return e


# Magic-byte → extension map for filling in a missing/wrong download extension.
_SNIFF_EXT = {"png": "png", "jpeg": "jpg", "bmp": "bmp"}

# Every char outside this set is replaced with '_' in a download filename (parity with the
# Zig CLI's deriveName sanitizer: alnum / '.' / '_' / '-' are kept, everything else → '_').
_UNSAFE_FILENAME_CHARS = re.compile(r"[^A-Za-z0-9._-]")


def _ext_for(item: MediaItem, data: bytes) -> str:
  """The extension to give a downloaded file: the item's format token, else a sniff."""
  if item.ext:
    return item.ext
  return _SNIFF_EXT.get(codecs.sniff(data), "")


def _safe_filename(
  item: MediaItem,
  idx: int,
  data: bytes,
  dims: Optional[Tuple[int, int]],
  used: set,
  custom: Optional[str] = None,
  multiple: bool = False,
) -> str:
  """Derive a safe, collision-free filename for a downloaded item.

  With ``custom`` set, the sanitized custom string is the stem (``-{index}`` appended when
  ``multiple`` so a batch stays distinct), plus the per-item extension. Otherwise the URL's
  last path segment is used; traversal (``..`` / separators) is rejected and a missing,
  unsafe, or colliding name falls back to ``source-{index}.{ext}``.
  """
  ext = _ext_for(item, data)
  if custom is not None:
    stem = _UNSAFE_FILENAME_CHARS.sub("_", custom).lstrip(".") or "source"
    if multiple:
      stem = "%s-%d" % (stem, idx)
    cname = stem
    if ext and not cname.lower().endswith("." + ext):
      cname = "%s.%s" % (cname, ext)
    return cname
  base = os.path.basename(urllib.parse.urlparse(item.url).path)
  # Guard against path traversal / separators sneaking through a basename.
  if base in ("", ".", "..") or "/" in base or "\\" in base:
    base = ""
  else:
    # Sanitize identically to the Zig CLI: replace every char outside [A-Za-z0-9._-]
    # with '_', then strip leading dots so a ".htaccess"-style name can't hide.
    base = _UNSAFE_FILENAME_CHARS.sub("_", base).lstrip(".")
  name = base
  if name and ext and not name.lower().endswith("." + ext):
    name = "%s.%s" % (name, ext)
  if not name or name in used:
    name = "source-%d" % idx
    if ext:
      name = "%s.%s" % (name, ext)
  return name


