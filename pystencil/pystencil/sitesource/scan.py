from __future__ import annotations

"""HTML scanning: the :class:`html.parser.HTMLParser` subclass that buckets media
records in document order, and :func:`scan_html`, which concatenates the buckets in
DESIGN scan order and dedupes first-wins.
"""

import urllib.parse
from html.parser import HTMLParser

from .._types import NoneType
from .._net import _is_http
from .format import MediaItem, _extract_css_urls, format_of

Attrs = dict[str, str]
MediaRefs = list[tuple[str, str, str]]


class _Scanner(HTMLParser):
  """Collect media records into per-category buckets, preserving document order.

  Buckets are concatenated in the DESIGN scan order (img → svg image → video+poster →
  picture source → background) by :func:`scan_html`, which then dedupes first-wins.
  """

  def __init__(self, base_url: str) -> None:
    super().__init__(convert_charrefs=True)
    self.base = base_url
    self._page_url = base_url
    self.imgs: MediaRefs = list()
    self.svgs: MediaRefs = list()
    self.videos: MediaRefs = list()
    self.sources: MediaRefs = list()
    self.bgs: MediaRefs = list()
    self._base_set = False
    self._cur_video: (dict | NoneType) = None
    self._picture_depth = 0
    self._style_depth = 0
    self._style_buf: list[str] = list()

  # attrs come as a list of (name, value|None) pairs; fold to a lower-cased dict.
  @staticmethod
  def _attr_dict(attrs) -> Attrs:
    return {k.lower(): (v or "") for k, v in attrs}

  def handle_starttag(self, tag, attrs):
    a = self._attr_dict(attrs)
    # A <base href> re-roots relative URL resolution for the whole document.
    # Per the HTML spec the FIRST <base href> wins; ignore any later one.
    if tag == "base" and a.get("href") and not self._base_set:
      self.base = urllib.parse.urljoin(self._page_url, a["href"].strip())
      self._base_set = True
    # Inline background-image on ANY element.
    for raw in _extract_css_urls(a.get("style", "")):
      self.bgs.append(("bg", raw, ""))
    handler = self._START_TAGS.get(tag)
    if handler is not None:
      handler(self, a)

  def _start_img(self, a: Attrs) -> None:
    raw = self._img_url(a)
    if raw:
      self.imgs.append(("img", raw, a.get("alt", "")))

  def _start_svg_image(self, a: Attrs) -> None:
    """Inline ``<svg><image href|xlink:href>``."""
    raw = a.get("href", "").strip() or a.get("xlink:href", "").strip()
    if raw:
      self.svgs.append(("img", raw, ""))

  def _start_picture(self, a: Attrs) -> None:
    self._picture_depth += 1

  def _start_video(self, a: Attrs) -> None:
    self._cur_video = {
      "src": a.get("src", "").strip(),
      "poster": a.get("poster", "").strip(),
      "alt": a.get("aria-label", "").strip(),
      "source": "",
    }

  def _start_source(self, a: Attrs) -> None:
    raw = a.get("src", "").strip()
    if self._cur_video is not None:
      # First http(s) <source> stands in when the <video src> isn't usable.
      if raw and not self._cur_video["source"] and _is_http(
        urllib.parse.urljoin(self.base, raw)
      ):
        self._cur_video["source"] = raw
    elif self._picture_depth > 0 and raw:
      self.sources.append(("img", raw, ""))

  def _start_style(self, a: Attrs) -> None:
    self._style_depth += 1

  def handle_endtag(self, tag):
    if tag == "video" and self._cur_video is not None:
      self._finish_video(self._cur_video)
      self._cur_video = None
    elif tag == "picture" and self._picture_depth > 0:
      self._picture_depth -= 1
    elif tag == "style" and self._style_depth > 0:
      self._style_depth -= 1
      for raw in _extract_css_urls("".join(self._style_buf)):
        self.bgs.append(("bg", raw, ""))
      self._style_buf = list()

  def handle_data(self, data):
    if self._style_depth > 0:
      self._style_buf.append(data)

  def _finish_video(self, v: dict) -> None:
    """Emit the video record (its downloadable URL) then a poster record (if any).

    Order is VIDEO then POSTER, matching the Zig CLI and DESIGN §2 item 3.
    """
    alt = v["alt"]
    # Prefer <video src> when it resolves to http(s); else the first http(s) <source>.
    chosen = ""
    if v["src"] and _is_http(urllib.parse.urljoin(self.base, v["src"])):
      chosen = v["src"]
    elif v["source"]:
      chosen = v["source"]
    if chosen:
      self.videos.append(("video", chosen, alt or "video"))
    if v["poster"]:
      self.videos.append(("poster", v["poster"], alt or "video poster"))

  # Flat tag dispatch: this table is what keeps handle_starttag simple.
  _START_TAGS = {
    "img": _start_img,
    "image": _start_svg_image,
    "picture": _start_picture,
    "video": _start_video,
    "source": _start_source,
    "style": _start_style,
  }

  @staticmethod
  def _img_url(a: Attrs) -> str:
    """Pick an ``<img>`` URL: ``src`` unless it's empty or a ``data:`` placeholder,
    then ``data-src`` / ``data-original`` / ``data-lazy-src`` / first ``srcset`` URL."""
    src = a.get("src", "").strip()
    if src and not src.lower().startswith("data:"):
      return src
    for key in ("data-src", "data-original", "data-lazy-src"):
      v = a.get(key, "").strip()
      if v:
        return v
    srcset = a.get("srcset", "").strip()
    if srcset:
      first = srcset.split(",", 1)[0].strip()
      if first:
        return first.split()[0]
    return src  # a lone data: placeholder — resolved out as non-http later


def scan_html(html: str, base_url: str) -> list[MediaItem]:
  """Parse ``html`` and return the ordered, deduped list of http(s) media candidates.

  URLs are resolved absolute against ``base_url`` (honoring a ``<base href>``), non-http(s)
  schemes (``data:``/``blob:``/…) are dropped, and duplicates are removed first-wins across
  every category. A poster URL matching an already-collected ``<img>`` just re-tags that
  item ``poster`` instead of duplicating it.
  """
  scanner = _Scanner(base_url)
  scanner.feed(html)
  scanner.close()
  records = (
    scanner.imgs + scanner.svgs + scanner.videos + scanner.sources + scanner.bgs
  )
  seen: dict[str, MediaItem] = dict()
  out: list[MediaItem] = list()
  for kind, raw, alt in records:
    if not raw:
      continue
    try:
      url = urllib.parse.urljoin(scanner.base, raw.strip())
    except ValueError:
      continue
    if not _is_http(url):
      continue
    if url in seen:
      existing = seen[url]
      if kind == "poster" and existing.kind != "poster":
        existing.kind = "poster"
      continue
    item = MediaItem(url=url, kind=kind, width=0, height=0, ext=format_of(url), alt=alt or "")
    seen[url] = item
    out.append(item)
  return out

