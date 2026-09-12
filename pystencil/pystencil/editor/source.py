from __future__ import annotations

"""Getting pixels IN: local paths, http(s) URLs, raw bytes, an :class:`Image`, or a
blank page. A mixin over the history plumbing :class:`Editor` owns.
"""

import os
import urllib.parse
from typing import Optional, Tuple

from .. import _net
from ..image import Image
from ._snapshot import _A4_FALLBACK, _Snapshot, LoadSource, _sniff_image_ext


class _SourceApi:
  """Source loading, blank-page creation and the name/extension derivation."""

  # ── source ─────────────────────────────────────────────────────────────────
  def load(
    self,
    src: LoadSource,
    *,
    frame: Optional[int] = None,
    name: Optional[str] = None,
    source: Optional[str] = None,
    resource: Optional[str] = None,
  ) -> "Editor":
    """Load a new original from a path, an http(s) URL, raw bytes, or an :class:`Image`.

    ``frame`` is accepted for API parity with the CLI's video-frame extraction but is
    not used here (codecs live in the adapters, not this stdlib-only package).
    ``name`` overrides the derived project name; ``source``/``resource`` record
    provenance for later server uploads. Replaces any current image + history.
    """
    img: Image
    derived_name: str
    # Raw encoded source bytes + ext, kept verbatim for a lossless save_project (None ⇒ none).
    src_bytes: Optional[bytes] = None
    src_ext: Optional[str] = None
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
      if self._is_url(src):
        src_bytes = self._fetch_url(src)
        img = Image.decode(src_bytes)
        src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
        derived_name = self._name_from_url(src)
        # Default the recorded source to the URL we fetched.
        if source is None:
          source = src
      else:
        # Read the file once and decode from the bytes (Image.open is just read+decode),
        # keeping the verbatim bytes for lossless .stencil embedding.
        with open(src, "rb") as fh:
          src_bytes = fh.read()
        img = Image.decode(src_bytes)
        derived_name = self._name_from_path(src)
        src_ext = os.path.splitext(src)[1].lstrip(".").lower() or _sniff_image_ext(src_bytes)
    else:
      raise TypeError("unsupported load source: %r" % type(src))
    self._set_source(img, name=name or derived_name, source=source, resource=resource,
            source_bytes=src_bytes, source_ext=src_ext)
    return self

  def blank(
    self,
    width: Optional[int] = None,
    height: Optional[int] = None,
    color: str = "#ffffff",
    page: str = "A4",
  ) -> "Editor":
    """Create a solid-colour blank page.

    With no explicit size, the dimensions come from the named ``page`` size rendered
    at the core's default DPI (``default_blank_size_px(named_page_size(page))``), so a
    blank A4 matches the CLI/browser blank exactly. ``page`` is any ISO A/B/C name
    (case-insensitive, e.g. "b5"); an unknown name quietly falls back to A4 —
    mirroring the Zig console, whose ``canonicalPageFormat`` maps unknown names to
    null and blanks on the default A4 page. ``color`` is any CSS colour the core
    understands; an unparseable colour falls back to opaque white.
    """
    core = self._get_core()
    if width is None or height is None:
      canonical = core.canonical_page_format(page)
      size = (core.named_page_size(canonical) if canonical else None) or _A4_FALLBACK
      default_w, default_h = core.default_blank_size_px(size[0], size[1])
      if width is None:
        width = default_w
      if height is None:
        height = default_h
    rgba = core.parse_color(color) or (255, 255, 255, 255)
    img = Image.blank(width, height, rgba)
    self._set_source(img, name="blank")
    return self

  def _set_source(
    self,
    img: Image,
    *,
    name: str,
    source: Optional[str] = None,
    resource: Optional[str] = None,
    source_bytes: Optional[bytes] = None,
    source_ext: Optional[str] = None,
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
    self._keywords = []
    self._history = [_Snapshot()]
    self._cursor = 0
    self._revision += 1


  # ── source helpers ─────────────────────────────────────────────────────────
  @staticmethod
  def _is_url(src: str) -> bool:
    """True for http(s) URLs (the only remote scheme load() fetches via urllib)."""
    low = src.lower()
    return low.startswith("http://") or low.startswith("https://")

  @staticmethod
  def _fetch_url(url: str, timeout: float = 30.0) -> bytes:
    """Fetch raw bytes from an http(s) URL through the shared guard in ``_net``.

    One copy of the rules for the whole package: http(s) only (urllib would otherwise
    open file://, ftp:// or data: URLs), internal/metadata addresses refused, redirects
    not followed, body size-capped, timeout bounded. Non-strict because ``load(url)`` is
    a URL the USER named, so loopback stays reachable while RFC1918/link-local do not.
    """
    return _net._fetch(url, strict=False, timeout=timeout)

  @staticmethod
  def _name_from_path(path: str) -> str:
    """Project name = file basename without extension (fallback "image")."""
    stem = os.path.splitext(os.path.basename(path))[0]
    return stem or "image"

  @staticmethod
  def _name_from_url(url: str) -> str:
    """Derive a project name from a URL's path basename (fallback "image")."""
    return _SourceApi._name_from_path(urllib.parse.urlparse(url).path)
