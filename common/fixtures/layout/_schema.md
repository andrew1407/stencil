# Layout `Line` / export-payload conformance vectors

Pin the layout schema shared by browser (`js/core/layout.js`, the reference), core,
cli (`layout.zig`), mcp (`layout.rs`), pystencil (`layout.py`) and bot (`Domain/Layout`).
Each `*.json` file is an array of vectors; comparison is **structural** (JSON values),
except that the export payload's **top-level key order is pinned** via `expectKeyOrder`
(the browser emits it deterministically from `LAYOUT_FIELDS`' `export` indices, so the
serialized bytes are stable). Per-LINE key order is NOT part of the contract.

Vector kinds (by fields present):
- `{ name, layout, expectPayload, expectKeyOrder?, forcedKeys?, expectLinesSameRef? }` —
  `buildLayoutPayload(layout)`, compared after a JSON round-trip (undefined-valued forced
  keys drop out; `forcedKeys` asserts they are still present on the live object).
- `{ name, sparse, expectSanitized, expectFilled }` — `sanitizeLines(sparse)` must equal
  `expectSanitized` (browser keeps omitted fields ABSENT; only `points`/`locked` are
  materialized); `expectFilled` is the same lines with the cross-surface per-line defaults
  filled in (color `#FFFF00`, thickness `2`, pointSize `4`, style `solid`, locked `false`,
  fillColor `transparent`, pointColor `''` = inherit stroke) — what the tolerant parsers
  (cli/mcp/pystencil/bot) should produce for the same sparse input.

Either kind may carry an optional informational `divergences` map
(`{ "<surface>": "one-line summary" }`) inventorying measured surface differences on the
vector. It is pure documentation — walkers ignore it; the surface-specific pinned values
(own lines/payloads/reject verdicts) live in each surface's local override file.
