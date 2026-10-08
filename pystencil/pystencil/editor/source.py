"""Getting pixels IN: local paths, http(s) URLs, raw bytes, an :class:`Image`, or a
blank page. A mixin over the history plumbing :class:`Editor` owns.
"""

from __future__ import annotations

import os
import urllib.parse

from .._ffi.types import NoneType
from .. import _net
from ..image import Image
from ..scriptpaths import is_url
from ._snapshot import _A4_FALLBACK, _Snapshot, LoadSource, _sniff_image_ext


class _SourceApi:
  def load(
    self,
    src: LoadSource,
    *,
    frame: (int | NoneType) = None,
    name: (str | NoneType) = None,
    source: (str | NoneType) = None,
    resource: (str | NoneType) = None,
  ) -> "Editor":
    """Load a new original from a path, an http(s) URL, raw bytes or an :class:`Image`, replacing
    the image and history. ``frame`` is accepted for CLI parity and unused; ``name`` overrides the
    derived project name; ``source``/``resource`` record provenance for server uploads."""
    img: Image
    derived_name: str
    src_bytes: (bytes | NoneType) = None
    src_ext: (str | NoneType) = None
    if isinstance(src, Image):
      # Copy so later in-place core ops never mutate the caller's image.
      img = src.copy()
      derived_name = "image"
    elif isinstance(src, (bytes, bytearray)):
      src_bytes = bytes(src)
      img = Image.decode(src_bytes)
      src_ext = _sniff_image_ext(src_bytes)
      derived_name = "image"
    elif isinstance(src, str):
      if is_url(src):
        src_bytes = self._fetch_url(src)
        img = Image.decode(src_bytes)
        src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
        derived_name = self._name_from_url(src)
        if source is None: source = src
      else:
        with open(src, "rb") as fh:
          src_bytes = fh.read()
        img = Image.decode(src_bytes)
        derived_name = self.__name_from_path(src)
        src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
    else:
      raise TypeError("unsupported load source: %r" % type(src))
    self.__set_source(img, name=name or derived_name, source=source, resource=resource,
            source_bytes=src_bytes, source_ext=src_ext)
    return self

  def blank(
    self,
    width: (int | NoneType) = None,
    height: (int | NoneType) = None,
    color: str = "#ffffff",
    page: str = "A4",
  ) -> "Editor":
    """A solid-colour blank page. Without an explicit size it is the named ``page`` (ISO A/B/C,
    case-insensitive; an unknown name falls back to A4, as the Zig console) at the core's default
    DPI, so a blank A4 matches the CLI and browser. An unparseable ``color`` is opaque white."""
    core = self._get_core()
    if width is None or height is None:
      canonical = core.canonical_page_format(page)
      size = (core.named_page_size(canonical) if canonical else None) or _A4_FALLBACK
      default_w, default_h = core.default_blank_size_px(size[0], size[1])
      if width is None: width = default_w
      if height is None: height = default_h
    rgba = core.parse_color(color) or (255, 255, 255, 255)
    img = Image.blank(width, height, rgba)
    self.__set_source(img, name="blank")
    return self

  def __set_source(
    self,
    img: Image,
    *,
    name: str,
    source: (str | NoneType) = None,
    resource: (str | NoneType) = None,
    source_bytes: (bytes | NoneType) = None,
    source_ext: (str | NoneType) = None,
  ) -> None:
    """Adopt ``img`` as the new original and reset history to a single pristine state."""
    self._original = img
    # Retain the raw source only with a known ext (else save_project re-encodes to PNG).
    self._source_bytes = source_bytes if source_ext else None
    self._source_ext = (source_ext or "").lower() or None
    self._name = name or "layout"
    self._source = source
    self._resource = resource
    # A fresh source is a fresh project, so its custom accent + keywords reset.
    self._color = ""
    self._keywords = list()
    self._history = [_Snapshot()]
    self._cursor = 0
    self._revision += 1


  @staticmethod
  def _fetch_url(url: str, timeout: float = 30.0) -> bytes:
    """Bytes from a USER-named http(s) URL through ``_net._fetch``, non-strict: loopback stays
    reachable, RFC1918 and link-local do not; redirects refused, body capped, timeout bounded."""
    return _net._fetch(url, strict=False, timeout=timeout)

  @staticmethod
  def __name_from_path(path: str) -> str:
    """Project name = file basename without extension (fallback "image")."""
    stem = os.path.splitext(os.path.basename(path))[0]
    return stem or "image"

  @staticmethod
  def _name_from_url(url: str) -> str:
    """Derive a project name from a URL's path basename (fallback "image")."""
    return _SourceApi.__name_from_path(urllib.parse.urlparse(url).path)
