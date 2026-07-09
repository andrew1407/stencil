from __future__ import annotations

"""Source-site scraping for pystencil — the stdlib-only port of the Zig CLI's
``cli/src/scrape.zig`` and a parity twin of the Chrome extension's page scanner
(``extension/src/lib/imageScan.js`` + ``filters.js``).

Fetch a web page over http(s), parse its HTML with :mod:`html.parser` (no third-party
libs), pull out image / video / background / poster media URLs, filter them by
category / format / pixel dimensions, and download the matching subset into a directory.

This is adapter territory (HTML parsing lives outside ``core/``), so nothing here touches
the shared C++ core. The semantics — tokens, extension normalization, scan ordering,
group/count math, and the stderr output grammar — are pinned by the cross-surface DESIGN
contract and MUST stay identical to the Zig CLI.

Static-HTML adaptations vs. the live-DOM extension (documented, deliberate):
  * ``<img>`` has no resolved ``currentSrc``, so we fall back to ``data-src`` /
    ``data-original`` / ``data-lazy-src`` / the first ``srcset`` URL — a superset.
  * background images are read from inline ``style="..."`` and ``<style>`` blocks only
    (no computed style / external stylesheets).
"""

import ipaddress
import os
import re
import socket
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from html.parser import HTMLParser
from typing import Dict, List, Optional, TextIO, Tuple

from . import codecs
from .codecs import image_dimensions

__all__ = [
    "MediaItem",
    "USER_AGENT",
    "scan_html",
    "format_of",
    "scan_page",
    "download_media",
]


# A browser-like User-Agent so plain static hosts (and CDNs that 403 the urllib default)
# serve us the same bytes a real browser would fetch.
USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
)


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


# ── format derivation (port of extension formatOf + norm) ───────────────────────
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


# ── HTML scanning ───────────────────────────────────────────────────────────────
def _is_http(url: str) -> bool:
    """True for absolute http(s) URLs (the only scheme we download)."""
    low = url.lower()
    return low.startswith("http://") or low.startswith("https://")


class _Scanner(HTMLParser):
    """Collect media records into per-category buckets, preserving document order.

    Buckets are concatenated in the DESIGN scan order (img → svg image → video+poster →
    picture source → background) by :func:`scan_html`, which then dedupes first-wins.
    """

    def __init__(self, base_url: str) -> None:
        super().__init__(convert_charrefs=True)
        self.base = base_url
        self._page_url = base_url
        self.imgs: List[Tuple[str, str, str]] = []
        self.svgs: List[Tuple[str, str, str]] = []
        self.videos: List[Tuple[str, str, str]] = []
        self.sources: List[Tuple[str, str, str]] = []
        self.bgs: List[Tuple[str, str, str]] = []
        self._base_set = False
        self._cur_video: Optional[dict] = None
        self._picture_depth = 0
        self._style_depth = 0
        self._style_buf: List[str] = []

    # attrs come as a list of (name, value|None) pairs; fold to a lower-cased dict.
    @staticmethod
    def _attr_dict(attrs) -> Dict[str, str]:
        return {k.lower(): (v or "") for k, v in attrs}

    def handle_starttag(self, tag, attrs):  # noqa: C901 - a flat tag dispatch
        a = self._attr_dict(attrs)
        # A <base href> re-roots relative URL resolution for the whole document.
        # Per the HTML spec the FIRST <base href> wins; ignore any later one.
        if tag == "base" and a.get("href") and not self._base_set:
            self.base = urllib.parse.urljoin(self._page_url, a["href"].strip())
            self._base_set = True
        # Inline background-image on ANY element.
        for raw in _extract_css_urls(a.get("style", "")):
            self.bgs.append(("bg", raw, ""))

        if tag == "img":
            raw = self._img_url(a)
            if raw:
                self.imgs.append(("img", raw, a.get("alt", "")))
        elif tag == "image":  # inline <svg><image href|xlink:href>
            raw = a.get("href", "").strip() or a.get("xlink:href", "").strip()
            if raw:
                self.svgs.append(("img", raw, ""))
        elif tag == "picture":
            self._picture_depth += 1
        elif tag == "video":
            self._cur_video = {
                "src": a.get("src", "").strip(),
                "poster": a.get("poster", "").strip(),
                "alt": a.get("aria-label", "").strip(),
                "source": "",
            }
        elif tag == "source":
            raw = a.get("src", "").strip()
            if self._cur_video is not None:
                # First http(s) <source> stands in when the <video src> isn't usable.
                if raw and not self._cur_video["source"] and _is_http(
                    urllib.parse.urljoin(self.base, raw)
                ):
                    self._cur_video["source"] = raw
            elif self._picture_depth > 0 and raw:
                self.sources.append(("img", raw, ""))
        elif tag == "style":
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
            self._style_buf = []

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

    @staticmethod
    def _img_url(a: Dict[str, str]) -> str:
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


def scan_html(html: str, base_url: str) -> List[MediaItem]:
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
    seen: Dict[str, MediaItem] = {}
    out: List[MediaItem] = []
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


