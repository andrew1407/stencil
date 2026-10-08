"""The one fetch guard (a stdlib port of ``cli/src/net.zig``) and the dev-only TLS context.

Every outward fetch goes through :func:`_fetch`: scheme gate, SSRF address block, refused
redirect, byte cap. A URL the user named runs ``strict=False`` (loopback allowed); one
harvested from untrusted content runs strict.
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


# CDNs that 403 the urllib default serve a browser-like agent the bytes a browser gets.
USER_AGENT = (
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
  "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
)


def _is_http(url: str) -> bool:
  """True for absolute http(s) URLs (the only scheme we download)."""
  low = url.lower()
  return low.startswith("http://") or low.startswith("https://")


# Bounds a hostile endless body; parity with net.zig's MAX_FETCH_BYTES.
MAX_FETCH_BYTES = 64 * 1024 * 1024


class _NoRedirect(urllib.request.HTTPRedirectHandler):
  """Refuse every 30x: a bounce to an internal host or another scheme would skip the host
  check, and urllib would replay ``Authorization`` to it (net.zig's ``.not_allowed``)."""

  def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D401
    raise urllib.error.HTTPError(
      req.full_url, code, "refusing to follow redirect to %s" % newurl, headers, fp
    )


_OPENER = urllib.request.build_opener(_NoRedirect)


def _no_redirect_opener(
  context: (ssl.SSLContext | NoneType) = None
) -> urllib.request.OpenerDirector:
  """The redirect-refusing opener, carrying ``context`` (a custom or dev-only TLS
  context) on its HTTPS handler; ``None`` is the shared default-verified opener."""
  if context is None: return _OPENER
  return urllib.request.build_opener(
    _NoRedirect, urllib.request.HTTPSHandler(context=context))


# The SSRF address table: a build-time copy of common/config/net/blockedRanges.json
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
  """True when the table's ``fetch`` policy or stdlib ``is_global`` refuses ``ip``, an IPv6
  form judged by the IPv4 it carries; loopback only when ``strict``."""
  ip = _carried(ip)
  if ip.is_loopback: return strict
  return _address_blocked(ip, "fetch", allowLoopback=not strict) or not ip.is_global


def _assert_fetchable(url: str, strict: bool) -> None:
  """Raise ValueError if ``url``'s host is a blocked SSRF target: an IP literal in any
  :func:`_literal` form, or a name ANY of whose addresses is blocked. The fetch resolves
  again, so DNS rebinding between the lookups is a residual TOCTOU, as in the Zig CLI."""
  host = urllib.parse.urlsplit(url).hostname
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
    return  # the fetch surfaces the connection error
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
  """Raw bytes over http(s), SSRF-guarded, redirect-refused, size-capped; ``ValueError`` on a
  refusal. ``strict`` also blocks loopback; ``timeout`` is seconds."""
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


# Bounds the sockets one scrape opens against one host.
MAX_FETCH_WORKERS = 8


def _fetch_all(jobs, work):
  """``map_parallel`` bounded by :data:`MAX_FETCH_WORKERS`."""
  return map_parallel(jobs, work, MAX_FETCH_WORKERS)


def _unverified_ssl_context() -> ssl.SSLContext:
  """The dev-only ``verify=False`` TLS context; ``check_hostname`` is cleared before the
  verify mode or ssl raises."""
  ctx = ssl.create_default_context()
  ctx.check_hostname = False
  ctx.verify_mode = ssl.CERT_NONE
  return ctx
