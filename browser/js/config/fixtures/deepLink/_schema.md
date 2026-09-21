# Deep-link codec conformance vectors

Pin the launch-payload / deep-link contract shared by browser (`js/core/launch/deepLink.js`,
the reference), desktop (`io/deepLink.cpp`), bot (`Infrastructure/Links/DeepLinkCodec.cs`)
and the extension (which builds `#stencil=` payloads).

`launchPayload.json` — `{ name, payload, expect: "ok"|"rejected", normalized? }`:
`normalizeLaunchPayload(payload)` must be `null` for "rejected", else deep-equal
`normalized` EXACTLY (which fields survive, which are nulled, `kind` precedence
server > dataUrl > src, `__proto__`/`constructor`/`prototype` dropped, `data:` required
for dataUrl, `https?:` for src). Oversize strings are stored as a RECIPE instead of a
literal: any `{ "$repeat": { "prefix", "char", "length" } }` value expands to
`prefix + char.repeat(length - prefix.length)` (total = `length` chars) before use —
`length` values are relative to `LAUNCH_DATA_URL_MAX` (32 MiB), asserted by the walker.

A `#stencil=` fragment may also carry a top-level `script` (a `.stc` the VS Code extension
hands over). It is deliberately OUTSIDE the normalized shape — the browser reads it off the
raw payload and every other surface ignores it, exactly as the unknown-key vectors pin — so
adding it moved no codec.

Vectors may carry an optional informational `divergences` map
(`{ "<surface>": "one-line summary" }`) inventorying measured surface differences; it is
never asserted — walkers ignore it (none recorded for this family so far; surface-specific
pinned values would live in each surface's local override file).

`telegramStart.json` — `{ name, serverUrl, projectId, expectPayload }`: the t.me
`?start=` codec (`"1" + base64url("host[:port]|id")`, 64-char cap → `null` on overflow).
These are the SAME golden vectors duplicated in the three suites' tests; decoding
surfaces (bot) should also round-trip payload → (normalized origin, id).