# ── filters (ported from extension filters.js) ──────────────────────────────────
_CATEGORY_KIND = {"img": "img", "video": "video", "background": "bg", "poster": "poster"}


def _category_kinds(category: str) -> Optional[set]:
    """Selected internal kinds, or ``None`` (every category) for empty / any ``all`` token.

    Matches the Zig ``tokenSelected``: as soon as ANY ``|``-separated token equals ``all``
    (case-insensitive) the whole filter passes.
    """
    if not category or not category.strip():
        return None
    kinds = set()
    for tok in category.split("|"):
        t = tok.strip().lower()
        if t == "all":
            return None
        if t in _CATEGORY_KIND:
            kinds.add(_CATEGORY_KIND[t])
    return kinds


def _format_tokens(formats: str) -> Optional[set]:
    """Selected format tokens, or ``None`` (every format) for empty / any ``all`` token."""
    if not formats or not formats.strip():
        return None
    tokens = set()
    for tok in formats.split("|"):
        t = tok.strip().lower()
        if t == "all":
            return None
        if t:
            tokens.add(t)
    return tokens


def _passes_dimension(
    item: MediaItem, min_w: int, max_w: int, min_h: int, max_h: int
) -> bool:
    """Inclusive width/height bounds, checked independently; unknown-size items pass.

    A bound applies only when set (``!= -1``). Port of ``filters.js:81-88``: an item with a
    known width is rejected only when ``< min_w`` or ``> max_w`` (likewise height); an
    unmeasured dimension (``<= 0``) passes unconditionally.
    """
    if item.width > 0:
        if min_w != -1 and item.width < min_w:
            return False
        if max_w != -1 and item.width > max_w:
            return False
    if item.height > 0:
        if min_h != -1 and item.height < min_h:
            return False
        if max_h != -1 and item.height > max_h:
            return False
    return True


def _is_image_kind(item: MediaItem) -> bool:
    """True for decodable-still categories (img / bg / poster), i.e. not video."""
    return item.kind in ("img", "bg", "poster")


# ── page scan (fetch + filter + group/count) ────────────────────────────────────
def scan_page(
    url: str,
    *,
    category: str = "all",
    formats: str = "all",
    name: Optional[str] = None,
    min_width: int = -1,
    max_width: int = -1,
    min_height: int = -1,
    max_height: int = -1,
    count: Optional[int] = None,
    group: int = 0,
) -> List[MediaItem]:
    """Fetch ``url``, scan it, filter (category → format → dimension), then window it.

    ``-1`` on any min/max bound = unset; ``category``/``formats`` are ``|``-joined token
    strings (``all`` = every one). ``count`` is items per group (``None`` = all matches,
    group ignored); ``group`` is the 0-based page index, windowing ``filtered[group*count :
    group*count+count]``. When a dimension bound is active, image-category candidates are
    fetched and measured BEFORE windowing (so the window is over the size-passing list).
    """
    # The page URL is user-named → non-strict (loopback allowed for the user's own dev server).
    page = _fetch(url, strict=False).decode("utf-8", "replace")
    page_host = urllib.parse.urlsplit(url).hostname or ""
    items = scan_html(page, url)

    kinds = _category_kinds(category)
    if kinds is not None:
        items = [it for it in items if it.kind in kinds]

    tokens = _format_tokens(formats)
    if tokens is not None:
        items = [it for it in items if (it.ext or "etc") in tokens]

    if name:
        # Regex matched against each media URL — parity with the CLI's --source-name. Python re
        # is a superset of POSIX ERE; the common metacharacter subset (. * + ? [] ^ $ | ())
        # behaves identically across Python re / the CLI's regex.h / the extension's RegExp.
        # An invalid pattern raises re.error, which the one-shot entry (_run_scrape) reports.
        rx = re.compile(name, re.IGNORECASE)
        items = [it for it in items if rx.search(it.url)]

    dim_active = any(b != -1 for b in (min_width, max_width, min_height, max_height))
    if dim_active:
        # Measure image-category candidates up front, then apply the size filter.
        for it in items:
            if _is_image_kind(it) and it.width <= 0:
                _measure_item(it, page_host)
        items = [
            it
            for it in items
            if _passes_dimension(it, min_width, max_width, min_height, max_height)
        ]

    if count is None:
        return items
    start = max(0, group) * count
    return items[start : start + count]


def _measure_item(item: MediaItem, page_host: str = "") -> None:
    """Fetch an image item's bytes and record its pixel dimensions (best-effort).

    The URL is a sub-resource harvested from page content, so loopback is blocked unless it is
    on the same host the user named (``_sub_strict``); a blocked/failed fetch is silently skipped.
    """
    try:
        data = _fetch(item.url, strict=_sub_strict(item.url, page_host))
    except (OSError, ValueError):
        return
    dims = image_dimensions(data)
    if dims:
        item.width, item.height = dims


# ── download ────────────────────────────────────────────────────────────────────
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
    for idx, item in enumerate(items):
        try:
            # Sub-resource URL: loopback blocked unless it's on the user-named page's host.
            data = _fetch(item.url, strict=_sub_strict(item.url, host))
        except (OSError, ValueError) as e:
            if err is not None:
                err.write("error: could not fetch %s (%s)\n" % (item.url, e))
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


