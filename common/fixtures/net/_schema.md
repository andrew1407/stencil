# SSRF host corpus

Pins how every fetch guard reads a host and what `net/blockedRanges.json` says about it:
bot (`Application/Editing/AddressRanges.cs`), the extension (`src/lib/connection/urlGuard.js`),
pystencil (`_net.py`), cli (`src/net/host.zig`) and desktop (`src/net/fetchGuard.cpp`).

`hosts.json` — `{ name, host, address, expect?, loopbackName? }`:

- `host` — the host as it stands in a URL authority: brackets and a zone ID may be present, and
  an IPv4 address may be in any `inet_aton` spelling. A walker that goes through a URL wraps an
  unbracketed IPv6 host in brackets and writes `%` as `%25`.
- `address` — the address the host names, compared as a parsed address rather than as text;
  `null` means the host is a name and no guard may read it as a literal.
- `expect` — one verdict, `"block"` or `"allow"`, per policy variant: `fetch`,
  `fetch+allowLoopback`, `serverTarget` and `serverTarget+allowPrivate` (the option after `+`
  is passed). Absent for a name.
- `loopbackName` — `true` on a name a guard that cannot resolve (the extension) must still
  refuse as loopback, as it refuses `127.0.0.1`; a resolving guard judges the address it gets.

A surface that deliberately differs pins its measured verdict in its own override file, with a
one-line note; the corpus itself never changes to fit a surface.
