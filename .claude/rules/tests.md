---
description: Which pin to re-record, how to run each surface's size lint, and why a red pin means fix the code
paths:
  - "browser/tests/**"
  - "extension/tests/**"
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

# Tests, pins and budgets

## A failing pin means the code is wrong

Pins exist so a refactor can prove it changed nothing. When one goes red, **fix the code**.
Re-record only when the visual or textual change is the *intent* of the commit — and then the
re-pin is the whole commit, with no code motion in it.

## Which pin, for what you touched

| You touched | Pin | Re-record with |
|---|---|---|
| browser CSS | `browser/tests/pins/css.json` | `cd browser && UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js` |
| extension CSS | `extension/tests/pins/css.json` | `cd extension && UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js` |
| browser or extension UI (rendered state) | `e2e/pins/*.json` — computed styles + DOM for 21 states | `cd e2e && UPDATE_PINS=1 npm run test:ui` |
| desktop QSS or any painted widget | `desktop/tests/pins/stylesheets.txt` (24 hashes) + `desktop/tests/pins/<platform>/*.png` (24 renders, @1x and @2x) | `STENCIL_UPDATE_UI_PINS=1` on the ui-pins target |
| CLI terminal output | `cli/tests/pins/*.txt` (20 TUI goldens) | `cd cli && STENCIL_UPDATE_PINS=1 zig build test` |
| mcp user-facing text | `mcp/tests/goldens/` | `cd mcp && MCP_UPDATE_GOLDENS=1 cargo test` |
| pystencil user-facing text | `pystencil/tests/goldens/` | `cd pystencil && PYSTENCIL_UPDATE_GOLDENS=1 python3 -m unittest discover -s tests` |
| server user-facing text | `server/internal/httpapi/goldens/` | `cd server && SERVER_UPDATE_GOLDENS=1 go test ./internal/httpapi/...` |
| bot user-facing text | `bot/tests/Stencil.TelegramBot.Tests/Goldens/` | `cd bot && BOT_UPDATE_GOLDENS=1 dotnet test Stencil.TelegramBot.slnx` |

The desktop image pins are platform-specific; on a platform with no recorded renders that half
**skips** rather than failing. Do not "fix" a skip by recording pins on a new platform unless
that is the task.

## Size + comment ratchet, per surface

`maxNewFileLines` is 230 everywhere. Run just the lint:

| Surface | Command |
|---|---|
| browser | `cd browser && node --test tests/sizeBudget.test.js` |
| extension | `cd extension && node --test tests/sizeBudget.test.js` |
| core | `core/build/stencil_tests -tc="size budget*"` |
| desktop | `ctest --test-dir desktop/build -R stencil_sizebudget_headless` |
| cli | `cd cli && zig build test` (`tests/size_budget_test.zig` is part of the suite) |
| mcp | `cd mcp && cargo test --test size_budget_test` |
| pystencil | `cd pystencil && python3 -m unittest tests.test_size_budget` |
| server | `cd server && go test ./internal/lint/...` |
| bot | `cd bot && dotnet test Stencil.TelegramBot.slnx --filter SizeBudgetTests` |

It checks three things: no new file over 230 lines, no listed file grew, no directory's
comment share rose. Lower a recorded number when code leaves the file; never raise one without
a note in the budget's `exceptions`.

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

`node --test` never loads wasm; it always exercises the JS fallback. The wasm-parity test
self-skips locally without a built artifact — CI builds wasm fresh to run it for real.
