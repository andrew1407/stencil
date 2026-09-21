# sanitizer fixtures — provider-error sanitizer conformance vectors

Vectors for the untrusted-provider-text sanitizer every surface ports from the
server's `sanitizeUpstreamText` (browser `sanitizeProviderText`, pystencil
`_clean_detail`, cli `sanitize`, …): control characters out, URLs and
token-shaped runs → `[redacted]`, whitespace collapsed, ≤ 800 chars scanned,
≤ 200 chars emitted (199 + `…`). Expectations here are the **browser's** output
(`browser/js/llm/client.js`); walked by `browser/tests/llm/llmWireFixtures.test.js`.

Every `*.json` file is an array of:

| field | req | meaning |
|---|---|---|
| `name` | yes | unique; a `DIVERGENCE(surface,…)` prefix marks cases where the named surfaces provably differ from the browser — their walkers must recompute their own expectation for that case and keep the invariant (no URL, no token run, ≤ 200) instead of the literal `expect` |
| `comment` | no | prose; for DIVERGENCE cases it describes the differing behavior |
| `divergences` | no | informational inventory `{ "<surface>": "one-line summary" }` of measured surface differences (incl. surfaces a `DIVERGENCE(...)` name does not list); never asserted — walkers ignore it and pin their own output in their local override file |
| `input` | yes | raw provider text; `null` means a null/absent input (skip if your language cannot represent it — expect empty) |
| `expect` | yes | the browser sanitizer's exact output |

Known divergences pinned here:
- **private-use / unassigned code points**: browser strips only `\p{Cc}\p{Cf}`;
  the Go server blanks every non-printing rune (`!unicode.IsPrint`).
- **length caps**: browser counts UTF-16 code units (and can split a surrogate
  pair at the 199 cut — one `expect` deliberately ends in a lone high surrogate,
  stored as `\ud83d` in JSON); Go/Python count code points, Zig counts bytes.
- **secret-fragment veto** (server only): the server additionally returns `""`
  when any 8-char run of its configured API key survives; clients have no key to
  compare against, so no vector exists for it — a server walker should add its own.
