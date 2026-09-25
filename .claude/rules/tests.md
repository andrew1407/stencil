---
description: Which pin to re-record, how to run each surface's size lint, and why a red pin means fix the code
paths:
  - "browser/tests/**"
  - "browser-extension/tests/**"
  - "vscode-extension/tests/**"
  - "core/tests/**"
  - "desktop/tests/**"
  - "cli/tests/**"
  - "mcp/tests/**"
  - "pystencil/tests/**"
  - "bot/tests/**"
  - "e2e/**"
  - "server/internal/lint/**"
  - "server/**/*_test.go"
---

# Tests, pins and floors

## A failing pin means the code is wrong

Pins exist so a refactor can prove it changed nothing. When one goes red, **fix the code**.
Re-record only when the visual or textual change is the *intent* of the commit — and then the
re-pin is the whole commit, with no code motion in it.

## Which pin, for what you touched

| You touched | Pin | Re-record with |
|---|---|---|
| browser CSS | `browser/tests/pins/css.json` | `cd browser && UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js` |
| browser-extension CSS | `browser-extension/tests/pins/css.json` | `cd browser-extension && UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js` |
| browser or extension UI (rendered state) | `e2e/pins/*.json` — computed styles + DOM for 21 states | `cd e2e && UPDATE_PINS=1 npm run test:ui` |
| desktop QSS or any painted widget | `desktop/tests/pins/stylesheets.txt` (24 hashes, tracked) + `desktop/tests/pins/<platform>/*.png` (12 states at @1x and @2x, a gitignored local baseline) | `STENCIL_UPDATE_UI_PINS=1 ctest --test-dir desktop/build -R uipins` |
| CLI terminal output | `cli/tests/pins/*.txt` (20 TUI goldens) | `cd cli && STENCIL_UPDATE_PINS=1 zig build test` |
| mcp user-facing text | `mcp/tests/goldens/` | `cd mcp && MCP_UPDATE_GOLDENS=1 cargo test` |
| pystencil user-facing text | `pystencil/tests/goldens/` | `cd pystencil && PYSTENCIL_UPDATE_GOLDENS=1 python3 -m unittest discover -s tests` |
| server user-facing text | `server/internal/httpapi/goldens/` | `cd server && SERVER_UPDATE_GOLDENS=1 go test ./internal/httpapi/...` |
| bot user-facing text | `bot/tests/Stencil.TelegramBot.Tests/Goldens/` | `cd bot && BOT_UPDATE_GOLDENS=1 dotnet test Stencil.TelegramBot.slnx` |

The desktop renders are a local baseline, not a tracked artifact: record them on the
pre-change tree with the update flag above, then run the target plain on the changed tree and
read the diff it prints. They are platform-specific, and a platform with no recorded renders —
every CI runner — **skips** that half rather than failing; only the stylesheet hashes run
everywhere. Never commit a render: the folder is gitignored, and their history was scrubbed.

## Test-count floor, per surface

Every suite carries a **test-count floor**: the guard against a suite that reports "0 failed"
while running a fraction of itself — a glob that stopped matching, a target dropped from a
source list, a walker that fell out of its manifest. It fails naming both numbers
(`… collapsed to N, floor is M`). It is a floor, not a pin: adding tests never trips it, so
raise one only when its suite has grown well past it.

| Surface | Where |
|---|---|
| browser, browser-extension, vscode-extension | `tests/testFloor.test.js` — spawns an inner `node --test` and reads its summary, so it takes a couple of seconds |
| e2e | `tests/specGuard.test.js` — counts `test(` declarations and proves every spec is claimed by a Playwright project; `npm test` runs it first |
| core | `tests/testMain.cpp` — doctest's own registry |
| desktop | `tests/testFloor.headless.cpp` — parses the generated `CTestTestfile.cmake` |
| cli | `tests/test_floor_test.zig` — scans declarations |
| mcp | `tests/test_floor_test.rs` — scans declarations, and checks every `harness = false` walker is still in `Cargo.toml` |
| pystencil | `tests/test_count_floor.py` — `defaultTestLoader.discover` |
| server | `internal/lint/testfloor_test.go` — scans declarations |
| bot | `TestCountFloorTests.cs` — reflects over the assembly, expanding every theory's rows |

Where the runner can be asked what it actually ran, the floor asks it; Zig, Rust and Go offer
no such introspection, so those scan declarations and catch a deleted test but not an
unexecuted one.

## Benchmarks are opt-in

They print timings and assert only relative ratios, so they are deliberately **out of CI** —
never wire one into a default test target.

- core: `core/build/stencil_tests -ts=bench --no-skip` (one case: `-tc="*rasterize*"`)
- cli: `cd cli && zig build bench`

## Fixtures

The LLM op-plan fixture corpus under `browser/js/config/llm/fixtures/` is walked by **every**
surface's tests — it is the cross-language proof that the seven validators agree. Its
mechanical half is generated: after any `opRegistry.json` edit, run
`cd browser && npm run gen-fixtures`, or the browser walker fails on a stale bundle.
Add the hand-written fixture for the interesting case yourself.

The `.stc` corpus is its twin: one plain-text file,
`browser/js/config/script/fixtures/cases.txt`, walked by `core/tests/script/scriptFixtures.test.cpp`,
`browser/tests/core/scriptFixtures.test.js` (and the wasm-parity script spec),
`cli/tests/script/script_fixtures_test.zig`, `pystencil/tests/fixtures/test_fixture_script.py`,
`vscode-extension/tests/parser/fixtureWalker.test.js` and the `e2e/` cli + browser script specs. Nothing about it is generated and no walker records
it: append the section by hand, run a walker, and read the mismatch it prints. A case named
`err-*` must produce an error and every other case must not, so the name is part of the
assertion.

`node --test` never loads wasm; it always exercises the JS fallback. The wasm-parity test
self-skips locally without a built artifact — CI builds wasm fresh to run it for real.
