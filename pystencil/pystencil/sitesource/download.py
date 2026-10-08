"""Download the scanned subset to disk: filename derivation and the parallel fetch."""

from __future__ import annotations

import os
import re
import urllib.parse
from typing import TextIO

from .._ffi.types import NoneType
from .. import _net
from .._severity import emit_error
from .. import codecs
from ..codecs import image_dimensions
from .format import MediaItem
from .net import _sub_strict


def download_media(
  items: list[MediaItem],
  out_dir: str,
  *,
  host: str,
  name: (str | NoneType) = None,
  err: (TextIO | NoneType) = None,
) -> list[str]:
  """Download ``items`` into ``out_dir``; return the written paths, a failed fetch skipped.

  ``err`` gets the DESIGN §3 lines (``wrote …``, ``error: could not fetch …``); the caller
  prints the summary.
  """
  os.makedirs(out_dir, exist_ok=True)
  written: list[str] = list()
  used: set = set()
  multiple = len(items) > 1
  # Named and written in input order, so names and stderr lines stay deterministic.
  fetched = _net._fetch_all(items, lambda it: __fetch_media(it, host))
  for idx, (item, data) in enumerate(zip(items, fetched)):
    if not isinstance(data, bytes):
      if err is not None:
        emit_error(err, "could not fetch %s (%s)" % (item.url, data))
      continue
    dims = image_dimensions(data)
    fname = __safe_filename(item, idx, data, dims, used, custom=name, multiple=multiple)
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
    elif err is not None: err.write("wrote %s (source %s)\n" % (path, host))
  return written


def __fetch_media(item: MediaItem, host: str):
  """The bytes, or the exception to report; loopback only on the user-named page's host."""
  try:
    return _net._fetch(item.url, strict=_sub_strict(item.url, host))
  except (OSError, ValueError) as e:
    return e


_SNIFF_EXT = {"png": "png", "jpeg": "jpg", "bmp": "bmp"}

# Twin of the Zig CLI's deriveName sanitizer.
_UNSAFE_FILENAME_CHARS = re.compile(r"[^A-Za-z0-9._-]")


def __ext_for(item: MediaItem, data: bytes) -> str:
  if item.ext: return item.ext
  return _SNIFF_EXT.get(codecs.sniff(data), "")


def __safe_filename(
  item: MediaItem,
  idx: int,
  data: bytes,
  dims: (tuple[int, int] | NoneType),
  used: set,
  custom: (str | NoneType) = None,
  multiple: bool = False,
) -> str:
  """The sanitized ``custom`` stem (``-{idx}`` when ``multiple``), else the URL's last segment;
  a traversal, missing or colliding name becomes ``source-{idx}``; the extension appended."""
  ext = __ext_for(item, data)
  if custom is not None:
    stem = _UNSAFE_FILENAME_CHARS.sub("_", custom).lstrip(".") or "source"
    if multiple: stem = "%s-%d" % (stem, idx)
    cname = stem
    if ext and not cname.lower().endswith("." + ext): cname = "%s.%s" % (cname, ext)
    return cname
  base = os.path.basename(urllib.parse.urlparse(item.url).path)
  if base in ("", ".", "..") or "/" in base or "\\" in base:
    base = ""
  else:
    # Leading dots stripped so a ".htaccess"-style name cannot hide.
    base = _UNSAFE_FILENAME_CHARS.sub("_", base).lstrip(".")
  name = base
  if name and ext and not name.lower().endswith("." + ext): name = "%s.%s" % (name, ext)
  if not name or name in used:
    name = "source-%d" % idx
    if ext: name = "%s.%s" % (name, ext)
  return name


