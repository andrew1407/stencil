"""HTML scanning: media records bucketed in document order, concatenated in DESIGN scan order
(img → svg image → video+poster → picture source → background) and deduped first-wins."""

from __future__ import annotations

import urllib.parse
from html.parser import HTMLParser

from .._ffi.types import NoneType
from .._net import _is_http
from .format import MediaItem, _extract_css_urls, format_of

Attrs = dict[str, str]
MediaRefs = list[tuple[str, str, str]]


class _Scanner(HTMLParser):
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

  @staticmethod
  def __attr_dict(attrs) -> Attrs:
    return {k.lower(): (v or "") for k, v in attrs}

  def handle_starttag(self, tag, attrs):
    a = self.__attr_dict(attrs)
    # Per the HTML spec the first <base href> wins.
    if tag == "base" and a.get("href") and not self._base_set:
      self.base = urllib.parse.urljoin(self._page_url, a["href"].strip())
      self._base_set = True
    for raw in _extract_css_urls(a.get("style", "")): self.bgs.append(("bg", raw, ""))
    handler = self._START_TAGS.get(tag)
    if handler is not None: handler(self, a)

  def _start_img(self, a: Attrs) -> None:
    raw = self.__img_url(a)
    if raw: self.imgs.append(("img", raw, a.get("alt", "")))

  def _start_svg_image(self, a: Attrs) -> None:
    raw = a.get("href", "").strip() or a.get("xlink:href", "").strip()
    if raw: self.svgs.append(("img", raw, ""))

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
      if raw and not self._cur_video["source"] and _is_http(
        urllib.parse.urljoin(self.base, raw)
      ):
        self._cur_video["source"] = raw
    elif self._picture_depth > 0 and raw: self.sources.append(("img", raw, ""))

  def _start_style(self, a: Attrs) -> None:
    self._style_depth += 1

  def handle_endtag(self, tag):
    if tag == "video" and self._cur_video is not None:
      self.__finish_video(self._cur_video)
      self._cur_video = None
    elif tag == "picture" and self._picture_depth > 0:
      self._picture_depth -= 1
    elif tag == "style" and self._style_depth > 0:
      self._style_depth -= 1
      for raw in _extract_css_urls("".join(self._style_buf)): self.bgs.append(("bg", raw, ""))
      self._style_buf = list()

  def handle_data(self, data):
    if self._style_depth > 0: self._style_buf.append(data)

  def __finish_video(self, v: dict) -> None:
    """The video record, then its poster (DESIGN §2 item 3)."""
    alt = v["alt"]
    chosen = ""
    if v["src"] and _is_http(urllib.parse.urljoin(self.base, v["src"])):
      chosen = v["src"]
    elif v["source"]: chosen = v["source"]
    if chosen: self.videos.append(("video", chosen, alt or "video"))
    if v["poster"]:
      self.videos.append(("poster", v["poster"], alt or "video poster"))

  _START_TAGS = {
    "img": _start_img,
    "image": _start_svg_image,
    "picture": _start_picture,
    "video": _start_video,
    "source": _start_source,
    "style": _start_style,
  }

  @staticmethod
  def __img_url(a: Attrs) -> str:
    """``src`` unless empty or a ``data:`` placeholder, then the lazy-load attributes, then
    the first ``srcset`` URL."""
    src = a.get("src", "").strip()
    if src and not src.lower().startswith("data:"): return src
    for key in ("data-src", "data-original", "data-lazy-src"):
      v = a.get(key, "").strip()
      if v: return v
    srcset = a.get("srcset", "").strip()
    if srcset:
      first = srcset.split(",", 1)[0].strip()
      if first: return first.split()[0]
    return src


def scan_html(html: str, base_url: str) -> list[MediaItem]:
  """The ordered http(s) media candidates, resolved against ``base_url`` or a ``<base href>``,
  deduped first-wins; a poster matching a collected item re-tags it ``poster``."""
  scanner = _Scanner(base_url)
  scanner.feed(html)
  scanner.close()
  records = (
    scanner.imgs + scanner.svgs + scanner.videos + scanner.sources + scanner.bgs
  )
  seen: dict[str, MediaItem] = dict()
  out: list[MediaItem] = list()
  for kind, raw, alt in records:
    if not raw: continue
    try:
      url = urllib.parse.urljoin(scanner.base, raw.strip())
    except ValueError:
      continue
    if not _is_http(url): continue
    if url in seen:
      existing = seen[url]
      if kind == "poster" and existing.kind != "poster": existing.kind = "poster"
      continue
    item = MediaItem(url=url, kind=kind, width=0, height=0, ext=format_of(url), alt=alt or "")
    seen[url] = item
    out.append(item)
  return out

