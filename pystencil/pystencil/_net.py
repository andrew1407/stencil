"""Shared outbound-HTTP plumbing for pystencil: the fetch guard (a stdlib-only port of
the Zig CLI's ``cli/src/net.zig``) plus the dev-only TLS context.

Every outward fetch in this package goes through :func:`_fetch`: the scraper's page and
media downloads (``sitesource``) and ``Editor.load(url)`` / layout-URL reads (``editor``).
Keeping one copy means one place to reason about the scheme gate, the SSRF address block,
the refused redirect and the size cap.

Callers differ only in strictness: a URL the USER named runs ``strict=False`` (loopback
allowed), a URL harvested from untrusted content runs strict. :func:`_fetch_all` runs a
batch of independent fetches at once, since they are pure I/O waits.
"""

from __future__ import annotations

import importlib.resources
import ipaddress
import json
import socket
import ssl
import urllib.error
import urllib.parse
import urllib.request

from ._ffi.types import NoneType
from ._raster.parallel import map_parallel


# A browser-like User-Agent so plain static hosts (and CDNs that 403 the urllib default)
# serve us the same bytes a real browser would fetch.
USER_AGENT = (
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
  "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
)


def _is_http(url: str) -> bool:
  """True for absolute http(s) URLs (the only scheme we download)."""
  low = url.lower()
  return low.startswith("http://") or low.startswith("https://")


# Bounds memory against a hostile host streaming an endless body (parity with the Zig
# CLI's net.MAX_FETCH_BYTES).
MAX_FETCH_BYTES = 64 * 1024 * 1024  # 64 MiB


class _NoRedirect(urllib.request.HTTPRedirectHandler):
  """Refuse HTTP redirects. A public first hop must not 30x-bounce to an internal host,
  which would slip past the pre-fetch host check (parity with net.zig's redirect_behavior =
  .not_allowed), and urllib would replay every header — ``Authorization`` included — to
  whatever host the 30x names. Also stops a redirect to a non-http(s) scheme."""

  def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D401
    raise urllib.error.HTTPError(
      req.full_url, code, "refusing to follow redirect to %s" % newurl, headers, fp
    )


# A dedicated opener with redirects refused (the default global opener follows them).
_OPENER = urllib.request.build_opener(_NoRedirect)


def _no_redirect_opener(
  context: (ssl.SSLContext | NoneType) = None
) -> urllib.request.OpenerDirector:
  """The redirect-refusing opener, carrying ``context`` (a custom or dev-only TLS
  context) on its HTTPS handler; ``None`` is the shared default-verified opener."""
  if context is None: return _OPENER
  return urllib.request.build_opener(
    _NoRedirect, urllib.request.HTTPSHandler(context=context))


# The SSRF address table: a checked-in copy of browser/js/config/net/blockedRanges.json
# (tests/test_canonical_drift.py byte-pins it); fixtures/net/hosts.json is its corpus.
_RANGES = json.loads(
  importlib.resources.files("pystencil").joinpath("_data/blockedRanges.json").read_text(encoding="utf-8")
)
_CLASSES = {
  name: tuple(ipaddress.ip_network(c) for c in cidrs) for name, cidrs in _RANGES["classes"].items()
}
_EMBEDS = tuple(
  (ipaddress.ip_network(r["prefix"]), r["offset"], tuple(ipaddress.ip_network(e) for e in r.get("except", ())))
  for r in _RANGES["embedsV4"]
)


def _carried(ip):
  """The IPv4 address an IPv6 form carries (mapped, compatible, NAT64, 6to4), else ``ip``."""
  for prefix, offset, exceptions in _EMBEDS:
    if ip in prefix and not any(ip in e for e in exceptions):
      return ipaddress.IPv4Address(ip.packed[offset : offset + 4])
  return ip


def _address_blocked(ip, policy: str, **options: bool) -> bool:
  """True when the table's ``policy`` refuses ``ip``; ``options`` are its ``blocksUnless`` switches."""
  rules = _RANGES["policies"][policy]
  names = list(rules["blocks"])
  for option, more in rules.get("blocksUnless", {}).items():
    if not options.get(option): names += more
  ip = _carried(ip)
  return any(ip in net for name in names for net in _CLASSES[name])


def _literal(host: str):
  """The address ``host`` spells — IPv6, or IPv4 in any ``inet_aton`` form (hex, octal, one
  to four parts), as the resolver reads it — else None: ``host`` is a name."""
  if ":" in host:
    try:
      return ipaddress.ip_address(host)
    except ValueError:
      return None
  if not host[:1].isdigit(): return None
  try:
    return ipaddress.IPv4Address(socket.inet_aton(host))
  except OSError:
    return None


def _is_blocked_ip(ip, strict: bool) -> bool:
  """True when ``ip`` is an internal/reserved target a fetch must refuse (SSRF guard).

  The table's ``fetch`` policy decides, judging an IPv6 form by the IPv4 address it carries;
  loopback is refused only when ``strict``. Anything stdlib ``is_global`` calls non-global is
  refused as well.
  """
  ip = _carried(ip)
  if ip.is_loopback: return strict
  return _address_blocked(ip, "fetch", allowLoopback=not strict) or not ip.is_global


def _assert_fetchable(url: str, strict: bool) -> None:
  """Raise ValueError if ``url``'s host is an internal/blocked SSRF target.

  Classifies an IP literal directly (``2130706433`` and ``0x7f000001`` included, read as
  :func:`_literal` reads them); for a DNS name, resolves it and refuses when ANY resolved
  address is internal (closes the hostname-with-internal-record vector). A residual
  DNS-rebinding TOCTOU remains, same as the Zig CLI. Resolution failure is left for the real
  fetch to surface as a connection error.
  """
  host = urllib.parse.urlsplit(url).hostname  # lowercased, IPv6 brackets stripped, no userinfo
  if not host:
    raise ValueError("could not parse a host from URL: %r" % url)
  ip = _literal(host)
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


# Bounded pool: these jobs are I/O-bound, but a scrape must never open an antisocial
# number of sockets against one host.
MAX_FETCH_WORKERS = 8


def _fetch_all(jobs, work):
  """Run a batch of independent fetches at once, bounded by :data:`MAX_FETCH_WORKERS`."""
  return map_parallel(jobs, work, MAX_FETCH_WORKERS)


def _unverified_ssl_context() -> ssl.SSLContext:
  """The dev-only ``verify=False`` context for the server client: TLS with the cert
  checks off, built from the public API (no ``ssl._create_unverified_context``).
  check_hostname must be cleared before the verify mode or ssl raises."""
  ctx = ssl.create_default_context()
  ctx.check_hostname = False
  ctx.verify_mode = ssl.CERT_NONE
  return ctx
