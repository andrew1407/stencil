# SSRF address table

`blockedRanges.json` is the one table every fetch guard judges an address by: a literal host,
or each address a host name resolves to. `common/fixtures/net/hosts.json` is its corpus.

- `classes` — named CIDR sets, IPv4 and IPv6 together. A class says what a range *is*; only a
  policy says whether it is refused.
- `embedsV4` — IPv6 prefixes that carry an IPv4 address at byte `offset` of the 16-byte form
  (IPv4-mapped, IPv4-compatible, NAT64, 6to4). An address inside one, and not in its `except`
  list, is judged as the IPv4 address it carries and never as itself.
- `policies` — `blocks` lists the classes always refused; `blocksUnless.<option>` lists the
  classes refused unless the caller passes that option. An address is refused when any class
  the policy applies contains it.

The two policies:

- `fetch` — what an image, layout or page fetcher refuses. `allowLoopback` is for a URL the
  user typed; a URL taken from fetched or scanned content never passes it.
- `serverTarget` — what a collaboration-server URL may not name. Link-local, cloud metadata,
  unspecified, multicast and reserved are always refused; `allowPrivate` admits loopback and
  the private ranges for a local or LAN server. Without it the policy equals strict `fetch`.

A guard parses a host the way its resolver would before judging it: brackets and a zone ID
dropped, and the `inet_aton` spellings (hex, octal, decimal, one to four parts) read as the
IPv4 address they name. A host that is no address at all is a name, judged by what it resolves
to.