# ── networking (http(s) gate + SSRF guard + size cap, mirroring the Zig CLI's net.zig) ──

# Hard cap on the bytes read from a single fetch — bounds memory against a hostile host that
# streams an endless/huge body (parity with the Zig CLI's net.MAX_FETCH_BYTES). Scrape fetches
# many URLs harvested from untrusted page content, so this matters most there.
MAX_FETCH_BYTES = 64 * 1024 * 1024  # 64 MiB


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Refuse HTTP redirects. A public first hop must not 30x-bounce to an internal host,
    which would slip past the pre-fetch host check (parity with net.zig's redirect_behavior =
    .not_allowed). Also stops a redirect to a non-http(s) scheme (ftp://, file://)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D401
        raise urllib.error.HTTPError(
            req.full_url, code, "refusing to follow redirect to %s" % newurl, headers, fp
        )


# A dedicated opener with redirects refused (the default global opener follows them).
_OPENER = urllib.request.build_opener(_NoRedirect)


def _is_blocked_ip(ip, strict: bool) -> bool:
    """True when ``ip`` is an internal/reserved target a fetch must refuse (SSRF guard).

    Mirrors net.zig's ``isBlockedV4``/``isBlockedV6``: private (RFC1918/ULA), link-local
    (incl. ``169.254.169.254`` cloud metadata), CGNAT, reserved, multicast and unspecified are
    always blocked. Loopback is blocked only when ``strict`` — allowed for a user-named URL,
    refused for a sub-resource harvested from untrusted content on a different host.
    """
    if isinstance(ip, ipaddress.IPv6Address) and ip.ipv4_mapped is not None:
        ip = ip.ipv4_mapped  # classify ::ffff:a.b.c.d as its embedded IPv4
    if ip.is_loopback:
        return strict
    # is_global is False for every private/link-local/reserved/CGNAT/TEST-NET/etc. range.
    return not ip.is_global


def _assert_fetchable(url: str, strict: bool) -> None:
    """Raise ValueError if ``url``'s host is an internal/blocked SSRF target.

    Classifies an IP literal directly; for a DNS name, resolves it and refuses when ANY
    resolved address is internal (closes the hostname-with-internal-record vector, and also
    catches alternate numeric IPv4 encodings — ``getaddrinfo`` canonicalizes ``2130706433`` /
    ``0x7f000001`` to the real address). A residual DNS-rebinding TOCTOU remains, same as the
    Zig CLI. Resolution failure is left for the real fetch to surface as a connection error.
    """
    host = urllib.parse.urlsplit(url).hostname  # lowercased, IPv6 brackets stripped, no userinfo
    if not host:
        raise ValueError("could not parse a host from URL: %r" % url)
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        ip = None
    if ip is not None:
        if _is_blocked_ip(ip, strict):
            raise ValueError("refusing to fetch internal/blocked host: %s" % host)
        return
    if strict and host == "localhost":
        raise ValueError("refusing to fetch internal/blocked host: %s" % host)
    try:
        infos = socket.getaddrinfo(host, None)
    except OSError:
        return  # let the real fetch surface the connection error rather than block the URL
    for info in infos:
        try:
            rip = ipaddress.ip_address(info[4][0])
        except ValueError:
            continue
        if _is_blocked_ip(rip, strict):
            raise ValueError(
                "refusing to fetch host %s — it resolves to internal address %s"
                % (host, info[4][0])
            )


def _sub_strict(media_url: str, page_host: str) -> bool:
    """Whether a media sub-resource fetch runs strict (loopback blocked). Loopback/internal is
    tolerated only when the media is on the SAME host the user named (so a ``localhost`` gallery
    stays scrapeable), while a public page smuggling ``<img src="http://127.0.0.1/…">`` (a
    DIFFERENT internal host) is refused. Parity with the Zig CLI's ``scrape.subStrict``."""
    mh = urllib.parse.urlsplit(media_url).hostname or ""
    return mh.lower() != (page_host or "").lower()


def _fetch(url: str, *, strict: bool = True, timeout: float = 30.0) -> bytes:
    """Fetch raw bytes over http(s) only, SSRF-guarded, redirect-refused, size-capped.

    ``strict`` blocks loopback in addition to the always-blocked internal ranges — the default,
    since most callers fetch sub-resources; pass ``strict=False`` for a URL the user named. A
    browser-like User-Agent is sent and a 30s timeout bounds a hostile/hung server.
    """
    if not _is_http(url):
        raise ValueError("refusing to fetch non-http(s) URL: %r" % url)
    _assert_fetchable(url, strict)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    # nosec - http(s)-gated, SSRF-checked, redirects refused via _OPENER, body size-capped below
    with _OPENER.open(req, timeout=timeout) as resp:
        data = resp.read(MAX_FETCH_BYTES + 1)
    if len(data) > MAX_FETCH_BYTES:
        raise ValueError(
            "response from %s exceeds the %d-byte fetch cap" % (url, MAX_FETCH_BYTES)
        )
    return data
