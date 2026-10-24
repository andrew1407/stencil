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

A `#stencil=` fragment may also carry a top-level `script` (a `.stc` the VS Code extension hands
over, at most `LAUNCH.scriptMaxChars` in `config/constants.json`)
and `scriptMode` (`open` or `run`, still written by senders; every receiver now opens the script
in its Script window and runs nothing). Both are
deliberately OUTSIDE the normalized shape — the browser reads them off the raw payload and
every other surface ignores them, exactly as the unknown-key vectors pin — so adding them moved
no codec. A script-only payload normalizes to nothing; the browser still takes its script.

Vectors may carry an optional informational `divergences` map
(`{ "<surface>": "one-line summary" }`) inventorying measured surface differences; it is
never asserted — walkers ignore it (none recorded for this family so far; surface-specific
pinned values would live in each surface's local override file).

`launchQuery.json` — `{ name, search?, hash?, expect }`: the launch URI
(`contracts/launch/launch-uri.md`), the `?key=value` pairs of the browser app's URL and the same
pairs in its fragment. `expect` is `null` when the parts carry nothing of the grammar, else the
fields of the parsed query that differ from the EMPTY query (`picture`, `name`, `layout` null;
`incognito` false; `script` ''; `theme` null; `settings`, `llm` `{}`; `chat` `{open:false, dock:null}`;
`connect`, `warnings` `[]`; `project` all null; `fromHash`, `opensSession` false) — a nested object
is given whole. Walked by `browser/tests/core/launch/queryFixtures.test.js` (the reference,
`js/core/launch/query.js`), `cli/tests/params/launch_uri_fixtures_test.zig`,
`desktop/tests/app/open/launchUriFixtures.headless.cpp` and
`pystencil/tests/fixtures/test_fixture_launchuri.py`; the vocabulary every parser reads is
`common/config/launchQuery.json`.

`telegramStart.json` — `{ name, serverUrl, projectId, expectPayload }`: the t.me
`?start=` codec (`"1" + base64url("host[:port]|id")`, 64-char cap → `null` on overflow).
These are the SAME golden vectors duplicated in the three suites' tests; decoding
surfaces (bot) should also round-trip payload → (normalized origin, id).
